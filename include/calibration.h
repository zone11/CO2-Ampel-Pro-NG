#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <Arduino.h>

// Fresh air calibration in the background: loop() passes every new reading on, so web and MQTT keep running
bool calibration_available(void);       // a CO2 sensor is present
void calibration_start(bool menu, bool force); // menu: LEDs and beep also with Quiet; force: sets the sensor
                                        // also after an unstable or out-of-range run (badly drifted sensor)
void calibration_abort(void);
bool calibration_running(void);
void calibration_reading(void);         // after check_sensors() found a new reading
bool calibration_leds(void);            // the calibration owns the LEDs (running, or result still shown)
bool calibration_status(char *buf, size_t len); // running/result text; false = none since boot

#endif
