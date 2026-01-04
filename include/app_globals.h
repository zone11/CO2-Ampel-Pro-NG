#ifndef APP_GLOBALS_H
#define APP_GLOBALS_H

#include "app_config.h"
#include "app_types.h"

#include <SparkFun_SCD30_Arduino_Library.h>
#include <SensirionI2CScd4x.h>
#include <Adafruit_BMP280.h>
#include <Arduino_LPS22HB.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi101.h>
#include <MQTT.h>

extern SETTINGS settings;
extern SCD30 scd30;
extern SensirionI2CScd4x scd4x;
extern Adafruit_BMP280 bmp280;
extern LPS22HBClass lps22;
extern Adafruit_NeoPixel ws2812;
extern WiFiClient mqttWifiClient;
extern MQTTClient mqttClient;
extern WiFiServer server;

extern unsigned int features;
extern unsigned int remote_on;
extern unsigned int buzzer_timer;
extern unsigned int co2_value;
extern unsigned int co2_average;
extern unsigned int light_value;
extern float temp_value;
extern float temp_offset;
extern float humi_value;
extern float pres_value;
extern float pres_last;
extern float temp2_value;

#endif
