#ifndef I2C_UTILS_H
#define I2C_UTILS_H

#include <Arduino.h>
#include <Wire.h>

int check_i2c(Sercom *sercom, byte addr); //1=okay
void i2c_scan(TwoWire &wire, const char *name, Print &out); //list responding addresses + chip IDs

#endif
