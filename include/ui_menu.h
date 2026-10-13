#ifndef UI_MENU_H
#define UI_MENU_H

#include <Arduino.h>

unsigned int select_value(unsigned int value, unsigned int min, unsigned int max, unsigned int fill, uint32_t color, uint32_t color_off);
void altitude_toffset(void); //Altitude und Temperaturoffset
void calibration(void); //Kalibrierung
void menu(void);

#endif
