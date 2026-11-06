# Blind Spots

Review of 2026-11-05, on the way to 0.9: what could stop a device from working as planned, and what the tests don't cover.
It comes from reading the documentation and the code, so unless noted, these points were not reproduced on a device.
Open work is tracked in [TODO.md](TODO.md); this document keeps the reasoning.

## Could Stop the Device Working as Planned

Items 1 to 3 are changed in the 0.9 code, as described under CO2 Sensor Failure and Reconnects in [DEVELOPMENT.md](DEVELOPMENT.md). Items 1 and
2 passed on hardware on 2026-11-06, item 3 has a host test. The text below describes the state before those changes.

1. **A dead CO2 sensor looks healthy.** After the first reading, `co2_valid` was never cleared, and a failed read kept the old
   value. If the sensor stopped, the LEDs stayed on the last color, possibly green; web and MQTT kept sending the frozen number,
   and Home Assistant showed the device as available. Failed reads weren't counted.
2. **A power cut can leave the device offline until someone restarts it.** If the router takes longer to return than the
   60-second WiFi attempt at boot, the device opens its setup network. The main loop treated that as fine and never retried the
   normal WiFi. That network has no password and no time limit, so without `sys.password` anyone nearby can change the settings.
   With `sys.nobuttons=1` the device keeps retrying instead.
3. **A calibration can report `OK` when the sensor refused the command.** The return value of the "set to 400 ppm" command was
   ignored, and on an SCD4x also its correction result.
4. **A calibration started indoors still sets the sensor.** The 200–800 ppm range only decides whether a run counts as stable. A
   run at 1100 ppm still sets 400 ppm after about 9 minutes, and again on the next two runs. MQTT can start one with nobody
   present, and there is no undo. Refusing out-of-range readings has a catch: a sensor reading 80 ppm outdoors only recovered
   because it was set anyway, so an override would be needed.
5. **Sensor drift goes unnoticed.** Two sensors were about 320 ppm low and 270 ppm high, and nothing had flagged it. At 320 ppm
   low, the traffic light shows green at a real 1300 ppm. Automatic self-calibration is off by default, the README only says "from
   time to time", and the device keeps no calibration date.

## Smaller Failure Paths

- **Sensor resets:** an SCD4x or pressure sensor that loses power briefly may stop measuring, because setup only runs at boot. The
  pressure would then freeze without an error. An SCD30 that hangs needs a power cycle, which the firmware can't do.
- **WiFi module missing at boot:** WiFi stays off until the next restart.
- **Hang during boot:** the device restarts over and over, with no safe mode.
- **Pressure readings:** any value from 260 to 1400 hPa is accepted and passed to the CO2 sensor. That's how a 16 hPa offset error
  got through.
- **Light value:** MQTT and `/json` report 1024 for the first minute after boot. It's only a start value; the web page hides it.
- **`enable` left active:** the traffic light stays paused with no time limit.

## Test Blind Spots

| Area | Gap |
|---|---|
| Setup under test isn't the default | both dev devices run with buzzer off, `led.auto=0`, brightness 20, no TLS, no password, `sys.nobuttons=0` |
| Buzzer alarm | no test found of the alarm at 1600 ppm or its 5-minute delay |
| Full brightness | no long run at the default 125, so power peaks with WiFi and LEDs together, and the heat effect on temperature readings, are unknown |
| TLS | only tested on older firmware as far as can be found, before the 0.8 reconnect rewrite; no test script |
| CO2 accuracy | only checked at 400 ppm, while two devices differed by about 50 ppm near 1000 ppm; temperature and humidity never compared with a reference |
| SCD4x and BMP280 | never run, yet the README lists them as supported |
| Fresh device | first setup from empty settings not tested on 0.9 |
| Long uptime | longest run on record about 39 hours, so the `millis()` wrap at 49 days is untested; a sleeping host computer and DHCP changes aren't covered either |
| Test code | hardware test scripts are local only, with fixed addresses, and there is no CI; host tests don't cover the traffic light, sensor reads, reconnects or the button |
| Open causes | the watchdog resets of 2026-11-01/02, the pressure offset and MQTT error -3 are unexplained |

## Documentation

- The README's "in daily use and tested on hardware" overstates things, given the SCD4x and BMP280 gaps.
- The README says a calibration "takes 3 minutes or more", but an unstable run took 10.
- The Tests section of [DEVELOPMENT.md](DEVELOPMENT.md) doesn't mention the calibration tests.
