#include "system_control.h"

#include "app_globals.h"
#include "led_control.h"

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

extern "C" void WDT_Handler(void)
{
  WDT->INTFLAG.reg = WDT_INTFLAG_EW;
  if(wdt_ticks < (WATCHDOG_TIMEOUT / WATCHDOG_EW_S))
  {
    wdt_ticks++;
    WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY; //the last clear was 2 s ago, so no sync is pending
  }
}

const char *reset_cause(void)
{
  uint8_t rc = PM->RCAUSE.reg;
  if(rc & PM_RCAUSE_WDT)                     { return "watchdog"; }
  if(rc & PM_RCAUSE_SYST)                    { return "software"; }
  if(rc & PM_RCAUSE_EXT)                     { return "reset pin"; }
  if(rc & (PM_RCAUSE_BOD12 | PM_RCAUSE_BOD33)) { return "brown-out"; }
  if(rc & PM_RCAUSE_POR)                     { return "power-on"; }
  return "unknown";
}
