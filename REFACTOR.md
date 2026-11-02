# Refactor

Accepted refactor items from the code audit of 0.7 (2026-11-02). Decision: no rewrite, targeted fixes.
Order: high first; H1/H2 are the likely causes of the watchdog hangs (TODO.md "Stability").
Done items are removed. Effort: S < 1 h, M = half a day, L = 1–2 days (incl. hardware test).

## High

- [ ] **H1 I2C waits without timeout** (M): the SAMD core 1.8.14 loops on `INTFLAG.MB/SB` without a deadline
      (`SERCOM.cpp:525,538,671`); a slave holding SCL low hangs the loop with interrupts running. Enable the
      SERCOM hardware timeouts `CTRLA.LOWTOUTEN` and `CTRLA.INACTOUT` after `Wire.begin()` (enable-protected:
      disable, set, enable), count BUSERR in `health` and run `i2c_bus_recover()` + `Wire.begin()` on errors.
      `main.cpp:117-120`, `i2c_utils.cpp`, `sensors.cpp` `check_sensors()`.
- [ ] **H2 Socket writes without timeout** (M): WiFi101 `WiFiSocketClass::write` retries forever on
      `SOCK_ERR_BUFFER_FULL` (`WiFiSocket.cpp:297`) when the peer stops reading; all MQTT publishes and web
      pages go through it. Write deadline (e.g. 5 s) in a thin Client wrapper or a library override, then
      close the socket; count it in `health`. `tls_client.*`, `mqtt_service.cpp`, `webserver.cpp` ChunkPrint.
- [ ] **H3 Blocking, duplicated reconnects** (L): `wifi_start()` runs from `loop()` every 30 s (`main.cpp:405`)
      and from `webserver_service()` every 60 s (`webserver.cpp:684`), each blocks ≥ 6 s, then `mqtt_connect()`
      up to ~40 s (DNS 20 s + connect 20 s + 2 s retry); LEDs, button and serial stand still. One connection
      state machine (WiFi → MQTT) with backoff, called only from the loop; combine with the per-phase watchdog
      (TODO.md).

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
- [ ] **L3** 25 globals in `include/app_globals.h`; a `readings` struct for the measurement values. Only together
      with H3 (M).
- [ ] **L4** Busy main loop without `__WFI()` sleep (less MCU self-heating). `main.cpp` `loop()` (S).
- [ ] **L5** Hygiene: initialize `ReplyBuffer::buf` / `ChunkPrint::buf` (cppcheck), explicit `flags` in the key
      table (-Wextra, 19×); cppcheck `json_parser.cpp:223` is a false positive (S).
- [ ] **L6** Host unit tests (`pio test -e native`) for `json_parser` and the settings import/validation, incl. a
      repeatable fuzz test against Python json (M).
