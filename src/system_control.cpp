#include "system_control.h"

#include "app_globals.h"
#include "led_control.h"

#include <Wire.h>

void reset_mcu(void)
{
  if(features & FEATURE_USB)
  {
    Serial.println("Reset...");
  }

  status_led(0);
  buzzer(0);
  ws2812.setBrightness(BRIGHTNESS_DARK); //dark
  leds(COLOR_WHITE); //LEDs white

  if(features & FEATURE_WINC1500)
  {
    WiFi.end(); //WiFi.disconnect();
  }
  if(features & FEATURE_SCD30)
  {
    scd30.stopPeriodicMeasurement();
  }
  if(features & FEATURE_SCD4X)
  {
    scd4x.stopPeriodicMeasurement();
  }

  Wire.end();
  Wire1.end();
  Serial.end();

  __disable_irq(); //disable interrupts
  NVIC_SystemReset(); //reset
  while(1);
}
