# ToDo

Open changes and features for CO2-Ampel Pro NG. Order: roadmap features top down; follow-ups when convenient.
Done items are removed (the GitHub releases list what was done).
Code refactoring items (audit of 0.7) are in [REFACTOR.md](REFACTOR.md).

## Stability (watchdog resets on the dev unit, 2026-11-01/02)

Three watchdog resets in one night (21:37, 02:34, 03:32 UTC): main loop hung for 2 minutes with interrupts
running. Diagnostics are in place (0.7: `diag`, `/diag`, `<base>/diag`, HA entities).
- [ ] Wait for the next hang, read `last_phase` / `last_pc` / `last_lr` (`addr2line` with the ELF of the flashed
      build, `workfiles/elf/`) and fix the cause.
- [ ] I2C timeout with a real stuck bus: tested only with the MCU itself holding SCL (2026-11-02); short SCL to GND
      on a dev unit and watch `i2c_errors`, also the recursion in `startTransmissionWIRE()` (DEVELOPMENT.md).
- [ ] SCD30 bus clock: `Wire.setClock(50000)` in `setup()` is undone by the `Wire.begin()` in the SCD30 init, the
      bus runs at 100 kHz. Decide on one and remove the other (test: the SCD30 is the device that hung before).
- [ ] Socket write deadline with real MQTT and web traffic: tested only with a raw test socket against a peer that
      stops reading (2026-11-02); needs a fake broker that stops reading after CONNACK, and a stalled browser.
- [ ] WiFi101: find the other calls that can block forever (SPI) and reset the WINC1500 instead of the MCU.
- [ ] Watchdog per phase (short limit for the normal loop, long only for connects) for shorter outages.
- [ ] Heap: check `ram_free_min` / `heap_free` over days; the web server mallocs 3 KB per request.
- [ ] Soak test with web/MQTT load and a router/WiFi restart.

## Features (roadmap)

- [ ] **OTA update triggered over MQTT** (pull): image ≤ ~123 KB, must not touch the settings area, version
      compare must handle `0.4` / `0.4.1`.
- [ ] **LoRaWAN/TTN** with MCCI LMIC: one image with a runtime `net.mode` only if it stays within the OTA
      limit, otherwise a separate env. Dev unit has an RFM9x and TTN coverage.
- [ ] Web interface with charts (listed as planned in the README, not prioritized yet).

## MQTT / Home Assistant follow-ups

- [ ] MQTT drop on 2026-11-02: HA entities flickered unavailable twice in 80 ms with one reconnect in the broker log; check
      whether the Mosquitto bridge echoes `<base>/status` back.
- [ ] Both units: `loop_max_ms` ~5.1 s (2026-11-02: 5084 and 5108 ms, no failed MQTT connects), time of the block
      not recorded, but already there 34 s after boot (second unit, 2026-11-02), so a boot/connect step; find it. First unit: two failed connects (error -3) on some boots.
- [ ] Clear the old retained `device` and `status` messages after a prefix change (empty retained message
      before the reconnect).
- [ ] Discovery prefix: configurable `mqtt.ha_prefix` (settings v8) or keep it fixed to `homeassistant`
      (README note is in place).
- [ ] `value_template` for missing keys (`co2` before the first reading or without a CO2 sensor): check the
      HA log for template warnings; if needed use `| default` / `is defined`.
- [ ] HA birth message (`homeassistant/status` = `online`): republish discovery, only matters for brokers
      without persistence.
- [ ] Apply other MQTT connection settings (broker, port, user, pass, tls) without `reset`.
- [ ] Home Assistant: check the removal (`mqtt.ha=0`) and an HA restart (initial switch state from the retained
      `<base>/state`) in a real HA; sensors, diagnostics and the "Quiet" switch work in HA 2026.10.4 (0.7 dev).

## Not tested on hardware

- [ ] Dark branch of the LED dimming (covered light sensor).
- [ ] SCD4x and BMP280 paths (dev unit has SCD30 + LPS22HB only).
- [ ] AP SSID with the default `sys.name` `co2ampel-ng-<MAC id>` (hostname and HA device name checked on the second dev unit).
- [ ] Dark start with Quiet on after a watchdog reset, watched on the device (code path and `status` checked).
- [ ] Button brightness level kept over a watchdog reset (needs a button press before the test hang).
