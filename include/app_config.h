#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// Version will be overridden by platformio.ini if VERSION is defined there
#ifndef VERSION
#define VERSION "SET VERSION!"
#endif
#define PRODUCT_NAME       "CO2-Ampel-NG" //default system name (+ MAC id), HA model
#define MANUFACTURER       "zone11"       //HA manufacturer

// Optional features (compile time, override with -DWITH_...=0 in platformio.ini build_flags).
// Runtime settings of a disabled feature stay in the settings record but are not offered.
#ifndef WITH_WEB
#define WITH_WEB           1 //web server: readings, /json, /cmk-agent, /config, WiFi form
#endif
#ifndef WITH_HA
#define WITH_HA            1 //Home Assistant MQTT discovery (mqtt.ha)
#endif
#ifndef WITH_MQTT_CONFIG
#define WITH_MQTT_CONFIG   1 //configuration over MQTT (mqtt.config)
#endif

// Default CO2 thresholds (can be changed via serial commands and saved to flash)
#define DEFAULT_T1         600  //>= 600ppm
#define DEFAULT_T2         1000 //>=1000ppm
#define DEFAULT_T3         1200 //>=1200ppm
#define DEFAULT_T4         1400 //>=1400ppm
#define DEFAULT_T5         1600 //>=1600ppm

// Default LED colors (can be changed via serial commands and saved to flash)
#define DEFAULT_COLOR_T1   0x007CB0 //sky blue
#define DEFAULT_COLOR_T2   0x00FF00 //green
#define DEFAULT_COLOR_T3   0xFF7F00 //orange-yellow
#define DEFAULT_COLOR_T4   0xFF0000 //red

//--- WiFi ---
#define WIFI_SSID          "" //WiFi SSID
#define WIFI_CODE          "" //WiFi password

//--- MQTT ---
#define MQTT_ENABLED       0      //0 = MQTT off, 1 = MQTT on
#define MQTT_BROKER        ""     //broker hostname or IP
#define MQTT_PORT          1883   //broker port (default 1883, TLS 8883)
#define MQTT_TLS           0      //1 = MQTT over TLS (MQTTS)
#define MQTT_USER          ""     //user name (optional)
#define MQTT_PASS          ""     //password (optional)
#define MQTT_CLIENT_ID     ""     //client ID (empty = derived from the MAC)
#define MQTT_TOPIC_PREFIX  "co2ampel" //topic prefix
#define MQTT_INTERVAL      60     //publish interval in seconds
#define MQTT_HA            0      //1 = Home Assistant MQTT discovery (mqtt.ha)
#define MQTT_DISCOVERY_PREFIX "homeassistant" //Home Assistant discovery prefix
#define MQTT_CONFIG        0      //1 = configuration over MQTT (topic <prefix>/<id>/cmd) allowed

//--- LED brightness ---
#define BRIGHTNESS         125 //1-255 (255=100%, 179=70%)
#define BRIGHTNESS_DARK    20  //1-255 (255=100%, 25=10%)
#define BRIGHTNESS_AUTO    1   //1 = dim with the light sensor (led.auto)
#define BRIGHTNESS_MENU    30  //brightness with "enable" and in the service menu
#define NUM_LEDS           4   //number of LEDs

//--- Light sensor ---
#define LIGHT_DARK         20  //<20 -> dark
#define LIGHT_INTERVAL     10  //10-120min (sensor check)
#define LIGHT_LEDS_OFF_MS  3   //ms LEDs off before the reading (sensor reacts immediately)

//--- General ---
#define MEASURE_INTERVAL   5   //2-1800s measurement interval (SCD30 only, SCD4X always 5s)
#define USE_CO2_AVERAGE    1   //1 = traffic light follows the CO2 average
#define CO2_ASC            0   //1 = automatic self calibration (ASC) on (needs 7 days of operation with 1h fresh air per day)
#define BUZZER             1   //buzzer on
#define BUZZER_DELAY       300 //300s, buzzer start delay
#define TEMP_OFFSET        6   //Pro WiFi, temperature offset in C (0-20)
#define PRESSURE_DIFF      5   //pressure change in hPa (5-20)
#define BAUDRATE           9600
#define CO2_START          500 //500ppm, CO2 start value

//--- Fixed LED colors (not configurable) ---
#define COLOR_SETTINGS     0x007CB0 //light blue
#define COLOR_MENU         0xFF00FF //violet (menu, "enable")
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
