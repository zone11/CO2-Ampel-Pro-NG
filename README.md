# CO2-Ampel Pro NG (Next Generation)

New / updated firmware for the Watterott CO2-Ampel Pro hardware, initially based on v24 of the original firmware.

✅ **Status**: Fully operational and tested on hardware

## Overview

This is going to be a modernized version of the original CO2-Ampel firmware, migrated from Arduino IDE to PlatformIO for better dependency management and professional development workflow.

**Goals:**
- ✅ Custom board definition for CO2-Ampel Pro hardware
- ✅ Hardware support: LEDs, buzzer, light sensor, CO2/pressure sensors, WiFi (LoRa radio: self test only)
- ✅ One firmware image, configured at runtime (thresholds, colors, WiFi, MQTT, ASC)
- ✅ Works with PlatformIO CLI and IDE

## Hardware

- **MCU**: ATSAMD21G18 ARM Cortex-M0+ @ 48MHz
- **CO2 Sensor**: Sensirion SCD30 or SCD4x (SCD40/SCD41)
- **Air Pressure sensor**: (BMP280 or LPS22HB)
- **LEDs**: 4x WS2812 RGB intelligent LEDs
- **Buzzer**: PWM-controlled acoustic buzzer
- **Additional Features**:
  - ATWINC1500B WiFi module (optional)
  - RFM95W/96W LoRa transceiver (optional)


## New Features
**Implemented Features:**
- ✅ Configurable CO2 thresholds via serial commands (no recompilation needed!)
- ✅ Customizable LED colors via serial commands
- ✅ Settings dump feature for backup/restore (key/value)
- ✅ MQTT support: measurements, device/WiFi info, online status, remote configuration
- ✅ Home Assistant MQTT discovery
- ✅ MQTTS (MQTT over TLS, certificate and hostname checked)
- ✅ PC tool for provisioning (settings + TLS root certificate in one run, no extra flashing)
- ✅ Settings with version and CRC, kept across `pio run -t upload`
- ✅ Automatic self-calibration (ASC) switchable at runtime (`co2.asc`)
- ✅ LED brightness: maximum, dark level, automatic dimming on/off, LEDs + buzzer off (`led.*`)
- ✅ Configuration as JSON: `json export` / `json import` on serial, JSON over MQTT, web import page
- ✅ Optional enable password for serial and the web forms

**Planned Features:**
- [ ] LoRaWAN/TTN integration
- [ ] Enhanced web interface with charts
- [ ] Over-the-air (OTA) firmware updates


## Build Configurations

The firmware is designed for the CO2-Ampel Pro hardware with WiFi (WINC1500) and pressure sensor (BMP280/LPS22HB).

| Environment | Description |
|------------|-------------|
| `co2ampel_pro` | Default build for Pro hardware |
| `co2ampel_pro_lora` | Placeholder for LoRaWAN; currently the same firmware as `co2ampel_pro` |

The original firmware's variants (Basic, Plus, COVID) are not separate builds: the hardware is detected at
boot, and the COVID thresholds are a preset you set with `set co2.*` (see below).

Optional features can be left out at compile time to save flash; all are on by default. Add the switch to
`build_flags` in `platformio.ini` (flash use with all features: about 88 KB of 246 KB):

| Switch | Leaves out | Saves |
|--------|-----------|-------|
| `-DWITH_WEB=0` | web interface (`/`, `/json`, `/cmk-agent`, `/config`, WiFi form; AP mode then has no use) | ~7 KB |
| `-DWITH_MQTT_CONFIG=0` | `mqtt.config` (configuration over MQTT) | ~2 KB, 3.3 KB RAM |
| `-DWITH_HA=0` | `mqtt.ha` (Home Assistant discovery) | ~1.5 KB |

## Usage

### LED Color Codes (Default Values)

All thresholds and colors are configurable via serial commands!

- **Blue** (0x007CB0): < 600 ppm (very fresh air)
- **Green** (0x00FF00): 600-999 ppm (good air quality)
- **Yellow** (0xFF7F00): 1000-1199 ppm (ventilation recommended)
- **Red** (0xFF0000): 1200-1399 ppm (ventilation needed)
- **Red Blinking**: ≥ 1400 ppm (poor air quality)
- **Buzzer**: ≥ 1600 ppm (critical - requires immediate ventilation), beeps in time with the blinking

Behavior:
- The traffic light follows a running average of the CO2 value (updated every second); the SCD30 measures
  every 5 s and is pressure-compensated automatically when a pressure sensor is present
- The buzzer stays silent for the first 5 minutes after power-on; a button press mutes it and restarts
  the 5-minute delay. `sys.buzzer=0` turns it off completely
- Ambient light is checked 1 minute after boot and then every 10 minutes; in the dark (value < 20) the
  LEDs dim to `led.brightness_dark` and return to `led.brightness` when it is light again (`led.auto=0`:
  always `led.brightness`)
- `led.disable=1` turns the LEDs, the status LED and the buzzer off immediately (e.g. at night over MQTT,
  works without `mqtt.config`; also the violet `enable` indicator); measuring and MQTT continue. Without
  `save` it lasts until the next restart
- The red status LED blinks briefly with every new measurement (not in the dark)
- Without a CO2 sensor (not found at boot) the traffic light is red, the boot log shows `Error: CO2 sensor
  not found` and no CO2/temperature/humidity values are published. At boot the firmware frees an I2C bus
  that a sensor still holds after a restart in the middle of a transfer (`I2C bus recovered` in the boot
  log); if the CO2 sensor is still missing, unplug the device briefly

**Customize your thresholds and colors:**
```bash
# Example: Set stricter thresholds (COVID mode)
enable
set co2.t1=600
set co2.t2=800
set co2.t3=1000
set co2.t4=1200
set co2.t5=1400
save

# Example: Change LED colors
enable
set led.color.t1=0000FF
set led.color.t2=00FF00
set led.color.t3=FFA500
set led.color.t4=FF0000
save

# Query current settings
get co2.*
get led.color.*
```

### Button Functions

- **Short press (100ms-3s)**: Halve the LED brightness; below `led.brightness_dark` it jumps back to
  `led.brightness` (not saved)
- **Any press**: mutes the buzzer and restarts its 5-minute start delay
- **Long press (>3s)**: Start WiFi Access Point mode (WiFi versions only, LEDs turn violet; reboot to leave it)
- **Hold during power-on**: Enter service menu

### Service Menu Options

1. **Self Test**: Buzzer, LEDs, WiFi module (if detected), LoRa radio (reported, optional), then sensors:
   LED 1 light, LED 2 CO2, LED 3 temperature/pressure, LED 4 humidity turn on when in range
2. **Air Test**: Live CO2 monitoring with color feedback
3. **Configuration**: three values in a row, each chosen with short presses and confirmed with a 2 s press
   (or 10 s timeout): altitude 0–1000 m in 250 m steps (red LEDs), temperature offset 0–8 °C in 2 °C steps
   (yellow LEDs), buzzer off/on (green). Altitude and offset are stored in the CO2 sensor, the buzzer in the settings
4. **Calibration**: forced recalibration to 400 ppm in fresh air (see Calibration)

Menu navigation: the number of violet LEDs is the selected option; a short press selects the next one, a 2 s
press (or 10 s without pressing) starts it.

### Serial Commands

Connect via USB serial at 9600 baud, 8N1:

```
enable [pw]    Allow changes (set/save; LEDs violet and dimmed unless led.disable, traffic light and buzzer paused);
               with sys.password set: enable <password>
exit           Leave enable mode (traffic light resumes)
status         Show measurements, CO2 sensor info, LED state (incl. the color shown) and WiFi/MQTT status
i2c scan       List responding I2C devices on both buses (with chip IDs)
winc bridge    WiFi module flash access for scripts/co2ampel_tool.py (enable; restarts the device)
reset          Reset device (after enable it answers OK and turns the LEDs off first)
version        Query firmware version
get <key>      Read a single setting
get <prefix.*> Read a group of settings by prefix
set <key>=<v>  Update a setting value
save           Save settings to flash
dump           Print all settings as set commands (backup, provisioning format)
json export    Print all settings as one JSON object
json import    Apply settings from a JSON object (on the same line or pasted afterwards, max 2 KB)
help           List all available keys
```

Lines may end with CR, LF or CRLF; a line without ending is accepted after 500 ms.

**Key list (settings):**
```
sys.serial_output
sys.password
sys.buzzer
co2.t1
co2.t2
co2.t3
co2.t4
co2.t5
co2.asc
led.brightness
led.brightness_dark
led.auto
led.disable
led.color.t1
led.color.t2
led.color.t3
led.color.t4
wifi.ssid
wifi.pass
mqtt.enabled
mqtt.broker
mqtt.port
mqtt.tls
mqtt.user
mqtt.pass
mqtt.client_id
mqtt.topic_prefix
mqtt.ha
mqtt.config
mqtt.interval
```

Value formats: numbers are decimal (or hex with `0x`), colors are hex RGB with optional `0x` or `#`
(`FF7F00`, `0xFF7F00`, `#ff7f00`), booleans accept `1/0`, `on/off`, `true/false`.
Thresholds, colors, LED settings, buzzer and `mqtt.ha` apply immediately; the other WiFi and MQTT settings
apply after `reset`.

LED brightness (1–255): `led.brightness` is the normal/maximum level (default 125), `led.brightness_dark`
the level in the dark (default 20), `led.auto=0` keeps `led.brightness` regardless of the light sensor.
`status` shows the current LED state.

**JSON configuration:** `json export` prints all settings in one line, e.g.
`{"sys.serial_output":false,...,"co2.t1":600,...,"led.color.t1":"007CB0",...}` (numbers, `true`/`false`,
colors as hex strings). `json import` (after `enable`) applies such an object; any subset of keys is
fine, nested objects also work (`{"co2":{"t1":600}}` = `{"co2.t1":600}`):
```
json import {"co2.t1":650,"co2.t2":900,"led.brightness":80,"save":true}
{"result":"OK","saved":true}
```
- All or nothing: if one value is invalid, nothing is changed
  (`{"result":"ERROR","errors":{"co2.t2":"Out of range"}}`)
- `"save":true` also writes the flash (otherwise the values last until the next restart);
  `"export":true` adds the complete configuration to the reply (`"config":{...}`)
- Unknown keys are reported as `warnings` and ignored (e.g. an export from a newer firmware); masked
  passwords (`"***"`) are left unchanged, so an export can be imported back as is
- The same import is used over MQTT, on the web page `/config` and by `scripts/co2ampel_tool.py`

**Enable password** (`sys.password`, empty = none): once set (`enable`, `set sys.password=...`, `save`),
`enable <password>` is required before `get`, `dump`, `json export/import`, `set`, `save` and
`winc bridge`; the web WiFi form and the web configuration page ask for it as well (without a password
both are open to anyone on the network, except for the passwords themselves). MQTT needs no password: protect `<base>/cmd` with broker ACLs. A wrong password
locks further attempts for 1, 2, 4 ... 30 s. A forgotten password can only be cleared with a factory reset,
which erases all settings: `CO2AMPEL_FACTORY_RESET=1 pio run -e co2ampel_pro -t upload`.

`co2.asc` turns the CO2 sensor's automatic self-calibration on or off (default off; see Calibration).
WiFi and MQTT connection settings (`wifi.*`, `mqtt.enabled/broker/port/tls/user/pass/ha/config`) and
`sys.password` can only be changed via serial (WiFi also via the web form); passwords are only shown to local
(serial) callers, over MQTT they read as `***`.

### Provisioning (before deployment)

`scripts/co2ampel_tool.py provision --config site.json` sets up a device in one run: it imports the JSON
file with `json import` and saves it (all or nothing), installs the MQTT broker's root CA if `mqtt.tls` is
on, restarts the device and reports whether MQTT connects. Values are never printed. With an enable password
set, export it first: `export CO2AMPEL_PASSWORD=...` (all tool commands use it).

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

`co2ampel_tool.py export [file.json]` saves all settings of a device (file mode 600, contains the
passwords), `co2ampel_tool.py import file.json` writes a file back without the certificate step.

### Settings Backup and Restore

Settings are stored in flash (last 2 KB, with version and CRC). `pio run -t upload` keeps them: the upload script reads the settings area and writes it back with the new firmware. Flashing with another tool (e.g. plain bossac or the Arduino IDE) erases them, so keep a backup with `dump`. Settings written by a newer firmware (after a downgrade) are not overwritten: the older firmware runs
with defaults until you `save`, and going back to the newer firmware finds the original settings again.

**Backup Settings:** `co2ampel_tool.py export backup.json`, or send `json export` (or `dump` for
readable `set` lines) after `enable` and keep the output. It contains the WiFi/MQTT passwords in clear text,
so store it safely.

**Restore Settings:** `co2ampel_tool.py import backup.json`, or send `enable` and `json import` with the
exported line (`dump` output: paste the `set` lines, then `save`); then `reset`.

**Factory reset:** `CO2AMPEL_FACTORY_RESET=1 pio run -e co2ampel_pro -t upload` flashes the firmware without
the stored settings; the device starts with the defaults (also clears a forgotten `sys.password`).


### WiFi Configuration via Serial

You can configure WiFi settings via serial commands:

**Configure WiFi:**
```bash
# 1. Connect to serial monitor
pio device monitor -b 9600

# 2. Allow changes
enable

# 3. Set WiFi credentials
set wifi.ssid=YourWiFiSSID
set wifi.pass=YourWiFiPassword

# 4. Save settings
save

# 5. Reset device to connect
reset
```

**Check WiFi Status:**
```bash
# Query WiFi configuration
get wifi.*
```

**Serial measurement output** (off by default): with `sys.serial_output=1` every new measurement is printed as
```
c: 812        CO2 (ppm)
t: 23.4       temperature (°C)
h: 45.1       humidity (%)
l: 512        light sensor
p: 964.10     pressure (hPa, only with a pressure sensor)
u: 26.80      pressure sensor temperature (°C, only with a pressure sensor)
```
```bash
enable
set sys.serial_output=1
save
```

**Notes:**
- WiFi credentials are stored in flash (kept across `pio run -t upload`; backup with `dump` anyway)
- After setting WiFi credentials, device will auto-connect on next boot
- If the connection fails at boot, the device starts AP (Access Point) mode instead; a lost connection is
  retried every 30 s (MQTT reconnects every 30 s as well)
- The device registers as hostname `CO2AMPEL-XX-XX` (last two MAC bytes)

### WiFi Web Interface

When WiFi is enabled, the device creates either:
- **Client Mode**: Connects to configured WiFi network
- **AP Mode**: Creates access point `CO2AMPEL-XX-XX`

Available endpoints:
- `/` - Live readings, plus a WiFi form (SSID + password, saved to flash, applies after a reboot; with
  `sys.password` set the form asks for it and does not show the current SSID)
- `/json` - JSON API with sensor readings
- `/cmk-agent` - CheckMK monitoring agent format (plugin section + local check with the CO2 thresholds)
- `/config` - Configuration page: all settings as a form (grouped like the keys; passwords are never shown,
  an empty password field keeps the current one), "Save" stores immediately (all or nothing, like
  `json import`); JSON upload (file or paste) and download; restart button (WiFi/MQTT changes apply after
  a restart). With `sys.password` set, every action asks for it
- `/config.json` - Download of all settings as JSON, passwords as `***` (importing such a file keeps the
  current passwords); with `sys.password` set, the config page also offers a download with passwords

Example JSON response:
```json
{"c": 800, "t": 22.5, "h": 45.0, "p": 1013.2, "u": 22.0, "l": 512}
```
`c` CO2 (ppm), `t` temperature (°C), `h` humidity (%), `l` light sensor value; `p` pressure (hPa) and
`u` pressure sensor temperature (°C) only with a pressure sensor, `c`/`t`/`h` only with a working CO2
sensor (the same applies to `/cmk-agent`, and the page `/` shows `--`). The page `/` refreshes every 2 minutes.

### MQTT

With `mqtt.enabled=1` the device connects to `mqtt.broker` and uses the base topic
`<mqtt.topic_prefix>/<device id>`, where the device id is the WiFi MAC address (e.g.
`co2ampel/F8F0056E78A2`). `mqtt.client_id` only sets the MQTT client id (empty = device id).

| Topic | Payload | Published | Retained |
|---|---|---|---|
| `<base>/sensors` | `{"co2":812,"temperature":23.4,"humidity":45.1,"light":512,"pressure":964.1,"pressure_temp":26.8}` | every `mqtt.interval` s (default 60) | no |
| `<base>/device` | `{"version":"0.5","sensors":["SCD30","LPS22HB"]}` (sensors detected at boot) | every interval | yes |
| `<base>/wifi` | `{"ssid":"MyWiFi","ip":"192.168.1.20","rssi":-60}` | every interval | no |
| `<base>/status` | `online`, or `offline` (last will, sent by the broker when the device disappears) | on connect | yes |
| `<base>/resp` | reply to a command, see below | after each command | no |

`sensors` units: CO2 ppm, temperatures °C, humidity %, pressure hPa, light raw ADC value (0–1023; 1024 until the first reading 1 minute after boot);
`pressure` and `pressure_temp` (the pressure sensor's own temperature) only with a pressure sensor;
`co2`, `temperature` and `humidity` only once the CO2 sensor has delivered a measurement (missing if the
CO2 sensor was not found at boot; the boot log then shows `Error: CO2 sensor not found`).

**MQTTS (TLS)** (off by default): the WiFi module does the TLS itself and checks the broker certificate
against its stored root CAs and against the hostname in `mqtt.broker` (use the name from the certificate,
not an IP address). TLS 1.2 with RSA certificate chains.

1. Make sure the broker's root CA is in the WiFi module. The module ships with a few roots; Let's Encrypt
   (ISRG Root X1) is not among them. Install it from a PC with `scripts/co2ampel_tool.py` (PlatformIO's
   Python has everything it needs; close the serial monitor first):
   ```bash
   ~/.platformio/penv/bin/python scripts/co2ampel_tool.py add --host mqtt.example.com:8883
   ```
   The tool fetches the broker's certificate chain, finds its root CA (certifi bundle), saves a backup of
   the store, writes and verifies it. The firmware passes the module's flash through for that
   (`winc bridge`, LEDs blue), no extra flashing is needed; the device restarts afterwards. Only RSA root
   certificates are supported; if the store is full, expired roots are removed.

   | Command | |
   |---|---|
   | `list` | stored roots (key size, expiry, name) |
   | `add --host host:port` / `add --pem root.pem` | add the broker's root CA / a root from a file |
   | `remove <n>` | remove root n |
   | `backup [file]` / `restore <file>` | save / write back the whole store (4 KB) |
   | `export [file]` / `import <file>` | settings to / from a JSON file |
   | `provision --config <file>` | settings and root CA in one run, see Provisioning |

   The root CA is only needed once: renewed broker certificates (e.g. Let's Encrypt every few months)
   need nothing on the device. Run the tool again when the broker moves to another CA, or after a
   firmware update of the WiFi module itself (that resets the store to the factory roots).
2. Switch MQTT to TLS: `enable`, `set mqtt.tls=1`, `set mqtt.port=8883`, `save`, `reset`.

If the connection fails, the boot log shows `TLS: root CA stored in the WINC1500? Does mqtt.broker match
the certificate?`.

**Home Assistant discovery** (off by default): enable it with `enable`, `set mqtt.ha=1`, `save` (applies
immediately). The device then publishes retained discovery configs (now and on every connect) to `homeassistant/sensor/<device id>/<value>/config`, so Home Assistant (with
the MQTT integration) creates a device "CO2-Ampel XXXX" with the entities CO2, Temperature, Humidity,
Light, and, with a pressure sensor, Pressure; "Pressure sensor temperature" and "WiFi signal" appear as
diagnostic entities. They read from `<base>/sensors` and `<base>/wifi` and become unavailable when
`<base>/status` is `offline`. `set mqtt.ha=0` removes the entities again right away (one empty retained
config each, only while MQTT is connected); with `mqtt.ha=0` nothing is sent to `homeassistant/`.

**LED control over MQTT** (always available when MQTT is on): `{"led.disable":true}` /
`{"led.disable":false}` on `<base>/cmd` switches the LEDs (and buzzer) off and on, also with
`mqtt.config=0`. It applies in RAM only (back to the saved value after a restart); everything else is
answered with `{"result":"ERROR","errors":{...:"Configuration over MQTT is off (mqtt.config)"}}`.

**Remote configuration** (off by default): enable it locally with `enable`, `set mqtt.config=1`, `save`
(applies immediately). The device then accepts all settings as **JSON objects** on `<base>/cmd` (max. 1 KB),
exactly like `json import`, and replies with JSON on `<base>/resp`:

```bash
BASE=co2ampel/F8F0056E78A2
mosquitto_sub -h broker -u user -P pass -t "$BASE/resp" &
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m '{"led.disable":true}'          # RAM only
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m '{"co2.t1":650,"save":true}'     # saved
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m '{"export":true}'                # read all
```

Replies: `{"result":"OK"}`, `{"result":"OK","saved":true}`, `{"result":"OK","config":{...}}` or
`{"result":"ERROR","errors":{"co2.t1":"Out of range"}}`.

Rules and notes:
- No password over MQTT: anyone allowed to publish to `<base>/cmd` can change the settings, so restrict it
  with broker ACLs (and use TLS)
- Without `"save":true` changes last until the next restart, e.g. `{"led.disable":true}` in the evening
  and `{"led.disable":false}` in the morning without wearing the flash
- Passwords read as `***` in the export; WiFi/MQTT connection settings (`wifi.*`, `mqtt.enabled/broker/
  port/tls/user/pass/ha/config`) and `sys.password` are refused (`Only via serial or web`) unless the value
  is unchanged, so a remote mistake can't cut the device off the network and an export can be sent back
- Text commands (`set ...`) are not accepted over MQTT (`JSON object expected`)
- After handling a command the device clears `<base>/cmd` with an empty retained message, so a command
  published with retain is executed only once
- Commands arriving while one is still being handled are ignored; send the next one after the reply
- Each `save` writes the flash; avoid saving in automated loops (flash endurance)

## Calibration

The CO2 sensor should be calibrated periodically via the service menu:

1. Place device in fresh air (outdoor or well-ventilated area)
2. Wait at least 2 minutes for sensor to stabilize
3. Enter the service menu (hold button during power-on) and select option 4 (four violet LEDs)
4. The LEDs turn white; LEDs 3–4 show the current CO2 value (blue ≤ 500, green ≤ 750, yellow ≤ 1500,
   red above). For about 3 minutes the readings must be stable (±30 ppm, 200–800 ppm), then the sensor is
   set to 400 ppm. Unstable readings restart the run automatically; a button press aborts it
5. Done: LEDs blue and a short beep (serial: `Calibration OK`)

Automatic Self-Calibration (ASC) is disabled by default. Enable it with `enable`, `set co2.asc=1`, `save`
(the default for new devices is `CO2_ASC` in `include/app_config.h`). ASC requires 7 days of
continuous operation with at least 1 hour of fresh air exposure daily.

## Development

### Adding New Features

1. The code is split into modules under `src/`: `main` (setup/loop), `sensors`, `led_control` (traffic
   light, brightness, buzzer), `settings_storage` (flash record), `settings_registry` (key table, enable
   password and JSON import, used by serial, web and MQTT), `serial_settings` (get/set/save/dump and
   JSON export), `json_parser` (JSON object parser), `serial_console` (serial commands), `wifi_manager`, `webserver`, `mqtt_service` (publish, HA discovery, commands),
   `tls_client` (TLS with hostname check), `winc_bridge` (WiFi module flash access), `ui_menu`/`diagnostics`
   (service menu, self test), `i2c_utils`, `system_control`. Compile-time defaults and feature switches live
   in `include/app_config.h`, pins in `variants/co2ampel/variant.h`. PC tools are in `scripts/`
   (`upload_samba.py` upload with settings kept, `co2ampel_tool.py` provisioning and root certificates)
2. New user settings: append the field to `SETTINGS` (`include/app_types.h`), set a default in
   `settings_defaults()` and validate it in `settings_validate()`, bump `SETTINGS_VERSION`
   (`src/settings_storage.cpp`), and add a key in `src/settings_registry.cpp`. Older, shorter records are
   read with their own size and get the defaults for the new fields automatically
3. Update `platformio.ini` if new dependencies are needed
4. Build with `pio run`, upload with `pio run -t upload` (settings are kept; connect the device directly,
   not through a hub, and close any serial monitor first)
5. Never use `%f` in printf formats: the build uses link time optimization without printf float support,
   format one-decimal values with `fmt1()` (`include/sensors.h`)

### Code Style

The original code is in German and follows Arduino conventions. Future enhancements should:
- Maintain compatibility with existing hardware
- Document changes in English
- Follow PlatformIO best practices

## License

This project maintains the Creative Commons Attribution Share-Alike License from the original CO2-Ampel project.

## Credits

- **Original Design**: Watterott electronic
- **Original Firmware**: Watterott electronic - https://github.com/watterott/CO2-Ampel
- **Original Documentation**: Watterott electronic- https://learn.watterott.com/breakouts/co2-ampel/

## Support

For issues specific to this PlatformIO port, please open an issue in this repository.

For hardware-related questions, refer to the original documentation:
- https://learn.watterott.com/breakouts/co2-ampel/
- https://github.com/watterott/CO2-Ampel
