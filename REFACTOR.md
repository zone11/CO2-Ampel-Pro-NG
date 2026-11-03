# Refactor

Accepted refactor items from the code audit of 0.7 (2026-11-02). Decision: no rewrite, targeted fixes.
Order: high first.
Done items are removed. Effort: S < 1 h, M = half a day, L = 1–2 days (incl. hardware test).

## High

- [ ] **H3 rest** (S–M): one reconnect path with backoff and the 1 s web wait are done (DEVELOPMENT.md "Reconnects"). Left:
      the per-phase watchdog (TODO.md), and MQTT connects still block up to ~40 s (fixed waits in WiFi101, needs a patched
      library).

## Medium

- [ ] **M1 Power-fail safe settings write** (M): one record, rows erased then written; power loss in between =
      CRC error = defaults (WiFi/MQTT lost). Two slots with a sequence counter in the existing 2 KB area (read
      the newest valid, write the other); keeps the upload backup/restore of the whole area. Check the BOD33
      fuse. `settings_storage.cpp` `settings_write()` / `settings_read()`.
- [ ] **M2 WiFi form on `/`** (S): reads the POST body only while `client.available()`, without Content-Length;
      a body split over TCP segments saves a truncated SSID/password. Use `read_body()` + `form_parse()` like
      `/config/save`, or drop the form and link to `/config`. `webserver.cpp:796-851`.
- [ ] **M3 SCD4x path** (S): a pressure change stops/starts the measurement with 1.5 s `delay()`;
      `set_ambient_pressure` is allowed during periodic measurement. `readMeasurement()` is polled every second
      (data every 5 s): use `getDataReadyStatus()`. Untested on hardware. `sensors.cpp:221-251`.
- [ ] **M4 Double soft float, 6.1 KB flash** (S + measure): `Print::println(float, n)` (`sensors.cpp:26,28,59,76`)
      → `fmt1()`; then check whether the LPS22HB/BMP280 code still pulls `__aeabi_d*`. Only worth it if both go.
- [ ] **M5 RAM for LoRaWAN/OTA** (S–M): unused `Serial1` in `variants/co2ampel/variant.cpp:129` (580 B RAM,
      ~1 KB flash); MQTT `reply` (1.4 KB static) as a local in `mqtt_handle_command()` or sharing `cmd_pending`;
      retained `device` only once per connect instead of every interval (`mqtt_service.cpp:14,48,646`).

## Low

- [ ] **L1** `check_i2c()` pokes SERCOM registers and leaves without a STOP; use the Wire version that is
      already there (commented out). `i2c_utils.cpp:39-82` (S).
- [ ] **L2** `settings_set()` formats "set key=value" to reuse the serial parser; call `cfg_find_item()` +
      `cfg_set_value()` directly. `settings_registry.cpp:180` (S).
- [ ] **L3** 25 globals in `include/app_globals.h`; a `readings` struct for the measurement values. Only with a larger
      rework (M).
- [ ] **L4** Busy main loop without `__WFI()` sleep (less MCU self-heating). `main.cpp` `loop()` (S).
- [ ] **L5** Hygiene: initialize `ReplyBuffer::buf` / `ChunkPrint::buf` (cppcheck), explicit `flags` in the key
      table (-Wextra, 19×); cppcheck `json_parser.cpp:223` is a false positive (S).
- [ ] **L6** Host unit tests (`pio test -e native`) for `json_parser` and the settings import/validation, incl. a
      repeatable fuzz test against Python json (M).
