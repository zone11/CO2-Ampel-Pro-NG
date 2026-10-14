/*
  CO2-Ampel Pro NG Firmware
    https://github.com/zone11/CO2-Ampel-Pro-NG
*/

#include <Arduino.h>
#include "app_config.h"
#include "app_types.h"


#include <Wire.h>
#include <SPI.h>
#include <SensirionI2cScd30.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_BMP280.h>
#include <Arduino_LPS22HB.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi101.h>
#include <MQTT.h>
#include "tls_client.h"

#include "led_control.h"
#include "sensors.h"
#include "mqtt_service.h"
#include "webserver.h"
#include "settings_storage.h"
#include "wifi_manager.h"
#include "serial_console.h"
#include "i2c_utils.h"
#include "ui_menu.h"

extern USBDeviceClass USBDevice; //USBCore.cpp

// PlatformIO: Wire1 is automatically defined by the Arduino SAMD framework
// via the definitions in variant.h (PIN_WIRE1_SDA, PIN_WIRE1_SCL, PERIPH_WIRE1, WIRE1_IT_HANDLER)

SETTINGS settings;
SensirionI2cScd30 scd30;
SensirionI2cScd4x scd4x;
Adafruit_BMP280 bmp280(&Wire1);
LPS22HBClass lps22(Wire1);
Adafruit_NeoPixel ws2812 = Adafruit_NeoPixel(NUM_LEDS, PIN_WS2812, NEO_GRB + NEO_KHZ800);
#if WITH_WEB
WiFiServer server(80); //web server port 80
#endif
WiFiClient mqttWifiClient;
VerifiedSSLClient mqttSslClient; //TLS in the WINC1500 incl. hostname check (root certificate must be in the WINC store)
#if WITH_MQTT_CONFIG
MQTTClient mqttClient(1200, 1536); //read: JSON commands up to 1 KB, write: replies up to ~1.4 KB
#else
MQTTClient mqttClient(256, 1536);
#endif

unsigned int features=0, remote_on=0, buzzer_timer=BUZZER_DELAY;
unsigned int co2_value=CO2_START, co2_average=CO2_START, light_value=1024;
float temp_value=20, temp_offset=TEMP_OFFSET, humi_value=50, pres_value=1013, pres_last=1013, temp2_value=20;
bool co2_valid=false, pres_valid=false; //start values above are no readings (not published)


void setup()
{
  int run_menu=0;

  //pins
  pinMode(6, INPUT_PULLUP); //PA08 SDA1
  pinMode(7, INPUT_PULLUP); //PA09 SCL1
  pinMode(8, INPUT_PULLUP); //PA12 SDA2
  pinMode(9, INPUT_PULLUP); //PA13 SCL2
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_WS2812, OUTPUT);
  digitalWrite(PIN_WS2812, LOW);
  pinMode(PIN_LSENSOR_PWR, OUTPUT);
  digitalWrite(PIN_LSENSOR_PWR, LOW); //light sensor off
  pinMode(PIN_LSENSOR, INPUT);
  pinMode(PIN_SWITCH, INPUT_PULLUP);
  pinMode(14, OUTPUT); //PA18 WINC1500 CS pin
  digitalWrite(14, HIGH); //WINC1500 CS high
  pinMode(PIN_RFM9X_CS, OUTPUT); //PA21 RFM9X CS pin
  digitalWrite(PIN_RFM9X_CS, HIGH); //RFM9X CS high

  if(digitalRead(PIN_SWITCH) == LOW) //button pressed
  {
    run_menu = 1;
  }

  //WS2812
  ws2812.begin();
  ws2812.setBrightness(BRIGHTNESS); //0...255
  ws2812.fill(COLOR_OFF, 0, NUM_LEDS);
  ws2812.fill(ws2812.Color(20,20,20), 0, 4); //4 LEDs white
  ws2812.show();

  //Wire/I2C: release hung buses (e.g. restart while the SCD30 sends), then start
  bool bus0_stuck = i2c_bus_recover(PIN_WIRE_SDA, PIN_WIRE_SCL);
  bool bus1_stuck = i2c_bus_recover(PIN_WIRE1_SDA, PIN_WIRE1_SCL);
  Wire.begin();
  Wire.setClock(50000); //50kHz, recommended for the SCD30
  Wire1.begin();
  Wire1.setClock(100000); //100kHz ATECC+LPS22HB+BMP280

  //serial port (USB)
  Serial.begin(BAUDRATE);
  //while(!Serial); //wait for USB

  delay(250);

  if(WiFi.status() != WL_NO_SHIELD) //ATWINC1500 found
  {
    features |= FEATURE_WINC1500;
  }

  //LPS22HB
  if(check_i2c(SERCOM2, ADDR_LPS22HB)) //LPS22HB found
  {
    if(lps22.begin())
    {
      features |= FEATURE_LPS22HB;
    }
  }

  //BMP280
  if(check_i2c(SERCOM2, ADDR_BMP280)) //BMP280 found
  {
    if(bmp280.begin(ADDR_BMP280))
    {
      features |= FEATURE_BMP280;
    }
  }
  else if(check_i2c(SERCOM2, ADDR_BMP280+1)) //BMP280 found
  {
    if(bmp280.begin(ADDR_BMP280+1))
    {
      features |= FEATURE_BMP280;
    }
  }

  //SCD30+SCD4X
  if(check_i2c(SERCOM0, ADDR_SCD30)) //SCD30 found
  {
    for(int t=5; t!=0; t--) //try 5 times
    {
      Wire.begin();
      scd30.begin(Wire, ADDR_SCD30);
      if(scd30.startPeriodicMeasurement(0) == 0) //0 = no pressure compensation
      {
        features |= FEATURE_SCD30;
        break;
      }
      status_led(1000);
    }
    scd30.setMeasurementInterval(MEASURE_INTERVAL);
    //scd30.startPeriodicMeasurement(1000); //0 or 700-1400, pressure in hPa
  }
  if(check_i2c(SERCOM0, ADDR_SCD4X)) //SCD4X found
  {
    for(int t=5; t!=0; t--) //try 5 times
    {
      Wire.begin();
      scd4x.begin(Wire, ADDR_SCD4X);
      scd4x.stopPeriodicMeasurement();
      delay(100);
      if(scd4x.startPeriodicMeasurement() == 0)
      {
        features |= FEATURE_SCD4X;
        break;
      }
      status_led(1000);
    }
  }

  //temperature offset
  if(features & FEATURE_SCD30)
  {
    uint16_t offset;
    if(scd30.getTemperatureOffset(offset) == 0)
    {
      temp_offset = offset / 100.0f;
    }
  }
  else if(features & FEATURE_SCD4X)
  {
    float offset;
    scd4x.stopPeriodicMeasurement();
    delay(500);
    if(scd4x.getTemperatureOffset(offset) == 0)
    {
      temp_offset = offset;
    }
    delay(500);
    scd4x.startPeriodicMeasurement();
  }
  if(temp_offset >= 20)
  {
    temp_offset = TEMP_OFFSET;
  }

  //settings
  SETTINGS_LOAD load = settings_read(&settings);
  if((load == SETTINGS_MIGRATED) || (load == SETTINGS_DEFAULTS))
  {
    settings_write(&settings); //save defaults or migrated settings
  }
  if(load == SETTINGS_DEFAULTS)
  {
    //default temperature offset (always Pro with WiFi and pressure sensor)
    temp_offset = TEMP_OFFSET;
    if(features & FEATURE_SCD30)
    {
      uint16_t offset = 0;
      scd30.getTemperatureOffset(offset); //degC*100
      if((offset == 0) || (offset > 1200))
      {
        scd30.setTemperatureOffset(temp_offset * 100);
      }
    }
    else if(features & FEATURE_SCD4X)
    {
      float offset;
      scd4x.getTemperatureOffset(offset);
      if((offset == 0) || (offset > 12))
      {
        scd4x.setTemperatureOffset(temp_offset);
      }
    }
  }
  co2_set_asc(settings.asc); //ASC from the settings
  led_brightness_update();

  //USB connection
  if(USBDevice.connected()) //(Serial) uses flow control for detection
  {
    features |= FEATURE_USB;
    delay(1500);
    Serial.println("\nCO2 Ampel Pro NG v" VERSION);
    Serial.print("Features:");
    if(features & FEATURE_SCD30)    { Serial.print(" SCD30"); }
    if(features & FEATURE_SCD4X)    { Serial.print(" SCD4X"); }
    if(features & FEATURE_LPS22HB)  { Serial.print(" LPS22HB"); }
    if(features & FEATURE_BMP280)   { Serial.print(" BMP280"); }
    if(features & FEATURE_WINC1500) { Serial.print(" WINC1500"); }
    Serial.println();
    if(bus0_stuck || bus1_stuck)
    {
      Serial.print("I2C bus recovered (SDA was held low):");
      Serial.println(bus0_stuck ? (bus1_stuck ? " Wire, Wire1" : " Wire") : " Wire1");
    }
    if((features & (FEATURE_SCD30|FEATURE_SCD4X)) == 0)
    {
      Serial.println("Error: CO2 sensor not found (no CO2/temperature/humidity values; power-cycle the device)");
    }
    Serial.print("Settings: ");
    Serial.println((load == SETTINGS_LOADED) ? "loaded" : (load == SETTINGS_MIGRATED) ? "migrated from old layout" :
                   (load == SETTINGS_NEWER) ? "from newer firmware, using defaults (flash kept until save)" : "defaults");
    Serial.println();
  }

  //service menu
  if(run_menu)
  {
    menu();
  }

  //Plus version
  if(features & FEATURE_WINC1500)
  {
    if(WiFi.status() != WL_NO_SHIELD) //ATWINC1500 found
    {
      if(wifi_start() != 0) //connect to WiFi
      {
        if(wifi_start_ap() != 0) //start AP
        {
          features &= ~FEATURE_WINC1500;
        }
      }
      delay(2000);
      if(features & FEATURE_USB)
      {
        String fv = WiFi.firmwareVersion();
        Serial.print("WINC1500 Firmware: ");
        Serial.println(fv);
        byte mac[6];
        WiFi.macAddress(mac);
        Serial.print("MAC: ");
        Serial.print(mac[5], HEX); Serial.print(":"); Serial.print(mac[4], HEX); Serial.print(":"); Serial.print(mac[3], HEX); Serial.print(":");
        Serial.print(mac[2], HEX); Serial.print(":"); Serial.print(mac[1], HEX); Serial.print(":"); Serial.print(mac[0], HEX); Serial.println("");
        IPAddress ip;
        ip = WiFi.localIP();
        Serial.print("IP: "); print_ip_address_line(ip);
        ip = WiFi.subnetMask();
        Serial.print("NM: "); print_ip_address_line(ip);
        ip = WiFi.gatewayIP();
        Serial.print("GW: "); print_ip_address_line(ip);
        Serial.println("");
      }
      //connect MQTT
      if(settings.mqtt_enabled)
      {
        delay(1000);
        mqtt_connect();
      }
    }
    else
    {
      features &= ~FEATURE_WINC1500;
      WiFi.end();
    }
  }

  //start measurement
  co2_value = co2_average = CO2_START;
  if(features & FEATURE_SCD30)
  {
    scd30.setMeasurementInterval(MEASURE_INTERVAL);
    delay(MEASURE_INTERVAL*1000UL); //wait one interval
  }
  else if(features & FEATURE_SCD4X)
  {
    //interval 5s
  }
  else
  {
    if(features & FEATURE_USB)
    {
      Serial.println("Error: CO2 sensor not found");
    }
    leds(COLOR_RED);
    status_led(1000);
    leds(COLOR_OFF);
    co2_value = co2_average = settings.range[2]; //set to red threshold
  }

  return;
}


void loop()
{
  static unsigned int dark=0, sw=0;
  static unsigned long t_switch=0, t_traffic=0, t_light=~((LIGHT_INTERVAL*1000UL*60UL)-60000UL); //check the light sensor after 60s
  static unsigned long t_wifi=0;
  unsigned int overwrite=0;

  //serial commands
  serial_service();

#if WITH_WEB
  //WiFi data
  webserver_service();
#endif

  //MQTT data
  mqtt_service();

  if(features & FEATURE_WINC1500)
  {
    int wifi_status = WiFi.status();
    if((wifi_status != WL_CONNECTED) && (wifi_status != WL_AP_LISTENING))
    {
      if(settings.wifi_ssid[0] != 0)
      {
        if((millis() - t_wifi) > 30000)
        {
          t_wifi = millis();
          if(wifi_start() == 0 && settings.mqtt_enabled)
          {
            mqtt_connect();
          }
        }
      }
    }
  }

  //button
  if(digitalRead(PIN_SWITCH) == LOW) //pressed
  {
    if(sw == 0)
    {
      sw = 1;
      t_switch = millis();
    }
  }
  else if(sw != 0) //released
  {
    sw = 0;
    buzzer(0);
    buzzer_timer = BUZZER_DELAY; //buzzer start delay
    if((millis()-t_switch) > 3000) //3s press
    {
      if(features & FEATURE_WINC1500)
      {
        leds(COLOR_MENU);
        wifi_start_ap();
      }
    }
    else if((millis()-t_switch) > 100) //100ms press
    {
      led_button_step(); //halve the brightness (not saved)
      overwrite = 1;
    }
  }

  if((millis()-t_traffic) > 1000) //traffic light once per second
  {
    t_traffic = millis();

    if(buzzer_timer > 0)
    {
      buzzer_timer--;
    }

    //USB connection
    if(USBDevice.connected()) //(Serial) uses flow control for detection
    {
      features |= FEATURE_USB;
    }
    //else
    //{
    //  features &= ~FEATURE_USB;
    //}

    //read sensor data
    if(check_sensors())
    {
      show_data();
      if((dark == 0) && !settings.led_disable)
      {
        status_led(2);
      }
    }

    co2_average = (co2_average + co2_sensor()) / 2; //average, once per second
  }
  else if(overwrite == 0)
  {
    return;
  }

  //traffic light
  if(remote_on == 0)
  {
    #if USE_CO2_AVERAGE > 0
      traffic_light(co2_average);
    #else
      traffic_light(co2_value);
    #endif
  }

  //light sensor
  if(remote_on == 0)
  {
    if((millis()-t_light) > (LIGHT_INTERVAL*1000UL*60UL))
    {
      t_light = millis();

      light_value = light_sensor();
      if(light_value < LIGHT_DARK)
      {
        if(dark == 0)
        {
          dark = 1;
          led_set_dark(true); //dark (only with led.auto)
        }
      }
      else
      {
        if(dark == 1)
        {
          dark = 0;
          led_set_dark(false); //bright
        }
      }
    }
  }

  return;
}
