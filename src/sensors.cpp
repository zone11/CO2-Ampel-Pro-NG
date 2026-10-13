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

void print_sensor_info(void) //Sensor-Konfiguration ausgeben
{
  Serial.print("CO2 Sensor: ");
  if(features & FEATURE_SCD30)
  {
    uint16_t offset = 0, altitude = 0, asc = 0, interval = 0;
    uint8_t fw_major = 0, fw_minor = 0;
    Serial.print("SCD30");
    if(scd30.readFirmwareVersion(fw_major, fw_minor) == 0)
    {
      Serial.print(" fw ");
      Serial.print(fw_major);
      Serial.print(".");
      Serial.print(fw_minor);
    }
    Serial.println();
    Serial.print("  Temp Offset (C): ");
    if(scd30.getTemperatureOffset(offset) == 0) { Serial.println(offset / 100.0f, 2); } else { Serial.println("error"); }
    Serial.print("  Altitude (m): ");
    if(scd30.getAltitudeCompensation(altitude) == 0) { Serial.println(altitude); } else { Serial.println("error"); }
    Serial.print("  ASC: ");
    if(scd30.getAutoCalibrationStatus(asc) == 0) { Serial.println(asc ? "on" : "off"); } else { Serial.println("error"); }
    Serial.print("  Interval (s): ");
    if(scd30.getMeasurementInterval(interval) == 0) { Serial.println(interval); } else { Serial.println("error"); }
  }
  else if(features & FEATURE_SCD4X)
  {
    Serial.println("SCD4X");
  }
  else
  {
    Serial.println("none");
  }
  Serial.print("Firmware Temp Offset (C): ");
  Serial.println(temp_offset, 2);
}

void show_data(void) //Daten anzeigen
{
  if((features & FEATURE_USB) && settings.serial_output)
  {
    print_measurements();
  }

  return;
}

// Reads the ambient light sensor. The sensor sits next to the LEDs, so they must be
// dark during the reading. The sensor supply settles while the LEDs are still on;
// the LEDs are then switched off only for dark_ms plus the two ADC reads, and the
// exact pixel buffer is restored afterwards (getPixelColor() is lossy with brightness).
static unsigned int light_read(unsigned int dark_ms)
{
  unsigned int i;
  uint8_t saved[NUM_LEDS * 3]; //NEO_GRB: 3 bytes per LED, brightness-scaled

  memcpy(saved, ws2812.getPixels(), sizeof(saved));

  digitalWrite(PIN_LSENSOR_PWR, HIGH); //Lichtsensor an
  delay(40); //40ms warten

  ws2812.clear(); //alle LEDs aus
  ws2812.show();
  delay(dark_ms);
  i = analogRead(PIN_LSENSOR); //0...1024
  i += analogRead(PIN_LSENSOR); //0...1024
  i /= 2;

  memcpy(ws2812.getPixels(), saved, sizeof(saved));
  ws2812.show();

  digitalWrite(PIN_LSENSOR_PWR, LOW); //Lichtsensor aus

  return i;
}

unsigned int light_sensor(void) //Auslesen des Lichtsensors
{
  return light_read(LICHT_DUNKEL_MS);
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

// Automatische Selbstkalibrierung (ASC) setzen, nur wenn abweichend
void co2_set_asc(bool on)
{
  uint16_t asc = 0;

  if(features & FEATURE_SCD30)
  {
    if((scd30.getAutoCalibrationStatus(asc) != 0) || ((asc != 0) != on))
    {
      scd30.activateAutoCalibration(on ? 1 : 0);
    }
  }
  else if(features & FEATURE_SCD4X)
  {
    scd4x.stopPeriodicMeasurement(); //Einstellungen nur im Idle-Modus
    delay(500);
    if((scd4x.getAutomaticSelfCalibrationEnabled(asc) != 0) || ((asc != 0) != on))
    {
      scd4x.setAutomaticSelfCalibrationEnabled(on ? 1 : 0);
    }
    scd4x.startPeriodicMeasurement();
  }
}

unsigned int check_sensors(void) //Sensoren auslesen
{
  if(features & FEATURE_SCD30)
  {
    uint16_t ready = 0;
    float v_co2, v_temp, v_humi;
    if((scd30.getDataReady(ready) == 0) && ready &&
       (scd30.readMeasurementData(v_co2, v_temp, v_humi) == 0))
    {
      co2_value  = (unsigned int)(v_co2 + 0.5f);
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
        if((pres_value >= 700) && (pres_value <= 1400)) //gueltiger Bereich
        {
          scd30.startPeriodicMeasurement(pres_value); //Druckkompensation, hPa=mBar
        }
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
        scd4x.setAmbientPressure((uint32_t)(pres_value * 100)); //hPa -> Pa
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
