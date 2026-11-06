#include "sensors.h"
#include "led_control.h"
#include "system_control.h"
#include "i2c_utils.h"
#include "health.h"
#include "errors.h"

// Formats like "%.1f" without printf float support (not linked: saves the libc float/dtoa code)
const char *fmt1(char *buf, float value)
{
  if(value != value) //NaN
  {
    strcpy(buf, "nan");
    return buf;
  }
  long t = (long)(value * 10.0f + ((value < 0) ? -0.5f : 0.5f));
  unsigned long a = (t < 0) ? -t : t;
  sprintf(buf, "%s%lu.%lu", (t < 0) ? "-" : "", a / 10, a % 10);
  return buf;
}

// Two decimals, same reason as fmt1()
static void print2(float value)
{
  char buf[16];
  long t = (long)(value * 100.0f + ((value < 0) ? -0.5f : 0.5f));
  unsigned long a = (t < 0) ? -t : t;
  sprintf(buf, "%s%lu.%02lu", (t < 0) ? "-" : "", a / 100, a % 100);
  Serial.println(buf);
}

void print_measurements(void)
{
  char f[16];
  if(co2_valid) //only real readings (none without a CO2 sensor or before the first reading)
  {
    Serial.print("c: ");           //CO2
    Serial.println(co2_value);     //ppm
    Serial.print("t: ");           //temperature
    Serial.println(fmt1(f, temp_value)); //°C
    Serial.print("h: ");           //humidity
    Serial.println(fmt1(f, humi_value)); //%
  }
  Serial.print("l: ");           //light
  Serial.println(light_value);
  if(pres_valid)
  {
    Serial.print("p: ");         //pressure
    Serial.println(fmt1(f, pres_value)); //hPa
    Serial.print("u: ");         //temperature
    Serial.println(fmt1(f, temp2_value)); //°C
  }
  Serial.println();
}

void print_sensor_info(void) //print the sensor configuration
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
    if(scd30.getTemperatureOffset(offset) == 0) { print2(offset / 100.0f); } else { Serial.println("error"); }
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
  if(co2_failed())
  {
    Serial.println("CO2 sensor: no new readings, values withheld");
  }
  Serial.print("Firmware Temp Offset (C): ");
  print2(temp_offset);
}

void show_data(void) //show the data
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

  digitalWrite(PIN_LSENSOR_PWR, HIGH); //light sensor on
  delay(40);

  ws2812.clear(); //all LEDs off
  ws2812.show();
  delay(dark_ms);
  i = analogRead(PIN_LSENSOR); //0...1024
  i += analogRead(PIN_LSENSOR); //0...1024
  i /= 2;

  memcpy(ws2812.getPixels(), saved, sizeof(saved));
  ws2812.show();

  digitalWrite(PIN_LSENSOR_PWR, LOW); //light sensor off

  return i;
}

unsigned int light_sensor(void) //read the light sensor
{
  return light_read(LIGHT_LEDS_OFF_MS);
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

bool co2_force_recalibration(void)
{
  bool ok = false;

  if(features & FEATURE_SCD30)
  {
    ok = (scd30.forceRecalibration(400) == 0); //400ppm = fresh air
    delay(500);
  }
  else if(features & FEATURE_SCD4X)
  {
    uint16_t corr = 0xFFFF;
    scd4x.stopPeriodicMeasurement();
    delay(1000);
    ok = (scd4x.performForcedRecalibration(400, corr) == 0) && (corr != 0xFFFF); //400ppm = fresh air; 0xFFFF = failed
    delay(1000);
    scd4x.startPeriodicMeasurement();
  }
  return ok;
}

// Set automatic self calibration (ASC), only if it differs
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
    scd4x.stopPeriodicMeasurement(); //settings only in idle mode
    delay(500);
    if((scd4x.getAutomaticSelfCalibrationEnabled(asc) != 0) || ((asc != 0) != on))
    {
      scd4x.setAutomaticSelfCalibrationEnabled(on ? 1 : 0);
    }
    scd4x.startPeriodicMeasurement();
  }
}

#define PRESSURE_STALE_MS (10UL*60UL*1000UL)
#define CO2_FROZEN_MS     (30UL*60UL*1000UL)
#define RANGE_REPEATS     3 //readings in a row until a value counts as out of range

static float pres_last_raw = 0;
static unsigned long t_pres_change = 0;

// Normal altitudes; anything else is not passed on to the CO2 sensor
static bool pressure_plausible(float pressure)
{
  return (pressure >= 800) && (pressure <= 1100);
}

// A failed read gives garbage (LPS22HB about 0): keep the last value
static void store_pressure(float pressure, float temperature)
{
  error_set(ERR_PRESSURE_RANGE, !pressure_plausible(pressure));
  if(pressure != pres_last_raw)
  {
    pres_last_raw = pressure;
    t_pres_change = millis();
  }
  if((pressure >= 260) && (pressure <= 1400))
  {
    pres_value  = pressure;
    temp2_value = temperature-temp_offset;
    pres_valid  = true;
  }
}

// Own read: the library's conversion uses double (soft double code)
static int lps22_reg(uint8_t reg)
{
  Wire1.beginTransmission(ADDR_LPS22HB);
  Wire1.write(reg);
  if((Wire1.endTransmission(false) != 0) || (Wire1.requestFrom(ADDR_LPS22HB, 1) != 1))
  {
    return -1;
  }
  return Wire1.read();
}

// Pressure offset RPDS_L/H: never set here, but found at 0x0100 (-16 hPa) on a device, also after a power cycle; cause unknown
void lps22_offset_check(void)
{
  int low = lps22_reg(0x18), high = lps22_reg(0x19);
  if((low <= 0) && (high <= 0)) //0 or a failed read
  {
    return;
  }
  health.pressure_offset = (((high > 0) ? high : 0) << 8) | ((low > 0) ? low : 0);
  error_event(ERR_PRESSURE_OFFSET);
  for(uint8_t reg = 0x18; reg <= 0x19; reg++)
  {
    Wire1.beginTransmission(ADDR_LPS22HB);
    Wire1.write(reg);
    Wire1.write((uint8_t)0);
    Wire1.endTransmission();
  }
  if(features & FEATURE_USB)
  {
    Serial.print("WARNING: pressure offset was set, cleared: 0x");
    Serial.println(health.pressure_offset, HEX);
  }
}

static void read_pressure(void)
{
  uint8_t phase = sys_phase;
  phase_set(PHASE_PRESSURE);
  if(i2c_wire1_ok())
  {
    if(features & FEATURE_LPS22HB)
    {
      lps22_offset_check();
      int b[5];
      bool ok = true;
      for(int i = 0; ok && (i < 5); i++)
      {
        b[i] = lps22_reg(0x28 + i); //PRESS_OUT_XL, L, H, TEMP_OUT_L, H
        ok = (b[i] >= 0);
      }
      if(ok)
      {
        store_pressure((b[0] | (b[1] << 8) | ((uint32_t)b[2] << 16)) / 4096.0f, (int16_t)(b[3] | (b[4] << 8)) / 100.0f);
      }
    }
    if(features & FEATURE_BMP280)
    {
      store_pressure(bmp280.readPressure()/100, bmp280.readTemperature()); //Pa -> hPa
    }
  }
  sys_phase = phase;
}

static unsigned long t_reading = 0;
static bool reading_failed = false;

static unsigned long t_co2_change = 0;
static unsigned int co2_last_value = 0;

static void repeat_check(ERROR_CODE e, uint8_t *repeats, bool bad)
{
  *repeats = bad ? ((*repeats < RANGE_REPEATS) ? *repeats + 1 : RANGE_REPEATS) : 0;
  error_set(e, *repeats >= RANGE_REPEATS);
}

// Ranges of the raw values, before they are clamped for display
static void co2_reading_check(float co2, float temperature, float humidity)
{
  static uint8_t bad_co2 = 0, bad_temp = 0, bad_humi = 0;
  repeat_check(ERR_CO2_RANGE, &bad_co2, !((co2 > 0) && (co2 <= 40000)));
  repeat_check(ERR_TEMP_RANGE, &bad_temp, !((temperature >= -10) && (temperature <= 60)));
  repeat_check(ERR_HUMIDITY_RANGE, &bad_humi, !((humidity >= 0) && (humidity <= 100)));
}

static void co2_reading_ok(void)
{
  t_reading = millis();
  co2_valid = true;
  reading_failed = false;
  error_set(ERR_CO2_STALE, false);
  if((co2_value != co2_last_value) || (t_co2_change == 0))
  {
    co2_last_value = co2_value;
    t_co2_change = millis();
  }
}

// Once per second from loop(): values that stopped changing
void sensors_error_check(void)
{
  error_set(ERR_CO2_FROZEN, !reading_failed && (t_co2_change != 0) && ((millis() - t_co2_change) > CO2_FROZEN_MS));
  if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
  {
    if(t_pres_change == 0)
    {
      t_pres_change = millis();
    }
    error_set(ERR_PRESSURE_STALE, (millis() - t_pres_change) > PRESSURE_STALE_MS);
  }
}

// Keeps the last value out of the web page, MQTT and the traffic light when the sensor stops delivering
bool co2_failed(void)
{
  if((features & (FEATURE_SCD30|FEATURE_SCD4X)) == 0)
  {
    return false; //no sensor found at boot: has its own display
  }
  if(t_reading == 0)
  {
    t_reading = millis(); //first call, end of the boot
  }
  if((millis() - t_reading) <= CO2_TIMEOUT)
  {
    return false;
  }
  if(!reading_failed)
  {
    reading_failed = true;
    co2_valid = false;
    error_set(ERR_CO2_STALE, true);
    health.co2_failures++;
    if(features & FEATURE_USB)
    {
      Serial.println("WARNING: no new CO2 reading for 60 s");
    }
  }
  return true;
}

unsigned int check_sensors(void) //read the sensors
{
  static unsigned long t_pres = 0;
  phase_set(PHASE_CO2);
  if((features & (FEATURE_SCD30|FEATURE_SCD4X)) && !i2c_wire_ok())
  {
    return 0;
  }
  if(features & FEATURE_SCD30)
  {
    static uint8_t read_fails = 0;
    uint16_t ready = 0;
    float v_co2, v_temp, v_humi;
    int16_t err = scd30.getDataReady(ready);
    if((err == 0) && ready)
    {
      err = scd30.readMeasurementData(v_co2, v_temp, v_humi);
    }
    repeat_check(ERR_CO2_READ_FAILED, &read_fails, err != 0);
    if((err == 0) && ready)
    {
      co2_reading_check(v_co2, v_temp, v_humi);
      co2_value  = (v_co2 > 0) ? (unsigned int)(v_co2 + 0.5f) : 0;
      temp_value = v_temp;
      humi_value = v_humi;
      read_pressure();
      if((pres_value < (pres_last-PRESSURE_DIFF)) || (pres_value > (pres_last+PRESSURE_DIFF)))
      {
        pres_last = pres_value;
        if(pressure_plausible(pres_value))
        {
          scd30.startPeriodicMeasurement(pres_value); //pressure compensation, hPa=mbar
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
      co2_reading_ok();
      return 1;
    }
  }
  else if(features & FEATURE_SCD4X)
  {
    uint16_t v_co2;
    uint16_t v_temp;
    uint16_t v_humi;
    if(scd4x.readMeasurementRaw(v_co2, v_temp, v_humi) == 0)
    {
      co2_value  = v_co2;
      temp_value = scd4x_temperature(v_temp);
      humi_value = scd4x_humidity(v_humi);
      co2_reading_check(v_co2, temp_value, humi_value);
      read_pressure();
      if(pressure_plausible(pres_value) && ((pres_value < (pres_last-PRESSURE_DIFF)) || (pres_value > (pres_last+PRESSURE_DIFF))))
      {
        pres_last = pres_value;
        scd4x.stopPeriodicMeasurement();
        delay(1000);
        scd4x.setAmbientPressureRaw((uint16_t)(pres_value + 0.5f)); //hPa
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
      co2_reading_ok();
      return 1;
    }
  }
  else if((millis() - t_pres) > (MEASURE_INTERVAL*1000UL)) //without a CO2 sensor: still read the pressure
  {
    t_pres = millis();
    read_pressure();
  }

  return 0;
}
