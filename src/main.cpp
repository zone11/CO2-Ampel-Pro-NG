/*
  CO2-Ampel Pro NG Firmware
    https://github.com/zone11/CO2-Ampel-Pro-NG
*/

#include <Arduino.h>

// Version will be overridden by platformio.ini if VERSION is defined there
#ifndef VERSION
#define VERSION "NG"
#endif

// Default CO2 thresholds (can be changed via serial commands and saved to flash)
#define DEFAULT_T1              600 //>= 600ppm
#define DEFAULT_T2              1000 //>=1000ppm
#define DEFAULT_T3              1200 //>=1200ppm
#define DEFAULT_T4              1400 //>=1400ppm
#define DEFAULT_T5              1600 //>=1600ppm

// Default LED colors (can be changed via serial commands and saved to flash)
#define DEFAULT_COLOR_T1         0x007CB0 //Himmelblau
#define DEFAULT_COLOR_T2        0x00FF00 //Gruen
#define DEFAULT_COLOR_T3         0xFF7F00 //Orange-Gelb
#define DEFAULT_COLOR_T4          0xFF0000 //Rot

//--- WiFi/WLAN ---
#define WIFI_SSID          "" //WiFi SSID
#define WIFI_CODE          "" //WiFi Passwort
#define WIFI_NM            255,255,255,  0 //Netzmaske
#define WIFI_IP              0,  0,  0,  0 //Lokale IP-Adresse, 0=DHCP
#define WIFI_GW            192,168,  1,100 //Gateway IP-Adresse
#define WIFI_DNS           192,168,  1,100 //DNS IP-Adresse

//--- MQTT ---
#define MQTT_ENABLED       0      //0 = MQTT deaktiviert, 1 = MQTT aktiviert
#define MQTT_BROKER        ""     //MQTT Broker Hostname oder IP
#define MQTT_PORT          1883   //MQTT Broker Port (Standard: 1883)
#define MQTT_USER          ""     //MQTT Benutzername (optional)
#define MQTT_PASS          ""     //MQTT Passwort (optional)
#define MQTT_CLIENT_ID     ""     //MQTT Client ID (leer = automatisch aus MAC)
#define MQTT_TOPIC_PREFIX  "co2ampel" //MQTT Topic Prefix
#define MQTT_INTERVAL      60     //MQTT Publish Intervall in Sekunden

//--- Ampelhelligkeit (LEDs) ---
#define HELLIGKEIT         180 //1-255 (255=100%, 179=70%)
#define HELLIGKEIT_DUNKEL  20  //1-255 (255=100%, 25=10%)
#define NUM_LEDS           4   //Anzahl der LEDs

//--- Lichtsensor ---
#define LICHT_DUNKEL       20   //<20 -> dunkel
#define LICHT_INTERVALL    60 //10-120min (Sensorpruefung)

//--- Allgemein ---
#define INTERVALL          2 //2-1800s Messintervall (nur SCD30, SCD4X immer 5s)
#define AMPEL_DURCHSCHNITT 1 //1 = CO2 Durchschnitt fuer Ampel verwenden
#define AUTO_KALIBRIERUNG  0 //1 = automatische Kalibrierung (ASC) an (erfordert 7 Tage Dauerbetrieb mit 1h Frischluft pro Tag)
#define BUZZER             1 //Buzzer aktivieren
#define BUZZER_DELAY     300 //300s, Buzzer Startverzögerung
#define TEMP_OFFSET        6 //Pro WiFi, Temperaturoffset in °C (0-20)
#define DRUCK_DIFF         5 //Druckunterschied in hPa (5-20)
#define BAUDRATE           9600 //9600 Baud
#define STARTWERT          500 //500ppm, CO2-Startwert

//--- Fixed Colors (not configurable) ---
#define FARBE_VIOLETT      0xFF00FF //0xFF00FF (used for menu UI)
#define FARBE_WEISS        0xFFFFFF //0xFFFFFF (used for menu UI)
#define FARBE_AUS          0x000000 //0x000000 (LEDs off)

// LED Colors
#define COLOR_SETTINGS     0x007CB0 //Lightblue
#define COLOR_MENU         0xFF00FF //Violett
#define COLOR_WHITE        0xFFFFFF
#define COLOR_BLUE         0x0000FF
#define COLOR_GREEN        0x00FF00
#define COLOR_YELLOW       0xFFFF00
#define COLOR_RED          0xFF0000
#define COLOR_OFF          0x000000

//--- I2C/Wire ---
#define ADDR_SCD30         0x61 //0x61, Wire=SERCOM0
#define ADDR_SCD4X         0x62 //0x62, Wire=SERCOM0
#define ADDR_LPS22HB       0x5C //0x5C, Wire1=SERCOM2
#define ADDR_BMP280        0x76 //0x76 or 0x77, Wire1=SERCOM2
#define ADDR_ATECC608      0x60 //0x60, Wire1=SERCOM2 (optional)

//--- Features ---
enum Features
{
  FEATURE_USB      = (1<<0),
  FEATURE_SCD30    = (1<<1),
  FEATURE_SCD4X    = (1<<2),
  FEATURE_LPS22HB  = (1<<3),
  FEATURE_BMP280   = (1<<4),
  FEATURE_WINC1500 = (1<<5),
};


#include <Wire.h>
#include <SPI.h>
//#include <FlashStorage.h> // Not used anymore - using fixed flash address
#include <SparkFun_SCD30_Arduino_Library.h>
#include <SensirionI2CScd4x.h>
#include <Adafruit_BMP280.h>
#include <Arduino_LPS22HB.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi101.h>
#include <MQTT.h>


extern USBDeviceClass USBDevice; //USBCore.cpp

// PlatformIO: Wire1 is automatically defined by the Arduino SAMD framework
// via the definitions in variant.h (PIN_WIRE1_SDA, PIN_WIRE1_SCL, PERIPH_WIRE1, WIRE1_IT_HANDLER)

// PlatformIO: Forward declarations for WiFi functions
unsigned int wifi_start_ap(void);
unsigned int wifi_start(void);

// PlatformIO: Forward declarations for helper functions
void get_chip_id(char *buffer, size_t buffer_size);
void get_device_id(char *buffer, size_t buffer_size);

typedef struct
{
  boolean valid;
  unsigned int brightness;
  unsigned int range[5];        // CO2 thresholds: [0]=green, [1]=yellow, [2]=red, [3]=red_blink, [4]=buzzer
  unsigned int buzzer;
  char wifi_ssid[64+1];
  char wifi_code[64+1];
  IPAddress netmask;
  IPAddress ip_local;
  IPAddress ip_gw;
  IPAddress ip_dns;
  // MQTT Configuration
  boolean mqtt_enabled;
  char mqtt_broker[64+1];
  unsigned int mqtt_port;
  char mqtt_user[32+1];
  char mqtt_pass[32+1];
  char mqtt_client_id[32+1];
  char mqtt_topic_prefix[32+1];
  unsigned int mqtt_interval;
  // LED Colors (configurable via serial)
  uint32_t color_t1;          // Color for CO2 < range[0] (very fresh air)
  uint32_t color_t2;         // Color for range[0] <= CO2 < range[1] (good)
  uint32_t color_t3;        // Color for range[1] <= CO2 < range[2] (warning)
  uint32_t color_t4;           // Color for CO2 >= range[2] (alert)
} SETTINGS;

// PlatformIO: Forward declarations for settings and MQTT functions
void settings_read(SETTINGS *data);
void settings_write(const SETTINGS *data);
void mqtt_connect(void);
void mqtt_reconnect(void);
void mqtt_service(void);
void mqtt_publish_sensors(void);

SETTINGS settings;
// Settings stored at high address in flash
// WARNING: Settings will be lost on firmware upload (bootloader erases entire app area)
// Recommended: Export settings via serial before updating firmware
// Place at 0x3F800 (last 2KB of flash) - may survive small firmware updates
#define SETTINGS_FLASH_ADDR ((const volatile SETTINGS*)0x0003F800)
SCD30 scd30;
SensirionI2CScd4x scd4x;
Adafruit_BMP280 bmp280(&Wire1);
LPS22HBClass lps22(Wire1);
Adafruit_NeoPixel ws2812 = Adafruit_NeoPixel(NUM_LEDS, PIN_WS2812, NEO_GRB + NEO_KHZ800);
WiFiServer server(80); //Webserver Port 80
WiFiClient mqttWifiClient;
MQTTClient mqttClient(256); //256 Byte Buffer

unsigned int features=0, remote_on=0, buzzer_timer=BUZZER_DELAY;
unsigned int co2_value=STARTWERT, co2_average=STARTWERT, light_value=1024;
float temp_value=20, temp_offset=TEMP_OFFSET, humi_value=50, pres_value=1013, pres_last=1013, temp2_value=20;

static void print_ip_address_line(const IPAddress &ip)
{
  Serial.print(ip[0]);
  Serial.print(".");
  Serial.print(ip[1]);
  Serial.print(".");
  Serial.print(ip[2]);
  Serial.print(".");
  Serial.println(ip[3]);
}


void leds(uint32_t color)
{
  ws2812.fill(color, 0, NUM_LEDS);
  ws2812.show();
}


void status_led(unsigned int on)
{
  if(on == 0)
  {
    digitalWrite(PIN_LED, LOW); //Status-LED aus
  }
  else if(on == 1)
  {
    digitalWrite(PIN_LED, HIGH); //Status-LED an
  }
  else if(on < 2000)
  {
    on = on/2;
    digitalWrite(PIN_LED, HIGH); //Status-LED an
    delay(on); //ms warten
    digitalWrite(PIN_LED, LOW); //Status-LED aus
    delay(on); //ms warten
  }

  return;
}


void buzzer(unsigned int on)
{
  if(on == 0)
  {
    analogWrite(PIN_BUZZER, 0); //Buzzer aus
  }
  else if(on == 1)
  {
    analogWrite(PIN_BUZZER, 255/2); //Buzzer an
  }
  else if(on < 2000)
  {
    analogWrite(PIN_BUZZER, 255/2); //Buzzer an
    delay(on); //ms warten
    analogWrite(PIN_BUZZER, 0); //Buzzer aus
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


void show_data(void) //Daten anzeigen
{
  if(features & FEATURE_USB)
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

  return;
}


void serial_service(void)
{
  static int calibration_done=0;
  int i, cmd, val;
  char tmp[32];

  if((features & FEATURE_USB) == 0)
  {
    return;
  }

  if(Serial.available() == 0)
  {
    return;
  }

  cmd = Serial.read(); //Befehl
  val = Serial.read(); //schreiben/lesen

  //Remote-Kontrolle pruefen (nur fuer Schreib-Befehle)
  if((cmd != 'R') && (val == '=') && (remote_on == 0))
  {
    Serial.println("ERROR: Remote control not enabled. Send R=1 first.");
    return;
  }

  //Spezialbehandlung fuer MQTT Befehle (MB=, MP=, MU=, etc.)
  if(toupper(cmd) == 'M' && val != '=' && val != '?')
  {
    char subcmd = toupper(val);
    char op = Serial.read(); //sollte '=' sein

    if(op == '=')
    {
      //Remote-Kontrolle pruefen
      if(remote_on == 0)
      {
        Serial.println("ERROR: Remote control not enabled. Send R=1 first.");
        return;
      }

      char tmp[128];
      int i;

      if(subcmd == 'B') //MB=broker (MQTT Broker)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          strncpy(settings.mqtt_broker, tmp, sizeof(settings.mqtt_broker)-1);
          settings.mqtt_broker[sizeof(settings.mqtt_broker)-1] = '\0';
          Serial.println("OK");
        }
      }
      else if(subcmd == 'P') //MP=port (MQTT Port)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          int port;
          sscanf(tmp, "%d", &port);
          if((port > 0) && (port <= 65535))
          {
            settings.mqtt_port = port;
            Serial.println("OK");
          }
        }
      }
      else if(subcmd == 'U') //MU=user (MQTT User)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          strncpy(settings.mqtt_user, tmp, sizeof(settings.mqtt_user)-1);
          settings.mqtt_user[sizeof(settings.mqtt_user)-1] = '\0';
          Serial.println("OK");
        }
      }
      else if(subcmd == 'K') //MK=password (MQTT Key/Password)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          strncpy(settings.mqtt_pass, tmp, sizeof(settings.mqtt_pass)-1);
          settings.mqtt_pass[sizeof(settings.mqtt_pass)-1] = '\0';
          Serial.println("OK");
        }
      }
      else if(subcmd == 'C') //MC=client_id (MQTT Client ID)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          strncpy(settings.mqtt_client_id, tmp, sizeof(settings.mqtt_client_id)-1);
          settings.mqtt_client_id[sizeof(settings.mqtt_client_id)-1] = '\0';
          Serial.println("OK");
        }
      }
      else if(subcmd == 'T') //MT=topic_prefix (MQTT Topic Prefix)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          strncpy(settings.mqtt_topic_prefix, tmp, sizeof(settings.mqtt_topic_prefix)-1);
          settings.mqtt_topic_prefix[sizeof(settings.mqtt_topic_prefix)-1] = '\0';
          Serial.println("OK");
        }
      }
      else if(subcmd == 'I') //MI=interval (MQTT Publish Interval)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          int interval;
          sscanf(tmp, "%d", &interval);
          if((interval >= 10) && (interval <= 3600))
          {
            settings.mqtt_interval = interval;
            Serial.println("OK");
          }
        }
      }
    }
    return;
  }

  //Spezialbehandlung fuer WiFi Befehle (WS=, WP=)
  if(toupper(cmd) == 'W' && val != '=' && val != '?')
  {
    char subcmd = toupper(val);
    char op = Serial.read(); //sollte '=' sein

    if(op == '=')
    {
      //Remote-Kontrolle pruefen
      if(remote_on == 0)
      {
        Serial.println("ERROR: Remote control not enabled. Send R=1 first.");
        return;
      }

      char tmp[128];
      int i;

      if(subcmd == 'S') //WS=ssid (WiFi SSID)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          strncpy(settings.wifi_ssid, tmp, sizeof(settings.wifi_ssid)-1);
          settings.wifi_ssid[sizeof(settings.wifi_ssid)-1] = '\0';
          Serial.println("OK");
        }
      }
      else if(subcmd == 'P') //WP=password (WiFi Password)
      {
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
        if(i > 0)
        {
          tmp[i] = 0;
          strncpy(settings.wifi_code, tmp, sizeof(settings.wifi_code)-1);
          settings.wifi_code[sizeof(settings.wifi_code)-1] = '\0';
          Serial.println("OK");
        }
      }
    }
    return;
  }

  //Spezialbehandlung fuer Color Befehle (CB=, CG=, CY=, CR=)
  if(toupper(cmd) == 'C' && val != '=' && val != '?')
  {
    char subcmd = toupper(val);
    char op = Serial.read(); //sollte '=' sein

    if(op == '=')
    {
      //Remote-Kontrolle pruefen
      if(remote_on == 0)
      {
        Serial.println("ERROR: Remote control not enabled. Send R=1 first.");
        return;
      }

      char tmp[128];
      int i;
      uint32_t color_val;

      i = Serial.readBytesUntil('\n', tmp, sizeof(tmp)-1);
      if(i > 0)
      {
        tmp[i] = 0;
        if(sscanf(tmp, "%X", &color_val) == 1)
        {
          if(subcmd == 'B') //CB=RRGGBB - Blue color (CO2 < green threshold)
          {
            settings.color_t1 = color_val;
            Serial.println("OK");
          }
          else if(subcmd == 'G') //CG=RRGGBB - Green color (green-yellow range)
          {
            settings.color_t2 = color_val;
            Serial.println("OK");
          }
          else if(subcmd == 'Y') //CY=RRGGBB - Yellow color (yellow-red range)
          {
            settings.color_t3 = color_val;
            Serial.println("OK");
          }
          else if(subcmd == 'R') //CR=RRGGBB - Red color (>= red threshold)
          {
            settings.color_t4 = color_val;
            Serial.println("OK");
          }
        }
      }
    }
    return;
  }

  if(val == '=') //=
  {
    switch(toupper(cmd))
    {
      case 'S': //Save/Speichern
        cmd = Serial.read();
        if(cmd == '1')
        {
          settings.valid = true;
          settings_write(&settings); //Einstellungen speichern
          Serial.println("OK");
        }
        break;

      case 'H': //LED Helligkeit
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp));
        if(i >= 0)
        {
          tmp[i] = 0;
          sscanf(tmp, "%X", &val);
          if(val < 0)
          {
            val = 0;
          }
          else if(val > 255)
          {
            val = 255;
          }
          settings.brightness = val;
          ws2812.setBrightness(val);
          ws2812.show();
          Serial.println("OK");
        }
        break;

      case 'L': //LED Farbe
        i = Serial.readBytesUntil('\n', tmp, sizeof(tmp));
        if(i > 0)
        {
          tmp[i] = 0;
          sscanf(tmp, "%X", &val);
          leds(val);
          Serial.println("OK");
        }
        break;

      case 'B': //Buzzer
        cmd = Serial.read();
        if(cmd == '1')
        {
          buzzer(500); //500ms Buzzer an
          settings.buzzer = 1;
          Serial.println("OK");
        }
        else if(cmd == '0')
        {
          settings.buzzer = 0;
          Serial.println("OK");
        }
        break;

      case 'T': //Thresholds (T1-T5) and Temperature Offset (TO)
        {
          int subcmd = Serial.read();

          if(subcmd == '1' || subcmd == '2' || subcmd == '3' || subcmd == '4' || subcmd == '5')
          {
            // T1-T5: Set CO2 thresholds
            i = Serial.readBytesUntil('\n', tmp, sizeof(tmp));
            if(i > 0)
            {
              tmp[i] = 0;
              sscanf(tmp, "%d", &val);
              if((val >= 400) && (val <= 10000))
              {
                settings.range[subcmd-'1'] = val;
                Serial.println("OK");
              }
            }
          }
          else if(subcmd == '?')
          {
            // T?: Query all CO2 thresholds
            Serial.println("CO2 Thresholds (ppm):");
            Serial.print("T1 (Green):      ");
            Serial.println(settings.range[0]);
            Serial.print("T2 (Yellow):     ");
            Serial.println(settings.range[1]);
            Serial.print("T3 (Red):        ");
            Serial.println(settings.range[2]);
            Serial.print("T4 (Red Blink):  ");
            Serial.println(settings.range[3]);
            Serial.print("T5 (Buzzer):     ");
            Serial.println(settings.range[4]);
          }
          else if(subcmd == 'O')
          {
            // TO=X: Set temperature offset
            i = Serial.readBytesUntil('\n', tmp, sizeof(tmp));
            if(i > 0)
            {
              tmp[i] = 0;
              sscanf(tmp, "%d", &val);
              if((val >= 0) && (val <= 20))
              {
                temp_offset = val;
                if(features & FEATURE_SCD30)
                {
                  scd30.setTemperatureOffset(val);
                  Serial.println("OK");
                }
                else if(features & FEATURE_SCD4X)
                {
                  scd4x.stopPeriodicMeasurement();
                  delay(1000);
                  if(scd4x.setTemperatureOffset(val) == 0)
                  {
                    Serial.println("OK");
                  }
                  else
                  {
                    Serial.println("ERROR");
                  }
                  delay(500);
                  scd4x.startPeriodicMeasurement();
                }
              }
            }
          }
        }
        break;

      case 'X': //Extended commands: XC=calibration, XA=altitude
        {
          if(val == 'C') //XC=1 - Calibration
          {
            char op = Serial.read(); // Should be '='
            if(op == '=')
            {
              i = Serial.readBytesUntil('\n', tmp, sizeof(tmp));
              if((i > 0) && (calibration_done == 0))
              {
                tmp[i] = 0;
                sscanf(tmp, "%d", &val);
                if((val > 0) && (val < 400))
                {
                  val = 400;
                }
                if((val >= 400) || (val <= 2000))
                {
                  calibration_done = 1;
                  if(features & FEATURE_SCD30)
                  {
                    scd30.setForcedRecalibrationFactor(val);
                    delay(500);
                  }
                  else if(features & FEATURE_SCD4X)
                  {
                    uint16_t corr;
                    scd4x.stopPeriodicMeasurement();
                    delay(1000);
                    scd4x.performForcedRecalibration(val, corr);
                    delay(1000);
                    scd4x.startPeriodicMeasurement();
                  }
                  Serial.println("OK");
                }
              }
            }
          }
          else if(val == 'A') //XA=X - Altitude compensation
          {
            char op = Serial.read(); // Should be '=' or '?'
            if(op == '=')
            {
              i = Serial.readBytesUntil('\n', tmp, sizeof(tmp));
              if(i > 0)
              {
                tmp[i] = 0;
                sscanf(tmp, "%d", &val);
                if((val >= 0) && (val <= 3000))
                {
                  if(features & FEATURE_SCD30)
                  {
                    scd30.setAltitudeCompensation(val); //Meter ueber dem Meeresspiegel
                    Serial.println("OK");
                  }
                  else if(features & FEATURE_SCD4X)
                  {
                    scd4x.stopPeriodicMeasurement();
                    delay(1000);
                    if(scd4x.setSensorAltitude(val) == 0) //Meter ueber dem Meeresspiegel
                    {
                      Serial.println("OK");
                    }
                    else
                    {
                      Serial.println("ERROR");
                    }
                    delay(500);
                    scd4x.startPeriodicMeasurement();
                  }
                }
              }
            }
            else if(op == '?') //XA? - Query altitude
            {
              if(features & FEATURE_SCD30)
              {
                val = scd30.getAltitudeCompensation();
              }
              else if(features & FEATURE_SCD4X)
              {
                uint16_t alt;
                scd4x.stopPeriodicMeasurement();
                delay(500);
                scd4x.getSensorAltitude(alt);
                delay(500);
                scd4x.startPeriodicMeasurement();
                val = alt;
              }
              Serial.print("Altitude: ");
              Serial.print(val, DEC);
              Serial.println(" m");
            }
          }
        }
        break;

      case 'R': //Remote control and Reset (R=1/0 for remote, R=R for reset)
        {
          if(val == '=')
          {
            // R=1, R=0, R=R (remote control and reset commands)
            i = Serial.readBytesUntil('\n', tmp, sizeof(tmp));
            if(i > 0)
            {
              tmp[i] = 0;
              if(tmp[0] == '1')
              {
                remote_on = 1;
                buzzer(0); //Buzzer aus
                ws2812.setBrightness(30); //0...255
                leds(COLOR_MENU);
                Serial.println("OK");
              }
              else if(tmp[0] == '0')
              {
                remote_on = 0;
                calibration_done = 0;
                ws2812.setBrightness(settings.brightness);
                Serial.println("OK");
              }
              else if((tmp[0] == 'r') || (tmp[0] == 'R'))
              {
                if(remote_on)
                {
                  Serial.println("OK");
                  leds(0); //LEDs aus
                  Serial.flush();
                  Serial.end();
                  delay(20); //20ms warten
                }
                NVIC_SystemReset(); //Reset
                while(1);
              }
            }
          }
        }
        break;

      case 'M': //MQTT Enable/Disable (M=0 or M=1)
        cmd = Serial.read();
        if(cmd == '0')
        {
          settings.mqtt_enabled = false;
          Serial.println("OK");
        }
        else if(cmd == '1')
        {
          settings.mqtt_enabled = true;
          Serial.println("OK");
        }
        break;
    }
  }
  else if(val == '?') //?
  {
    switch(toupper(cmd))
    {
      case 'V': //Version
        Serial.println(VERSION);
        break;
      case 'H': //LED Helligkeit
        Serial.println(settings.brightness, HEX);
        break;
      case 'B': //Buzzer
        Serial.println(settings.buzzer, DEC);
        break;
      case 'T': //T? for thresholds, TO? for temperature offset
        {
          int subcmd = Serial.read();
          if(subcmd == 'O')
          {
            // TO?: Query temperature offset
            if(features & FEATURE_SCD30)
            {
              val = scd30.getTemperatureOffset();
            }
            else if(features & FEATURE_SCD4X)
            {
              float offset;
              scd4x.stopPeriodicMeasurement();
              delay(500);
              scd4x.getTemperatureOffset(offset);
              delay(500);
              scd4x.startPeriodicMeasurement();
              val = offset;
            }
            Serial.print("Temperature Offset: ");
            Serial.print(val, DEC);
            Serial.println(" °C");
          }
          // Note: T? is handled in the T command case in the write section above
        }
        break;
      case 'W': //WiFi Status
        {
          if(features & FEATURE_WINC1500)
          {
            Serial.print("WiFi SSID: ");
            Serial.println(strlen(settings.wifi_ssid) > 0 ? settings.wifi_ssid : "(not configured)");
            Serial.print("WiFi Password: ");
            Serial.println(strlen(settings.wifi_code) > 0 ? "***" : "(not configured)");
            Serial.print("WiFi Status: ");

            int status = WiFi.status();
            switch(status)
            {
              case WL_CONNECTED:
                Serial.print("Connected to ");
                Serial.println(WiFi.SSID());
                Serial.print("IP Address: ");
                print_ip_address_line(WiFi.localIP());
                Serial.print("Signal Strength: ");
                Serial.print(WiFi.RSSI());
                Serial.println(" dBm");
                break;
              case WL_NO_SHIELD:
                Serial.println("No WiFi hardware");
                break;
              case WL_IDLE_STATUS:
                Serial.println("Idle");
                break;
              case WL_NO_SSID_AVAIL:
                Serial.println("SSID not available");
                break;
              case WL_SCAN_COMPLETED:
                Serial.println("Scan completed");
                break;
              case WL_CONNECT_FAILED:
                Serial.println("Connection failed");
                break;
              case WL_CONNECTION_LOST:
                Serial.println("Connection lost");
                break;
              case WL_DISCONNECTED:
                Serial.println("Disconnected");
                break;
              case WL_AP_LISTENING:
                Serial.print("Access Point Mode - SSID: ");
                Serial.println(WiFi.SSID());
                Serial.print("AP IP Address: ");
                print_ip_address_line(WiFi.localIP());
                break;
              default:
                Serial.print("Unknown (");
                Serial.print(status);
                Serial.println(")");
                break;
            }
          }
          else
          {
            Serial.println("WiFi hardware not available");
          }
        }
        break;
      case 'M': //MQTT Status
        {
          Serial.print("MQTT Enabled: ");
          Serial.println(settings.mqtt_enabled ? "Yes" : "No");
          Serial.print("Broker: ");
          Serial.print(settings.mqtt_broker);
          Serial.print(":");
          Serial.println(settings.mqtt_port);
          Serial.print("Username: ");
          Serial.println(strlen(settings.mqtt_user) > 0 ? settings.mqtt_user : "(none)");
          //Serial.print("Password: ");
          //Serial.println(strlen(settings.mqtt_pass) > 0 ? settings.mqtt_pass : "(none)");
          Serial.print("Client ID: ");
          if(strlen(settings.mqtt_client_id) > 0)
          {
            Serial.println(settings.mqtt_client_id);
          }
          else
          {
            //Auto-generierte Client ID anzeigen
            char device_id[32];
            get_device_id(device_id, sizeof(device_id));
            Serial.print(device_id);
            Serial.println(" (auto)");
          }
          Serial.print("Topic Prefix: ");
          Serial.println(settings.mqtt_topic_prefix);

          //Chip ID anzeigen
          char chip_id[40];
          get_chip_id(chip_id, sizeof(chip_id));
          Serial.print("Chip ID: ");
          Serial.println(chip_id);

          Serial.print("Interval: ");
          Serial.print(settings.mqtt_interval);
          Serial.println("s");
          if(settings.mqtt_enabled && (features & FEATURE_WINC1500))
          {
            Serial.print("WiFi: ");
            Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
            Serial.print("Status: ");
            Serial.println(mqttClient.connected() ? "Connected" : "Disconnected");
          }
        }
        break;

      case 'C': //Color query (C?)
        {
          Serial.print("Color T1 (CO2 < ");
          Serial.print(settings.range[0]);
          Serial.print(" ppm): 0x");
          Serial.println(settings.color_t1, HEX);
          Serial.print("Color T2 (");
          Serial.print(settings.range[0]);
          Serial.print("-");
          Serial.print(settings.range[1]-1);
          Serial.print(" ppm): 0x");
          Serial.println(settings.color_t2, HEX);
          Serial.print("Color T3 (");
          Serial.print(settings.range[1]);
          Serial.print("-");
          Serial.print(settings.range[2]-1);
          Serial.print(" ppm): 0x");
          Serial.println(settings.color_t3, HEX);
          Serial.print("Color T4 (>= ");
          Serial.print(settings.range[2]);
          Serial.print(" ppm): 0x");
          Serial.println(settings.color_t4, HEX);
        }
        break;

      case 'D': //Dump all settings as serial commands
        Serial.println("# Settings Dump - Copy and paste to restore");
        Serial.println("# Enable remote control first");
        Serial.println("R=1");
        Serial.println();

        Serial.println("# LED Brightness (hex)");
        Serial.print("H=");
        Serial.println(settings.brightness, HEX);
        Serial.println();

        Serial.println("# Buzzer (0=off, 1=on)");
        Serial.print("B=");
        Serial.println(settings.buzzer);
        Serial.println();

        Serial.println("# CO2 Thresholds (ppm)");
        Serial.print("T1=");
        Serial.println(settings.range[0]);
        Serial.print("T2=");
        Serial.println(settings.range[1]);
        Serial.print("T3=");
        Serial.println(settings.range[2]);
        Serial.print("T4=");
        Serial.println(settings.range[3]);
        Serial.print("T5=");
        Serial.println(settings.range[4]);
        Serial.println();

        Serial.println("# LED Colors (hex RRGGBB)");
        Serial.print("CB=");
        Serial.println(settings.color_t1, HEX);
        Serial.print("CG=");
        Serial.println(settings.color_t2, HEX);
        Serial.print("CY=");
        Serial.println(settings.color_t3, HEX);
        Serial.print("CR=");
        Serial.println(settings.color_t3, HEX);
        Serial.println();

        Serial.println("# Temperature Offset");
        if(features & FEATURE_SCD30)
        {
          Serial.print("TO=");
          Serial.println(scd30.getTemperatureOffset());
        }
        else if(features & FEATURE_SCD4X)
        {
          float offset;
          scd4x.stopPeriodicMeasurement();
          delay(500);
          scd4x.getTemperatureOffset(offset);
          delay(500);
          scd4x.startPeriodicMeasurement();
          Serial.print("TO=");
          Serial.println((int)offset);
        }
        Serial.println();

        Serial.println("# Altitude (meters)");
        if(features & FEATURE_SCD30)
        {
          Serial.print("XA=");
          Serial.println(scd30.getAltitudeCompensation());
        }
        else if(features & FEATURE_SCD4X)
        {
          uint16_t alt;
          scd4x.stopPeriodicMeasurement();
          delay(500);
          scd4x.getSensorAltitude(alt);
          delay(500);
          scd4x.startPeriodicMeasurement();
          Serial.print("XA=");
          Serial.println(alt);
        }
        Serial.println();

        if(features & FEATURE_WINC1500)
        {
          Serial.println("# WiFi Settings");
          if(strlen(settings.wifi_ssid) > 0)
          {
            Serial.print("WS=");
            Serial.println(settings.wifi_ssid);
          }
          if(strlen(settings.wifi_code) > 0)
          {
            Serial.print("WP=");
            Serial.println(settings.wifi_code);
          }
          Serial.println();

          Serial.println("# MQTT Settings");
          if(strlen(settings.mqtt_broker) > 0)
          {
            Serial.print("MB=");
            Serial.println(settings.mqtt_broker);
          }
          Serial.print("MP=");
          Serial.println(settings.mqtt_port);
          if(strlen(settings.mqtt_user) > 0)
          {
            Serial.print("MU=");
            Serial.println(settings.mqtt_user);
          }
          if(strlen(settings.mqtt_pass) > 0)
          {
            Serial.print("MK=");
            Serial.println(settings.mqtt_pass);
          }
          if(strlen(settings.mqtt_client_id) > 0)
          {
            Serial.print("MC=");
            Serial.println(settings.mqtt_client_id);
          }
          if(strlen(settings.mqtt_topic_prefix) > 0)
          {
            Serial.print("MT=");
            Serial.println(settings.mqtt_topic_prefix);
          }
          Serial.print("MI=");
          Serial.println(settings.mqtt_interval);
          Serial.print("M=");
          Serial.println(settings.mqtt_enabled ? "1" : "0");
          Serial.println();
        }

        Serial.println("# Save settings to flash");
        Serial.println("S=1");
        Serial.println();
        Serial.println("# Settings dump complete");
        break;
    }
  }

  return;
}


void urldecode(char *src) //URL Parameter dekodieren
{
  char a, b, *dst = src;

  while(*src) 
  {
    if((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((uint8_t)a) && isxdigit((uint8_t)b))) 
    {
      if (a >= 'a')
        a -= 'a' - 'A';

      if (a >= 'A')
        a -= 'A' - 10;
      else
        a -= '0';

      if (b >= 'a')
        b -= 'a' - 'A';

      if (b >= 'A')
        b -= 'A' - 10;
      else
        b -= '0';

      *dst++ = 16 * a + b;
      src += 3;
    }
    else if (*src == '+')
    {
      *dst++ = ' ';
      src++;
    }
    else
    {
      *dst++ = *src++;
    }
  }
  *dst++ = '\0';

  return;
}


void webserver_service(void)
{
  static unsigned long t_check=0;
  unsigned int status;

  if((features & FEATURE_WINC1500) == 0)
  {
    return;
  }

  status = WiFi.status();

  if(status == WL_IDLE_STATUS) //Verbindungsaufbau
  {
    return;
  }
  else if((status == WL_CONNECT_FAILED) ||
          (status == WL_CONNECTION_LOST) || 
          (status == WL_DISCONNECTED)) //Verbindungsabbruch
  {
    if((millis()-t_check) > (1*60000UL)) //1min
    {
      t_check = millis();
      wifi_start();
    }
    return;
  }

  t_check = millis(); //Zeit speichern fuer Neuverbindung nach 1min

  WiFiClient client = server.available();
  if(!client) //Client nicht verbunden
  {
    return;
  }
  //if(features & FEATURE_USB)
  //{
  //  Serial.println("WiFi client connected");
  //}
  boolean currentLineIsBlank=true;
  unsigned int pos=0;
  char buf[1024];
  char req[2][64+1]; //HTTP request
  req[0][0] = 0;
  while(client.connected())
  {
    if((millis()-t_check) > (5*1000UL)) //Stop nach 5s
    {
      break;
    }
    if(client.available())
    {
      char c = client.read();
      if(c == '\n' && currentLineIsBlank) //Header zu Ende
      {
        if(strncmp(req[0], "GET ", 4) && strncmp(req[0], "POST ", 5)) //kein GET oder POST
        {
          sprintf(buf, 
              "HTTP/1.1 400 Bad Request\r\n" \
              "Content-Type: text/plain\r\n" \
              "Connection: close\r\n" \
              "\r\n" \
              "400 Bad Request\r\n"
          );
          client.print(buf);
        }
        else if(strncmp(req[0], "GET /json", 9) == 0) //JSON
        {
          if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: application/json\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                "{\r\n" \
                " \"c\": %i,\r\n" \
                " \"t\": %.1f,\r\n" \
                " \"h\": %.1f,\r\n" \
                " \"p\": %.1f,\r\n" \
                " \"u\": %.1f,\r\n" \
                " \"l\": %i\r\n" \
                "}\r\n",
                co2_value, temp_value, humi_value, pres_value, temp2_value, light_value
            );
          }
          else
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: application/json\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                "{\r\n" \
                " \"c\": %i,\r\n" \
                " \"t\": %.1f,\r\n" \
                " \"h\": %.1f,\r\n" \
                " \"l\": %i\r\n" \
                "}\r\n",
                co2_value, temp_value, humi_value, light_value
            );
          }
          client.print(buf);
        }
        else if(strncmp(req[0], "GET /cmk-agent", 14) == 0) //Checkmk Agent
        {
          //CO2-Ampeln koennen so direkt ins Monitoring von checkmk.com 
          //aufgenommen werden. Plugins sind nicht zwingend erforderlich.
          //Da HTTP als Uebertragungsweg genutzt wird, "Data Source" 
          //verwenden: wget -O - http://ip_address/cmk-agent
          //Siehe: https://docs.checkmk.com/latest/de/datasource_programs.html
          if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: text/plain\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                //Plaintext im von Checkmk erwarteten Format
                //Siehe: https://docs.checkmk.com/latest/en/devel_check_plugins.html
                "<<<check_mk>>>\r\n" \
                "AgentOS: arduino\r\n" \
                //Check-Plugin fuer den Server erforderlich, um die Metriken auszuwerten 
                "<<<watterott_co2ampel_plugin>>>\r\n" \
                "co2 %i\r\n" \
                "temp %.1f\r\n" \
                "humidity %.1f\r\n" \
                "lighting %i\r\n" \
                "pressure %.1f\r\n" \
                "temp2 %.1f\r\n" \
                //Ad-hoc Check, der kein Server-Plugin benoetigt, nutzt Schwellwerte der Ampel.
                //Achtung: Nur eine Zeile - der Checkmk-Server nimmt die Bewertung selbst an
                //Hand der uebergebenen Schwellwerte vor. Die lesen wir hier aus der Ampel aus:
                "<<<local:sep(0)>>>\r\n" \
                "P \"CO2 level (ppm)\" co2ppm=%i;%i;%i CO2/ventilation control with Watterott CO2-Ampel, thresholds taken from sensor board.\r\n",
                co2_value, temp_value, humi_value, light_value, pres_value, temp2_value,
                co2_value, settings.range[1], settings.range[2]
            );
          }
          else
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: text/plain\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                //Plaintext im von Checkmk erwarteten Format
                //Siehe: https://docs.checkmk.com/latest/en/devel_check_plugins.html
                "<<<check_mk>>>\r\n" \
                "AgentOS: arduino\r\n" \
                //Check-Plugin fuer den Server erforderlich, um die Metriken auszuwerten 
                "<<<watterott_co2ampel_plugin>>>\r\n" \
                "co2 %i\r\n" \
                "temp %.1f\r\n" \
                "humidity %.1f\r\n" \
                "lighting %i\r\n" \
                //Ad-hoc Check, der kein Server-Plugin benoetigt, nutzt Schwellwerte der Ampel.
                //Achtung: Nur eine Zeile - der Checkmk-Server nimmt die Bewertung selbst an
                //Hand der uebergebenen Schwellwerte vor. Die lesen wir hier aus der Ampel aus:
                "<<<local:sep(0)>>>\r\n" \
                "P \"CO2 level (ppm)\" co2ppm=%i;%i;%i CO2/ventilation control with Watterott CO2-Ampel, thresholds taken from sensor board.\r\n",
                co2_value, temp_value, humi_value, light_value,
                co2_value, settings.range[1], settings.range[2]
            );
          }
          client.print(buf);
        }
        else if(strncmp(req[0], "GET /favicon", 12) == 0) //Favicon 
        {
          sprintf(buf, 
              "HTTP/1.1 404 Not Found\r\n" \
              "Content-Type: text/plain\r\n" \
              "Connection: close\r\n" \
              "\r\n" \
              "404 Not Found\r\n"
          );
          client.print(buf);
        }
        else
        {
          //HTTP Post Daten verarbeiten
          if((strncmp(req[0], "POST ", 5) == 0) && client.available())
          {
            req[0][0] = 0; //SSID
            req[1][0] = 0; //Code
            for(unsigned int r=0, i=0, last_c=0; client.available();)
            {
              c = client.read();
              if(c == '&') //Aufbau: 1=xxx&2=yyy
              {
                r = 0;
              }
              else if((c == '=') && isdigit(last_c)) //1=xxx
              {
                r = last_c-'0';
                i = 0;
              }
              else if((r > 0) && (r < 3)) //1 bis 2
              {
                req[r-1][i++] = c;
                req[r-1][i] = 0;
              }
              last_c = c;
            }
            urldecode(req[0]); //Serial.println(req[0]);
            urldecode(req[1]); //Serial.println(req[1]);
            if(strcmp(req[0], settings.wifi_ssid) || strcmp(req[1], settings.wifi_code))
            {
              //todo: Leerzeichen am Ende entfernen
              strcpy(settings.wifi_ssid, req[0]);
              strcpy(settings.wifi_code, req[1]);
              settings_write(&settings); //Einstellungen speichern
            }
          }
          //HTTP Header+Daten
          sprintf(buf,
              "HTTP/1.1 200 OK\r\n" \
              "Content-Type: text/html\r\n" \
              "Connection: close\r\n" \
              "\r\n"
              "<!DOCTYPE html>\r\n" \
              "<html>\r\n" \
              "<head>\r\n" \
              "<meta charset=utf-8>\r\n" \
              "<meta http-equiv=refresh content=120>\r\n" \
              "<title>CO2-Ampel</title>\r\n" \
              "<link rel=icon href=\"data:image/gif;base64,R0lGODlhAQABAAAAACwAAAAAAQABAAA=\">\r\n" \
              "<style>\r\n" \
              "body { font-size:1.0em; font-family:Lato,sans-serif; padding:10px; }\r\n" \
              "#data { font-size:3.0em; }\r\n" \
              "#wifi { font-size:1.0em; display:none; }\r\n" \
              "#info { font-size:0.9em; }\r\n" \
              "</style>\r\n" \
              "<script>\r\n" \
              "function wifi() {\r\n" \
              "var box = document.getElementById('wifi');\r\n" \
              "if(box.style.display != 'block') { box.style.display = 'block'; }\r\n" \
              "else { box.style.display = 'none'; }\r\n" \
              "}\r\n" \
              "</script>\r\n" \
              "</head>\r\n" \
              "<body>\r\n"
          );
          client.print(buf);

          if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
          {
            sprintf(buf,
                "<div id=data>\r\n" \
                "CO2 (ppm): %i<br/>\r\n" \
                "Temperatur (&deg;C): %.1f<br/>\r\n" \
                "Luftfeuchte (% rel): %.1f<br/>\r\n" \
                "Druck (hPa): %.1f<br/>\r\n" \
                "Temperatur (&deg;C): %.1f<br/>\r\n" \
                "</div>\r\n",
                co2_value, temp_value, humi_value, pres_value, temp2_value
            );
          }
          else
          {
            sprintf(buf,
                "<div id=data>\r\n" \
                "CO2 (ppm): %i<br/>\r\n" \
                "Temperatur (&deg;C): %.1f<br/>\r\n" \
                "Luftfeuchte (% rel): %.1f<br/>\r\n" \
                "</div>\r\n",
                co2_value, temp_value, humi_value
            );
          }
          client.print(buf);

          String fv = WiFi.firmwareVersion();
          byte mac[6];
          WiFi.macAddress(mac);
          sprintf(buf,
              "<br/><br/>\r\n" \
              "<a href='/json'>JSON</a> - <a href='/cmk-agent'>Checkmk</a> - <a href='#' onclick='wifi();'>WiFi Login</a>\r\n" \
              "<br/><br/>\r\n" \
              "<div id=wifi>\r\n" \
              "<form method=post>\r\n" \
              "SSID <input name=1 size=30 maxlength=64 placeholder=SSID value='%s'><br/>\r\n" \
              "Code <input name=2 size=30 maxlength=64 placeholder=Password value=''><br/>\r\n" \
              "<input type=submit> (Neustart erforderlich, requires reboot)<br/>\r\n" \
              "</form><br/>\r\n" \
              "<div id=info>\r\n" \
              "Firmware: v" VERSION ", \r\n" \
              "WINC1500: %s, \r\n" \
              "MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n" \
              "</div>\r\n" \
              "</div>\r\n" \
              "</body>\r\n" \
              "</html>\r\n",
              settings.wifi_ssid, /*settings.wifi_code, */ fv.c_str(), 
              mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]
          );
          client.print(buf);
        }
        break;
      }
      else //save request
      {
        if(pos < (sizeof(req[0])-1))
        {
          req[0][pos++] = c;
          req[0][pos] = 0;
        }
      }
      if(c == '\n')
      {
        currentLineIsBlank = true;
      }
      else if(c != '\r')
      {
        currentLineIsBlank = false;
      }
    }
  }

  delay(20); //20ms warten zum Senden
  client.stop();

  return;
}


int check_i2c(Sercom *sercom, byte addr) //1=okay
{
  int res = 0;

  for(int t=3; (t!=0) && (res==0); t--) //try 3 times
  {
    sercom->I2CM.CTRLA.bit.ENABLE = 1; //enable master mode
    delay(10); //wait 10ms
    sercom->I2CM.ADDR.bit.ADDR = (addr<<1) | 0x00; //start transfer
    delay(10); //wait 10ms
    if(sercom->I2CM.INTFLAG.bit.MB || sercom->I2CM.INTFLAG.bit.SB) //data transmitted
    {
      if(!sercom->I2CM.STATUS.bit.RXNACK) //ack received
      {
        res = 1; //ok
        break;
      }
    }
  }

  /*
  if(res == 0)
  {
    if(sercom == SERCOM1)
    {
      Wire.beginTransmission(addr);
      if(Wire.endTransmission() == 0)
      {
        res = 1; //ok
      }
    }
    else if(sercom == SERCOM2)
    {
      Wire1.beginTransmission(addr);
      if(Wire1.endTransmission() == 0)
      {
        res = 1; //ok
      }
    }
  }
  */

  return res;
}


void self_test(void) //Testprogramm
{
  //Buzzer-Test
  buzzer(500); //500ms Buzzer an

  //LED-Test
  leds(0xFF0000); //LEDs rot
  delay(1000); //1s warten
  leds(0x00FF00); //LEDs gruen
  delay(1000); //1s warten
  leds(0x0000FF); //LEDs blau
  delay(1000); //1s warten
  leds(FARBE_AUS); //LEDs aus

  #if WIFI_AMPEL
    //ATWINC1500-Test
    if(WiFi.status() == WL_NO_SHIELD) //ATWINC1500 Fehler
    {
      if(features & FEATURE_USB)
      {
        Serial.println("Error: ATWINC1500");
      }
      while(1)
      {
        leds(settings.color_red); //LEDs rot
        delay(500); //500ms warten
        leds(settings.color_yellow); //LEDs gelb
        delay(500); //500ms warten
      }
    }
    else
    {
      leds(FARBE_WEISS); //LEDs weiss
      buzzer(1000); //1s Buzzer an
    }
  #endif

  //RFM9X-Test
  #if PRO_AMPEL
    SPI.begin();
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV128);
    digitalWrite(20, LOW); //RFM9X CS low/active
    SPI.transfer(0x42); //0x42 = version
    byte i = SPI.transfer(0x00);
    digitalWrite(20, HIGH); //RFM9X CS high
    if(i == 0x12) //check version
    {
      leds(FARBE_WEISS); //LEDs weiss
      buzzer(1000); //1s Buzzer an
    }
  #endif

  //Sensor-Test
  unsigned int co2, light;
  float temp, humi, pres;
  co2_value  = 0;
  temp_value = 0;
  humi_value = 0;
  #if PRO_AMPEL
    pres_value = 0;
  #endif
  ws2812.fill(FARBE_AUS, 0, 4); //LEDs aus
  for(unsigned int okay=0; okay < 15;)
  {
    if(digitalRead(PIN_SWITCH) == 0) //Taster gedrueckt?
    {
      break; //Abbruch
    }
    status_led(200); //Status-LED

    digitalWrite(PIN_LSENSOR_PWR, HIGH); //Lichtsensor an
    delay(50); //50ms warten
    light = analogRead(PIN_LSENSOR); //0...1024
    digitalWrite(PIN_LSENSOR_PWR, LOW); //Lichtsensor aus
    if((light >= 50) && (light <= 1000)) //50-1000
    {
      okay |= (1<<0);
      ws2812.setPixelColor(0, COLOR_GREEN);
    }
    else
    {
      okay &= ~(1<<0);
      ws2812.setPixelColor(0, COLOR_OFF);
    }

    if(check_sensors())
    {
      co2  = co2_sensor();
      temp = temp_sensor();
      humi = humi_sensor();
      pres = pres_sensor();

      if((co2 >= 100) && (co2 <= 1500)) //100-1500ppm
      {
        okay |= (1<<1);
        ws2812.setPixelColor(1, COLOR_BLUE);
      }
      else
      {
        okay &= ~(1<<1);
        ws2812.setPixelColor(1, COLOR_OFF);
      }

      if(((temp >=   5) && (temp <=   35)) && //5-35°C
         ((pres >= 700) && (pres <= 1400)))   //700-1400 hPa
      {
        okay |= (1<<2);
        ws2812.setPixelColor(2, COLOR_GREEN);
      }
      else
      {
        okay &= ~(1<<2);
        ws2812.setPixelColor(2, COLOR_OFF);
      }

      if((humi >= 20) && (humi <= 80)) //20-80%
      {
        okay |= (1<<3);
        ws2812.setPixelColor(3, COLOR_GREEN);
      }
      else
      {
        okay &= ~(1<<3);
        ws2812.setPixelColor(3, COLOR_OFF);
      }

      show_data();
    }

    ws2812.show();
  }
  
  delay(2000); //2s warten

  return;
}


void air_test(void) //Frischluft-Test
{
  unsigned int co2;

  ws2812.fill(COLOR_WHITE, 0, 4); //LEDs weiss
  ws2812.show();

  while(1)
  {
    if(digitalRead(PIN_SWITCH) == 0) //Taster gedrueckt?
    {
      break; //Abbruch
    }

    status_led(200); //Status-LED

    if(check_sensors())
    {
      co2 = co2_sensor();

      if(co2 < 300)
      {
        ws2812.fill(COLOR_RED, 0, NUM_LEDS); //rot
      }
      else if(co2 < 350)
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS); //gelb
      }
      else if(co2 <= 450)
      {
        ws2812.fill(COLOR_BLUE, 0, NUM_LEDS); //blau
      }
      else if(co2 <= 500)
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS); //gelb
      }
      else //>500
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS); //rot
      }
      ws2812.show();

      show_data();
    }
  }

  //Ende
  leds(COLOR_OFF);//LEDs aus
  buzzer(250); //250ms Buzzer an

  return;
}


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

  //Der Messintervall während der Kalibrierung und im Betrieb sollte gleich sein.
  //Unterschiedliche Intervalle können zu Abweichungen und schwankenden Messwerten führen.
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
  for(again=0, cycle=0; cycle < (180/interval);) //mindestens 3 Minuten
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


unsigned int wifi_start_ap(void)
{
  byte mac[6];
  char ssid[32];

  if(features & FEATURE_USB)
  {
    Serial.println("WiFi AP start...");
  }

  WiFi.macAddress(mac); //MAC-Adresse abfragen
  sprintf(ssid, "CO2AMPEL-%X-%X", mac[1], mac[0]);

  if(WiFi.status() != WL_IDLE_STATUS)
  {
    WiFi.end(); //WiFi.disconnect();
    //reset_mcu();
  }

  WiFi.hostname(ssid); //Hostname setzen
  if(WiFi.beginAP(ssid) != WL_AP_LISTENING)
  {
    WiFi.end();
    return 1;
  }

  delay(5000); //5s warten

  server.begin(); //starte Webserver

  return 0;
}


unsigned int wifi_start(void)
{
  byte mac[6];
  char name[32];

  if(settings.wifi_ssid[0] == 0) //keine Logindaten
  {
    return 1;
  }

  if(features & FEATURE_USB)
  {
    Serial.println("WiFi connect...");
  }

  WiFi.macAddress(mac); //MAC-Adresse abfragen
  sprintf(name, "CO2AMPEL-%X-%X", mac[1], mac[0]);

  if(WiFi.status() != WL_IDLE_STATUS)
  {
    WiFi.end(); //WiFi.disconnect();
    //reset_mcu();
  }

  WiFi.hostname(name); //Hostname setzen
  if(settings.ip_local[0] != 0) //statische IP
  {
    WiFi.config(settings.ip_local, settings.ip_dns, settings.ip_gw, settings.netmask);  //IP setzen
  }
  if(strlen(settings.wifi_code) > 0) //Passwort
  {
    WiFi.begin(settings.wifi_ssid, settings.wifi_code); //verbinde WiFi Netzwerk mit Passwort
  }
  else
  {
    WiFi.begin(settings.wifi_ssid); //verbinde WiFi Netzwerk ohne Passwort
  }

  //auf Verbindung warten
  for(unsigned int t=0; WiFi.status() == WL_IDLE_STATUS; t++)
  {
    if(t >= 6) //6s
    {
      break;
    }
    status_led(1000); //Status-LED
  }

  if(!(WiFi.status() == WL_CONNECTED)) //Verbindung fehlgeschlagen
  {
    return 1;
  }

  server.begin(); //starte Webserver

  return 0;
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
    scd30.StopMeasurement();
    //scd30.reset; //soft reset
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


//--- MQTT Functions ---

// Holt die eindeutige Chip-ID des SAMD21
void get_chip_id(char *buffer, size_t buffer_size)
{
  // SAMD21G18A Unique Identifier (128-bit = 4x 32-bit words)
  volatile uint32_t *id0 = (volatile uint32_t *)0x0080A00C;
  volatile uint32_t *id1 = (volatile uint32_t *)0x0080A040;
  volatile uint32_t *id2 = (volatile uint32_t *)0x0080A044;
  volatile uint32_t *id3 = (volatile uint32_t *)0x0080A048;

  snprintf(buffer, buffer_size, "%08X%08X%08X%08X",
           (unsigned int)*id0, (unsigned int)*id1,
           (unsigned int)*id2, (unsigned int)*id3);
}

// Generiert Device-ID aus MAC-Adresse (volle MAC-Adresse)
void get_device_id(char *buffer, size_t buffer_size)
{
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(buffer, buffer_size, "%02X%02X%02X%02X%02X%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

//--- Settings Storage Functions (Flash Memory) ---

// Liest Einstellungen aus Flash
void settings_read(SETTINGS *data)
{
  memcpy(data, (const void*)SETTINGS_FLASH_ADDR, sizeof(SETTINGS));
}

// Schreibt Einstellungen in Flash
void settings_write(const SETTINGS *data)
{
  // SAMD21 Flash-Programmierung
  // Page-Größe: 64 Bytes (Row = 256 Bytes = 4 Pages)

  const uint32_t flash_addr = (uint32_t)SETTINGS_FLASH_ADDR;
  const uint32_t page_size = 64;
  const uint32_t row_size = 256;
  const uint32_t num_rows = (sizeof(SETTINGS) + row_size - 1) / row_size;

  // Disable interrupts during flash operations
  __disable_irq();

  // Erase rows first
  for(uint32_t i = 0; i < num_rows; i++)
  {
    uint32_t addr = flash_addr + (i * row_size);

    // Execute "Erase Row" command
    NVMCTRL->ADDR.reg = addr / 2; // Address must be divided by 2
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
    while(!NVMCTRL->INTFLAG.bit.READY);
  }

  // Write data page by page
  const uint32_t *src = (const uint32_t *)data;
  uint32_t num_pages = (sizeof(SETTINGS) + page_size - 1) / page_size;

  for(uint32_t page = 0; page < num_pages; page++)
  {
    // Clear page buffer
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
    while(!NVMCTRL->INTFLAG.bit.READY);

    // Fill page buffer (16 words = 64 bytes per page)
    uint32_t *dst = (uint32_t *)(flash_addr + (page * page_size));
    for(uint32_t i = 0; i < (page_size / 4); i++)
    {
      dst[i] = *src++;
    }

    // Execute "Write Page" command
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
    while(!NVMCTRL->INTFLAG.bit.READY);
  }

  // Re-enable interrupts
  __enable_irq();
}

void mqtt_connect(void)
{
  char client_id[48];

  if(!settings.mqtt_enabled)
  {
    return;
  }

  if((features & FEATURE_WINC1500) == 0)
  {
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT: WiFi hardware not available");
    }
    return; //keine WiFi-Hardware
  }

  if(WiFi.status() != WL_CONNECTED)
  {
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT: WiFi not connected");
    }
    return; //keine WiFi-Verbindung
  }

  //MQTT Client ID generieren wenn leer
  if(strlen(settings.mqtt_client_id) == 0)
  {
    get_device_id(client_id, sizeof(client_id));
  }
  else
  {
    strncpy(client_id, settings.mqtt_client_id, sizeof(client_id)-1);
    client_id[sizeof(client_id)-1] = '\0';
  }

  if(features & FEATURE_USB)
  {
    Serial.print("MQTT connecting to ");
    Serial.print(settings.mqtt_broker);
    Serial.print(":");
    Serial.print(settings.mqtt_port);
    Serial.print(" as '");
    Serial.print(client_id);
    Serial.print("'");
    if(strlen(settings.mqtt_user) > 0)
    {
      Serial.print(" with user '");
      Serial.print(settings.mqtt_user);
      Serial.print("'");
    }
    Serial.println();
  }

  //MQTT Client initialisieren
  mqttClient.begin(settings.mqtt_broker, settings.mqtt_port, mqttWifiClient);

  //Verbinden
  boolean connected = false;
  if(strlen(settings.mqtt_user) > 0)
  {
    //Mit Authentifizierung
    connected = mqttClient.connect(client_id, settings.mqtt_user, settings.mqtt_pass);
  }
  else
  {
    //Ohne Authentifizierung
    connected = mqttClient.connect(client_id);
  }

  if(connected)
  {
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT: Connected successfully");
    }
  }
  else
  {
    if(features & FEATURE_USB)
    {
      Serial.print("MQTT: Connection failed - ");

      //Detaillierte Fehlerausgabe
      lwmqtt_err_t err = mqttClient.lastError();
      lwmqtt_return_code_t rc = mqttClient.returnCode();

      if(err != LWMQTT_SUCCESS)
      {
        Serial.print("Error: ");
        switch(err)
        {
          case LWMQTT_NETWORK_FAILED_CONNECT: Serial.println("Network connection failed"); break;
          case LWMQTT_NETWORK_TIMEOUT: Serial.println("Network timeout"); break;
          case LWMQTT_NETWORK_FAILED_READ: Serial.println("Network read failed"); break;
          case LWMQTT_NETWORK_FAILED_WRITE: Serial.println("Network write failed"); break;
          case LWMQTT_CONNECTION_DENIED: Serial.println("Connection denied by broker"); break;
          default: Serial.print("Code "); Serial.println(err); break;
        }
      }

      if(rc != LWMQTT_CONNECTION_ACCEPTED)
      {
        Serial.print("Return Code: ");
        switch(rc)
        {
          case LWMQTT_UNACCEPTABLE_PROTOCOL: Serial.println("Unacceptable protocol version"); break;
          case LWMQTT_IDENTIFIER_REJECTED: Serial.println("Client ID rejected"); break;
          case LWMQTT_SERVER_UNAVAILABLE: Serial.println("Server unavailable"); break;
          case LWMQTT_BAD_USERNAME_OR_PASSWORD: Serial.println("Bad username or password"); break;
          case LWMQTT_NOT_AUTHORIZED: Serial.println("Not authorized"); break;
          default: Serial.print("Code "); Serial.println(rc); break;
        }
      }
    }
  }
}


void mqtt_reconnect(void)
{
  static unsigned long last_attempt = 0;

  if(!settings.mqtt_enabled)
  {
    return;
  }

  //Nur alle 30 Sekunden neu versuchen
  if((millis() - last_attempt) < 30000UL)
  {
    return;
  }

  last_attempt = millis();
  mqtt_connect();
}


void mqtt_publish_sensors(void)
{
  char topic[128];
  char value[64];
  char device_id[32];
  char chip_id[40];

  if(!settings.mqtt_enabled)
  {
    return;
  }

  if(!mqttClient.connected())
  {
    return;
  }

  //Device-ID aus MAC-Adresse generieren
  get_device_id(device_id, sizeof(device_id));
  
 /*
  //Chip-ID holen
  get_chip_id(chip_id, sizeof(chip_id));

 
  //Chip-ID publizieren
  sprintf(topic, "%s/%s/chipid", settings.mqtt_topic_prefix, device_id);
  mqttClient.publish(topic, chip_id, true, 0); //retained, damit immer verfügbar
  */
  
  //CO2
  sprintf(topic, "%s/%s/co2", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%d", co2_value);
  mqttClient.publish(topic, value, false, 0);

  //Temperatur
  sprintf(topic, "%s/%s/temperature", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%.1f", temp_value);
  mqttClient.publish(topic, value, false, 0);

  //Luftfeuchtigkeit
  sprintf(topic, "%s/%s/humidity", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%.1f", humi_value);
  mqttClient.publish(topic, value, false, 0);

  //Lichtsensor
  sprintf(topic, "%s/%s/light", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%d", light_value);
  mqttClient.publish(topic, value, false, 0);

  //Druck (nur Pro Version)
  if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
  {
    sprintf(topic, "%s/%s/pressure", settings.mqtt_topic_prefix, device_id);
    sprintf(value, "%.1f", pres_value);
    mqttClient.publish(topic, value, false, 0);
  }

  if(features & FEATURE_USB)
  {
    Serial.print("MQTT published to ");
    Serial.print(settings.mqtt_topic_prefix);
    Serial.print("/");
    Serial.println(device_id);
  }
}


void mqtt_service(void)
{
  static unsigned long last_publish = 0;

  if(!settings.mqtt_enabled)
  {
    return;
  }

  if((features & FEATURE_WINC1500) == 0)
  {
    return;
  }

  //MQTT Loop (non-blocking)
  mqttClient.loop();

  //Verbindung pruefen
  if(!mqttClient.connected())
  {
    mqtt_reconnect();
    return;
  }

  //Periodisches Publishing
  if((millis() - last_publish) > (settings.mqtt_interval * 1000UL))
  {
    last_publish = millis();
    mqtt_publish_sensors();
  }
}


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
  pinMode(20, OUTPUT); //PA21 RFM9X CS-Pin
  digitalWrite(20, HIGH); //RFM9X CS high

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
  Serial.setTimeout(500); //500ms Timeout beim Lesen
  //while(!Serial); //warten auf USB-Verbindung

  delay(250); //250ms warten

  #if WIFI_AMPEL
    //ATWINC1500
    if(WiFi.status() != WL_NO_SHIELD) //ATWINC1500 gefunden
    {
      features |= FEATURE_WINC1500;
    }
  #endif

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
      if(scd30.begin(Wire, AUTO_KALIBRIERUNG))
      {
        features |= FEATURE_SCD30;
        break;
      }
      status_led(1000); //Status-LED
    }
    scd30.setMeasurementInterval(INTERVALL); //setze Messintervall
    //scd30.setAmbientPressure(1000); //0 oder 700-1400, Luftdruck in hPa
  }
  if(check_i2c(SERCOM0, ADDR_SCD4X)) //SCD4X gefunden
  {
    for(int t=5; t!=0; t--) //try 5 times
    {
      Wire.begin();
      scd4x.begin(Wire);
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
    temp_offset = scd30.getTemperatureOffset();
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
  settings_read(&settings); //Einstellungen lesen
  if((settings.valid == false) || (settings.brightness > 255) || (settings.range[0] < 100))
  {
    settings.brightness   = HELLIGKEIT;
    settings.range[0]     = DEFAULT_T1;
    settings.range[1]     = DEFAULT_T2;
    settings.range[2]     = DEFAULT_T3;
    settings.range[3]     = DEFAULT_T4;
    settings.range[4]     = DEFAULT_T5;
    settings.buzzer       = BUZZER;
    settings.wifi_ssid[0] = 0;
    strcpy(settings.wifi_ssid, WIFI_SSID);
    settings.wifi_code[0] = 0;
    strcpy(settings.wifi_code, WIFI_CODE);
    settings.netmask      = IPAddress(WIFI_NM);
    settings.ip_local     = IPAddress(WIFI_IP);
    settings.ip_gw        = IPAddress(WIFI_GW);
    settings.ip_dns       = IPAddress(WIFI_DNS);

    //MQTT Standardeinstellungen
    settings.mqtt_enabled = MQTT_ENABLED;
    settings.mqtt_broker[0] = 0;
    strcpy(settings.mqtt_broker, MQTT_BROKER);
    settings.mqtt_port = MQTT_PORT;
    settings.mqtt_user[0] = 0;
    strcpy(settings.mqtt_user, MQTT_USER);
    settings.mqtt_pass[0] = 0;
    strcpy(settings.mqtt_pass, MQTT_PASS);
    settings.mqtt_client_id[0] = 0;
    strcpy(settings.mqtt_client_id, MQTT_CLIENT_ID);
    settings.mqtt_topic_prefix[0] = 0;
    strcpy(settings.mqtt_topic_prefix, MQTT_TOPIC_PREFIX);
    settings.mqtt_interval = MQTT_INTERVAL;

    //LED Color Defaults
    settings.color_t1   = DEFAULT_COLOR_T1;
    settings.color_t2  = DEFAULT_COLOR_T2;
    settings.color_t3 = DEFAULT_COLOR_T3;
    settings.color_t4    = DEFAULT_COLOR_T4;
    settings.valid        = true;
    settings_write(&settings);
    //Standard Temperaturoffset (always Pro with WiFi and pressure sensor)
    temp_offset = TEMP_OFFSET;
    if(features & FEATURE_SCD30)
    {
      float offset;
      offset = scd30.getTemperatureOffset();
      if((offset == 0) || (offset > 12))
      {
        scd30.setTemperatureOffset(temp_offset); //Temperaturoffset
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
    Serial.println("\n");
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


void ampel(unsigned int co2)
{
  static unsigned int blinken=0;

  //LEDs
  if(co2 < settings.range[0]) //blau (very fresh air)
  {
    blinken = 0;
    leds(settings.color_t1);
  }
  else if(co2 < settings.range[1]) //gruen (good)
  {
    blinken = 0;
    leds(settings.color_t2);
  }
  else if(co2 < settings.range[2]) //gelb (warning)
  {
    blinken = 0;
    leds(settings.color_t3);
  }
  else if(co2 < settings.range[3]) //rot (alert)
  {
    blinken = 0;
    leds(settings.color_t4);
  }
  else //rot blinken (critical - blinking)
  {
    if(blinken == 0)
    {
      leds(ws2812.Color(10,0,0)); //rot schwache Helligkeit
    }
    else
    {
      leds(settings.color_t4); //rot
    }
    blinken = 1-blinken; //invertieren
  }

  //Buzzer
  if(co2 < settings.range[4])
  {
    buzzer(0); //Buzzer aus
  }
  else
  {
    if((blinken == 0) && (buzzer_timer == 0) && settings.buzzer)
    {
      buzzer(1); //Buzzer an
    }
    else
    {
      buzzer(0); //Buzzer aus
    }
  }

  return;
}


void loop()
{
  static unsigned int dark=0, sw=0;
  static unsigned long t_switch=0, t_ampel=0, t_light=~((LICHT_INTERVALL*1000UL*60UL)-60000UL); //Lichtsensor nach 60s pruefen
  unsigned int overwrite=0;

  //serielle Befehle verarbeiten
  serial_service();

  //WiFi-Daten verarbeiten
  webserver_service();

  //MQTT-Daten verarbeiten
  mqtt_service();

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
