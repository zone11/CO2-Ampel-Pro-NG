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
#
# The erase also wipes the settings area, so an upload that breaks off loses them. Every valid
# settings area read from the device is therefore kept in .pio/settings-backup.bin (contains the
# WiFi/MQTT passwords in clear text; a different previous backup is kept as .prev); restore it with
# CO2AMPEL_RESTORE_SETTINGS=1 (or =<file>, e.g. the .prev one).
#
# Factory reset (e.g. forgotten enable password): CO2AMPEL_FACTORY_RESET=1 pio run -e co2ampel_pro -t upload

Import("env")

import os
import struct
import subprocess
import tempfile
import time
import zlib

from serial import Serial
from serial.tools import list_ports

APP_ID = (0x04D8, 0xEACE)
BOOT_ID = (0x04D8, 0xEACF)
WAIT_S = 20

APP_START = 0x2000       # bossac 1.7 addresses flash from the end of the bootloader
SETTINGS_ADDR = 0x3F800
SETTINGS_SIZE = 0x800
FLASH_END = 0x40000
SETTINGS_MAGIC = 0x434F3241  # "CO2A" (record layout: src/settings_storage.cpp)
BACKUP = os.path.join(env.subst("$PROJECT_DIR"), ".pio", "settings-backup.bin")


def settings_valid(area):
    """Checks magic, size and CRC32 of a settings record."""
    if len(area) != SETTINGS_SIZE:
        return False
    magic, _version, size = struct.unpack_from("<IHH", area, 0)
    if magic != SETTINGS_MAGIC or 8 + size + 4 > SETTINGS_SIZE:
        return False
    crc = struct.unpack_from("<I", area, 8 + size)[0]
    return zlib.crc32(area[:8 + size]) & 0xFFFFFFFF == crc


def save_backup(area):
    """Writes the backup; a different previous one is kept as .prev."""
    if os.path.exists(BACKUP):
        with open(BACKUP, "rb") as f:
            if f.read() == area:
                return
        os.replace(BACKUP, BACKUP + ".prev")
        print("Settings changed since the last backup, the previous one is kept as %s.prev" % BACKUP)
    fd = os.open(BACKUP, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(area)


def restore_source():
    """CO2AMPEL_RESTORE_SETTINGS: 1 = last backup, otherwise a file name; None = not requested."""
    value = os.environ.get("CO2AMPEL_RESTORE_SETTINGS", "")
    if value in ("", "0"):
        return None
    return BACKUP if value == "1" else value


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
    # settings to restore: checked (and read, before the backup is updated) while the app still runs
    restore = restore_source()
    if restore:
        try:
            with open(restore, "rb") as f:
                restored = f.read()
        except OSError as e:
            print("CO2AMPEL_RESTORE_SETTINGS: %s" % e)
            return 1
        if not settings_valid(restored):
            print("CO2AMPEL_RESTORE_SETTINGS: %s is no valid settings record" % restore)
            return 1

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

    # temp files hold the flash dump with the passwords: removed after the upload
    with tempfile.TemporaryDirectory(prefix="co2ampel-upload-") as tmpdir:
        dump = os.path.join(tmpdir, "flash.bin")
        settings = b""
        if bossac(env, port, ["--read", dump]) == 0 and os.path.getsize(dump) == FLASH_END - APP_START:
            with open(dump, "rb") as f:
                f.seek(settings_off)
                settings = f.read(SETTINGS_SIZE)
            if settings.count(b"\xff") == len(settings):
                settings = b""  # area erased, nothing to keep
            elif settings_valid(settings):
                save_backup(settings)
                print("Settings backup: %s" % BACKUP)
        else:
            print("Warning: could not read flash, settings will be lost")

        if restore:
            print("Restoring settings from %s" % restore)
            settings = restored
        elif os.environ.get("CO2AMPEL_FACTORY_RESET") == "1":
            if settings:
                print("CO2AMPEL_FACTORY_RESET=1: settings area is erased (device starts with defaults)")
            settings = b""
        elif not settings and os.path.exists(BACKUP):
            print("Note: the device has no settings (e.g. after an interrupted upload). To restore the")
            print("      last backup: CO2AMPEL_RESTORE_SETTINGS=1 pio run -e co2ampel_pro -t upload")

        if settings:
            if not restore:
                print("Keeping settings area (%d bytes at 0x%X)" % (len(settings), SETTINGS_ADDR))
            image = image + b"\xff" * (settings_off - len(image)) + settings
        combined = os.path.join(tmpdir, "upload.bin")
        with open(combined, "wb") as f:
            f.write(image)

        return bossac(env, port, ["--erase", "--write", "--verify", "--reset", combined])


env.Replace(UPLOADCMD=upload)
