#include "ui_menu.h"

#include "app_globals.h"
#include "diagnostics.h"
#include "led_control.h"
#include "sensors.h"
#include "settings_registry.h"

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
    float offset;
    scd4x.getTemperatureOffset(offset); //degC
    value = offset / 2;
  }

  value = select_value(value, 0, 4, 1, COLOR_YELLOW, COLOR_BLUE) * 2;

  if(features & FEATURE_SCD30)
  {
    scd30.setTemperatureOffset(value * 100); //degC*100
  }
  else if(features & FEATURE_SCD4X)
  {
    scd4x.setTemperatureOffset(value);
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

void calibration(void) //calibration
{
  unsigned int abort=0, cycle, again, interval=MEASURE_INTERVAL, co2, co2_last;

  //The measurement interval during the calibration and in operation should be the same.
  //Different intervals can cause deviations and unstable readings.
  //scd30.setMeasurementInterval(MEASURE_INTERVAL);

  ws2812.fill(COLOR_WHITE, 0, 4); //LEDs white
  ws2812.show();

  if(features & FEATURE_SCD4X)
  {
    interval = 5; //5s
  }

  //ASC from the settings (co2.asc)
  co2_set_asc(settings.asc);

  calibration_start:

  //calibration
  co2 = co2_last = co2_sensor();
  for(again=0, cycle=0; cycle < (180/interval);)
  {
    if(digitalRead(PIN_SWITCH) == 0) //button pressed?
    {
      abort = 1;
      break; //abort
    }

    status_led(200);

    if(check_sensors())
    {
      co2 = co2_sensor();
      if((co2 >= 200) && (co2 <= 800) && 
         (co2 >= (co2_last-30)) &&
         (co2 <= (co2_last+30))) //+/-30ppm tolerance to the previous value
      {
        cycle++;
        again = 0;
      }
      else //sensor badly calibrated
      {
        again++;
        if(again > 3)
        {
          again = 1;
          cycle++;
        }
      }
      co2_last = co2;

      if(co2 <= 500)
      {
        ws2812.fill(COLOR_BLUE, 2, 2);
      }
      else if(co2 <= 750)
      {
        ws2812.fill(COLOR_GREEN, 2, 2);
      }
      else if(co2 <= 1500)
      {
        ws2812.fill(COLOR_YELLOW, 2, 2);
      }
      else //>1500
      {
        ws2812.fill(COLOR_RED, 2, 2); //rot
      }
      ws2812.show();

      if(features & FEATURE_USB)
      {
        Serial.print("loop: ");
        Serial.println(cycle);
      }

      show_data();
    }
  }
  if(abort == 0)
  {
    if(features & FEATURE_SCD30)
    {
      scd30.forceRecalibration(400); //400ppm = fresh air
      delay(500);
    }
    else if(features & FEATURE_SCD4X)
    {
      uint16_t corr;
      scd4x.stopPeriodicMeasurement();
      delay(1000);
      scd4x.performForcedRecalibration(400, corr); //400ppm = fresh air
      delay(1000);
      scd4x.startPeriodicMeasurement();
    }
    if(again != 0)
    {
      Serial.println("Restart calibration");
      goto calibration_start;
    }
    leds(COLOR_BLUE);
    buzzer(500);
    if(features & FEATURE_USB)
    {
      Serial.println("Calibration OK");
    }
    delay(3000);
  }

  return;
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
    case 4: calibration();      break;
  }

  led_brightness_update();
  leds(ws2812.Color(20,20,20)); //LEDs white

  return;
}
