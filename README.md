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
- ✅ Settings with version and CRC, kept across `pio run -t upload`
- ✅ Automatic self-calibration (ASC) switchable at runtime (`co2.asc`)

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
`build_flags` in `platformio.ini` (flash use with all features: about 89 KB of 246 KB):

| Switch | Leaves out | Saves |
|--------|-----------|-------|
| `-DWITH_CERT_CMDS=0` | serial `cert` commands (the PC tool still works) | ~10 KB |
| `-DWITH_WEB=0` | web interface (`/`, `/json`, `/cmk-agent`, WiFi form; AP mode then has no use) | ~5 KB |
| `-DWITH_MQTT_CONFIG=0` | `mqtt.config` (configuration over MQTT) | ~2 KB, 1.8 KB RAM |
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
  LEDs dim to brightness 20 and return to `sys.brightness` when it is light again
- The red status LED blinks briefly with every new measurement (not in the dark)

**Customize your thresholds and colors:**
```bash
# Example: Set stricter thresholds (COVID mode)
remote on
set co2.t1=600
set co2.t2=800
set co2.t3=1000
set co2.t4=1200
set co2.t5=1400
save

# Example: Change LED colors
remote on
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

- **Short press (100ms-3s)**: Halve the LED brightness; below the minimum it jumps back to the default (not saved)
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
remote on      Enable remote control (required for set/save; LEDs turn violet, traffic light paused)
remote off     Disable remote control
status         Show measurements, CO2 sensor info and WiFi/MQTT status
i2c scan       List responding I2C devices on both buses (with chip IDs)
cert list      List the TLS root certificates in the WiFi module (remote on; restarts the device)
cert add       Add a root certificate: paste the PEM afterwards (remote on; restarts the device)
cert remove <n> Remove root certificate n from the list (remote on; restarts the device)
reset          Reset device (with remote on it answers OK and turns the LEDs off first)
version        Query firmware version
get <key>      Read a single setting
get <prefix.*> Read a group of settings by prefix
set <key>=<v>  Update a setting value
save           Save settings to flash
dump           Print all settings as set commands
help           List all available keys
```

Lines may end with CR, LF or CRLF; a line without ending is accepted after 500 ms.

**Key list (settings):**
```
sys.serial_output
sys.brightness
sys.buzzer
co2.t1
co2.t2
co2.t3
co2.t4
co2.t5
co2.asc
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
Thresholds, colors, brightness and buzzer apply immediately; WiFi and MQTT settings apply after `reset`.

`co2.asc` turns the CO2 sensor's automatic self-calibration on or off (default off; see Calibration).
WiFi and MQTT connection settings (`wifi.*`, `mqtt.enabled/broker/port/tls/user/pass/ha/config`) can only be
changed via serial or the web form, and passwords are only shown to local (serial) callers; over MQTT they
read as `***`.

### Settings Backup and Restore

Settings are stored in flash (last 2 KB, with version and CRC). `pio run -t upload` keeps them: the upload script reads the settings area and writes it back with the new firmware. Flashing with another tool (e.g. plain bossac or the Arduino IDE) erases them, so keep a backup with `dump`. Settings written by a newer firmware (after a downgrade) are not overwritten: the older firmware runs
with defaults until you `save`, and going back to the newer firmware finds the original settings again.

**Backup Settings:**
```bash
# Connect to serial port and send:
dump
```


### WiFi Configuration via Serial

You can configure WiFi settings via serial commands:

**Configure WiFi:**
```bash
# 1. Connect to serial monitor
pio device monitor -b 9600

# 2. Enable remote control
remote on

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
remote on
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
- `/` - Live readings, plus a WiFi form (SSID + password, saved to flash, applies after a reboot)
- `/json` - JSON API with sensor readings
- `/cmk-agent` - CheckMK monitoring agent format (plugin section + local check with the CO2 thresholds)

Example JSON response:
```json
{"c": 800, "t": 22.5, "h": 45.0, "p": 1013.2, "u": 22.0, "l": 512}
```
`c` CO2 (ppm), `t` temperature (°C), `h` humidity (%), `l` light sensor value; `p` pressure (hPa) and
`u` pressure sensor temperature (°C) only with a pressure sensor. The page `/` refreshes every 2 minutes.

### MQTT

With `mqtt.enabled=1` the device connects to `mqtt.broker` and uses the base topic
`<mqtt.topic_prefix>/<device id>`, where the device id is the WiFi MAC address (e.g.
`co2ampel/F8F0056E78A2`). `mqtt.client_id` only sets the MQTT client id (empty = device id).

| Topic | Payload | Published | Retained |
|---|---|---|---|
| `<base>/sensors` | `{"co2":812,"temperature":23.4,"humidity":45.1,"light":512,"pressure":964.1,"pressure_temp":26.8}` | every `mqtt.interval` s (default 60) | no |
| `<base>/device` | `{"version":"0.4.0","sensors":["SCD30","LPS22HB"]}` (detected sensors) | every interval | yes |
| `<base>/wifi` | `{"ssid":"MyWiFi","ip":"192.168.1.20","rssi":-60}` | every interval | no |
| `<base>/status` | `online`, or `offline` (last will, sent by the broker when the device disappears) | on connect | yes |
| `<base>/resp` | reply to a command, see below | after each command | no |

`sensors` units: CO2 ppm, temperatures °C, humidity %, pressure hPa, light raw ADC value (0–1023; 1024 until the first reading 1 minute after boot);
`pressure` and `pressure_temp` (the pressure sensor's own temperature) only with a pressure sensor.

**MQTTS (TLS)** (off by default): the WiFi module does the TLS itself and checks the broker certificate
against its stored root CAs and against the hostname in `mqtt.broker` (use the name from the certificate,
not an IP address). TLS 1.2 with RSA certificate chains.

1. Make sure the broker's root CA is in the WiFi module. The module ships with a few roots; Let's Encrypt
   (ISRG Root X1) is not among them. Directly on the device, in any serial terminal:
   ```
   remote on
   cert add
   (paste the root certificate, from -----BEGIN CERTIFICATE----- to -----END CERTIFICATE-----,
    e.g. "ISRG Root X1" from https://letsencrypt.org/certificates/)
   ```
   The device shows the certificate name, writes it, verifies it and restarts. `cert list` shows the
   stored roots (key size, validity, name hash), `cert remove <n>` removes one. Only RSA root certificates
   are supported; if the store is full, expired roots are removed (the date comes from the network). WiFi
   is stopped while the store is changed; a power loss in that moment leaves the store empty (WiFi itself
   keeps working, the root has to be added again).

   Alternatively from a PC with PlatformIO (also makes a backup of the whole store first):
   ```bash
   pio run -d tools/winc_updater -t upload                    # temporary flash tool, settings are kept
   ~/.platformio/penv/bin/python scripts/winc_rootcert.py add --host mqtt.example.com:8883
   pio run -e co2ampel_pro -t upload                          # firmware back
   ```
   `scripts/winc_rootcert.py list` shows the stored roots, `add --pem root.pem` adds a root from a file,
   `restore <backup>.bin` restores a backup. If the store is full, expired roots are removed.
2. Switch MQTT to TLS: `remote on`, `set mqtt.tls=1`, `set mqtt.port=8883`, `save`, `reset`.

If the connection fails, the boot log shows `TLS: root CA stored in the WINC1500? Does mqtt.broker match
the certificate?`.

**Home Assistant discovery** (off by default): enable it with `remote on`, `set mqtt.ha=1`, `save`, `reset`.
On every connect the device then publishes
retained discovery configs to `homeassistant/sensor/<device id>/<value>/config`, so Home Assistant (with
the MQTT integration) creates a device "CO2-Ampel XXXX" with the entities CO2, Temperature, Humidity,
Light, and, with a pressure sensor, Pressure; "Pressure sensor temperature" and "WiFi signal" appear as
diagnostic entities. They read from `<base>/sensors` and `<base>/wifi` and become unavailable when
`<base>/status` is `offline`. With `set mqtt.ha=0`, `save`, `reset` the device removes its entities
again (empty retained configs).

**Remote configuration** (off by default): enable it locally with `remote on`, `set mqtt.config=1`, `save`,
`reset`. The device then accepts commands on `<base>/cmd`: one or more settings commands (`get`, `set`,
`save`, `dump`, `help`), one per line, max. 400 bytes. The reply has the same text as on serial
(`OK`, `ERROR: ...`, `key=value`), one line per result, on `<base>/resp`.

```bash
BASE=co2ampel/F8F0056E78A2
mosquitto_sub -h broker -u user -P pass -t "$BASE/resp" &
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m 'get co2.*'
mosquitto_pub -h broker -u user -P pass -t "$BASE/cmd" -m "$(printf 'set co2.t1=650\nset led.color.t1=0000FF\nsave')"
```

Rules and notes:
- No `remote on` is needed over MQTT; changes apply like on serial and are kept only after `save`
- Passwords read as `***`, and WiFi/MQTT connection settings (including `mqtt.config` itself) are refused
  with `ERROR: Only via serial or web`, so a remote mistake can't cut the device off the network
- After handling a command the device clears `<base>/cmd` with an empty retained message, so a command
  published with retain is executed only once
- Commands arriving while one is still being handled are ignored; send the next one after the reply
- Anyone allowed to publish to `<base>/cmd` can change the settings: restrict it with broker ACLs
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

Automatic Self-Calibration (ASC) is disabled by default. Enable it with `remote on`, `set co2.asc=1`, `save`
(the default for new devices is `AUTO_KALIBRIERUNG` in `include/app_config.h`). ASC requires 7 days of
continuous operation with at least 1 hour of fresh air exposure daily.

## Development

### Adding New Features

1. The code is split into modules under `src/`: `main` (setup/loop, traffic light), `sensors`,
   `led_control`, `settings_storage` (flash), `settings_registry` (key table used by serial and web),
   `serial_settings`/`serial_console` (serial API), `wifi_manager`, `webserver`, `mqtt_service`,
   `ui_menu`/`diagnostics` (service menu), `i2c_utils`, `system_control`. Compile-time defaults live in
   `include/app_config.h`, pins in `variants/co2ampel/variant.h`
2. New user settings: add the field at the end of `SETTINGS` (`include/app_types.h`), a default in
   `settings_defaults()`, a migration in `settings_read()` with a `SETTINGS_VERSION` bump
   (`src/settings_storage.cpp`), and a key in `src/settings_registry.cpp`
3. Update `platformio.ini` if new dependencies are needed
4. Build with `pio run`, upload with `pio run -t upload` (settings are kept)

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
