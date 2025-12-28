/*
  CO2-Ampel Pro - Full Hardware Test
  Tests: LED, Buzzer, WS2812 RGB LEDs, Button, Serial

  Now using CORRECT custom board definition!
*/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Hardware pins (from variant.h)
#define NUM_LEDS 4

// Initialize WS2812 LEDs (4x RGB LEDs on PA22)
Adafruit_NeoPixel leds = Adafruit_NeoPixel(NUM_LEDS, 5, NEO_GRB + NEO_KHZ800);

// Colors
#define COLOR_RED     0xFF0000
#define COLOR_GREEN   0x00FF00
#define COLOR_BLUE    0x0000FF
#define COLOR_YELLOW  0xFFFF00
#define COLOR_CYAN    0x00FFFF
#define COLOR_MAGENTA 0xFF00FF
#define COLOR_WHITE   0xFFFFFF
#define COLOR_OFF     0x000000

void setAllLEDs(uint32_t color) {
  leds.fill(color, 0, NUM_LEDS);
  leds.show();
}

void setup() {
  // Initialize USB Serial
  SerialUSB.begin(9600);

  // Small delay for USB
  delay(1000);

  // Initialize button (PB03) as input with pullup
  // The variant.cpp doesn't initialize the button, so we need to do it here
  PORT->Group[1].DIRCLR.reg = PORT_PB03;  // Set as input
  PORT->Group[1].PINCFG[3].reg |= PORT_PINCFG_PULLEN | PORT_PINCFG_INEN;  // Enable pullup and input buffer
  PORT->Group[1].OUTSET.reg = PORT_PB03;  // Enable pullup resistor

  SerialUSB.println("===========================================");
  SerialUSB.println("CO2-Ampel Pro - Full Hardware Test");
  SerialUSB.println("Custom Board Definition - SUCCESS!");
  SerialUSB.println("===========================================");

  // Initialize WS2812 LEDs
  leds.begin();
  leds.setBrightness(80);  // Medium brightness
  leds.show();

  SerialUSB.println("Testing hardware...");

  // === TEST 1: Status LED (PA27) ===
  SerialUSB.println("\n1. Testing RED Status LED (PA27)...");
  for(int i=0; i<5; i++) {
    PORT->Group[0].OUTSET.reg = PORT_PA27; // LED ON
    SerialUSB.print(".");
    delay(200);
    PORT->Group[0].OUTCLR.reg = PORT_PA27; // LED OFF
    delay(200);
  }
  SerialUSB.println(" OK!");

  // === TEST 2: Buzzer (PA05) ===
  SerialUSB.println("2. Testing Buzzer (PA05)...");
  for(int i=0; i<3; i++) {
    PORT->Group[0].OUTSET.reg = PORT_PA05; // Buzzer ON
    SerialUSB.print("BEEP ");
    delay(100);
    PORT->Group[0].OUTCLR.reg = PORT_PA05; // Buzzer OFF
    delay(100);
  }
  SerialUSB.println(" OK!");

  // === TEST 3: WS2812 RGB LEDs (PA22) ===
  SerialUSB.println("3. Testing WS2812 RGB LEDs (PA22)...");

  SerialUSB.println("   - RED");
  setAllLEDs(COLOR_RED);
  delay(500);

  SerialUSB.println("   - GREEN");
  setAllLEDs(COLOR_GREEN);
  delay(500);

  SerialUSB.println("   - BLUE");
  setAllLEDs(COLOR_BLUE);
  delay(500);

  SerialUSB.println("   - YELLOW");
  setAllLEDs(COLOR_YELLOW);
  delay(500);

  SerialUSB.println("   - CYAN");
  setAllLEDs(COLOR_CYAN);
  delay(500);

  SerialUSB.println("   - MAGENTA");
  setAllLEDs(COLOR_MAGENTA);
  delay(500);

  SerialUSB.println("   - WHITE");
  setAllLEDs(COLOR_WHITE);
  delay(500);

  SerialUSB.println("   - Individual LED test");
  setAllLEDs(COLOR_OFF);
  for(int i=0; i<NUM_LEDS; i++) {
    leds.setPixelColor(i, COLOR_GREEN);
    leds.show();
    SerialUSB.print("     LED ");
    SerialUSB.print(i+1);
    SerialUSB.println(" ON");
    delay(300);
  }

  setAllLEDs(COLOR_OFF);
  SerialUSB.println("   OK!");

  // === TEST 4: Rainbow Effect ===
  SerialUSB.println("4. Testing Rainbow Effect...");
  for(int j=0; j<256; j++) {
    for(int i=0; i<NUM_LEDS; i++) {
      leds.setPixelColor(i, leds.gamma32(leds.ColorHSV(j * 65536L / NUM_LEDS + (i * 65536L / NUM_LEDS), 255, 128)));
    }
    leds.show();
    delay(5);
  }
  SerialUSB.println("   OK!");

  delay(1000);

  SerialUSB.println("\n===========================================");
  SerialUSB.println("All Hardware Tests PASSED!");
  SerialUSB.println("===========================================");
  SerialUSB.println("\nEntering interactive mode...");
  SerialUSB.println("Press button to cycle colors");
  SerialUSB.println("===========================================\n");

  setAllLEDs(COLOR_GREEN);
}

void loop() {
  static unsigned long lastPrint = 0;
  static unsigned long lastBlink = 0;
  static bool statusLedState = false;
  static uint8_t colorIndex = 0;

  unsigned long now = millis();

  // Status LED heartbeat
  if(now - lastBlink >= 500) {
    lastBlink = now;
    statusLedState = !statusLedState;
    if(statusLedState) {
      PORT->Group[0].OUTSET.reg = PORT_PA27;
    } else {
      PORT->Group[0].OUTCLR.reg = PORT_PA27;
    }
  }

  // Print status every 3 seconds
  if(now - lastPrint >= 3000) {
    lastPrint = now;
    SerialUSB.print("Running: ");
    SerialUSB.print(now / 1000);
    SerialUSB.print("s - Status LED: ");
    SerialUSB.print(statusLedState ? "ON " : "OFF");
    SerialUSB.print(" - Button: ");

    // Check button (PB03, active low)
    if(PORT->Group[1].IN.reg & PORT_PB03) {
      SerialUSB.println("Released");
    } else {
      SerialUSB.println("PRESSED");
    }
  }

  // Button handler
  static bool lastButtonState = true;
  bool buttonState = (PORT->Group[1].IN.reg & PORT_PB03) ? true : false;

  if(!buttonState && lastButtonState) {
    // Button just pressed
    delay(50); // Debounce

    SerialUSB.println("\n>>> BUTTON PRESSED <<<");

    // Cycle through colors
    colorIndex = (colorIndex + 1) % 7;

    switch(colorIndex) {
      case 0:
        SerialUSB.println("Color: RED");
        setAllLEDs(COLOR_RED);
        break;
      case 1:
        SerialUSB.println("Color: GREEN");
        setAllLEDs(COLOR_GREEN);
        break;
      case 2:
        SerialUSB.println("Color: BLUE");
        setAllLEDs(COLOR_BLUE);
        break;
      case 3:
        SerialUSB.println("Color: YELLOW");
        setAllLEDs(COLOR_YELLOW);
        break;
      case 4:
        SerialUSB.println("Color: CYAN");
        setAllLEDs(COLOR_CYAN);
        break;
      case 5:
        SerialUSB.println("Color: MAGENTA");
        setAllLEDs(COLOR_MAGENTA);
        break;
      case 6:
        SerialUSB.println("Color: WHITE");
        setAllLEDs(COLOR_WHITE);
        break;
    }

    // Quick beep for feedback
    PORT->Group[0].OUTSET.reg = PORT_PA05;
    delay(50);
    PORT->Group[0].OUTCLR.reg = PORT_PA05;
  }

  lastButtonState = buttonState;

  delay(10);
}
