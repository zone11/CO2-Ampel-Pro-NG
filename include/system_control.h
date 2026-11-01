#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

void reset_mcu(void);

// Watchdog: the hardware WDT (4 s) is cleared from its early warning interrupt as long as
// watchdog_feed() was called within WATCHDOG_TIMEOUT s; a hung main loop resets after that time,
// stuck interrupts (or a HardFault) after 4 s.
void watchdog_init(void);
void watchdog_feed(void);

// Cause of the last reset: "power-on", "watchdog", "software", "reset pin", "brown-out" or "unknown"
const char *reset_cause(void);

#endif
