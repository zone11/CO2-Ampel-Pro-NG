#include "system_control.h"

#include "app_globals.h"
#include "led_control.h"

#include <Wire.h>

// Sekunden seit Start; zaehlt millis()-Ueberlaeufe (alle 49 Tage), muss daher regelmaessig laufen (loop)
uint32_t uptime_seconds(void)
{
  static uint32_t last_ms = 0, wraps = 0;
  uint32_t now = millis();

  if(now < last_ms)
  {
    wraps++;
  }
  last_ms = now;

  return (uint32_t)((((uint64_t)wraps << 32) | now) / 1000);
}

void reset_mcu(void)
{
  if(features & FEATURE_USB)
  {
    Serial.println("Reset...");
  }

  status_led(0);
  buzzer(0);
  ws2812.setBrightness(HELLIGKEIT_DUNKEL); //dunkel
  leds(FARBE_WEISS); //LEDs weiss

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
