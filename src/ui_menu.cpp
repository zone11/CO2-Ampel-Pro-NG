#include "ui_menu.h"

#include "app_globals.h"
#include "calibration.h"
#include "diagnostics.h"
#include "led_control.h"
#include "sensors.h"
#include "settings_registry.h"
#include "system_control.h"

unsigned int select_value(unsigned int value, unsigned int min, unsigned int max, unsigned int fill, uint32_t color, uint32_t color_off)
{
  unsigned int timeout, sw;

  ws2812.fill(color_off, 0, 4);
  if(fill == 0)
  {
    ws2812.setPixelColor(value, COLOR_SETTINGS);
  }
  else if(value > 0)
  {
    ws2812.fill(color, 0, value);
  }
  ws2812.show();

  for(sw=0, timeout=0; timeout<1000; timeout++) //10s timeout
  {
    delay(10);
    watchdog_feed(); //the timeout restarts while the button is held

    if(digitalRead(PIN_SWITCH) == LOW) //button pressed
    {
      status_led(1);
      sw++;
      if(sw > 200)
      {
        leds(COLOR_OFF);
      }
      timeout = 0;
    }
    else //released
    {
      status_led(0);
      if(sw > 200) //2s press
      {
        break;
      }
      else if(sw > 10) //100ms press
      {
        value++;
        if(value > max)
        {
          value = min;
        }
        ws2812.fill(color_off, 0, 4);
        if(fill == 0)
        {
          ws2812.setPixelColor(value, color);
        }
        else if(value > 0)
        {
          ws2812.fill(color, 0, value);
        }
        ws2812.show();
      }
      sw = 0;
    }
  }

  leds(COLOR_OFF);
  delay(500);

  return value;
}

void altitude_toffset(void) //altitude and temperature offset
{
  unsigned int value=0;

  //altitude
  if(features & FEATURE_SCD30)
  {
    uint16_t altitude = 0;
    scd30.getAltitudeCompensation(altitude); //meters above sea level
    value = altitude / 250;
  }
  else if(features & FEATURE_SCD4X)
  {
    uint16_t altitude;
    scd4x.stopPeriodicMeasurement();
    delay(500);
    scd4x.getSensorAltitude(altitude); //meters above sea level
    value = altitude/250;
  }

  value = select_value(value, 0, 4, 1, COLOR_RED, COLOR_WHITE) * 250;

  if(features & FEATURE_SCD30)
  {
    scd30.setAltitudeCompensation(value); //meters above sea level
  }
  else if(features & FEATURE_SCD4X)
  {
    scd4x.setSensorAltitude(value); //meters above sea level
  }

  if(features & FEATURE_USB)
  {
    Serial.print("Altitude: ");
    Serial.println(value, DEC);
  }

  //temperature offset
  if(features & FEATURE_SCD30)
  {
    uint16_t offset = 0;
    scd30.getTemperatureOffset(offset); //degC*100
    value = (offset / 100) / 2;
  }
  else if(features & FEATURE_SCD4X)
  {
    uint16_t offset;
    scd4x.getTemperatureOffsetRaw(offset);
    value = scd4x_offset(offset) / 2; //degC
  }

  value = select_value(value, 0, 4, 1, COLOR_YELLOW, COLOR_BLUE) * 2;

  if(features & FEATURE_SCD30)
  {
    scd30.setTemperatureOffset(value * 100); //degC*100
  }
  else if(features & FEATURE_SCD4X)
  {
    scd4x.setTemperatureOffsetRaw(scd4x_offset_raw(value));
    delay(500);
    scd4x.startPeriodicMeasurement();
  }

  if(features & FEATURE_USB)
  {
    Serial.print("Temperature: ");
    Serial.println(value, DEC);
  }

  //buzzer
  settings.buzzer = select_value(settings.buzzer, 0, 1, 1, COLOR_GREEN, COLOR_WHITE);

  if(features & FEATURE_USB)
  {
    Serial.print("Buzzer: ");
    Serial.println(settings.buzzer, DEC);
  }

  //done
  settings_save();
  leds(COLOR_BLUE);
  buzzer(250);

  return;
}


// Service menu: runs before the network starts, so nothing else needs loop() here
static void calibration_menu(void)
{
  calibration_start(true);
  while(calibration_running())
  {
    watchdog_feed(); //calibration takes several minutes
    if((digitalRead(PIN_SWITCH) == 0) || Serial.available()) //button pressed or any input on the serial port
    {
      calibration_abort();
      break;
    }

    status_led(200);

    if(check_sensors())
    {
      calibration_reading();
      show_data();
    }
  }
  if(calibration_leds())
  {
    delay(3000); //result
  }
}

void menu(void)
{
  unsigned int value;

  ws2812.setBrightness(BRIGHTNESS_MENU); //0...255
  leds(COLOR_MENU);
  delay(500);
  leds(COLOR_OFF);
  buzzer(250);

  value = select_value(1, 1, 4, 1, COLOR_MENU, ws2812.Color(20,20,20));

  switch(value)
  {
    case 1: self_test();        break;
    case 2: air_test();         break;
    case 3: altitude_toffset(); break;
    case 4: calibration_menu(); break;
  }

  led_brightness_update();
  leds(ws2812.Color(20,20,20)); //LEDs white

  return;
}
