#ifndef ERRORS_H
#define ERRORS_H

#include <Arduino.h>

// What is wrong right now, for the "errors" list in diag. History is left to the monitoring tool.
typedef enum
{
  ERR_CO2_MISSING, ERR_CO2_STALE, ERR_CO2_READ_FAILED, ERR_CO2_RANGE, ERR_CO2_FROZEN,
  ERR_TEMP_RANGE, ERR_HUMIDITY_RANGE,
  ERR_PRESSURE_MISSING, ERR_PRESSURE_STALE, ERR_PRESSURE_RANGE, ERR_PRESSURE_OFFSET,
  ERR_I2C_BUS, ERR_I2C_STUCK, ERR_WIFI_MODULE_MISSING,
  ERR_RESET_HANG, ERR_RESET_FAULT, ERR_RESET_BROWNOUT,
  ERR_SETTINGS_LOST, ERR_SETTINGS_NEWER, ERR_RAM_LOW, ERR_LOOP_SLOW,
  ERR_WIFI_LOST, ERR_WIFI_RECONNECTS, ERR_WIFI_WEAK, ERR_SETUP_NETWORK,
  ERR_MQTT_RECONNECTS, ERR_MQTT_CONNECT, ERR_MQTT_PUBLISH, ERR_NET_WRITE_TIMEOUT,
  ERR_CAL_REFUSED, ERR_CAL_FAILED,
  ERR_COUNT
} ERROR_CODE;

void error_set(ERROR_CODE e, bool active); // a condition: listed while it lasts
void error_event(ERROR_CODE e);            // a one-off event: listed for ERROR_HOLD, so a tool can't miss it between messages
bool errors_changed(void);                 // the list differs from the one errors_json() last wrote
size_t errors_json(char *buf, size_t size);// JSON array; ends with "truncated" if it doesn't fit

#endif
