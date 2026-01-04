#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// Version will be overridden by platformio.ini if VERSION is defined there
#ifndef VERSION
#define VERSION "SET VERSION!"
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
#define HELLIGKEIT         125 //1-255 (255=100%, 179=70%)
#define HELLIGKEIT_DUNKEL  20  //1-255 (255=100%, 25=10%)
#define NUM_LEDS           4   //Anzahl der LEDs

//--- Lichtsensor ---
#define LICHT_DUNKEL       20   //<20 -> dunkel
#define LICHT_INTERVALL    10 //10-120min (Sensorpruefung)

//--- Allgemein ---
#define INTERVALL          5 //2-1800s Messintervall (nur SCD30, SCD4X immer 5s)
#define AMPEL_DURCHSCHNITT 1 //1 = CO2 Durchschnitt fuer Ampel verwenden
#define AUTO_KALIBRIERUNG  0 //1 = automatische Kalibrierung (ASC) an (erfordert 7 Tage Dauerbetrieb mit 1h Frischluft pro Tag)
#define BUZZER             1 //Buzzer aktivieren
#define BUZZER_DELAY     300 //300s, Buzzer Startverzogerung
#define TEMP_OFFSET        6 //Pro WiFi, Temperaturoffset in C (0-20)
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

#endif
