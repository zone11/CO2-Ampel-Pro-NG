# Changelog

All notable changes to the CO2-Ampel Pro NG project.

## [1.0.0] - 2025-12-28 - Initial PlatformIO Migration

### Added
- **Custom Board Definition**: Created `boards/co2ampel.json` for CO2-Ampel Pro hardware
- **Custom Variant Files**: Ported pin definitions and hardware initialization from legacy project
  - `variants/co2ampel/variant.h` - Pin definitions
  - `variants/co2ampel/variant.cpp` - Hardware initialization (initVariant)
  - `variants/co2ampel/linker_scripts/gcc/flash_with_bootloader.ld` - Bootloader-compatible linker script
- **PlatformIO Configuration**: Created `platformio.ini` with 6 build environments
  - co2ampel_pro (WiFi + Pressure)
  - co2ampel_pro_lora (WiFi + Pressure + LoRa, future)
  - co2ampel_basic (No WiFi, no pressure)
  - co2ampel_plus (WiFi, no pressure)
  - co2ampel_debug (Debug build)
  - co2ampel_covid (COVID-19 thresholds)
- **Hardware Test**: Comprehensive test suite in `test/hardware_test.cpp`
  - Tests LED, buzzer, WS2812 RGB LEDs, button, serial
  - Rainbow effects and color cycling
  - Button press detection
- **Documentation**:
  - Updated README.md with Quick Start and hardware test info
  - Created workfiles/PROJECT_CONTEXT.json with complete project reference
  - Created workfiles/README.md explaining the documentation structure
  - Created CHANGELOG.md (this file)

### Changed
- **Main Firmware**: Updated `src/main.cpp` for PlatformIO compatibility
  - Added Arduino.h include
  - Added forward declarations for WiFi functions
  - Removed duplicate Wire1 declaration (provided by framework)
  - Added build flag guards (#ifndef VERSION, COVID, etc.)
- **Project Structure**: Organized into proper PlatformIO layout
  - Moved original firmware to `legacy/` for reference
  - Created `boards/` for custom board definition
  - Created `variants/` for custom variant files
  - Created `workfiles/` for development documentation

### Fixed
- **USB Enumeration**: Correct VID/PID (0x04D8:0xEACF) with custom board definition
- **Hardware Initialization**: Proper GPIO setup via initVariant() in variant.cpp
- **I2C Bus Support**: Wire1 (SERCOM2) automatically configured by framework
- **Button Functionality**: Added explicit pullup configuration for PB03
- **Compilation Errors**: Resolved forward declaration issues for WiFi functions
- **Linker Errors**: Specified correct linker script path in platformio.ini

### Verified
- ✅ Status LED (PA27) working
- ✅ Buzzer (PA05) working
- ✅ WS2812 RGB LEDs (PA22) working - all 4 LEDs
- ✅ Button (PB03) working with correct pullup
- ✅ USB serial communication (9600 baud)
- ✅ USB device enumeration (correct VID/PID)
- ✅ Firmware compiles successfully (RAM: 17.9%, Flash: 32.6%)
- ✅ Firmware uploads successfully
- ✅ Firmware running on hardware

### Known Issues
- Compiler warnings (non-critical):
  - USB_PID redefined (platformio.ini overrides variant.h, expected)
  - USB_MANUFACTURER redefined (platformio.ini overrides variant.h, expected)
  - CRYSTALLESS redefined (duplicate definition, harmless)
  - Unknown conversion character 'r' in webserver format strings
  - Unused variables in altitude_toffset and menu functions

### Not Yet Tested
- CO2 sensor detection and readings (no sensor connected during initial testing)
- Pressure sensor functionality
- WiFi features (deferred by user for later)
- LoRa functionality (future implementation)
- Display support (SSD1306, if present)

### Migration Notes
- Original firmware from: https://github.com/watterott/CO2-Ampel/tree/master
- Hardware: Watterott CO2-Ampel Pro with SAMD21G18A MCU
- Bootloader: USB CDC bootloader with sam-ba protocol
- Key breakthrough: Creating custom board definition instead of using Arduino Zero

### For Developers
See `workfiles/PROJECT_CONTEXT.json` for complete project context including:
- Detailed hardware specifications
- Complete pin mappings
- I2C device addresses
- Build configuration details
- All fixes and solutions applied
- Development workflow
- Future work roadmap

---

## Format
This changelog follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format.

Version format: MAJOR.MINOR.PATCH
- MAJOR: Incompatible hardware or API changes
- MINOR: New features, backward compatible
- PATCH: Bug fixes, backward compatible
