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
- ✅ More settings avilable with simple dump feature (D?)

**Features in progress:**
- 🚧 MQTT support for IoT platforms (MQTTS still missing..)

**Planned Features:**
- [ ] Modular code structure (separate files for WiFi, sensors, console)
- [ ] Non-blocking architecture with state machine or RTOS features
- [ ] LoRaWAN/TTN integration
- [ ] Enhanced web interface with charts
- [ ] Over-the-air (OTA) firmware updates


## Build Configurations

The `platformio.ini` file defines multiple build environments:

| Environment | Description | Build Flags |
|------------|-------------|-------------|
| `co2ampel_pro` | Pro version with WiFi and pressure sensor | `PRO_AMPEL=1`, `WIFI_AMPEL=1` |
| `co2ampel_basic` | Basic version without WiFi or pressure | `PRO_AMPEL=0`, `WIFI_AMPEL=0` |
| `co2ampel_plus` | Plus version with WiFi, no pressure | `PRO_AMPEL=0`, `WIFI_AMPEL=1` |
| `co2ampel_covid` | COVID-19 threshold variant | `COVID=1`, `PRO_AMPEL=1`, `WIFI_AMPEL=1` |
| `co2ampel_debug` | Debug build with debugging symbols | All features + debug output |

## Usage

### LED Color Codes

- **Blue**: < 600 ppm (very fresh air)
- **Green**: 600-999 ppm (good air quality)
- **Yellow**: 1000-1199 ppm (ventilation recommended)
- **Red**: 1200-1399 ppm (ventilation needed)
- **Red Blinking**: ≥ 1400 ppm (poor air quality)
- **Buzzer**: ≥ 1600 ppm (critical - requires immediate ventilation)

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
R=1      Enable remote control
R=0      Disable remote control
V?       Query firmware version
D?       Dump all settings as serial commands (for backup/restore)
T=X      Set temperature offset (0-20°C)
A=X      Set altitude compensation (0-3000m)
C=1      Calibrate to 400ppm (requires 2+ min in fresh air)
H=XX     Set LED brightness (hex, 00-FF)
L=RRGGBB Set LED color (hex RGB)
B=1      Enable buzzer
B=0      Disable buzzer
WS=X     Set WiFi SSID
WP=X     Set WiFi password
W?       Query WiFi status and connection info
M=1      Enable MQTT
M=0      Disable MQTT
MB=X     Set MQTT broker hostname/IP
MP=X     Set MQTT port (default: 1883)
MU=X     Set MQTT username (optional)
MK=X     Set MQTT password (optional)
MC=X     Set MQTT client ID (empty = auto-generate from MAC)
MT=X     Set MQTT topic prefix (default: co2ampel)
MI=X     Set MQTT publish interval in seconds (default: 60)
M?       Query MQTT status and connection
S=1      Save settings to flash
R=R      Reset device
```

### Settings Backup and Restore

**Important:** Settings are stored in flash and **will be lost** when uploading new firmware. Always backup your settings before updating!

**Backup Settings:**
```bash
# Connect to serial port and send:
D?
```


### WiFi Configuration via Serial

You can configure WiFi settings via serial commands:

**Configure WiFi:**
```bash
# 1. Connect to serial monitor
pio device monitor -b 9600

# 2. Enable remote control
R=1

# 3. Set WiFi credentials
WS=YourWiFiSSID
WP=YourWiFiPassword

# 4. Save settings
S=1

# 5. Reset device to connect
R=R
```

**Check WiFi Status:**
```bash
# Query WiFi connection info
W?

# Example output:
# WiFi SSID: MyNetwork
# WiFi Password: ***
# WiFi Status: Connected to MyNetwork
# IP Address: 192.168.1.100
# Signal Strength: -45 dBm
```

**Notes:**
- WiFi credentials are stored in flash (will be lost on firmware update - backup with `D?`)
- After setting WiFi credentials, device will auto-connect on next boot
- If connection fails, device will create an AP (Access Point) mode instead
- Use `W?` to check connection status and IP address

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

The CO2 sensor should be calibrated periodically:

1. Place device in fresh air (outdoor or well-ventilated area)
2. Wait at least 2 minutes for sensor to stabilize
3. Send command `R=1` followed by `C=1` via serial
4. Wait for confirmation
5. Send `S=1` to save calibration

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
