#ifndef I2C_UTILS_H
#define I2C_UTILS_H

#include <Arduino.h>
#include <Wire.h>

int check_i2c(Sercom *sercom, byte addr); //1=okay

// Frees a bus that a device holds after an MCU reset in the middle of a transfer (SDA stuck low):
// up to 9 SCL pulses, then a STOP. Call before Wire.begin(). Returns true if SDA was stuck.
bool i2c_bus_recover(uint8_t sda, uint8_t scl);
void i2c_scan(TwoWire &wire, const char *name, Print &out); //list responding addresses + chip IDs

#endif
