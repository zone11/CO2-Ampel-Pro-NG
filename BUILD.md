# Building and flashing

This page covers building the firmware yourself, flashing it over USB, resetting or restoring the settings, and
building a variant without some of the optional features. The [README](README.md) explains how to use the device.

## What you need

- A computer with [PlatformIO](https://platformio.org/install), either the command line tool or the VS Code
  extension. PlatformIO downloads the compiler and all libraries on the first build.
- A USB cable. Plug the device in directly, without a USB hub.
- This repository, downloaded or cloned.

## Build and flash

Run these in the repository folder. Close any serial monitor first, or the upload can't open the port.

```bash
pio run                              # build only
pio run -e co2ampel_pro -t upload    # build and flash, settings are kept
pio device monitor -b 9600           # serial monitor
```

There is one build, `co2ampel_pro`, for every CO2-Ampel Pro. The firmware finds the fitted sensors, the WiFi module
and the LoRa radio when it starts, so the same file works with or without them.

The upload keeps your settings. It reads them from the device first and writes them back together with the new
firmware. Each upload also stores a copy in `.pio/settings-backup.bin`, and the one before it in
`.pio/settings-backup.bin.prev` when it differs. Both files contain your passwords in plain text.

## Factory reset and restoring settings

A factory reset flashes the firmware without the settings. The device starts with the defaults, and a forgotten
password is gone too:

```bash
CO2AMPEL_FACTORY_RESET=1 pio run -e co2ampel_pro -t upload
```

An upload that breaks off loses the settings, because the flash is erased before it is written. The next upload
prints a note about it. To write the last backup back:

```bash
CO2AMPEL_RESTORE_SETTINGS=1 pio run -e co2ampel_pro -t upload
CO2AMPEL_RESTORE_SETTINGS=.pio/settings-backup.bin.prev pio run -e co2ampel_pro -t upload   # an older one
```

## Release files

Each GitHub release has the firmware as a `.bin` file. **Flashing that file with other tools, such as bossac or the
Arduino IDE, erases your settings.** Use the upload command above whenever you can.

## If the device doesn't show up on USB

After an interrupted upload the device may no longer appear as a serial port. Press the red reset button twice
quickly. That starts the bootloader, and the upload command works again.

## Optional features

Four features can be left out of the build. All of them are included by default. To drop one, add its switch to
`build_flags` in `platformio.ini`:

```ini
build_flags =
    ${env.build_flags}
    -DWITH_LORA=0
```

For a single build, `PLATFORMIO_BUILD_FLAGS=-DWITH_LORA=0 pio run -e co2ampel_pro -t upload` works too. The settings
of a feature you leave out stay stored on the device, and a build with the feature finds them again.

The status page lists the included features under "Built with".

### WITH_WEB: web interface

`WITH_WEB=0` removes the web server. The device then doesn't answer on port 80: no status page, no `/config`,
`/config.json`, `/json` or `/diag`. A long button press still opens the device's own WiFi network, but there is no
page to set it up with, so the setup has to go over USB serial. The calibration buttons of the web page are gone
too. WiFi, MQTT, Home Assistant and LoRaWAN work as before.

### WITH_HA: Home Assistant discovery

`WITH_HA=0` removes the discovery messages under `homeassistant/...`, so Home Assistant no longer creates the device,
its sensors and the Quiet switch on its own. The setting `mqtt.ha` disappears. MQTT itself stays, with all its
topics, so sensors set up by hand in Home Assistant keep working.

Such a build can't remove discovery messages that are already stored on the broker. Set `mqtt.ha=0` before flashing
it to a device that had Home Assistant on.

### WITH_MQTT_CONFIG: commands over MQTT

`WITH_MQTT_CONFIG=0` stops the device from listening on `<base>/cmd`. Settings can no longer be changed over MQTT,
there are no replies on `<base>/resp`, and a calibration can't be started over MQTT. Quiet goes with it, including
the Quiet switch in Home Assistant, which the device removes because nothing would answer it any more. The setting
`mqtt.config` disappears. Publishing the readings is not affected.

### WITH_LORA: LoRaWAN

`WITH_LORA=0` removes LoRaWAN: no join, no messages to The Things Network, no downlinks. The `lora.*` settings, the
LoRaWAN lines in `status`, the LoRaWAN section of the web page, the `lora` block in `diag`, the LoRaWAN entities in
Home Assistant and the `lora_join` error are gone. The self test still reports whether a LoRa radio is fitted.

`wifi.enabled` stays in such a build. Leave it at 1, since without LoRaWAN a device with WiFi off has no network at
all.

### Size

Measured on 1.0-pre1 (8882fd4). The full build takes 117 KB (119,856 bytes) of the 246 KB available.

| Switch | Flash saved | RAM saved |
|---|---|---|
| `-DWITH_LORA=0` | 18.5 KB | 0.9 KB |
| `-DWITH_WEB=0` | 12.2 KB | almost none |
| `-DWITH_HA=0` | 3.2 KB | none |
| `-DWITH_MQTT_CONFIG=0` | 2.3 KB | 1 KB, plus about 0.9 KB of the MQTT buffer |
| all four | 35.1 KB | 1.9 KB |

Only the full build runs on our test devices. The smaller builds compile without warnings but have not been tested
on hardware.

## Fixed build flags

These are in `platformio.ini` as well. They are not meant to be changed for a normal build.

| Flag | What it does |
|---|---|
| `-DVERSION="..."` | the version shown in `status`, on the web page, over MQTT and in Home Assistant |
| `-flto` | link time optimization, which makes the firmware about 14 KB smaller |
| `-Wl,-u,exception_table` | keeps the vector table, which the optimization would otherwise drop; without it the firmware doesn't start |
| `-g` | debug information in `firmware.elf`, used to find the place of a crash. The firmware on the device is the same with or without it |
| `-DUSBCON` | the USB serial port |
| `-DCFG_eu868=1` | the LoRaWAN region, Europe 863 to 870 MHz. Another region needs its own build |
| `-DCFG_sx1276_radio=1` | the radio chip in the RFM95 |
| `-DDISABLE_PING`, `-DDISABLE_BEACONS` | leave out LoRaWAN Class B, which the device doesn't use |
| `-DLMIC_ENABLE_long_messages=0` | limits LoRaWAN frames to 64 bytes, which saves RAM; the device sends 10 |
| `-DLMIC_ENABLE_DeviceTimeReq=0` | leaves out the network time request |
| `-DLMIC_ENABLE_onEvent=0` | leaves out an old way of reporting LoRaWAN events; the firmware uses the newer one |
| `-DLMIC_ENABLE_arbitrary_clock_error=1` | allows a clock tolerance of 1 %. The board has no crystal, and without this flag the LoRaWAN stack stops at 0.4 % |
| `-DLMIC_DEBUG_LEVEL=0` | no debug output from the LoRaWAN stack |
| `-DARDUINO_LMIC_PROJECT_CONFIG_H_SUPPRESS` | the LoRaWAN stack takes its settings from these flags instead of a config file |

## Defaults

The defaults of the settings, such as thresholds, colors, brightness and the MQTT values, are in
`include/app_config.h`. They apply to a new device and after a factory reset. A device that already has settings
keeps them.

Code structure and internals are in [DEVELOPMENT.md](DEVELOPMENT.md).
