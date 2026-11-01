# ToDo

Open changes and features for CO2-Ampel Pro NG. Not yet prioritized.

## Release

- [ ] **Release 0.6**: PR dev → main, tag/release `0.6` with the .bin. Release notes: web `/config` page
      (all settings, JSON upload/download, restart), `sys.name` (hostname, AP SSID, MQTT `device`, HA device;
      settings v7), English web UI and identifiers, topic prefix validation and reconnect on prefix/client id change,
      README HA/ACL notes.

## Features (roadmap)

- [ ] **OTA update triggered over MQTT** (pull): image ≤ ~123 KB, must not touch the settings area, version
      compare must handle `0.4` / `0.4.1`.
- [ ] **LoRaWAN/TTN** with MCCI LMIC: one image with a runtime `net.mode` only if it stays within the OTA
      limit, otherwise a separate env. Dev unit has an RFM9x and TTN coverage.

## MQTT / Home Assistant follow-ups

- [ ] Clear the old retained `device` and `status` messages after a prefix change (empty retained message
      before the reconnect).
- [ ] Discovery prefix: configurable `mqtt.ha_prefix` (settings v8) or keep it fixed to `homeassistant`
      (README note is in place).
- [ ] `value_template` for missing keys (`co2` before the first reading or without a CO2 sensor): check the
      HA log for template warnings; if needed use `| default` / `is defined`.
- [ ] HA birth message (`homeassistant/status` = `online`): republish discovery, only matters for brokers
      without persistence.
- [ ] Apply other MQTT connection settings (broker, port, user, pass, tls) without `reset`.
- [ ] Test in a real Home Assistant (payloads, switch and removal were checked on the broker without HA).

## Done for 0.6 (remove after the release)

- [x] Watchdog (WDT 4 s, extended to 120 s in software; reset cause in boot log, `status`, MQTT `device`).
- [x] HA switch "LEDs" for `led.disable`, retained `<base>/state`.
- [x] `upload_samba.py`: settings backup in `.pio/settings-backup.bin` (+ `.prev`), restore with
      `CO2AMPEL_RESTORE_SETTINGS`, temp folder removed after the upload.

## Not tested on hardware

- [ ] Dark branch of the LED dimming (covered light sensor).
- [ ] SCD4x and BMP280 paths (dev unit has SCD30 + LPS22HB only).

## Housekeeping


- [ ] `workfiles/CHANGELOG.md` stops at 0.3.3; update it or drop it in favor of the GitHub releases.
- [ ] `test/hardware_test.cpp`: check whether it still builds / is still useful.
