#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "app_globals.h"
#include "serial_settings.h"

void leds(uint32_t color);
bool apply_brightness(void *user, const cfg_item_t *item);
void status_led(unsigned int on);
void buzzer(unsigned int on);
void ampel(unsigned int co2);

#endif
