#ifndef SENSORS_H
#define SENSORS_H

#include "app_globals.h"

unsigned int light_sensor(void);
float co2_sensor(void);
float temp_sensor(void);
float humi_sensor(void);
float pres_sensor(void);
unsigned int check_sensors(void);
void co2_set_asc(bool on);
void print_measurements(void);
void print_sensor_info(void);
void show_data(void);
const char *fmt1(char *buf, float value); // value with one decimal like "%.1f", buf >= 16 bytes

#endif
