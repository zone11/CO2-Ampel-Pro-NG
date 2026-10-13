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
WiFiServer server(80); //Webserver Port 80
WiFiClient mqttWifiClient;
MQTTClient mqttClient(256); //256 Byte Buffer

unsigned int features=0, remote_on=0, buzzer_timer=BUZZER_DELAY;
unsigned int co2_value=STARTWERT, co2_average=STARTWERT, light_value=1024;
float temp_value=20, temp_offset=TEMP_OFFSET, humi_value=50, pres_value=1013, pres_last=1013, temp2_value=20;


void setup()
{
  int run_menu=0;

  //setze Pins
  pinMode(6, INPUT_PULLUP); //PA08 SDA1
  pinMode(7, INPUT_PULLUP); //PA09 SCL1
  pinMode(8, INPUT_PULLUP); //PA12 SDA2
  pinMode(9, INPUT_PULLUP); //PA13 SCL2
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW); //LED aus
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_WS2812, OUTPUT);
  digitalWrite(PIN_WS2812, LOW);
  pinMode(PIN_LSENSOR_PWR, OUTPUT);
  digitalWrite(PIN_LSENSOR_PWR, LOW); //Lichtsensor aus
  pinMode(PIN_LSENSOR, INPUT);
  pinMode(PIN_SWITCH, INPUT_PULLUP);
  pinMode(14, OUTPUT); //PA18 WINC1500 CS-Pin
  digitalWrite(14, HIGH); //WINC1500 CS high
  pinMode(PIN_RFM9X_CS, OUTPUT); //PA21 RFM9X CS-Pin
  digitalWrite(PIN_RFM9X_CS, HIGH); //RFM9X CS high

  if(digitalRead(PIN_SWITCH) == LOW) //Taster gedrueckt
  {
    run_menu = 1;
  }

  //WS2812
  ws2812.begin();
  ws2812.setBrightness(HELLIGKEIT); //0...255
  ws2812.fill(FARBE_AUS, 0, NUM_LEDS); //LEDs aus
  ws2812.fill(ws2812.Color(20,20,20), 0, 4); //4 LEDs weiss
  ws2812.show();

  //Wire/I2C
  Wire.begin();
  Wire.setClock(50000); //50kHz, empfohlen fuer SCD30
  Wire1.begin();
  Wire1.setClock(100000); //100kHz ATECC+LPS22HB+BMP280

  //serielle Schnittstelle (USB)
  Serial.begin(BAUDRATE); //seriellen Port starten
  //while(!Serial); //warten auf USB-Verbindung

  delay(250); //250ms warten

  if(WiFi.status() != WL_NO_SHIELD) //ATWINC1500 gefunden
  {
    features |= FEATURE_WINC1500;
  }

  //LPS22HB
  if(check_i2c(SERCOM2, ADDR_LPS22HB)) //LPS22HB gefunden
  {
    if(lps22.begin())
    {
      features |= FEATURE_LPS22HB;
    }
  }

  //BMP280
  if(check_i2c(SERCOM2, ADDR_BMP280)) //BMP280 gefunden
  {
    if(bmp280.begin(ADDR_BMP280))
    {
      features |= FEATURE_BMP280;
    }
  }
  else if(check_i2c(SERCOM2, ADDR_BMP280+1)) //BMP280 gefunden
  {
    if(bmp280.begin(ADDR_BMP280+1))
    {
      features |= FEATURE_BMP280;
    }
  }

  //SCD30+SCD4X
  if(check_i2c(SERCOM0, ADDR_SCD30)) //SCD30 gefunden
  {
    for(int t=5; t!=0; t--) //try 5 times
    {
      Wire.begin();
      scd30.begin(Wire, ADDR_SCD30);
      if(scd30.startPeriodicMeasurement(0) == 0) //0 = keine Druckkompensation
      {
        scd30.activateAutoCalibration(AUTO_KALIBRIERUNG);
        features |= FEATURE_SCD30;
        break;
      }
      status_led(1000); //Status-LED
    }
    scd30.setMeasurementInterval(INTERVALL); //setze Messintervall
    //scd30.startPeriodicMeasurement(1000); //0 oder 700-1400, Luftdruck in hPa
  }
  if(check_i2c(SERCOM0, ADDR_SCD4X)) //SCD4X gefunden
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
      status_led(1000); //Status-LED
    }
  }

  //Temperaturoffset
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

  //Einstellungen
  SETTINGS_LOAD load = settings_read(&settings); //Einstellungen lesen
  if(load != SETTINGS_LOADED)
  {
    settings_write(&settings); //Defaults bzw. migrierte Einstellungen speichern
  }
  if(load == SETTINGS_DEFAULTS)
  {
    //Standard Temperaturoffset (always Pro with WiFi and pressure sensor)
    temp_offset = TEMP_OFFSET;
    if(features & FEATURE_SCD30)
    {
      uint16_t offset = 0;
      scd30.getTemperatureOffset(offset); //degC*100
      if((offset == 0) || (offset > 1200))
      {
        scd30.setTemperatureOffset(temp_offset * 100); //Temperaturoffset
      }
    }
    else if(features & FEATURE_SCD4X)
    {
      float offset;
      scd4x.getTemperatureOffset(offset);
      if((offset == 0) || (offset > 12))
      {
        scd4x.setTemperatureOffset(temp_offset); //Temperaturoffset
      }
    }
  }
  ws2812.setBrightness(settings.brightness); //0...255

  //USB-Verbindung
  if(USBDevice.connected()) //(Serial) nutzt Flow-Control zur Erkennung
  {
    features |= FEATURE_USB;
    delay(1500); //1500ms warten
    Serial.println("\nCO2 Ampel Pro NG v" VERSION);
    Serial.print("Features:");
    if(features & FEATURE_SCD30)    { Serial.print(" SCD30"); }
    if(features & FEATURE_SCD4X)    { Serial.print(" SCD4X"); }
    if(features & FEATURE_LPS22HB)  { Serial.print(" LPS22HB"); }
    if(features & FEATURE_BMP280)   { Serial.print(" BMP280"); }
    if(features & FEATURE_WINC1500) { Serial.print(" WINC1500"); }
    Serial.println();
    Serial.print("Settings: ");
    Serial.println((load == SETTINGS_LOADED) ? "loaded" : (load == SETTINGS_MIGRATED) ? "migrated from old layout" : "defaults");
    Serial.println();
  }

  //Service-Menue
  if(run_menu)
  {
    menu(); //Menue aufrufen
  }

  //Plus-Version
  if(features & FEATURE_WINC1500)
  {
    if(WiFi.status() != WL_NO_SHIELD) //ATWINC1500 gefunden
    {
      if(wifi_start() != 0) //verbinde WiFi Netzwerk
      {
        if(wifi_start_ap() != 0) //starte AP
        {
          features &= ~FEATURE_WINC1500;
        }
      }
      delay(2000); //2s warten
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
      //MQTT verbinden
      if(settings.mqtt_enabled)
      {
        delay(1000); //1s warten
        mqtt_connect();
      }
    }
    else
    {
      features &= ~FEATURE_WINC1500;
      WiFi.end();
    }
  }

  //Messung starten
  co2_value = co2_average = STARTWERT;
  if(features & FEATURE_SCD30)
  {
    scd30.setMeasurementInterval(INTERVALL); //setze Messintervall
    delay(INTERVALL*1000UL); //Intervallsekunden warten
  }
  else if(features & FEATURE_SCD4X)
  {
    //Intervall 5s
  }
  else
  {
    if(features & FEATURE_USB)
    {
      Serial.println("Error: CO2 sensor not found");
    }
    leds(COLOR_RED);
    status_led(1000); //Status-LED
    leds(FARBE_AUS);
    co2_value = co2_average = settings.range[2]; // Set to red threshold
  }

  return;
}


void loop()
{
  static unsigned int dark=0, sw=0;
  static unsigned long t_switch=0, t_ampel=0, t_light=~((LICHT_INTERVALL*1000UL*60UL)-60000UL); //Lichtsensor nach 60s pruefen
  static unsigned long t_wifi=0;
  unsigned int overwrite=0;

  //serielle Befehle verarbeiten
  serial_service();

  //WiFi-Daten verarbeiten
  webserver_service();

  //MQTT-Daten verarbeiten
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

  //Taster pruefen
  if(digitalRead(PIN_SWITCH) == LOW) //Taster gedrueckt
  {
    if(sw == 0)
    {
      sw = 1;
      t_switch = millis(); //Zeit speichern
    }
  }
  else if(sw != 0) //Taster losgelassen
  {
    sw = 0;
    buzzer(0); //Buzzer aus
    buzzer_timer = BUZZER_DELAY; //Buzzer Startverzögerung
    if((millis()-t_switch) > 3000) //3s Tastendruck
    {
      if(features & FEATURE_WINC1500)
      {
        leds(FARBE_VIOLETT); //LEDs violett
        wifi_start_ap();
      }
    }
    else if((millis()-t_switch) > 100) //100ms Tastendruck
    {
      settings.brightness = settings.brightness/2; //Helligkeit halbieren
      if(settings.brightness < HELLIGKEIT_DUNKEL)
      {
        settings.brightness = HELLIGKEIT;
      }
      ws2812.setBrightness(settings.brightness);
      overwrite = 1;
    }
  }

  if((millis()-t_ampel) > 1000) //Ampelfunktion nur jede Sekunde ausfuehren
  {
    t_ampel = millis(); //Zeit speichern

    if(buzzer_timer > 0)
    {
      buzzer_timer--;
    }

    //USB-Verbindung
    if(USBDevice.connected()) //(Serial) nutzt Flow-Control zur Erkennung
    {
      features |= FEATURE_USB;
    }
    //else
    //{
    //  features &= ~FEATURE_USB;
    //}

    //Sensordaten auslesen
    if(check_sensors())
    {
      show_data();
      if(dark == 0)
      {
        status_led(2); //Status-LED
      }
    }

    co2_average = (co2_average + co2_sensor()) / 2; //Berechnung jede Sekunde
  }
  else if(overwrite == 0)
  {
    return;
  }

  //Ampel
  if(remote_on == 0)
  {
    #if AMPEL_DURCHSCHNITT > 0
      ampel(co2_average);
    #else
      ampel(co2_value);
    #endif
  }

  //Lichtsensor
  if(remote_on == 0)
  {
    if((millis()-t_light) > (LICHT_INTERVALL*1000UL*60UL))
    {
      t_light = millis(); //Zeit speichern

      light_value = light_sensor();
      if(light_value < LICHT_DUNKEL)
      {
        if(dark == 0)
        {
          dark = 1;
          if(settings.brightness > HELLIGKEIT_DUNKEL)
          {
            ws2812.setBrightness(HELLIGKEIT_DUNKEL); //dunkel
          }
        }
      }
      else
      {
        if(dark == 1)
        {
          dark = 0;
          ws2812.setBrightness(settings.brightness); //hell
        }
      }
    }
  }

  return;
}
