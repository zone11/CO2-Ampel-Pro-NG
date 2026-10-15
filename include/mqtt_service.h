#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include "app_globals.h"

void get_chip_id(char *buffer, size_t buffer_size);
void print_mqtt_status(void);
void mqtt_connect(void);
void mqtt_reconnect(void);
void mqtt_publish_sensors(void);
void mqtt_service(void);

// Home Assistant discovery after a change of mqtt.ha (immediately, if connected): on = publish the
// configs, off = remove the entities once (empty retained configs). Nothing is sent while it stays off.
void mqtt_ha_update(void);

#endif
