# Custom sam-ba upload for the CO2-Ampel bootloader.
#
# PlatformIO's built-in upload often fails on this board: bossac starts while the
# bootloader is still enumerating. This script does the 1200 bps touch itself,
# waits for the bootloader USB ID, pauses briefly, and then runs bossac.
#
# Settings survive the upload: bossac 1.7 can only erase the whole application
# area, so the script first reads the flash, takes the settings area (last 2KB,
# reserved in the linker script) and appends it to the firmware image before
# the usual erase + write + verify.

Import("env")

import os
import subprocess
import tempfile
import time

from serial import Serial
from serial.tools import list_ports

APP_ID = (0x04D8, 0xEACE)
BOOT_ID = (0x04D8, 0xEACF)
WAIT_S = 20

APP_START = 0x2000       # bossac 1.7 addresses flash from the end of the bootloader
SETTINGS_ADDR = 0x3F800
SETTINGS_SIZE = 0x800
FLASH_END = 0x40000


def find_port(usb_id):
    for p in list_ports.comports():
        if (p.vid, p.pid) == usb_id:
            return p.device
    return None


def enter_bootloader(env):
    port = find_port(BOOT_ID)
    if port:
        return port
    app_port = env.subst("$UPLOAD_PORT") or find_port(APP_ID)
    if not app_port:
        print("CO2-Ampel not found (neither app %04X:%04X nor bootloader %04X:%04X)" % (APP_ID + BOOT_ID))
        return None
    print("Resetting %s into bootloader (1200 bps touch)" % app_port)
    s = Serial(app_port, 1200)
    s.dtr = False
    time.sleep(0.1)
    s.close()
    deadline = time.time() + WAIT_S
    while time.time() < deadline:
        time.sleep(0.1)
        port = find_port(BOOT_ID)
        if port:
            return port
    print("Bootloader did not appear within %ds" % WAIT_S)
    return None


def bossac(env, port, args):
    tool = os.path.join(env.PioPlatform().get_package_dir("tool-bossac"), "bossac")
    cmd = [tool, "--port", os.path.basename(port), "-U", "true"] + args
    print(" ".join('"%s"' % c if " " in c else c for c in cmd))
    return subprocess.call(cmd)


def upload(source, target, env):
    port = enter_bootloader(env)
    if port is None:
        return 1
    time.sleep(0.5)  # let the bootloader finish enumerating
    print("Bootloader on %s" % port)

    with open(str(source[0]), "rb") as f:
        image = f.read()
    settings_off = SETTINGS_ADDR - APP_START
    if len(image) > settings_off:
        print("Firmware overlaps the settings area")
        return 1

    tmpdir = tempfile.mkdtemp(prefix="co2ampel-upload-")
    dump = os.path.join(tmpdir, "flash.bin")
    settings = b""
    if bossac(env, port, ["--read", dump]) == 0 and os.path.getsize(dump) == FLASH_END - APP_START:
        with open(dump, "rb") as f:
            f.seek(settings_off)
            settings = f.read(SETTINGS_SIZE)
        if settings.count(b"\xff") == len(settings):
            settings = b""  # area erased, nothing to keep
    else:
        print("Warning: could not read flash, settings will be lost")

    if settings:
        print("Keeping settings area (%d bytes at 0x%X)" % (len(settings), SETTINGS_ADDR))
        image = image + b"\xff" * (settings_off - len(image)) + settings
    combined = os.path.join(tmpdir, "upload.bin")
    with open(combined, "wb") as f:
        f.write(image)

    return bossac(env, port, ["--erase", "--write", "--verify", "--reset", combined])


env.Replace(UPLOADCMD=upload)
