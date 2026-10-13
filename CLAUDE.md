# CO2-Ampel Pro NG

PlatformIO firmware for the Watterott CO2-Ampel Pro (SAMD21G18A). Port of original firmware v24
(https://github.com/watterott/CO2-Ampel), reference copy in `legacy/`.

## Workflow
- Work happens on branch `dev`; `main` receives merges via PR.
- Build: `pio run -e co2ampel_pro` · Upload: `pio run -e co2ampel_pro -t upload` · Monitor: `pio device monitor -b 9600`
- Board/variant are custom: `boards/co2ampel.json`, `variants/co2ampel/`. USB IDs come from the board JSON
  (`build.hwids[0]` + `vendor`): app = 04D8:EACE, bootloader (sam-ba) = 04D8:EACF. Don't add `-DUSB_*` flags.
- Upload: PlatformIO's built-in 1200 bps reset + bossac is unreliable. What works: 1200 bps touch on the app port,
  wait for the bootloader PID, pause ~0.5 s, then run bossac. Connect the device directly (no hub). Uploading erases settings.
- `workfiles/` is local-only (gitignored). `workfiles/private/` holds real credentials: never commit or paste them.

## Code layout (split into modules, PR #3)
- `src/main.cpp`: setup/loop, sensor init, measurement + LED logic
- `serial_settings.*`: table-driven `get/set/dump/save/help` serial API; `serial_console.*` handles line input
- `settings_storage.*`: SETTINGS struct (`include/app_types.h`) persisted to flash at 0x0003F800
- `sensors.*`, `led_control.*`, `wifi_manager.*`, `webserver.*` (`/`, `/json`, `/cmk-agent`), `mqtt_service.*`,
  `ui_menu.*` (service menu), `diagnostics.*`, `i2c_utils.*`, `system_control.*`
- Defaults and constants: `include/app_config.h` (German comments inherited from the original)

## Hardware
- PA27 status LED, PA05 buzzer, PA22 4x WS2812, PB03 button
- Wire (SERCOM0): SCD30 0x61 / SCD4x 0x62 · Wire1 (SERCOM2): LPS22HB 0x5C / BMP280 0x76 / ATECC608 0x60
- WINC1500 WiFi (CS PA18), optional RFM9x LoRa (CS PA21)
- Dev unit (verified with `i2c scan`): SCD30 + LPS22HB + ATECC608, WINC1500 fw 19.6.1. No SCD4x or BMP280 to test against.

## Known state (verified 2026-10-13)
- dev builds cleanly: RAM 18.8 %, Flash 33.0 %; version string is still `0.3` in platformio.ini
- No warnings from project code (the only remaining one is in the WiFi101 library)
- Platform atmelsam 9.0.0. CO2 drivers: Sensirion I2C SCD30 1.1.1 (verified) and SCD4x 1.1.0 (compile-only)
- Deferred: Adafruit BMP280 stays at 2.6.8 (3.0.0 is a major update, and there's no BMP280 on the dev unit)
- SCD30 temperature needs ~10 min after boot to settle; compare readings only after warm-up
