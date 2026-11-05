#ifndef UI_MENU_H
#define UI_MENU_H

#include <Arduino.h>
#include "system_control.h"

unsigned int select_value(unsigned int value, unsigned int min, unsigned int max, unsigned int fill, uint32_t color, uint32_t color_off);
void altitude_toffset(void); //altitude and temperature offset
CAL_RESULT calibration(void); //fresh air calibration
bool calibration_available(void); //a CO2 sensor is present
void calibration_run(void); //calibration, then restart (serial, web)
void menu(void);

#endif
