#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "app_globals.h"

void get_chip_id(char *buffer, size_t buffer_size);
void get_device_id(char *buffer, size_t buffer_size);
void print_mqtt_status(void);
void mqtt_connect(void);
void mqtt_reconnect(void);
void mqtt_publish_sensors(void);
void mqtt_service(void);

#endif
