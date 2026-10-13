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
- ✅ MQTT support for IoT platforms (publish only)
- ✅ Settings with version and CRC, kept across `pio run -t upload`
- ✅ Automatic self-calibration (ASC) switchable at runtime (`co2.asc`)

**Planned Features:**
- [ ] Configuration commands over MQTT
- [ ] MQTTS (secure MQTT with TLS)
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

## Usage

### LED Color Codes (Default Values)

All thresholds and colors are configurable via serial commands!

- **Blue** (0x007CB0): < 600 ppm (very fresh air)
- **Green** (0x00FF00): 600-999 ppm (good air quality)
- **Yellow** (0xFF7F00): 1000-1199 ppm (ventilation recommended)
- **Red** (0xFF0000): 1200-1399 ppm (ventilation needed)
- **Red Blinking**: ≥ 1400 ppm (poor air quality)
- **Buzzer**: ≥ 1600 ppm (critical - requires immediate ventilation)

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
- **Long press (>3s)**: Start WiFi Access Point mode (WiFi versions only)
- **Hold during power-on**: Enter service menu

### Service Menu Options

1. **Self Test**: Buzzer, LEDs, WiFi module (if detected), LoRa radio (reported, optional), then sensors:
   LED 1 light, LED 2 CO2, LED 3 temperature/pressure, LED 4 humidity turn on when in range
2. **Air Test**: Live CO2 monitoring with color feedback
3. **Configuration**: Adjust altitude, temperature offset, buzzer settings (CO2 sensor values are stored in the sensor)
4. **Calibration**: Manual CO2 sensor calibration (requires fresh air)

### Serial Commands

Connect via USB serial at 9600 baud, 8N1:

```
remote on      Enable remote control (required for set/save)
remote off     Disable remote control
status         Show measurements, CO2 sensor info and WiFi/MQTT status
i2c scan       List responding I2C devices on both buses (with chip IDs)
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
mqtt.user
mqtt.pass
mqtt.client_id
mqtt.topic_prefix
mqtt.interval
```

`co2.asc` turns the CO2 sensor's automatic self-calibration on or off (default off; see Calibration).
WiFi and MQTT connection settings (`wifi.*`, `mqtt.enabled/broker/port/user/pass`) can only be changed
via serial or the web form, and passwords are only shown to local (serial) callers. This matters once
remote configuration (e.g. over MQTT) is added.

### Settings Backup and Restore

Settings are stored in flash (last 2 KB, with version and CRC). `pio run -t upload` keeps them: the upload script reads the settings area and writes it back with the new firmware. Flashing with another tool (e.g. plain bossac or the Arduino IDE) erases them, so keep a backup with `dump`. Settings from an incompatible firmware version are replaced by defaults.

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

**Toggle serial measurement output:**
```bash
remote on
set sys.serial_output=0
save
```

**Notes:**
- WiFi credentials are stored in flash (kept across `pio run -t upload`; backup with `dump` anyway)
- After setting WiFi credentials, device will auto-connect on next boot
- If connection fails, device will create an AP (Access Point) mode instead

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
{
  "c": 800,      // CO2 in ppm
  "t": 22.5,     // Temperature in °C
  "h": 45.0,     // Humidity in %
  "p": 1013.2,   // Pressure in hPa (only with a pressure sensor)
  "u": 22.0,     // Pressure sensor temperature (only with a pressure sensor)
  "l": 512       // Light sensor value
}
```

### MQTT

With `mqtt.enabled=1` the device publishes every `mqtt.interval` seconds (default 60) to
`<mqtt.topic_prefix>/<device id>/<value>`, where the device id is the WiFi MAC address (e.g. `F8F0056E78A2`)
and the values are `co2`, `temperature`, `humidity`, `light` and, with a pressure sensor, `pressure`.
Messages are not retained. `mqtt.client_id` only sets the MQTT client id (empty = device id).

## Calibration

The CO2 sensor should be calibrated periodically via the service menu:

1. Place device in fresh air (outdoor or well-ventilated area)
2. Wait at least 2 minutes for sensor to stabilize
3. Enter the service menu (hold button during power-on)
4. Use the Calibration option and follow on-screen feedback

Automatic Self-Calibration (ASC) is disabled by default. Enable it with `remote on`, `set co2.asc=1`, `save`
(the default for new devices is `AUTO_KALIBRIERUNG` in `include/app_config.h`). ASC requires 7 days of
continuous operation with at least 1 hour of fresh air exposure daily.

## Development

### Adding New Features

1. The code is split into modules under `src/` (see `CLAUDE.md` for the layout); compile-time defaults
   live in `include/app_config.h`
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
