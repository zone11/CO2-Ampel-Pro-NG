#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "app_globals.h"
#include "serial_settings.h"

void leds(uint32_t color);

// LED brightness: led.brightness (max), stepped down by the button (not saved), dimmed to
// led.brightness_dark when the light sensor reports darkness and led.auto is on
void led_brightness_update(void);  // applies the current state (call after enable/exit/menu)
void led_set_dark(bool dark);      // light sensor result
void led_button_step(void);        // short button press: halve, wrap back to the maximum
void led_state_restore(void);      // at boot after the settings: led.disable and the button level of the
                                   // previous run after a watchdog reset or crash (saved settings otherwise)
bool apply_brightness(void *user, const cfg_item_t *item);
bool apply_led_disable(void *user, const cfg_item_t *item); // led.disable takes effect immediately
void print_led_status(void);
void status_led(unsigned int on);
void buzzer(unsigned int on);
void traffic_light(unsigned int co2);

#endif
