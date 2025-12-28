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
- ✅ Settings dump feature for backup/restore (D?)
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
R=1        # Enable remote control
T1=600     # Green threshold
T2=800     # Yellow threshold
T3=1000    # Red threshold
T4=1200    # Red blink threshold
T5=1400    # Buzzer threshold
S=1        # Save to flash

# Example: Change LED colors
R=1
CB=0000FF  # Blue color
CG=00FF00  # Green color
CY=FFA500  # Orange-yellow
CR=FF0000  # Red color
S=1        # Save to flash

# Example: Adjust temperature offset
R=1
TO=8       # Set temperature offset to 8°C
S=1        # Save to flash

# Query current settings
T?         # Show CO2 thresholds
C?         # Show LED colors
TO?        # Show temperature offset
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
R=1      Enable remote control
R=0      Disable remote control
R=R      Reset device
V?       Query firmware version
D?       Dump all settings as serial commands (for backup/restore)
T?       Query CO2 thresholds
T1=X     Set green threshold (ppm, default: 600)
T2=X     Set yellow threshold (ppm, default: 1000)
T3=X     Set red threshold (ppm, default: 1200)
T4=X     Set red blink threshold (ppm, default: 1400)
T5=X     Set buzzer threshold (ppm, default: 1600)
TO=X     Set temperature offset (0-20°C)
TO?      Query temperature offset
H=XX     Set LED brightness (hex, 00-FF)
L=RRGGBB Set LED color temporarily (hex RGB)
B=1      Enable buzzer
B=0      Disable buzzer
CB=RRGGBB Set blue LED color (CO2 < green threshold)
CG=RRGGBB Set green LED color (green-yellow range)
CY=RRGGBB Set yellow LED color (yellow-red range)
CR=RRGGBB Set red LED color (CO2 >= red threshold)
C?       Query all LED color settings
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
XA=X     Set altitude compensation (0-3000m)
XA?      Query altitude compensation
XC=1     Calibrate to 400ppm (requires 2+ min in fresh air)
S=1      Save settings to flash
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
