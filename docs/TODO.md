# ToDo

Open changes and features for CO2-Ampel Pro NG. Order: roadmap features top down; follow-ups when convenient.
Done items are removed (the GitHub releases list what was done).
Code refactoring items (audit of 0.7) are in [REFACTOR.md](REFACTOR.md). On hold, not planned: firmware updates over the network
([OTA.md](OTA.md)) and root certificate upload in the web interface ([ROOT_CERT_UPLOAD.md](ROOT_CERT_UPLOAD.md)).

## Stability (watchdog resets on the dev unit, 2026-11-01/02)

Three watchdog resets in one night (21:37, 02:34, 03:32 UTC): main loop hung for 2 minutes with interrupts
running. Diagnostics are in place (0.7: `diag`, `/diag`, `<base>/diag`, HA entities).
- [ ] Wait for the next hang, read `last_phase` / `last_pc` / `last_lr` (`addr2line` with the ELF of the flashed
      build, `workfiles/elf/`) and fix the cause.
- [ ] I2C timeout with a real stuck bus: tested only with the MCU itself holding SCL (2026-11-02); short SCL to GND
      on a dev unit and watch `i2c_errors`, also the recursion in `startTransmissionWIRE()` (DEVELOPMENT.md).
- [ ] Socket write deadline with real MQTT and web traffic: tested only with a raw test socket against a peer that
      stops reading (2026-11-02); needs a fake broker that stops reading after CONNACK, and a stalled browser.
- [ ] WiFi101: find the other calls that can block forever (SPI) and reset the WINC1500 instead of the MCU.
- [ ] Heap: check `ram_free_min` / `heap_free` over days; the web server mallocs 3 KB per request.
- [ ] Soak test with web/MQTT load and a router restart.
- [ ] Long WiFi outage (10 minutes or more): retry delay up to its 5 minute maximum and failed attempts up to the 20 s limit
      (`loop_max_ms`). Needs a separate SSID for the devices, so only they lose WiFi. Short outage (about a minute) passed on
      both units 2026-11-04.

## Features (roadmap)

- [ ] **LoRaWAN/TTN** with MCCI LMIC: one image with a runtime `net.mode` if it fits, otherwise a separate env. Dev unit has an RFM9x and TTN coverage.
- [ ] Web interface with charts (listed as planned in the README, not prioritized yet).

## MQTT / Home Assistant follow-ups

- [ ] Failed first MQTT connect (error -3) on some boots, the retry connects. Seen on both units while they shared a subnet
      with the broker; 7 of 7 restarts clean on a routed network (2026-11-04), then once more there on the second unit, after a software
      restart (`mqtt_failures` 1, `mqtt_error` -3), so the subnet is not the whole explanation. After a prefix change it still happens once.
- [ ] Apply other MQTT connection settings (broker, port, user, pass, tls) without `reset`.

## Not tested on hardware

- [ ] Complete calibration started over serial or web, in fresh air (start, abort and result were tested, not the part that
      sets the sensor). 
- [ ] `WITH_HA=0` on the device (image checked: no `mqtt.ha` or discovery strings).
- [ ] Home Assistant restart with Quiet on (with Quiet off the switch returned as `off` at once, HA 2026.11.0, 2026-11-05).
- [ ] 0.8.1 on a USB charger (the 22 h run was on the Mac's USB).
- [ ] Real power cut during a settings save (the damaged states were flashed as crafted images, 2026-11-04).
- [ ] Dark branch of the LED dimming (covered light sensor).
- [ ] SCD4x and BMP280 paths (dev unit has SCD30 + LPS22HB only). SCD4x uses raw values with own float conversions since
      0.8.1 (formulas checked by the host tests, no sensor to test with).
- [ ] AP SSID with the default `sys.name` `co2ampel-ng-<MAC id>` (hostname and HA device name checked on the second dev unit).
- [ ] Dark start with Quiet on after a watchdog reset, watched on the device (code path and `status` checked).
- [ ] Button brightness level kept over a watchdog reset (needs a button press before the test hang).
