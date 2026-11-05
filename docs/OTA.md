# Firmware Updates Over the Network (OTA)

**Status: on hold, not planned.** No OTA code exists. It is still open whether OTA is needed at all once the MQTT
implementation is complete. This document keeps the analysis of 2026-11-05 for that decision.

Firmware updates currently need USB (see [DEVELOPMENT.md](DEVELOPMENT.md)).

## The Issue

The UF2 bootloader stays in place and works, but as far as known it only accepts firmware over USB, so it can't help
with OTA.

A device can only run the image at `0x2000`. OTA therefore downloads the new image to a spare area of the flash, then
copies it over the running firmware:

```
0x0000   UF2 bootloader     stays intact, USB only
0x2000   running firmware   erased, then overwritten during the copy
0x21000  downloaded image   stays intact during the copy
0x3F800  settings           stays intact
```

The copy runs from RAM, because the firmware erases itself while copying. A power cut has different results by stage:

| Stage | Result of a power cut |
|---|---|
| Download | safe, the running firmware isn't touched |
| Final copy (estimated up to about 6 s) | the new image is still complete in the spare area, but the firmware at `0x2000` is half-written and RAM is gone. On restart nothing knows that a copy was under way, and the bootloader doesn't look at the spare area, so the device needs a USB reflash |

The device isn't bricked, but someone has to fetch it and connect USB. The bootloader and the settings should survive.

## Option 1: Libraries Only

| Library | Purpose |
|---|---|
| JAndrassy `ArduinoOTA` 1.1.1 (`InternalStorage`) | stores the image and does the copy |
| `ArduinoHttpClient` 0.6.2 | download |
| `ArduinoECCX08` 1.4.1 | signature check on the ATECC608; `micro-ecc` is the fallback if the chip's lock state doesn't allow it |

Flow:

1. An MQTT command on `<base>/cmd`, only with `mqtt.config=1`, names the target version (a serial command for tests).
2. The device ignores that version if it is already running it or if it is older.
3. It downloads `co2ampel-pro-ng-<version>.bin` into the upper half of the flash.
4. It verifies the signature, which covers the image and its version.
5. It disables the watchdog and calls `InternalStorage.apply()`, which copies the image and restarts.

Still our own code: the glue (`src/ota.cpp`), the version compare, status reporting and a small signing script using
standard tools. The private key stays outside the repo.

Findings from the library source:

- **Settings can be erased:** the library accepts images up to 126,976 bytes, but from about 124.9 KB the erase reaches
  the settings at `0x3F800`. Images over 120 KB must be refused by our own code.
- **The watchdog can kill the copy:** the copy runs with interrupts off, so our watchdog resets after 4 s, possibly
  halfway through. It must be disabled before `apply()`. A hang during the copy then stays stuck until a power cycle.
- **Wrong I2C bus:** the ATECC608 is on `Wire1`, and the library's global object may use `Wire`, so create an own
  `ECCX08Class(Wire1, 0x60)`. If its `begin()` calls `Wire1.begin()`, re-enable the I2C timeouts afterwards.
- **Never lock the chip:** locking the ATECC608 can't be undone, so don't call its lock or configuration functions.
- **No redirects:** `ArduinoHttpClient` doesn't follow them, so a server that forces HTTP to HTTPS could fail.
- **HTTPS** only works with an RSA certificate chain (WiFi module limit). Plain HTTP is acceptable, because the signature
  protects the image.
- **Flash budget:** LoRaWAN may not fit under 120 KB together with OTA (93.8 KB now, plus these libraries), so it might
  need a separate build.
- **Not an option as staging area:** the WiFi module's flash. Driver 19.5.2 has no host-file download, and that flash is
  only writable in download mode with WiFi off.

## Option 2: Own Loader

A small, permanent program at `0x2000`, with the application moved up behind it. It runs first on every boot and checks
a "copy pending" marker in flash.

It adds:

- A power cut during the copy no longer needs USB: the copy restarts on the next boot.
- A hang during the copy can restart and resume.
- Optionally a check of the image on every boot, and a rollback of a bad image (extra work, and space for the old image).

It costs:

- The loader can't safely be updated by OTA: a bug in it means USB for every unit, so it has to be right the first time.
- Less room: the image limit drops from about 120 KB to about 119 KB, and a rollback copy would take more.
- The application's start address, the linker script and the upload script change. It needs its own tests, including
  real power cuts.
- It is security-critical code that we would own.

It doesn't have to be all or nothing: the loader is likely a few hundred lines, while HTTP and signature checks still
come from libraries. Only the library's copy step is replaced.

## Comparison

| | Libraries only | With own loader |
|---|---|---|
| Power cut during download | safe | safe |
| Power cut during copy | USB reflash | copy restarts on the next boot |
| Hang during copy | stuck until a power cycle | can restart and resume |
| New image boots but misbehaves | USB reflash | USB reflash, unless rollback is added |
| Signed images | yes | yes |
| Own bootloader code | none | yes |

## Recommendation

Libraries only. Updates would only run when triggered by hand, the window is a few seconds, and the units are easy to
reach by USB. The loader can be added later if that risk starts to matter, at the cost of one more USB flash per unit.
Both options need one USB flash per unit to get the first OTA-capable firmware.

Left out in both: a Home Assistant update entity and automatic update checks.

Earlier decisions, no longer current (2026-11-04): a loader that finishes an interrupted copy, signed images from the
start, images on the user's webspace.

## Not Verified

The analysis comes from reading the library source. Nothing was compiled or run.

- All timings are estimates, including the length of the copy.
- The flash limits assume 256 KB and no EEPROM reservation in the fuses; the library derives them on the device.
- Arduino's SFU and SDU (as far as known they need external flash or an SD card), MCUboot and the UF2 bootloader source
  were not checked.

## If OTA Comes Back

Start with a spike on a dev unit that is on USB, using a temporary patch. It is the go/no-go step:

- flash and RAM cost of the libraries
- the flash limits the library calculates on this hardware (`InternalStorage.debugPrint()`)
- download speed and copy time, and whether the server redirects to HTTPS
- whether the RAM copy functions survive the LTO build
- whether the library leaves the flash controller in a state that the settings save can work with (it sets `MANW` to 0)
- whether the ATECC608 can verify in its current lock state, otherwise `micro-ecc`
- whether the settings survive

Then test on hardware: cut-off download, WiFi loss during the download, wrong signature, oversized image, repeated
trigger, redirect, and optionally a deliberate power cut during the copy to prove the USB recovery.
