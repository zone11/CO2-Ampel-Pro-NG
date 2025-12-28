# CO2-Ampel Pro NG (Next Generation)

New PlatformIO-based firmware for the Watterott CO2-Ampel Pro hardware, initially based on V25 of the original firmware.

✅ **Status**: Fully operational and tested on hardware

## Overview

This is going to be a modernized version of the original CO2-Ampel firmware, migrated from Arduino IDE to PlatformIO for better dependency management and professional development workflow.

**Key Features:**
- ✅ Custom board definition for CO2-Ampel Pro hardware
- ✅ Full hardware support (LEDs, buzzer, sensors, WiFi)
- ✅ Multiple build configurations (Pro, Basic, Plus, COVID)
- ✅ Works with PlatformIO CLI and IDE

## Quick Start

```bash
# Build and upload firmware
pio run -e co2ampel_pro -t upload

# Monitor serial output
pio device monitor -b 9600
```

For first-time setup, see [Setup Instructions](#setup-instructions) below.

## Hardware

- **MCU**: ATSAMD21G18 ARM Cortex-M0+ @ 48MHz
- **CO2 Sensor**: Sensirion SCD30 or SCD4x (SCD40/SCD41)
- **LEDs**: 4x WS2812 RGB intelligent LEDs
- **Buzzer**: PWM-controlled acoustic buzzer
- **Pro Version Additional Features**:
  - Air pressure sensor (BMP280 or LPS22HB)
  - ATWINC1500B WiFi module (optional)
  - RFM95W/96W LoRa transceiver (optional)

## Project Structure

```
CO2-Ampel-Pro-NG/
├── platformio.ini          # PlatformIO configuration
├── boards/
│   └── co2ampel.json      # Custom board definition
├── variants/
│   └── co2ampel/          # Custom variant (pin definitions, hardware init)
├── src/
│   └── main.cpp           # Main firmware code
├── include/               # Project headers
├── lib/                   # Custom libraries (if needed)
├── test/
│   ├── hardware_test.cpp  # Comprehensive hardware test
│   └── README.md          # Hardware test documentation
└── legacy/                # Original source code from Watterott (reference)
```

## Setup Instructions

### 1. Install PlatformIO

#### Option A: PlatformIO IDE (VS Code Extension)
1. Install [Visual Studio Code](https://code.visualstudio.com/)
2. Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)

#### Option B: PlatformIO Core (Command Line)
```bash
# Using pip
pip install platformio

# Or using Homebrew on macOS
brew install platformio
```

### 2. Clone or Download This Repository

```bash
cd /Users/christian/Desktop/CO2-Ampel-Pro-NG
```

### 3. Install Dependencies

PlatformIO will automatically install all required libraries on first build:

```bash
pio lib install
```

### 4. Build the Firmware

#### For CO2-Ampel Pro (default configuration):
```bash
pio run -e co2ampel_pro
```

#### For other variants:
```bash
# Basic version (no WiFi, no pressure sensor)
pio run -e co2ampel_basic

# Plus version (WiFi, no pressure sensor)
pio run -e co2ampel_plus

# COVID-19 threshold version
pio run -e co2ampel_covid

# Development/debug version
pio run -e co2ampel_debug
```

### 5. Upload to Hardware

Connect your CO2-Ampel Pro via USB and run:

```bash
# Upload to Pro version
pio run -e co2ampel_pro -t upload

# Or use the bootloader by double-pressing the reset button
# and uploading while in bootloader mode
```

### 6. Monitor Serial Output

```bash
pio device monitor -b 9600
```

Or combined upload + monitor:
```bash
pio run -e co2ampel_pro -t upload && pio device monitor -b 9600
```

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
T=X      Set temperature offset (0-20°C)
A=X      Set altitude compensation (0-3000m)
C=1      Calibrate to 400ppm (requires 2+ min in fresh air)
H=XX     Set LED brightness (hex, 00-FF)
L=RRGGBB Set LED color (hex RGB)
B=1      Enable buzzer
B=0      Disable buzzer
S=1      Save settings to flash
R=R      Reset device
```

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

## Hardware Test

A comprehensive hardware test is available to verify all components:

```bash
# Copy hardware test to main source
cp test/hardware_test.cpp src/main.cpp

# Build and upload
pio run -e co2ampel_pro -t upload

# Monitor output
pio device monitor -b 9600
```

**What it tests:**
- ✅ Status LED (PA27) - Red LED blinks
- ✅ Buzzer (PA05) - Makes beeping sounds
- ✅ WS2812 RGB LEDs (PA22) - 4x RGB LEDs show color patterns
- ✅ Button (PB03) - Cycles through colors when pressed
- ✅ USB Serial - Outputs status information

See `test/README.md` for detailed documentation.

## Troubleshooting

### Compilation Issues

**Problem**: Missing libraries
```
Solution: Run `pio lib install` to fetch all dependencies
```

**Problem**: Board not found
```
Solution: Ensure USB cable is connected and board is in bootloader mode
         Double-press the reset button to enter bootloader
```

**Problem**: Upload fails
```
Solution: Try specifying the port manually:
         pio run -e co2ampel_pro -t upload --upload-port /dev/ttyACM0
```

### Runtime Issues

**Problem**: No sensor detected
```
Solution: Check I2C connections and sensor address
         View serial output to see which sensors were detected
```

**Problem**: WiFi not connecting
```
Solution: 1. Configure WiFi credentials via AP mode
         2. Hold button >3s to start AP mode
         3. Connect to CO2AMPEL-XX-XX and enter credentials
         4. Restart device
```

**Problem**: Incorrect CO2 readings
```
Solution: Calibrate sensor in fresh air (see Calibration section)
         Adjust temperature offset if needed (typical: 4-8°C)
```

## Development

### Adding New Features

1. Edit `src/main.cpp` for firmware changes
2. Add custom libraries to `lib/` directory
3. Update `platformio.ini` if new dependencies are needed
4. Test with `pio run -e co2ampel_debug`

### Code Style

The original code is in German and follows Arduino conventions. Future enhancements should:
- Maintain compatibility with existing hardware
- Document changes in English and German
- Follow PlatformIO best practices

### Debugging

Use the debug environment for development:

```bash
pio debug -e co2ampel_debug
```

Note: Requires hardware debugger (e.g., Atmel-ICE) connected to SWD pins.

## Future Enhancements

Planned improvements for the Next Generation firmware:

- [ ] Modular code structure (separate files for WiFi, sensors, display)
- [ ] Non-blocking architecture with state machine
- [ ] LoRaWAN/TTN integration
- [ ] MQTT support for IoT platforms
- [ ] Enhanced web interface with charts
- [ ] Over-the-air (OTA) firmware updates
- [ ] Historical data logging
- [ ] Multi-language support

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
