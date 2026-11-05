# CO2-Ampel Pro NG (Next Generation)

Alternative firmware for the **Watterott CO2-Ampel Pro**: a CO2 traffic light that shows the air quality
with four LEDs, warns with a buzzer when it is time to ventilate, and sends its readings to your network
(web page, MQTT, Home Assistant). It started as a port of version 24 of the
[original firmware](https://github.com/watterott/CO2-Ampel) by Watterott electronic and has since been
largely rewritten. It runs on the unmodified hardware.

✅ **Status**: in daily use and tested on hardware

## Contents

- [Features](#features)
- [What You Need](#what-you-need)
- [Installation and Updates](#installation-and-updates)
- [Quick Start](#quick-start)
- [Using the Device](#using-the-device)
- [Settings](#settings)
- [Web Interface](#web-interface)
- [MQTT and Home Assistant](#mqtt-and-home-assistant)
- [Backup, Restore and Provisioning](#backup-restore-and-provisioning)
- [Troubleshooting](#troubleshooting)
- [Serial Command Reference](#serial-command-reference)

Building your own variant, the code structure and the internals are described in
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Features

- Traffic light with configurable CO2 thresholds and LED colors, buzzer alarm, automatic dimming in the dark
- All settings at runtime: over USB serial, on the web configuration page or over MQTT (no recompiling)
- Web page with the current measurements and the WiFi, MQTT and system status; JSON endpoint for your own scripts
- MQTT with optional TLS (certificate and hostname checked)
- Home Assistant discovery: the device, its sensors and a "Quiet" switch appear automatically
- Quiet for the night: LEDs and buzzer off with one switch in Home Assistant or over MQTT
- Settings survive firmware updates, and a power loss while saving keeps the previous settings; backup and restore as a JSON file
- Optional password for changes over USB and the web interface
- Watchdog: restarts the device on its own if the firmware hangs, and reports why it restarted
- Calibration and self test from the built-in service menu

**Planned:** LoRaWAN/TTN, charts on the web page.

## What You Need

- A Watterott CO2-Ampel Pro (CO2 sensor SCD30 or SCD4x, pressure sensor LPS22HB or BMP280, WiFi module).
  The firmware detects the fitted sensors and the WiFi module at start; it has been tested on the Pro with
  SCD30 and LPS22HB.
- A USB cable and a computer with [PlatformIO](https://platformio.org/install) (the command line tool or
  the VS Code extension)
- For MQTT: an MQTT broker, e.g. the Mosquitto add-on of Home Assistant

## Installation and Updates

1. Download or clone this repository.
2. Plug the device in **directly** (not through a USB hub) and close any serial monitor.
3. In the repository folder run:
   ```bash
   pio run -e co2ampel_pro -t upload
   ```

The same command updates the firmware later. **Your settings are kept**: the upload reads them from the
device and writes them back with the new firmware. Each upload also keeps a copy in
`.pio/settings-backup.bin` (contains your passwords; see
[DEVELOPMENT.md](docs/DEVELOPMENT.md#upload-script) for restoring it).

- **Factory reset** (all settings back to the defaults, also clears a forgotten password):
  `CO2AMPEL_FACTORY_RESET=1 pio run -e co2ampel_pro -t upload`
- The releases on GitHub also contain the firmware as a `.bin` file. Flashing it with other tools (e.g.
  bossac or the Arduino IDE) **erases your settings**; use the command above whenever you can.
- If the device no longer shows up on USB (e.g. after an interrupted upload), press the red reset button
  twice quickly. That starts the bootloader, and the upload command works again.

## Quick Start

1. Open a serial monitor at 9600 baud, e.g. `pio device monitor -b 9600`.
2. Allow changes and enter your WiFi:
   ```
   enable
   set wifi.ssid=YourWiFi
   set wifi.pass=YourPassword
   save
   reset
   ```
3. After the restart `status` shows the IP address. Open `http://<IP address>/` in a browser.

Without a serial monitor: hold the button for more than 3 seconds. The device opens its own WiFi network
(named like the device, e.g. `co2ampel-ng-F8F0056E78A2`), and the web page there has a "Change WiFi" form.

MQTT and Home Assistant: see [MQTT and Home Assistant](#mqtt-and-home-assistant).

## Using the Device

### LED Colors (Defaults)

| CO2 | LEDs | Meaning |
|---|---|---|
| below 600 ppm | blue | very fresh air |
| 600–999 ppm | green | good |
| 1000–1199 ppm | yellow | ventilating recommended |
| 1200–1399 ppm | red | ventilating needed |
| from 1400 ppm | red, blinking | poor air |
| from 1600 ppm | red, blinking + buzzer | ventilate now |

Thresholds and colors can be changed (`co2.t1` … `co2.t5`, `led.color.t1` … `led.color.t4`), e.g. stricter
values:
```
enable
set co2.t1=600
set co2.t2=800
set co2.t3=1000
set co2.t4=1200
set co2.t5=1400
save
```

- The traffic light follows a running average of the CO2 value. The SCD30 measures every 5 seconds and is
  corrected with the air pressure automatically.
- The small red status LED blinks briefly with every new measurement (not in the dark).
- The buzzer stays silent for the first 5 minutes after power-on. A button press mutes it and starts the
  5 minutes again. `sys.buzzer=0` turns it off completely.
- The light sensor is checked 1 minute after power-on and then every 10 minutes. In the dark the LEDs dim to
  `led.brightness_dark` and go back to `led.brightness` when it is light again (`led.auto=0` turns this off).

### Quiet (Night)

Quiet turns the LEDs, the small status LED and the buzzer off; measuring, the web page and MQTT keep
running. Switch it with the Home Assistant switch "Quiet", over MQTT (see
[below](#quiet-over-mqtt)) or with `set sys.quiet=1`.

- It is not saved automatically (no flash wear from nightly automations): it stays on through a watchdog
  restart, but a power loss, `reset`, the restart button or a firmware upload switch it back off. To keep it
  on permanently, `save` it like any other setting.
- When the device restarts with Quiet on, it stays dark from the start.
- Turning Quiet off brings the buzzer back only if `sys.buzzer=1`.

### Button

| Press | Action |
|---|---|
| short (0.1–3 s) | halve the LED brightness; below `led.brightness_dark` it jumps back to the maximum (not saved) |
| any press | mute the buzzer, start its 5-minute delay again |
| long (more than 3 s) | open the device's own WiFi network for setup (LEDs violet; restart to leave) |
| hold while plugging in | service menu |

For devices in public areas, set `sys.nobuttons=1`. All of the above then does nothing, including aborting a calibration. The
device also no longer opens its own WiFi network when WiFi fails at startup; it keeps trying to connect, so wrong WiFi settings
have to be fixed over USB. "Start calibration" on the web page needs `sys.password` to be set.

**Set `sys.password` as well.** Without it, anyone on the network or with USB access can turn the setting off again. With
`mqtt.config=1`, so can anyone allowed to publish to the device's topics on the broker, because MQTT commands need no password. The red
reset button and the USB port always stay active. If the stored settings are ever lost, the button works again.

### Service Menu

The number of violet LEDs shows the selected option. A short press selects the next one; a 2-second press
(or 10 seconds without pressing) starts it.

1. **Self test**: buzzer, LEDs, WiFi module, LoRa radio (if fitted), then the sensors: LED 1 light, LED 2
   CO2, LED 3 temperature/pressure, LED 4 humidity light up when their values are plausible
2. **Air test**: live CO2 value as LED color
3. **Configuration**: three values in a row, each chosen with short presses and confirmed with a 2-second
   press (or after 10 seconds): altitude 0–1000 m in 250 m steps (red LEDs), temperature offset 0–8 °C in
   2 °C steps (yellow LEDs), buzzer off/on (green LEDs)
4. **Calibration**: see below

### Calibration

Calibrate the CO2 sensor from time to time:

1. Put the device in fresh air (outdoors or at an open window) and wait at least 2 minutes.
2. Hold the button while plugging it in and select option 4 (four violet LEDs).
3. The LEDs turn white; LEDs 3–4 show the current value (blue ≤ 500, green ≤ 750, yellow ≤ 1500 ppm, red
   above). Once the readings are stable for about 3 minutes (±30 ppm, between 200 and 800 ppm), the sensor
   is set to 400 ppm. Unstable readings start the run again; a button press aborts it.
4. Done: LEDs blue and a short beep.

Without the button, start it over USB serial with `enable`, then `calibrate`, or with "Start calibration" on the web page
`/config`. Put the device in fresh air first: the current reading is set to 400 ppm, wherever the device is. It takes 3 minutes
or more, and the web page and MQTT keep running. The main page shows the progress and then the result under "Calibration" until
the next restart. Stop it with "Abort calibration" on `/config`, with `calibrate abort`, or with a button press. If the readings
stay unstable, it gives up after three runs (LEDs red), but the sensor has been set anyway, so repeat it in fresh air. With
Quiet on, or while `enable` is active on the serial port, the LEDs don't show the calibration.

Automatic self-calibration (`co2.asc=1`, off by default) needs 7 days of continuous operation with at least
1 hour of fresh air every day.

## Settings

### How to Change Settings

- **USB serial** (9600 baud): `enable`, then `set <key>=<value>`, `save`. `get <key>` or `get co2.*` reads
  values, `help` lists all keys.
- **Web page** `http://<IP address>/config`: all settings as a form.
- **MQTT** (if allowed with `mqtt.config=1`): JSON on `<base>/cmd`, see
  [Remote Configuration](#remote-configuration).

Values: numbers (or hex with `0x`), colors as hex RGB (`FF7F00`, `0xFF7F00` or `#ff7f00`), on/off as `1/0`,
`on/off` or `true/false`. Changes take effect right away; WiFi and MQTT connection settings after `reset`.
Without `save` a change lasts until the next restart.

### All Settings

| Key | Default | Meaning |
|---|---|---|
| `sys.name` | empty | device name (max. 32 characters): WiFi hostname, name of the device's own WiFi network, Home Assistant device name. Empty = `co2ampel-ng-<MAC address>`, e.g. `co2ampel-ng-F8F0056E78A2` |
| `sys.serialout` | 0 | print every measurement on the serial port |
| `sys.password` | empty | password for changes, see [Password](#password) |
| `sys.buzzer` | 1 | buzzer on (1) or off (0) |
| `sys.quiet` | 0 | Quiet: LEDs, status LED and buzzer off, see [Quiet](#quiet-night) |
| `sys.nobuttons` | 0 | for devices in public areas, see [Button](#button) |
| `co2.t1` … `co2.t5` | 600, 1000, 1200, 1400, 1600 | CO2 thresholds in ppm (400–10000): blue/green, green/yellow, yellow/red, red/blinking, buzzer |
| `co2.asc` | 0 | automatic self-calibration of the CO2 sensor |
| `led.brightness` | 125 | LED brightness 1–255 |
| `led.brightness_dark` | 20 | LED brightness in the dark 1–255 |
| `led.auto` | 1 | dim automatically in the dark |
| `led.color.t1` … `led.color.t4` | 007CB0, 00FF00, FF7F00, FF0000 | colors below `co2.t1`, up to `co2.t2`, up to `co2.t3`, from `co2.t3` |
| `wifi.ssid`, `wifi.pass` | empty | WiFi network and password |
| `mqtt.enabled` | 0 | MQTT on |
| `mqtt.broker` | empty | broker hostname or IP address (with TLS: the name in the certificate) |
| `mqtt.port` | 1883 | broker port (TLS usually 8883) |
| `mqtt.tls` | 0 | MQTT over TLS |
| `mqtt.user`, `mqtt.pass` | empty | broker login |
| `mqtt.client_id` | empty | MQTT client ID (empty = the device ID) |
| `mqtt.topic_prefix` | `co2ampel` | first part of the topics, see [MQTT](#mqtt-and-home-assistant) |
| `mqtt.ha` | 0 | Home Assistant discovery |
| `mqtt.config` | 0 | allow configuration over MQTT |
| `mqtt.interval` | 60 | seconds between two MQTT messages (10–3600) |

Passwords are never shown over MQTT or on the web pages. The WiFi and MQTT connection settings
(`wifi.*`, `mqtt.enabled/broker/port/tls/user/pass/ha/config`) and `sys.password` can only be changed over
USB serial or the web page, so a mistake over MQTT can't cut the device off the network.

### Password

Without a password, anyone on your network can change the settings on the web page (the stored passwords
themselves are never shown). With `sys.password` set (`enable`, `set sys.password=...`, `save`):
- `enable <password>` is required before `get`, `dump`, `json export/import`, `set` and `save` on the serial port
- the web configuration page and the WiFi form ask for it
- the main web page no longer shows the WiFi name, broker and topic
- a wrong password locks further attempts for 1, 2, 4 … 30 seconds

MQTT has no password of its own: protect it with the broker's access rules. A forgotten password can only
be cleared with a factory reset (see [Installation](#installation-and-updates)), which erases all settings.

### JSON

`json export` prints all settings as one line of JSON. `json import` (after `enable`) applies such an
object; any subset of keys is fine:
```
json import {"co2.t1":650,"co2.t2":900,"led.brightness":80,"save":true}
{"result":"OK","saved":true}
```
- All or nothing: if one value is invalid, nothing is changed
  (`{"result":"ERROR","errors":{"co2.t2":"Out of range"}}`)
- `"save":true` stores the values; without it they last until the next restart
- `"export":true` adds all settings to the reply
- Unknown keys are reported as warnings and ignored; passwords shown as `"***"` are left unchanged, so an
  export can be imported again as it is
- Nested objects work too: `{"co2":{"t1":600}}` is the same as `{"co2.t1":600}`

The web page, MQTT and the PC tool use the same import.

## Web Interface

When WiFi is connected, the device answers at `http://<IP address>/`:

| Address | Content |
|---|---|
| `/` | status page `CO2-Ampel-NG \| <name>`: measurements (with the current LED color), WiFi, MQTT (connected or the last error, last message, connection counters) and system (firmware, uptime, last restart, calibration, optional features the firmware was built with). Refreshes every 30 seconds |
| `/config` | all settings as a form, JSON upload and download, calibration, restart button |
| `/config.json` | all settings as JSON, passwords as `***` |
| `/json` | measurements as JSON, e.g. `{"c": 800, "t": 22.5, "h": 45.0, "p": 1013.2, "u": 22.0, "l": 512}` |
| `/diag` | diagnostics as JSON, see [Troubleshooting](#troubleshooting) |

`/json`: `c` CO2 (ppm), `t` temperature (°C), `h` humidity (%), `p` pressure (hPa), `u` temperature of the
pressure sensor (°C), `l` light sensor (0–1023). `p`/`u` only with a pressure sensor, `c`/`t`/`h` only with
a working CO2 sensor.

On the configuration page "Save" stores the changes at once (all or nothing). WiFi and MQTT changes apply
after the restart button.

## MQTT and Home Assistant

### Setup

```
enable
set mqtt.broker=192.168.1.10
set mqtt.user=co2ampel
set mqtt.pass=secret
set mqtt.enabled=1
set mqtt.ha=1
save
reset
```

`mqtt.ha=1` is only needed for Home Assistant. The web page shows in the MQTT section whether the
connection works, or why not.

### Topics

All topics start with `<mqtt.topic_prefix>/<device ID>`, the device ID being the WiFi MAC address, e.g.
`co2ampel/F8F0056E78A2` (called `<base>` below). The prefix may have several levels (`home/co2`), but no
`+`, `#` or empty levels.

| Topic | Content | Sent | Retained |
|---|---|---|---|
| `<base>/sensors` | `{"co2":812,"temperature":23.4,"humidity":45.1,"light":512,"pressure":964.1,"pressure_temp":26.8}` | every `mqtt.interval` seconds | no |
| `<base>/device` | `{"name":"co2ampel-ng-F8F0056E78A2","version":"0.9","reset":"power-on","sensors":["SCD30","LPS22HB"]}` | on connect and when `sys.name` changes | yes |
| `<base>/wifi` | `{"ssid":"MyWiFi","ip":"192.168.1.20","rssi":-60}` | every interval | no |
| `<base>/diag` | diagnostics, see [Troubleshooting](#troubleshooting) | every interval | yes |
| `<base>/status` | `online` / `offline` (also when the device disappears without saying goodbye) | on connect | yes |
| `<base>/state` | `{"quiet":false}` | on connect and on every change | yes |
| `<base>/cmd` | commands to the device, see below | – | – |
| `<base>/resp` | replies to commands | after each command | no |

`sensors`: CO2 in ppm, temperatures in °C, humidity in %, pressure in hPa, light 0–1023 (1024 until the
first reading 1 minute after power-on). Values the device doesn't have (no pressure sensor, CO2 sensor
missing or not ready yet) are left out.

After a change of the prefix or client ID the device reconnects with the new topics at once and removes its
retained messages under the old prefix.

### Home Assistant

With `mqtt.ha=1` Home Assistant (with the MQTT integration) creates a device named like `sys.name` with
the entities CO2, Temperature, Humidity, Light and (with a pressure sensor) Pressure, a switch "Quiet", and
the diagnostic entities Pressure sensor temperature, WiFi signal, Uptime, Reset cause, Last reset phase,
Free RAM minimum and Longest loop. They show "unavailable" while the device is offline (up to 45 seconds later if it loses power or hangs). `mqtt.ha=0` removes
them again.

- Moved a device? Just change `sys.name`: the device name in Home Assistant follows at once, and the entity
  IDs stay the same (e.g. `sensor.co2ampel_ng_f8f0056e78a2_co2`), so dashboards and automations keep working.
  If you named the device yourself in Home Assistant, your name stays until you reset it there.
- Needs Home Assistant 2025.10 or newer. Devices added with an older firmware keep their entity IDs.
- Any topic prefix works; renaming the prefix keeps the entities and their history.
- If your broker uses access rules, the device needs write access to `<prefix>/#`,
  `homeassistant/sensor/<device ID>/#` and `homeassistant/switch/<device ID>/#`; Home Assistant needs read
  access to `<prefix>/#` and `homeassistant/#` and write access to `<base>/cmd`. Without them the broker
  silently drops the messages and nothing appears in Home Assistant.
- Home Assistant finds the device under its default discovery prefix `homeassistant`. This only matters if
  you changed that prefix in Home Assistant's MQTT settings: then the device does not show up.

### Quiet Over MQTT

`{"sys.quiet":true}` / `{"sys.quiet":false}` on `<base>/cmd` switches [Quiet](#quiet-night) on and
off. This always works, also with `mqtt.config=0`, and is what the Home Assistant switch sends.
`<base>/state` shows the current value right after every change. It is not saved (see [Quiet](#quiet-night)).

### Remote Configuration

With `mqtt.config=1` (set over USB or the web page) the device accepts all settings as JSON on
`<base>/cmd` (max. 1 KB), exactly like `json import`, and replies on `<base>/resp`:

```bash
BASE=co2ampel/F8F0056E78A2
mosquitto_sub -h broker -u user -P pass -t "$BASE/resp" &
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m '{"co2.t1":650,"save":true}'   # change and save
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m '{"export":true}'              # read all settings
```

- Anyone allowed to publish to `<base>/cmd` can change settings: restrict it with the broker's access rules
  (and use TLS).
- WiFi/MQTT connection settings and `sys.password` are refused over MQTT.
- Only JSON is accepted; send the next command after the reply.
- Each `"save":true` writes the flash; don't save in automated loops.

### MQTT over TLS

The WiFi module checks the broker certificate against its stored root certificates and against the name
in `mqtt.broker` (use the name from the certificate, not an IP address). TLS 1.2 with RSA certificates.

1. Install the broker's root certificate once (close the serial monitor first). Let's Encrypt (ISRG
   Root X1) is not among the factory certificates:
   ```bash
   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py add --host mqtt.example.com:8883
   ```
   The tool fetches the certificate chain, saves a backup of the stored certificates, installs the root
   and checks it; the device restarts afterwards. Renewed broker certificates need nothing on the device;
   run the tool again only if the broker moves to another certificate authority.
2. Switch to TLS: `enable`, `set mqtt.tls=1`, `set mqtt.port=8883`, `save`, `reset`.

More tool commands: `list` (stored certificates), `remove <n>`, `backup [file]`, `restore <file>`.

## Backup, Restore and Provisioning

The PC tool `scripts/co2ampel_tool.py` talks to the device over USB (close the serial monitor first). If a
password is set, give it to the tool with `export CO2AMPEL_PASSWORD=...`.

| Task | Command |
|---|---|
| backup | `co2ampel_tool.py export backup.json` (contains your passwords in plain text: store it safely) |
| restore | `co2ampel_tool.py import backup.json` |
| set up a new device | `co2ampel_tool.py provision --config site.json` |

Without the tool: `enable`, `json export`, keep the line; to restore, `enable` and `json import` with that
line, then `reset`.

`provision` imports and saves the settings file, installs the broker's root certificate if TLS is on,
restarts the device and reports whether MQTT connects. Example `site.json`:
```json
{
  "wifi.ssid": "MyNetwork",
  "wifi.pass": "secret",
  "mqtt.enabled": true,
  "mqtt.broker": "mqtt.example.com",
  "mqtt.port": 8883,
  "mqtt.tls": true,
  "mqtt.user": "co2ampel",
  "mqtt.pass": "secret"
}
```

## Troubleshooting

| Problem | What to do |
|---|---|
| No CO2 values, traffic light stays red | The CO2 sensor was not found at power-on (`status` / boot log: `Error: CO2 sensor not found`). Unplug the device for a few seconds. |
| Device not reachable on WiFi | Check `status` over USB. If WiFi fails at power-on, the device opens its own WiFi network for setup; a lost connection is retried automatically, every 30 seconds at first and up to every 5 minutes. |
| MQTT not connected | The MQTT section of the web page shows the last error (e.g. wrong password, broker not reachable). With TLS: is the root certificate installed, and does `mqtt.broker` match the certificate? |
| Nothing appears in Home Assistant | `mqtt.ha=1`? Check the broker access rules (see [Home Assistant](#home-assistant)). |
| Upload fails with "Resource busy" | A serial monitor still has the port open; close it. |
| Device doesn't show up on USB | Plug it in directly (no hub); press the red reset button twice quickly for the bootloader. |
| Device restarts by itself | See below. |

### Restarts and Diagnostics

A watchdog restarts the device if the firmware hangs for 30 seconds (2 minutes while it connects to the network). The cause of the last restart is shown
in `status`, on the web page, in Home Assistant ("Reset cause") and in the MQTT `device` topic: `power-on`,
`watchdog`, `fault` (crash), `software` (`reset`, restart button, upload), `reset pin`, `brown-out`.

Serial `diag`, the web address `/diag` and the MQTT topic `<base>/diag` give more details:

| Key | Meaning |
|---|---|
| `uptime` | seconds since the last start |
| `resets` | restarts since power-on |
| `reset` | cause of the last restart |
| `last_event` | `hang` (watchdog), `fault` (crash) or `none` |
| `last_phase` | what the firmware was doing before the restart (e.g. `mqtt.loop`, `co2`, `web`) |
| `last_uptime` | how long the device had been running before the restart (seconds) |
| `ram_free`, `ram_free_min`, `heap_free` | free memory now / lowest since start / inside the heap (bytes) |
| `loop_max_ms` | longest pause of the main loop (ms) |
| `wifi_connects`, `mqtt_connects`, `mqtt_failures`, `mqtt_error` | connection counters since start |
| `i2c_errors` | sensor bus errors since start; the device repairs the bus itself, a rising count points to a sensor or wiring problem |
| `net_write_timeouts` | network sends given up because the other side stopped reading; the connection is closed and made again |

These values survive restarts, but not a power loss: read them before unplugging the device. If the device
restarts repeatedly, please open an issue with the `diag` output and the firmware version (`last_pc` and
`last_lr` tell the developer where it happened).

## Serial Command Reference

USB serial, 9600 baud. Lines may end with CR, LF or CRLF.

```
enable [password]  allow changes (LEDs violet, traffic light paused until exit)
exit               leave enable mode
status             measurements, sensors, LEDs, WiFi and MQTT status
diag               diagnostics as JSON
version            firmware version
get <key>          read a setting (get co2.* reads a group)
set <key>=<value>  change a setting
save               store the settings
dump               all settings as set commands
json export        all settings as JSON
json import        apply settings from JSON (on the same line or pasted afterwards, max. 2 KB)
help               list all keys
i2c scan           list the sensors found on the I2C buses
calibrate          start the fresh air calibration, needs enable
calibrate abort    stop it, needs enable
reset              restart the device
```

With `sys.serialout=1` every measurement is printed:
```
c: 812        CO2 (ppm)
t: 23.4       temperature (°C)
h: 45.1       humidity (%)
l: 512        light sensor
p: 964.10     pressure (hPa, only with a pressure sensor)
u: 26.80      pressure sensor temperature (°C, only with a pressure sensor)
```

## License

This project keeps the Creative Commons Attribution Share-Alike license of the original CO2-Ampel project.

## Credits

- **Original design and firmware**: Watterott electronic – https://github.com/watterott/CO2-Ampel
- **Original documentation**: https://learn.watterott.com/breakouts/co2-ampel/

## Support

For problems with this firmware, please open an issue in this repository. For hardware questions, see the
[original documentation](https://learn.watterott.com/breakouts/co2-ampel/).
