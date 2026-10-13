#!/usr/bin/env python3
# Manage the TLS root certificates stored in the WINC1500 WiFi module (needed for MQTTS).
#
# Requires tools/winc_updater running on the device:
#   pio run -d tools/winc_updater -t upload
# Run with PlatformIO's Python (has pyserial, cryptography, certifi):
#   ~/.platformio/penv/bin/python scripts/winc_rootcert.py list
#   ~/.platformio/penv/bin/python scripts/winc_rootcert.py add --host mqtt.example.com:8883
#   ~/.platformio/penv/bin/python scripts/winc_rootcert.py add --pem root.pem
#   ~/.platformio/penv/bin/python scripts/winc_rootcert.py restore winc_rootcert_<date>.bin
# Afterwards flash the firmware again: pio run -e co2ampel_pro -t upload
#
# Every write saves a backup of the current store first and is verified by reading it back.
# Only RSA roots are supported (the WINC1500 verifies RSA chains without host support).
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


class Updater:
    """Serial protocol of the WiFi101 FirmwareUpdater sketch (115200 baud)."""

    def __init__(self, port=None):
        deadline = time.time() + 15 #device may still be enumerating after an upload
        while port is None:
            ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == APP_ID]
            if ports:
                port = ports[0]
            elif time.time() > deadline:
                sys.exit("CO2-Ampel not found (is tools/winc_updater running?)")
            else:
                time.sleep(0.2)
        self.s = serial.Serial(port, 115200, timeout=3)
        time.sleep(1.5)
        self.s.reset_input_buffer()
        self._send(0x99, 0x11223344, 0x55667788)
        if self.s.read(6) != b"v10000":
            sys.exit("No answer from the FirmwareUpdater sketch (upload tools/winc_updater first)")

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


def name_hash(cert):
    return hashlib.sha1(b"".join(a.value.encode() for a in cert.subject)).digest()


def parse_store(store):
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


def root_for_host(hostport):
    """Root CA of the server's chain, looked up in the certifi bundle by issuer name."""
    host, _, port = hostport.partition(":")
    out = subprocess.run(["openssl", "s_client", "-connect", "%s:%s" % (host, port or "8883"),
                          "-servername", host, "-showcerts"],
                         input="", capture_output=True, text=True, timeout=20).stdout
    chain = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", out, re.S)
    if not chain:
        sys.exit("No TLS certificate chain from %s" % hostport)
    top = x509.load_pem_x509_certificate(chain[-1].encode())
    for root in known_roots():
        if root.subject == top.issuer or root.subject == top.subject:
            return root
    sys.exit("Root CA '%s' not found in the certifi bundle; use --pem" % top.issuer.rfc4514_string())


def describe(entries, roots):
    names = {name_hash(c): c.subject.rfc4514_string() for c in roots}
    today = datetime.date.today()
    for i, e in enumerate(entries):
        kind = "RSA %d" % e["bits"] if e["type"] == KEY_RSA else "type %d" % e["type"]
        state = "EXPIRED" if e["expires"] < today else "       "
        print("%2d  %-9s expires %s %s %s" % (i, kind, e["expires"], state,
                                               names.get(e["hash"], "(not in certifi bundle)")))


def main():
    ap = argparse.ArgumentParser(description="WINC1500 TLS root certificate store")
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    add = sub.add_parser("add")
    src = add.add_mutually_exclusive_group(required=True)
    src.add_argument("--host", help="broker host:port, its root CA is looked up in certifi")
    src.add_argument("--pem", help="root certificate file (PEM)")
    res = sub.add_parser("restore")
    res.add_argument("file", help="backup file written by this script")
    args = ap.parse_args()

    dev = Updater(args.port)
    store = dev.read(STORE_ADDR, STORE_SIZE)
    entries = parse_store(store)

    if args.cmd == "list":
        describe(entries, known_roots())
        return

    if args.cmd == "restore":
        new = open(args.file, "rb").read()
        if len(new) != STORE_SIZE:
            sys.exit("Backup must be %d bytes" % STORE_SIZE)
        parse_store(new)
    else:
        cert = root_for_host(args.host) if args.host else \
            x509.load_pem_x509_certificate(open(args.pem, "rb").read())
        print("Root CA:", cert.subject.rfc4514_string())
        if any(e["hash"] == name_hash(cert) for e in entries):
            print("Already in the store, nothing to do")
            return
        entry = make_entry(cert)
        keep = [e["raw"] for e in entries]
        if 20 + sum(map(len, keep)) + len(entry) > STORE_SIZE:
            today = datetime.date.today()
            dropped = [e for e in entries if e["expires"] < today]
            keep = [e["raw"] for e in entries if e["expires"] >= today]
            print("Not enough space: removing %d expired root(s)" % len(dropped))
        new = build_store(keep + [entry])

    backup = "winc_rootcert_%s.bin" % datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    open(backup, "wb").write(store)
    print("Backup of the current store:", backup)

    dev.erase(STORE_ADDR, STORE_SIZE)
    dev.write(STORE_ADDR, new)
    if dev.read(STORE_ADDR, STORE_SIZE) != new:
        sys.exit("Verify FAILED - restore the backup with: restore %s" % backup)
    print("Written and verified. Now flash the firmware again: pio run -e co2ampel_pro -t upload")
    describe(parse_store(new), known_roots())


if __name__ == "__main__":
    main()
