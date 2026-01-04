#include "sensors.h"
#include "led_control.h"

void print_measurements(void)
{
  Serial.print("c: ");           //CO2
  Serial.println(co2_value);     //Wert in ppm
  Serial.print("t: ");           //Temperatur
  Serial.println(temp_value, 1); //Wert in °C
  Serial.print("h: ");           //Humidity/Luftfeuchte
  Serial.println(humi_value, 1); //Wert in %
  Serial.print("l: ");           //Licht
  Serial.println(light_value);
  if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
  {
    Serial.print("p: ");         //Druck
    Serial.println(pres_value);  //Wert in hPa
    Serial.print("u: ");         //Temperatur
    Serial.println(temp2_value); //Wert in °C
  }
  Serial.println();
}

void show_data(void) //Daten anzeigen
{
  if((features & FEATURE_USB) && settings.serial_output)
  {
    print_measurements();
  }

  return;
}

unsigned int light_sensor(void) //Auslesen des Lichtsensors
{
  unsigned int i;
  uint32_t color = ws2812.getPixelColor(0); //aktuelle Farbe speichern

  //ws2812.setPixelColor(2, FARBE_AUS); //LED 3 aus
  ws2812.fill(COLOR_OFF, 0, 4); //alle 4 LEDs aus
  ws2812.show();

  digitalWrite(PIN_LSENSOR_PWR, HIGH); //Lichtsensor an
  delay(40); //40ms warten
  i = analogRead(PIN_LSENSOR); //0...1024
  delay(10); //10ms warten
  i += analogRead(PIN_LSENSOR); //0...1024
  i /= 2;
  digitalWrite(PIN_LSENSOR_PWR, LOW); //Lichtsensor aus

  //ws2812.setPixelColor(2, color); //LED 3 an
  leds(color);

  return i;
}

float co2_sensor(void)
{
  return co2_value;
}

float temp_sensor(void)
{
  return temp_value;
}

float humi_sensor(void)
{
  return humi_value;
}

float pres_sensor(void)
{
  return pres_value;
}

unsigned int check_sensors(void) //Sensoren auslesen
{
  if(features & FEATURE_SCD30)
  {
    if(scd30.dataAvailable())
    {
      co2_value  = scd30.getCO2();
      temp_value = scd30.getTemperature();
      humi_value = scd30.getHumidity();
      if(features & FEATURE_LPS22HB)
      {
        pres_value  = lps22.readPressure()*10; //kPa -> hPa
        temp2_value = lps22.readTemperature()-temp_offset;
      }
      if(features & FEATURE_BMP280)
      {
        pres_value  = bmp280.readPressure()/100; //Pa -> hPa
        temp2_value = bmp280.readTemperature()-temp_offset;
      }
      if((pres_value < (pres_last-DRUCK_DIFF)) || (pres_value > (pres_last+DRUCK_DIFF)))
      {
        pres_last = pres_value;
        scd30.setAmbientPressure(pres_value); //hPa=mBar
      }
      if(humi_value < 0)
      {
        humi_value = 0;
      }
      else if(humi_value > 100)
      {
        humi_value = 100;
      }
      return 1;
    }
  }
  else if(features & FEATURE_SCD4X)
  {
    uint16_t v_co2;
    float v_temp;
    float v_humi;
    if(scd4x.readMeasurement(v_co2, v_temp, v_humi) == 0)
    {
      co2_value  = v_co2;
      temp_value = v_temp;
      humi_value = v_humi;
      if(features & FEATURE_LPS22HB)
      {
        pres_value  = lps22.readPressure()*10; //kPa -> hPa
        temp2_value = lps22.readTemperature()-temp_offset;
      }
      if(features & FEATURE_BMP280)
      {
        pres_value  = bmp280.readPressure()/100; //Pa -> hPa
        temp2_value = bmp280.readTemperature()-temp_offset;
      }
      if((pres_value < (pres_last-DRUCK_DIFF)) || (pres_value > (pres_last+DRUCK_DIFF)))
      {
        pres_last = pres_value;
        scd4x.stopPeriodicMeasurement();
        delay(1000);
        scd4x.setAmbientPressure(pres_value); //hPa=mBar
        delay(500);
        scd4x.startPeriodicMeasurement();
      }
      if(humi_value < 0)
      {
        humi_value = 0;
      }
      else if(humi_value > 100)
      {
        humi_value = 100;
      }
      return 1;
    }
  }

  return 0;
}
