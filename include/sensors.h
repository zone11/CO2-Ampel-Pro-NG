#ifndef SENSORS_H
#define SENSORS_H

#include "app_globals.h"

unsigned int light_sensor(void);
float co2_sensor(void);
float temp_sensor(void);
float humi_sensor(void);
float pres_sensor(void);
unsigned int check_sensors(void);
void print_measurements(void);
void show_data(void);

#endif
