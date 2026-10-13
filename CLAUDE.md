# CO2-Ampel Pro NG

PlatformIO firmware for the Watterott CO2-Ampel Pro (SAMD21G18A). Port of original firmware v24
(https://github.com/watterott/CO2-Ampel), reference copy in `legacy/`.

## Workflow
- Work happens on branch `dev`; `main` receives merges via PR.
- Build: `pio run -e co2ampel_pro` · Upload: `pio run -e co2ampel_pro -t upload` · Monitor: `pio device monitor -b 9600`
- Board/variant are custom: `boards/co2ampel.json`, `variants/co2ampel/`. USB IDs come from the board JSON
  (`build.hwids[0]` + `vendor`): app = 04D8:EACE, bootloader (sam-ba) = 04D8:EACF. Don't add `-DUSB_*` flags.
- Upload: `scripts/upload_samba.py` (custom upload_protocol) does the 1200 bps touch, waits for the bootloader PID,
  pauses ~0.5 s, reads the flash, and re-flashes firmware + the 2 KB settings area (bossac 1.7 can only erase the
  whole app area). Settings survive `pio run -t upload`. Connect the device directly (no hub).
- Settings: versioned record (magic "CO2A", version, size, CRC32) at 0x3F800, reserved in the linker script.
  Settings v2 (adds `asc`). On any SETTINGS change: bump `SETTINGS_VERSION` and add a migration case in
  `settings_read()`; the `static_assert` on `sizeof(SETTINGS)` catches size changes.
- `workfiles/` is local-only (gitignored). `workfiles/private/` holds real credentials: never commit or paste them.

## Code layout (split into modules, PR #3)
- `src/main.cpp`: setup/loop, sensor init, measurement + LED logic
- `serial_settings.*`: generic table-driven `get/set/dump/save/help` handler (flags `CFG_SECRET`, `CFG_LOCAL`)
- `settings_registry.*`: the one key table + `settings_handle_line/settings_set/settings_save`, used by
  serial (`serial_console.*`, non-blocking line input) and the web form; future MQTT/LoRa front ends call it
  with `local=false` (secrets masked, credentials read-only)
- `settings_storage.*`: SETTINGS struct (`include/app_types.h`) persisted to flash at 0x0003F800
- `sensors.*`, `led_control.*`, `wifi_manager.*`, `webserver.*` (`/`, `/json`, `/cmk-agent`), `mqtt_service.*`,
  `ui_menu.*` (service menu), `diagnostics.*`, `i2c_utils.*`, `system_control.*`
- Defaults and constants: `include/app_config.h` (German comments inherited from the original)

## Hardware
- PA27 status LED, PA05 buzzer, PA22 4x WS2812, PB03 button
- RFM9x: CS PA21, DIO0 PA14, DIO1 PA15 (`PIN_RFM9X_*` in `variants/co2ampel/variant.h`, the only pin definition)
- Wire (SERCOM0): SCD30 0x61 / SCD4x 0x62 · Wire1 (SERCOM2): LPS22HB 0x5C / BMP280 0x76 / ATECC608 0x60
- WINC1500 WiFi (CS PA18), optional RFM9x LoRa (CS PA21)
- Dev unit (verified with `i2c scan`): SCD30 + LPS22HB + ATECC608, WINC1500 fw 19.6.1. No SCD4x or BMP280 to test against.

## Known state (verified 2026-10-13)
- v0.4.0. dev builds cleanly: RAM 19.5 %, Flash 35.2 % (88.8 KB) (of 246 KB; the last 2 KB are reserved for settings)
- No warnings from project code (the only remaining one is in the WiFi101 library)
- Platform atmelsam 9.0.0. CO2 drivers: Sensirion I2C SCD30 1.1.1 (verified) and SCD4x 1.1.0 (compile-only)
- Adafruit BMP280 3.0.0: relicense only (BSD → MIT), code identical to 2.6.8; compile-only (no BMP280 on the dev unit)
- SCD30 temperature needs ~10 min after boot to settle; compare readings only after warm-up
- Dev unit has an RFM95/96 and TTN coverage (per user); self test probes it, no LoRaWAN stack yet

## Roadmap (agreed 2026-10-13)
MQTT config (`<prefix>/<id>/cmd` → registry with `local=false`, reply on `resp`) → MQTTS (`WiFiSSLClient`,
root CA in WINC flash) → OTA pull triggered over MQTT (image limit ≈ 123 KB: half of 0x2000–0x3F800,
must not touch settings) → LoRaWAN/TTN with MCCI LMIC (one image with runtime `net.mode` only if it stays
within the OTA limit, else separate env).
