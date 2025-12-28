# Hardware Test

## Purpose
This is a comprehensive hardware test for the CO2-Ampel Pro that verifies all basic hardware components are functioning correctly.

## Usage

### To Run the Hardware Test:
```bash
# Copy test to src
cp test/hardware_test.cpp src/main.cpp

# Build and upload
pio run -e co2ampel_pro -t upload

# Monitor output
pio device monitor -b 9600
```

### What It Tests:
1. ✅ **Status LED (PA27)** - Red LED blinks
2. ✅ **Buzzer (PA05)** - Makes beeping sounds
3. ✅ **WS2812 RGB LEDs (PA22)** - 4x RGB LEDs show color patterns
4. ✅ **Button (PB03)** - Cycles through colors when pressed
5. ✅ **USB Serial** - Outputs status information

### Expected Behavior:

#### Startup Sequence:
1. Status LED blinks 5 times
2. Buzzer beeps 3 times
3. WS2812 LEDs cycle through colors: RED → GREEN → BLUE → YELLOW → CYAN → MAGENTA → WHITE
4. Each LED lights up individually
5. Rainbow sweep effect
6. LEDs stay GREEN

#### Continuous Operation:
- Status LED blinks every 500ms (heartbeat)
- WS2812 LEDs stay GREEN
- Serial prints status every 3 seconds
- Button press cycles WS2812 colors and beeps

### Hardware Verified:
- ✅ Custom board definition working
- ✅ Pin initialization correct
- ✅ GPIO control working
- ✅ PWM for buzzer working
- ✅ WS2812 protocol working
- ✅ Button input with pullup working
- ✅ USB CDC serial working

## Notes
- This test uses the **custom CO2-Ampel Pro board definition**
- All hardware access is done correctly for the SAMD21G18A
- Button requires explicit initialization (not in variant.cpp)
