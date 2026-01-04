#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <Arduino.h>

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

typedef struct
{
  boolean valid;
  unsigned int brightness;
  unsigned int range[5];        // CO2 thresholds: [0]=green, [1]=yellow, [2]=red, [3]=red_blink, [4]=buzzer
  unsigned int buzzer;
  char wifi_ssid[64+1];
  char wifi_code[64+1];
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
  boolean serial_output;      // Enable serial measurement output
} SETTINGS;

#endif
