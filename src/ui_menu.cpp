#include "ui_menu.h"

#include "app_globals.h"
#include "diagnostics.h"
#include "led_control.h"
#include "sensors.h"
#include "settings_storage.h"

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

  for(sw=0, timeout=0; timeout<1000; timeout++) //10s Timeout
  {
    delay(10); //10ms warten

    if(digitalRead(PIN_SWITCH) == LOW) //Taster gedrueckt
    {
      status_led(1); //Status-LED an
      sw++;
      if(sw > 200)
      {
        leds(COLOR_OFF); //LEDs aus
      }
      timeout = 0;
    }
    else //Taster losgelassen
    {
      status_led(0); //Status-LED aus
      if(sw > 200) //2s Tastendruck
      {
        break;
      }
      else if(sw > 10) //100ms Tastendruck
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

  leds(COLOR_OFF); //LEDs aus
  delay(500); //500ms warten

  return value;
}

void altitude_toffset(void) //Altitude und Temperaturoffset
{
  unsigned int timeout, sw, value=0;

  //Altitude
  if(features & FEATURE_SCD30)
  {
    value = scd30.getAltitudeCompensation() / 250; //Meter ueber dem Meeresspiegel
  }
  else if(features & FEATURE_SCD4X)
  {
    uint16_t altitude;
    scd4x.stopPeriodicMeasurement();
    delay(500);
    scd4x.getSensorAltitude(altitude); //Meter ueber dem Meeresspiegel
    value = altitude/250;
  }

  value = select_value(value, 0, 4, 1, COLOR_RED, FARBE_WEISS) * 250;

  if(features & FEATURE_SCD30)
  {
    scd30.setAltitudeCompensation(value); //Meter ueber dem Meeresspiegel
  }
  else if(features & FEATURE_SCD4X)
  {
    scd4x.setSensorAltitude(value); //Meter ueber dem Meeresspiegel
  }

  if(features & FEATURE_USB)
  {
    Serial.print("Altitude: ");
    Serial.println(value, DEC);
  }

  //Temperaturoffset
  if(features & FEATURE_SCD30)
  {
    value = scd30.getTemperatureOffset() / 2; //Temperaturoffset
  }
  else if(features & FEATURE_SCD4X)
  {
    float offset;
    scd4x.getTemperatureOffset(offset); //Meter ueber dem Meeresspiegel
    value = offset / 2;
  }

  value = select_value(value, 0, 4, 1, COLOR_YELLOW, COLOR_BLUE) * 2;

  if(features & FEATURE_SCD30)
  {
    scd30.setTemperatureOffset(value); //Temperaturoffset
  }
  else if(features & FEATURE_SCD4X)
  {
    scd4x.setTemperatureOffset(value); //Temperaturoffset
    delay(500);
    scd4x.startPeriodicMeasurement();
  }

  if(features & FEATURE_USB)
  {
    Serial.print("Temperature: ");
    Serial.println(value, DEC);
  }

  //Buzzer
  settings.buzzer = select_value(settings.buzzer, 0, 1, 1, COLOR_GREEN, COLOR_WHITE);

  if(features & FEATURE_USB)
  {
    Serial.print("Buzzer: ");
    Serial.println(settings.buzzer, DEC);
  }

  //Ende
  settings_write(&settings); //Einstellungen speichern
  leds(COLOR_BLUE);//LEDs blau
  buzzer(250); //250ms Buzzer an

  return;
}

void calibration(void) //Kalibrierung
{
  unsigned int abort=0, cycle, again, interval=INTERVALL, co2, co2_last;

  //Der Messintervall waehrend der Kalibrierung und im Betrieb sollte gleich sein.
  //Unterschiedliche Intervalle koennen zu Abweichungen und schwankenden Messwerten fuehren.
  //scd30.setMeasurementInterval(INTERVALL); //setze Messintervall

  ws2812.fill(FARBE_WEISS, 0, 4); //LEDs weiss
  ws2812.show();

  if(features & FEATURE_SCD4X)
  {
    interval = 5; //5s
  }

  //ASC
  if(features & FEATURE_SCD30)
  {
    if(AUTO_KALIBRIERUNG) //ASC on
    {
      if(scd30.getAutoSelfCalibration() == 0)
      {
        scd30.setAutoSelfCalibration(1);
      }
    }
    else //ASC off
    {
      if(scd30.getAutoSelfCalibration() != 0)
      {
        scd30.setAutoSelfCalibration(0);
      }
    }
  }
  else if(features & FEATURE_SCD4X)
  {
    scd4x.stopPeriodicMeasurement();
    delay(1000);
    if(AUTO_KALIBRIERUNG) //ASC on
    {
      uint16_t asc;
      scd4x.getAutomaticSelfCalibration(asc);
      if(asc == 0)
      {
        scd4x.setAutomaticSelfCalibration(1);
      }
    }
    else //ASC off
    {
      uint16_t asc;
      scd4x.getAutomaticSelfCalibration(asc);
      if(asc != 0)
      {
        scd4x.setAutomaticSelfCalibration(0);
      }
    }
    delay(500);
    scd4x.startPeriodicMeasurement();
  }

  calibration_start:

  //Kalibrierung
  co2 = co2_last = co2_sensor();
  for(again=0, cycle=0; cycle < (180/interval);)
  {
    if(digitalRead(PIN_SWITCH) == 0) //Taster gedrueckt?
    {
      abort = 1;
      break; //Abbruch
    }

    status_led(200); //Status-LED

    if(check_sensors())
    {
      co2 = co2_sensor();
      if((co2 >= 200) && (co2 <= 800) && 
         (co2 >= (co2_last-30)) &&
         (co2 <= (co2_last+30))) //+/-30ppm Toleranz zum vorherigen Wert
      {
        cycle++;
        again = 0;
      }
      else //Sensor falsch kalibriert
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
        ws2812.fill(COLOR_BLUE, 2, 2); //blau
      }
      else if(co2 <= 750)
      {
        ws2812.fill(COLOR_GREEN, 2, 2); //gruen
      }
      else if(co2 <= 1500)
      {
        ws2812.fill(COLOR_YELLOW, 2, 2); //gelb
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
      scd30.setForcedRecalibrationFactor(400); //400ppm = Frischluft
      delay(500);
    }
    else if(features & FEATURE_SCD4X)
    {
      uint16_t corr;
      scd4x.stopPeriodicMeasurement();
      delay(1000);
      scd4x.performForcedRecalibration(400, corr); //400ppm = Frischluft
      delay(1000);
      scd4x.startPeriodicMeasurement();
    }
    if(again != 0)
    {
      Serial.println("Restart calibration");
      goto calibration_start;
    }
    leds(COLOR_BLUE);//LEDs blau
    buzzer(500); //500ms Buzzer an
    if(features & FEATURE_USB)
    {
      Serial.println("Calibration OK");
    }
    delay(3000); //3s warten
  }

  return;
}

void menu(void)
{
  unsigned int timeout, sw, value;

  ws2812.setBrightness(30); //0...255
  leds(FARBE_VIOLETT); //LEDs violett
  delay(500); //500ms warten
  leds(FARBE_AUS); //LEDs aus
  buzzer(250); //250ms Buzzer an

  value = select_value(1, 1, 4, 1, FARBE_VIOLETT, ws2812.Color(20,20,20));

  switch(value)
  {
    case 1: self_test();        break;
    case 2: air_test();         break;
    case 3: altitude_toffset(); break;
    case 4: calibration();      break;
  }

  ws2812.setBrightness(settings.brightness); //0...255
  leds(ws2812.Color(20,20,20));//LEDs weiss

  return;
}
