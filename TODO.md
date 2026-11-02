# ToDo

Open changes and features for CO2-Ampel Pro NG. Order: roadmap features top down; follow-ups when convenient.
Done items are removed (the GitHub releases list what was done).

## Stability (watchdog resets on the dev unit, 2026-11-01/02)

Three watchdog resets in one night (21:37, 02:34, 03:32 UTC): main loop hung for 2 minutes with interrupts
running. Diagnostics are in place (0.7: `diag`, `/diag`, `<base>/diag`, HA entities).
- [ ] Wait for the next hang, read `last_phase` / `last_pc` / `last_lr` (`addr2line` with
      `workfiles/elf/0.7-dev-diag.elf`) and fix the cause.
- [ ] I2C: the SAMD Wire library waits without timeout; bounded waits, bus recovery + error counter on timeout.
- [ ] WiFi101: find the calls that can block forever (socket write, SPI) and reset the WINC1500 instead of the MCU.
- [ ] Watchdog per phase (short limit for the normal loop, long only for connects) for shorter outages.
- [ ] Keep `led.disable` (set over MQTT) and the button level over a watchdog reset (RAM record), so a
      reset doesn't turn the LEDs back on.
- [ ] Heap: check `ram_free_min` / `heap_free` over days; the web server mallocs 3 KB per request.
- [ ] Soak test with web/MQTT load and a router/WiFi restart.


## Features (roadmap)

- [ ] **OTA update triggered over MQTT** (pull): image ≤ ~123 KB, must not touch the settings area, version
      compare must handle `0.4` / `0.4.1`.
- [ ] **LoRaWAN/TTN** with MCCI LMIC: one image with a runtime `net.mode` only if it stays within the OTA
      limit, otherwise a separate env. Dev unit has an RFM9x and TTN coverage.
- [ ] Web interface with charts (listed as planned in the README, not prioritized yet).

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
- [ ] Home Assistant: check the removal (`mqtt.ha=0`) and an HA restart (initial switch state from the retained
      `<base>/state`) in a real HA; sensors and the LED switch work in HA 2026.10.4 (0.6.1).

## Not tested on hardware

- [ ] Dark branch of the LED dimming (covered light sensor).
- [ ] SCD4x and BMP280 paths (dev unit has SCD30 + LPS22HB only).
