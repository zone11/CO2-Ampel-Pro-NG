#include "system_control.h"

#include "app_globals.h"
#include "led_control.h"
#include "mqtt_service.h"

#include <Wire.h>

#define WATCHDOG_EW_S  2 //early warning interval in s (WDT clock 1024 Hz, EWOFFSET 2K)

static volatile unsigned int wdt_ticks = 0; //early warnings since the last watchdog_feed()

void reset_mcu(void)
{
  if(features & FEATURE_USB)
  {
    Serial.println("Reset...");
  }

  status_led(0);
  buzzer(0);
  ws2812.setBrightness(BRIGHTNESS_DARK); //dark
  leds(COLOR_WHITE); //LEDs white

  if(features & FEATURE_WINC1500)
  {
    mqtt_shutdown();
    WiFi.end(); //WiFi.disconnect();
  }
  if(features & FEATURE_SCD30)
  {
    scd30.stopPeriodicMeasurement();
  }
  if(features & FEATURE_SCD4X)
  {
    scd4x.stopPeriodicMeasurement();
  }

  Wire.end();
  Wire1.end();
  Serial.end();

  __disable_irq(); //disable interrupts
  NVIC_SystemReset(); //reset
  while(1);
}

// Reset record: .noinit is neither copied nor zeroed at startup, so it keeps its content over a reset
#define RECORD_MAGIC 0x44494147 //"DIAG"

typedef struct
{
  uint32_t magic, resets, uptime, event, phase, pc, lr, led, check;
} RESET_RECORD;

static RESET_RECORD record __attribute__((section(".noinit")));
static RESET_INFO last;
volatile uint8_t sys_phase = PHASE_BOOT;

static const char *const phase_names[PHASE_COUNT] =
{
  "boot", "loop", "serial", "web", "wifi", "wifi.connect",
  "mqtt.loop", "mqtt.connect", "mqtt.publish", "mqtt.cmd",
  "co2", "pressure", "light", "leds",
};

const char *phase_name(unsigned int p)
{
  return (p < PHASE_COUNT) ? phase_names[p] : "unknown";
}

const char *event_name(uint32_t e)
{
  return (e == EVENT_HANG) ? "hang" : (e == EVENT_FAULT) ? "fault" : "none";
}

static uint32_t record_check(const RESET_RECORD *r)
{
  return r->magic ^ r->resets ^ r->uptime ^ r->event ^ r->phase ^ r->pc ^ r->lr ^ r->led ^ 0x5AA55AA5;
}

// Uptime in s, counted from millis() steps so it doesn't wrap after 49 days; called from the
// WDT interrupt every 2 s and from uptime_s() with interrupts off
static uint32_t up_s = 0, up_ms = 0;
static uint32_t uptime_update(void)
{
  uint32_t d = (millis() - up_ms) / 1000;
  up_s += d;
  up_ms += d * 1000;
  return up_s;
}

uint32_t uptime_s(void)
{
  __disable_irq();
  uint32_t s = uptime_update();
  __enable_irq();
  return s;
}

void reset_info_init(void)
{
  uint8_t rc = PM->RCAUSE.reg;
  bool cold = (rc & (PM_RCAUSE_POR | PM_RCAUSE_BOD12 | PM_RCAUSE_BOD33)) != 0;

  last.valid = !cold && (record.magic == RECORD_MAGIC) && (record.check == record_check(&record));
  if(last.valid)
  {
    last.event  = record.event;
    last.phase  = record.phase;
    last.pc     = record.pc;
    last.lr     = record.lr;
    last.uptime = record.uptime;
    last.led    = record.led;
  }
  record.resets = last.valid ? record.resets + 1 : 0;
  record.magic  = RECORD_MAGIC;
  record.uptime = 0;
  record.event  = EVENT_NONE;
  record.phase  = PHASE_BOOT;
  record.pc     = 0;
  record.lr     = 0;
  record.led    = 0;
  record.check  = record_check(&record);
}

bool reset_unplanned(void)
{
  uint8_t rc = PM->RCAUSE.reg;
  return last.valid && ((rc & PM_RCAUSE_WDT) || ((rc & PM_RCAUSE_SYST) && (last.event == EVENT_FAULT)));
}

void reset_record_led(uint32_t led)
{
  __disable_irq(); //the WDT interrupt also writes the record
  record.led = led;
  record.check = record_check(&record);
  __enable_irq();
}

const RESET_INFO *last_run(void)
{
  return &last;
}

uint32_t reset_count(void)
{
  return record.resets;
}

// Saves the interrupted code address from the exception frame (r0-r3, r12, lr, pc, xpsr)
static void record_event(uint32_t event, const uint32_t *frame)
{
  bool ok = ((uint32_t)frame >= 0x20000000) && ((uint32_t)frame <= (0x20008000 - 32));
  record.event = event;
  record.phase = sys_phase;
  record.pc = ok ? frame[6] : 0;
  record.lr = ok ? frame[5] : 0;
  record.uptime = uptime_update();
  record.check = record_check(&record);
}

void watchdog_init(void)
{
  //GCLK2 = OSCULP32K / 32 = 1024 Hz for the WDT
  GCLK->GENDIV.reg = GCLK_GENDIV_ID(2) | GCLK_GENDIV_DIV(4); //DIVSEL: 2^(4+1)
  GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(2) | GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_OSCULP32K | GCLK_GENCTRL_DIVSEL;
  while(GCLK->STATUS.bit.SYNCBUSY);
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_WDT | GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK2;

  WDT->CTRL.reg = 0;
  while(WDT->STATUS.bit.SYNCBUSY);
  WDT->CONFIG.reg = WDT_CONFIG_PER_4K;        //reset after 4 s without a clear
  WDT->EWCTRL.reg = WDT_EWCTRL_EWOFFSET_2K;   //early warning after 2 s
  WDT->INTFLAG.reg = WDT_INTFLAG_EW;
  WDT->INTENSET.reg = WDT_INTENSET_EW;
  NVIC_ClearPendingIRQ(WDT_IRQn);
  NVIC_EnableIRQ(WDT_IRQn);
  wdt_ticks = 0;
  WDT->CTRL.reg = WDT_CTRL_ENABLE;
  while(WDT->STATUS.bit.SYNCBUSY);
}

void watchdog_feed(void)
{
  wdt_ticks = 0;
}

// Exception entry: passes the stack with the exception frame (MSP or PSP) to the C handler
#define FRAME_HANDLER(handler) \
  __asm volatile( \
    "movs r0, #4    \n" \
    "mov  r1, lr    \n" \
    "tst  r0, r1    \n" \
    "mrs  r0, msp   \n" \
    "beq  1f        \n" \
    "mrs  r0, psp   \n" \
    "1:             \n" \
    "ldr  r1, =" #handler " \n" \
    "bx   r1        \n" \
    ".align 2       \n" \
    ".ltorg         \n")

extern "C" __attribute__((used)) void wdt_handler_c(uint32_t *frame)
{
  WDT->INTFLAG.reg = WDT_INTFLAG_EW;
  if(wdt_ticks < (WATCHDOG_TIMEOUT / WATCHDOG_EW_S))
  {
    wdt_ticks++;
    WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY; //the last clear was 2 s ago, so no sync is pending
    record.phase = sys_phase;
    record.uptime = uptime_update();
    record.check = record_check(&record);
  }
  else if(record.event == EVENT_NONE)
  {
    record_event(EVENT_HANG, frame); //no clear: the WDT resets in 2 s
  }
}

extern "C" __attribute__((naked)) void WDT_Handler(void)
{
  FRAME_HANDLER(wdt_handler_c);
}

extern "C" __attribute__((used)) void fault_handler_c(uint32_t *frame)
{
  record_event(EVENT_FAULT, frame);
  NVIC_SystemReset();
  while(1);
}

extern "C" __attribute__((naked)) void HardFault_Handler(void)
{
  FRAME_HANDLER(fault_handler_c);
}

const char *reset_cause(void)
{
  uint8_t rc = PM->RCAUSE.reg;
  if(rc & PM_RCAUSE_WDT)                     { return "watchdog"; }
  if(rc & PM_RCAUSE_SYST)                    { return (last.valid && (last.event == EVENT_FAULT)) ? "fault" : "software"; }
  if(rc & PM_RCAUSE_EXT)                     { return "reset pin"; }
  if(rc & (PM_RCAUSE_BOD12 | PM_RCAUSE_BOD33)) { return "brown-out"; }
  if(rc & PM_RCAUSE_POR)                     { return "power-on"; }
  return "unknown";
}
