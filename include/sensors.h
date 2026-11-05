#ifndef SENSORS_H
#define SENSORS_H

#include "app_globals.h"
#include "scd4x_convert.h"

unsigned int light_sensor(void);
float co2_sensor(void);
float temp_sensor(void);
float humi_sensor(void);
float pres_sensor(void);
unsigned int check_sensors(void);
bool co2_failed(void); // the sensor found at boot delivers no new readings (clears co2_valid until it does)
void co2_set_asc(bool on);
void lps22_offset_check(void); // clears the pressure offset register if it is set, and reports it
bool co2_force_recalibration(void); // sets the current reading to 400 ppm; false = the sensor refused
void print_measurements(void);
void print_sensor_info(void);
void show_data(void);
const char *fmt1(char *buf, float value); // value with one decimal like "%.1f", buf >= 16 bytes

#endif
