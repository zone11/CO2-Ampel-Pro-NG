#ifndef I2C_UTILS_H
#define I2C_UTILS_H

#include <Arduino.h>
#include <Wire.h>

int check_i2c(Sercom *sercom, byte addr); //1=okay

// Frees a bus that a device holds after an MCU reset in the middle of a transfer (SDA stuck low):
// up to 9 SCL pulses, then a STOP. Call before Wire.begin(). Returns true if SDA was stuck.
bool i2c_bus_recover(uint8_t sda, uint8_t scl);
// SCL low timeout on both buses (hardware, 25-35 ms): the Wire library waits without a limit, a device holding
// SCL low would hang the loop. Wire.begin() clears it: call after the sensor init (which calls begin()).
void i2c_timeouts_enable(void);
// Before using a bus: after a timeout, bus error or stuck line count the error and re-init the bus. false = still stuck
bool i2c_wire_ok(void);
bool i2c_wire1_ok(void);
void i2c_scan(TwoWire &wire, const char *name, Print &out); //list responding addresses + chip IDs

#endif
