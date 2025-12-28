/*
  CO2-Ampel Pin Definitions for PlatformIO

  Based on Watterott CO2-Ampel variant for Arduino IDE
  Adapted for PlatformIO SAMD21 Zero USB board
*/

#pragma once

// CO2-Ampel specific pins
// -----------------------
#define PIN_LSENSOR     (0u)  // Analog light sensor input (PA02/A0)
#define PIN_LSENSOR_PWR (1u)  // Light sensor power control (PA03/A1)
#define PIN_SWITCH      (2u)  // User button input (PA14)
#define PIN_LED         (3u)  // Status LED (PA27)
#define PIN_BUZZER      (4u)  // Buzzer PWM output (PA06)
#define PIN_WS2812      (5u)  // WS2812 RGB LED data (PA07)

// LED definitions
#define LED_BUILTIN     PIN_LED

// Analog pins
#define PIN_A0          (0u)
#define PIN_A1          (1u)

// I2C Wire (SERCOM0) - Main sensor bus
// SDA: PA08 (pin 6), SCL: PA09 (pin 7)
// Used for: SCD30/SCD4X CO2 sensor, SSD1306 display
#define PIN_WIRE_SDA    (6u)
#define PIN_WIRE_SCL    (7u)

// I2C Wire1 (SERCOM2) - Secondary sensor bus
// SDA: PA12 (pin 8), SCL: PA13 (pin 9)
// Used for: BMP280/LPS22HB pressure sensor, ATECC608 crypto chip
#define PIN_WIRE1_SDA   (8u)
#define PIN_WIRE1_SCL   (9u)

// WiFi ATWINC1500 pins
#define WINC1501_CHIP_EN_PIN (10u) // PA19 - WiFi chip enable
#define WINC1501_WAKE_PIN    (11u) // PA20 - WiFi wake
#define WINC1501_RESET_PIN   (12u) // PA15 - WiFi reset
#define WINC1501_INTN_PIN    (13u) // PA16 - WiFi interrupt
#define WINC1501_SPI_CS_PIN  (14u) // PA18 - WiFi SPI CS

// SPI pins (SERCOM1)
#define PIN_SPI_SS      (14u) // PA18 - WINC1500 CS
#define PIN_SPI_MOSI    (15u) // PA22 - SPI MOSI
#define PIN_SPI_SCK     (16u) // PA23 - SPI SCK
#define PIN_SPI_MISO    (17u) // PA17 - SPI MISO

// Additional SPI CS pins
#define PIN_RFM9X_CS    (20u) // PA21 - LoRa RFM9X CS (Pro version)

// Serial1 pins (SERCOM3)
#define PIN_SERIAL1_TX  (18u) // PA04
#define PIN_SERIAL1_RX  (19u) // PA05

// USB pins
#define PIN_USB_DM      (23u) // PA24
#define PIN_USB_DP      (24u) // PA25

// Clock configuration
#define VARIANT_MAINOSC (32768ul)
#define VARIANT_MCK     (48000000ul)
