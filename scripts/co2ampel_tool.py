#!/usr/bin/env python3
# CO2-Ampel provisioning tool: settings and the TLS root certificates of the WINC1500 WiFi module
# (needed for MQTTS). Talks to the normal firmware over USB serial, no extra flashing needed.
#
# Run with PlatformIO's Python (has pyserial, cryptography, certifi), close any serial monitor first:
#   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py list
#   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py add --host mqtt.example.com:8883
#   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py add --pem root.pem
#   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py remove 3
#   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py backup [file]
#   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py restore winc_rootcert_<date>.bin
#   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py provision --config site.txt
#
# Certificates: the firmware's "winc bridge" command passes the WiFi module's flash through (protocol
# of the WiFi101 FirmwareUpdater sketch, writes limited to the certificate sector); the device restarts
# afterwards. Every write saves a backup of the store first and is verified by reading it back.
# Only RSA roots are supported (the WINC1500 verifies RSA chains without host support).
#
# provision: sends the "set key=value" lines of a settings file (format of the "dump" command), saves,
# installs the root CA of the MQTT broker if mqtt.tls is on, restarts and reports the MQTT connection.
# Values are never printed.
#
# Store layout (WINC flash 0x4000, 4 KB, format "v2"), little endian:
#   16 bytes start pattern 11 F1 12 F2 ... 18 F8, u32 number of entries, then per entry:
#   20 bytes SHA-1 over the concatenated subject attribute values, start and expiry date
#   (u16 year, u8 month, day, hour, minute, second, pad), u32 key type (1 = RSA),
#   u16 modulus size, u16 exponent size, modulus, exponent, padded to 4 bytes with FF.

import argparse
import datetime
import hashlib
import re
import struct
import subprocess
import sys
import time
import warnings

import serial
from serial.tools import list_ports
from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import rsa

APP_ID = (0x04D8, 0xEACE)
STORE_ADDR = 0x4000
STORE_SIZE = 0x1000
START_PATTERN = bytes([0x11, 0xF1, 0x12, 0xF2, 0x13, 0xF3, 0x14, 0xF4,
                       0x15, 0xF5, 0x16, 0xF6, 0x17, 0xF7, 0x18, 0xF8])
KEY_RSA = 1


def find_port(timeout=15):
    deadline = time.time() + timeout #device may still be enumerating after a restart
    while True:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == APP_ID]
        if ports:
            return ports[0]
        if time.time() > deadline:
            sys.exit("CO2-Ampel not found (USB 04D8:EACE)")
        time.sleep(0.2)


class Device:
    """Text console of the firmware (never opened at 1200 baud: that starts the bootloader)."""

    def __init__(self, port=None):
        deadline = time.time() + 10 #port can be busy for a moment while the device restarts
        while True:
            self.port = port or find_port()
            try:
                self.s = serial.Serial(self.port, 115200, timeout=0.2, exclusive=True)
                time.sleep(0.5)
                self.s.reset_input_buffer()
                break
            except (serial.SerialException, OSError) as e: #OSError: port vanished (device restarting)
                if time.time() > deadline:
                    sys.exit("Cannot open %s (%s) - close the serial monitor first" % (self.port, e))
                time.sleep(0.5)

    def readline(self, timeout):
        deadline = time.time() + timeout
        buf = b""
        while time.time() < deadline:
            c = self.s.read(1)
            if c in (b"\n", b"\r"):
                if buf:
                    return buf.decode(errors="replace")
            elif c:
                buf += c
        return None

    def command(self, line, timeout=3):
        """Sends one line, returns the reply lines up to OK / ERROR (log lines in between are kept)."""
        self.s.write((line + "\n").encode())
        lines = []
        while True:
            r = self.readline(timeout)
            if r is None:
                sys.exit("No reply from the device to '%s'" % line.split("=")[0])
            lines.append(r)
            if r == "OK" or r.startswith("ERROR"):
                return lines

    def get(self, key):
        self.s.write(("get %s\n" % key).encode())
        while True:
            r = self.readline(3)
            if r is None or r.startswith("ERROR"):
                return None
            if r.startswith(key + "="):
                return r[len(key) + 1:]

    def remote_on(self):
        deadline = time.time() + 45 #the console answers only after the boot (WiFi/MQTT connect)
        while True:
            self.s.write(b"enable\n")
            r = self.readline(2)
            while r is not None and r != "OK":
                r = self.readline(2)
            if r == "OK":
                time.sleep(0.5) #replies to repeated "enable" lines sent while booting
                self.s.reset_input_buffer()
                return
            #still in bridge mode from an aborted run? -> restart it
            hello = struct.pack(">BIIH", 0x99, 0x11223344, 0x55667788, 0)
            self.s.write(hello) #first one may be swallowed by the bridge's resync
            time.sleep(0.3)
            self.s.write(hello)
            time.sleep(0.3)
            if b"v10000" in self.s.read(256):
                print("Device was still in bridge mode: restarting it")
                self.s.write(struct.pack(">BIIH", 0x7F, 0, 0, 0))
                self.s.close()
                time.sleep(3)
                self.__init__()
                deadline = time.time() + 45
                continue
            if time.time() > deadline:
                sys.exit("No reply from the device (still booting? serial output off?)")

    def bridge(self):
        self.remote_on()
        self.s.write(b"winc bridge\n")
        while True:
            r = self.readline(3)
            if r == "OK bridge":
                break
            if r is None or r.startswith("ERROR"):
                sys.exit("Device has no 'winc bridge' command (firmware too old?)")
        time.sleep(0.5) #WiFi stops, module switches to download mode
        self.s.reset_input_buffer()
        return Bridge(self.s)


class Bridge:
    """WINC1500 flash access, protocol of the WiFi101 FirmwareUpdater sketch."""

    def __init__(self, s):
        self.s = s
        self.s.timeout = 3
        self._send(0x99, 0x11223344, 0x55667788)
        if self.s.read(6) != b"v10000":
            sys.exit("No answer from the bridge")

    def _send(self, cmd, addr=0, arg=0, payload=b""):
        self.s.write(struct.pack(">BIIH", cmd, addr, arg, len(payload)) + payload)

    def _ok(self, what):
        r = self.s.read(2)
        if r != b"OK":
            sys.exit("%s failed (%r)" % (what, r))

    def read(self, addr, n):
        out = b""
        while n:
            k = min(n, 1024)
            self._send(0x01, addr, k)
            data = self.s.read(k)
            if len(data) != k:
                sys.exit("read failed at 0x%X" % addr)
            self._ok("read")
            out += data
            addr += k
            n -= k
        return out

    def erase(self, addr, n):
        self._send(0x03, addr, n)
        self._ok("erase")

    def write(self, addr, data):
        for i in range(0, len(data), 1024):
            self._send(0x02, addr + i, 0, data[i:i + 1024])
            self._ok("write")

    def restart(self):
        port = self.s.port
        self._send(0x7F)
        self._ok("restart")
        self.s.close()
        deadline = time.time() + 5 #wait until the device has really reset (USB port gone)
        while time.time() < deadline and port in [p.device for p in list_ports.comports()]:
            time.sleep(0.1)


def name_hash(cert):
    return hashlib.sha1(b"".join(a.value.encode() for a in cert.subject)).digest()


def parse_store(store):
    if store[:16] == b"\xff" * 16 and store[16:20] == b"\xff" * 4:
        return [] #erased (e.g. power loss while writing): start empty
    if store[:16] != START_PATTERN:
        sys.exit("Unknown certificate store format (start pattern %s)" % store[:16].hex())
    count = struct.unpack("<I", store[16:20])[0]
    entries, off = [], 20
    for _ in range(count):
        ey, emo, ed = struct.unpack("<HBB", store[off + 28:off + 32])
        ktype, a, b = struct.unpack("<IHH", store[off + 36:off + 44])
        size = 44 + ((a + b + 3) // 4 * 4 if ktype == KEY_RSA else (b + 3) // 4 * 4)
        entries.append({"raw": store[off:off + size], "hash": store[off:off + 20],
                        "expires": datetime.date(ey, emo, ed), "type": ktype,
                        "bits": a * 8 if ktype == KEY_RSA else b * 8})
        off += size
    return entries


def build_store(raw_entries):
    store = START_PATTERN + struct.pack("<I", len(raw_entries)) + b"".join(raw_entries)
    if len(store) > STORE_SIZE:
        sys.exit("Certificates do not fit (%d of %d bytes)" % (len(store), STORE_SIZE))
    return store + b"\xff" * (STORE_SIZE - len(store))


def make_entry(cert):
    key = cert.public_key()
    if not isinstance(key, rsa.RSAPublicKey):
        sys.exit("Only RSA root certificates are supported")
    nums = key.public_numbers()
    n = nums.n.to_bytes((key.key_size + 7) // 8, "big")
    e = nums.e.to_bytes((nums.e.bit_length() + 7) // 8, "big")

    def date(d):
        return struct.pack("<HBBBBBB", d.year, d.month, d.day, d.hour, d.minute, d.second, 0)

    entry = (name_hash(cert) + date(cert.not_valid_before_utc) + date(cert.not_valid_after_utc) +
             struct.pack("<IHH", KEY_RSA, len(n), len(e)) + n + e)
    return entry + b"\xff" * (-len(entry) % 4)


def known_roots():
    import certifi
    pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
                      open(certifi.where()).read(), re.S)
    with warnings.catch_warnings(): #a few bundle roots have non-RFC serial numbers
        warnings.simplefilter("ignore")
        return [x509.load_pem_x509_certificate(p.encode()) for p in pems]


def root_for_host(host, port):
    """Root CA of the server's chain, looked up in the certifi bundle by issuer name."""
    out = subprocess.run(["openssl", "s_client", "-connect", "%s:%s" % (host, port),
                          "-servername", host, "-showcerts"],
                         input="", capture_output=True, text=True, timeout=20).stdout
    chain = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", out, re.S)
    if not chain:
        sys.exit("No TLS certificate chain from the broker (port %s)" % port)
    top = x509.load_pem_x509_certificate(chain[-1].encode())
    for root in known_roots():
        if root.subject == top.issuer or root.subject == top.subject:
            return root
    sys.exit("Root CA '%s' not found in the certifi bundle; use add --pem" % top.issuer.rfc4514_string())


def describe(entries, roots):
    names = {name_hash(c): c.subject.rfc4514_string() for c in roots}
    today = datetime.date.today()
    for i, e in enumerate(entries):
        kind = "RSA %d" % e["bits"] if e["type"] == KEY_RSA else "type %d" % e["type"]
        state = "EXPIRED" if e["expires"] < today else "       "
        print("%2d  %-9s expires %s %s %s" % (i, kind, e["expires"], state,
                                               names.get(e["hash"], "(not in certifi bundle)")))


def store_with(entries, cert):
    """New store with cert added, or None if it is already there."""
    print("Root CA:", cert.subject.rfc4514_string())
    if any(e["hash"] == name_hash(cert) for e in entries):
        print("Already in the store, nothing to do")
        return None
    entry = make_entry(cert)
    keep = [e["raw"] for e in entries]
    if 20 + sum(map(len, keep)) + len(entry) > STORE_SIZE:
        today = datetime.date.today()
        keep = [e["raw"] for e in entries if e["expires"] >= today]
        print("Not enough space: removing %d expired root(s)" % (len(entries) - len(keep)))
    return build_store(keep + [entry])


def write_store(bridge, old, new):
    backup = "winc_rootcert_%s.bin" % datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    open(backup, "wb").write(old)
    print("Backup of the current store:", backup)
    bridge.erase(STORE_ADDR, STORE_SIZE)
    bridge.write(STORE_ADDR, new)
    if bridge.read(STORE_ADDR, STORE_SIZE) != new:
        bridge.restart()
        sys.exit("Verify FAILED - restore the backup with: restore %s" % backup)
    print("Written and verified")
    describe(parse_store(new), known_roots())


def read_config(path):
    """set key=value lines; enable / exit / save / reset / comments (and the old "remote on") are ignored."""
    items = []
    for n, line in enumerate(open(path), 1):
        line = line.strip()
        if not line or line.startswith("#") or line in ("enable", "exit", "save", "reset", "remote on"):
            continue
        m = re.match(r"set ([a-z0-9_.]+)=(.*)$", line)
        if not m:
            sys.exit("%s line %d: expected 'set key=value'" % (path, n))
        items.append((m.group(1), m.group(2)))
    return items


def watch_boot(timeout=40):
    """Reads the boot log after a restart and reports the MQTT result (hosts/users redacted)."""
    time.sleep(2)
    try:
        s = serial.Serial(find_port(), 115200, timeout=0.2)
    except serial.SerialException:
        print("Device restarted (boot log not readable)")
        return
    deadline, buf = time.time() + timeout, b""
    while time.time() < deadline:
        buf += s.read(256)
        text = buf.decode(errors="replace")
        if "MQTT connected successfully" in text:
            print("Device restarted, MQTT connected")
            return
        for line in text.splitlines():
            if line.startswith("MQTT") and "failed" in line:
                print("Device restarted, MQTT problem:", re.sub(r"(to|user) \S+", r"\1 <redacted>", line))
                return
    print("Device restarted, no MQTT connection within %d s (WiFi/MQTT off or not reachable?)" % timeout)


def main():
    ap = argparse.ArgumentParser(description="CO2-Ampel provisioning and WiFi module root certificates")
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    add = sub.add_parser("add")
    src = add.add_mutually_exclusive_group(required=True)
    src.add_argument("--host", help="broker host:port, its root CA is looked up in certifi")
    src.add_argument("--pem", help="root certificate file (PEM)")
    rm = sub.add_parser("remove")
    rm.add_argument("index", type=int)
    bk = sub.add_parser("backup")
    bk.add_argument("file", nargs="?")
    res = sub.add_parser("restore")
    res.add_argument("file", help="backup file written by this tool")
    pv = sub.add_parser("provision")
    pv.add_argument("--config", required=True, help="settings file with 'set key=value' lines")
    args = ap.parse_args()

    dev = Device(args.port)

    if args.cmd == "provision":
        items = read_config(args.config)
        dev.remote_on()
        for key, value in items:
            r = dev.command("set %s=%s" % (key, value))
            if r[-1] != "OK":
                sys.exit("set %s: %s - nothing saved (restart the device to discard the keys set so far)" % (key, r[-1]))
            print("set", key)
        if dev.command("save")[-1] != "OK":
            sys.exit("save failed")
        print("Settings saved (%d keys)" % len(items))
        conf = dict(items)
        for key in ("mqtt.enabled", "mqtt.tls", "mqtt.broker", "mqtt.port"):
            if key not in conf:
                conf[key] = dev.get(key) or ""
        cert = None
        if conf["mqtt.enabled"] == "1" and conf["mqtt.tls"] == "1" and conf["mqtt.broker"]:
            cert = root_for_host(conf["mqtt.broker"], conf["mqtt.port"] or "8883")
        else:
            print("MQTT over TLS not enabled: no root certificate needed")
        bridge = dev.bridge()
        if cert:
            old = bridge.read(STORE_ADDR, STORE_SIZE)
            new = store_with(parse_store(old), cert)
            if new:
                write_store(bridge, old, new)
        bridge.restart()
        watch_boot()
        return

    cert = None
    if args.cmd == "add": #before the bridge starts (WiFi off): needs the network, may fail
        if args.host:
            host, _, port = args.host.partition(":")
            cert = root_for_host(host, port or "8883")
        else:
            cert = x509.load_pem_x509_certificate(open(args.pem, "rb").read())
        make_entry(cert) #RSA check

    bridge = dev.bridge()
    store = bridge.read(STORE_ADDR, STORE_SIZE)
    entries = parse_store(store)
    new = None

    if args.cmd == "list":
        print("Root certificates: %d" % len(entries))
        describe(entries, known_roots())
    elif args.cmd == "backup":
        name = args.file or "winc_rootcert_%s.bin" % datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        open(name, "wb").write(store)
        print("Saved:", name)
    elif args.cmd == "restore":
        new = open(args.file, "rb").read()
        if len(new) != STORE_SIZE:
            bridge.restart()
            sys.exit("Backup must be %d bytes" % STORE_SIZE)
        parse_store(new)
    elif args.cmd == "remove":
        if not 0 <= args.index < len(entries):
            bridge.restart()
            sys.exit("No entry %d (0..%d)" % (args.index, len(entries) - 1))
        new = build_store([e["raw"] for i, e in enumerate(entries) if i != args.index])
    else:
        new = store_with(entries, cert)

    if new:
        write_store(bridge, store, new)
    bridge.restart()
    print("Device restarts")


if __name__ == "__main__":
    main()
