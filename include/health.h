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
} HEALTH_COUNTERS;

extern HEALTH_COUNTERS health;

void health_init(void); // fills the free RAM with a pattern (early in setup), for the lowest free RAM
void health_loop(void); // at the start of loop(): loop time

uint32_t health_ram_free(void); // free RAM between heap and stack now (bytes)

// Health as one JSON object (serial "diag", web /diag, MQTT <base>/diag); returns the length
size_t health_json(char *buf, size_t size);

#endif
