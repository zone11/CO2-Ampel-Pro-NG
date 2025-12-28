# CO2-Ampel Pro NG (Next Generation)

New / updated firmware for the Watterott CO2-Ampel Pro hardware, initially based on v24 of the original firmware.

✅ **Status**: Fully operational and tested on hardware

## Overview

This is going to be a modernized version of the original CO2-Ampel firmware, migrated from Arduino IDE to PlatformIO for better dependency management and professional development workflow.

**Goals:**
- ✅ Custom board definition for CO2-Ampel Pro hardware
- ✅ Full hardware support (LEDs, buzzer, sensors, WiFi)
- ✅ Multiple build configurations (Pro, Basic, Plus, COVID)
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
- ✅ MQTT support for IoT platforms

**Planned Features:**
- [ ] MQTTS (secure MQTT with TLS)
- [ ] LoRaWAN/TTN integration
- [ ] Enhanced web interface with charts
- [ ] Over-the-air (OTA) firmware updates


## Build Configurations

The firmware is designed for the CO2-Ampel Pro hardware with WiFi (WINC1500) and pressure sensor (BMP280/LPS22HB).

| Environment | Description |
|------------|-------------|
| `co2ampel_pro` | Default build for Pro hardware |
| `co2ampel_pro_lora` | Pro hardware + LoRa support (future) |

**Note:** CO2 thresholds and LED colors are now configurable at runtime via serial commands and saved to flash memory.

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

- **Short press (100ms-3s)**: Cycle through brightness levels
- **Long press (>3s)**: Start WiFi Access Point mode (WiFi versions only)
- **Hold during power-on**: Enter service menu

### Service Menu Options

1. **Self Test**: Tests all hardware components
2. **Air Test**: Live CO2 monitoring with color feedback
3. **Configuration**: Adjust altitude, temperature offset, buzzer settings
4. **Calibration**: Manual CO2 sensor calibration (requires fresh air)

### Serial Commands

Connect via USB serial at 9600 baud, 8N1:

```
remote on      Enable remote control (required for set/save)
remote off     Disable remote control
status         Show measurements and WiFi/MQTT status
reset          Reset device (remote must be on)
version        Query firmware version
get <key>      Read a single setting
get <prefix.*> Read a group of settings by prefix
set <key>=<v>  Update a setting value
save           Save settings to flash
dump           Print all settings as set commands
help           List all available keys
```

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

### Settings Backup and Restore

**Important:** Settings are stored in flash and **will be lost** when uploading new firmware. Always backup your settings before updating!

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
- WiFi credentials are stored in flash (will be lost on firmware update - backup with `dump`)
- After setting WiFi credentials, device will auto-connect on next boot
- If connection fails, device will create an AP (Access Point) mode instead

### WiFi Web Interface

When WiFi is enabled, the device creates either:
- **Client Mode**: Connects to configured WiFi network
- **AP Mode**: Creates access point `CO2AMPEL-XX-XX`

Available endpoints:
- `/` - Main web interface with live data
- `/json` - JSON API with sensor readings
- `/cmk-agent` - CheckMK monitoring agent format

Example JSON response:
```json
{
  "c": 800,      // CO2 in ppm
  "t": 22.5,     // Temperature in °C
  "h": 45.0,     // Humidity in %
  "p": 1013.2,   // Pressure in hPa (Pro only)
  "u": 22.0,     // Secondary temperature (Pro only)
  "l": 512       // Light sensor value
}
```

## Calibration

The CO2 sensor should be calibrated periodically via the service menu:

1. Place device in fresh air (outdoor or well-ventilated area)
2. Wait at least 2 minutes for sensor to stabilize
3. Enter the service menu (hold button during power-on)
4. Use the Calibration option and follow on-screen feedback

Automatic Self-Calibration (ASC) is disabled by default. To enable, set `AUTO_KALIBRIERUNG=1` in `main.cpp` and rebuild. ASC requires 7 days of continuous operation with at least 1 hour of fresh air exposure daily.

## Development

### Adding New Features

1. Edit `src/main.cpp` for firmware changes
2. Add custom libraries to `lib/` directory
3. Update `platformio.ini` if new dependencies are needed
4. Test with `pio run -e co2ampel_debug`

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
