#ifndef HEALTH_H
#define HEALTH_H

#include <stddef.h>
#include <stdint.h>

// Runtime health: RAM, loop time, connection counters, reset record (system_control)
typedef struct
{
  uint32_t wifi_connects; // WiFi connect attempts since boot
  uint32_t mqtt_connects; // successful MQTT connects since boot
  uint32_t mqtt_failures; // failed MQTT connects since boot
  int mqtt_error;         // lwmqtt error of the last failed connect
  uint32_t i2c_errors;    // I2C bus timeouts, bus errors and stuck lines since boot
  uint32_t net_write_timeouts; // socket writes given up after NET_WRITE_TIMEOUT since boot
  uint32_t co2_failures;  // times the CO2 sensor stopped delivering readings since boot
  uint32_t pressure_offset; // last value found in the LPS22HB offset register since boot, 0 = none
} HEALTH_COUNTERS;

extern HEALTH_COUNTERS health;

void health_init(void); // fills the free RAM with a pattern (early in setup), for the lowest free RAM
void health_error_check(void); // once per second: RAM, repeats of connects and bus errors
void health_loop(void); // at the start of loop(): loop time

uint32_t health_ram_free(void); // free RAM between heap and stack now (bytes)

// Health as one JSON object (serial "diag", web /diag, MQTT <base>/diag); returns the length
size_t health_json(char *buf, size_t size);

#endif
