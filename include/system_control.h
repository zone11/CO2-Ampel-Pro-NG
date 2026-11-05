#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <stdint.h>

void reset_mcu(void);

// Watchdog: the hardware WDT (4 s) is cleared from its early warning interrupt as long as
// watchdog_feed() was called within the current limit; a hung main loop resets after that time,
// stuck interrupts after 4 s. Before the reset the hung code address is saved (reset record).
// The limit is WATCHDOG_LONG from boot and after watchdog_long(), and WATCHDOG_LOOP after watchdog_loop().
void watchdog_init(void);
void watchdog_feed(void);
void watchdog_long(void); // feed, the next blocking call may take up to WATCHDOG_LONG
void watchdog_loop(void); // feed, back to WATCHDOG_LOOP: once per pass of loop()

// Cause of the last reset: "poweron", "watchdog", "fault" (HardFault), "software", "reset pin",
// "brown-out" or "unknown"
const char *reset_cause(void);

// What the firmware is doing right now (breadcrumb); saved with a hang or fault and every 2 s
typedef enum
{
  PHASE_BOOT, PHASE_LOOP, PHASE_SERIAL, PHASE_WEB, PHASE_WIFI, PHASE_WIFI_CONNECT,
  PHASE_MQTT_LOOP, PHASE_MQTT_CONNECT, PHASE_MQTT_PUBLISH, PHASE_MQTT_CMD,
  PHASE_CO2, PHASE_PRESSURE, PHASE_LIGHT, PHASE_LEDS,
  PHASE_COUNT
} PHASE;

extern volatile uint8_t sys_phase;
static inline void phase_set(PHASE p) { sys_phase = p; }
const char *phase_name(unsigned int p);

// Reset record in RAM that survives every reset except power-on/brown-out
typedef enum { EVENT_NONE, EVENT_HANG, EVENT_FAULT } RESET_EVENT;
typedef struct
{
  bool valid;      // false: power-on/brown-out or record lost
  uint32_t event;  // RESET_EVENT of the previous run
  uint32_t phase;  // last phase of the previous run
  uint32_t pc, lr; // code address at the hang/fault (0 without event)
  uint32_t uptime; // s, uptime of the previous run (2 s resolution)
  uint32_t led;    // LED state of the previous run (led_control: Quiet + button level)
} RESET_INFO;

void reset_info_init(void);         // at boot, before watchdog_init()
const RESET_INFO *last_run(void);   // previous run
uint32_t reset_count(void);         // resets since power-on
uint32_t uptime_s(void);            // s since boot (does not wrap with millis())
const char *event_name(uint32_t e); // "none", "hang", "fault"
bool reset_unplanned(void);         // watchdog or crash (not power-on, reset command, upload, reset pin)
void reset_record_led(uint32_t led);// stores the LED state for the next run

#endif
