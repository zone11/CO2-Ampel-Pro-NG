#include "calibration.h"

#include "app_globals.h"
#include "led_control.h"
#include "sensors.h"

#define CALIBRATION_RUNS    3     //a run that stays unstable starts again, up to this many runs
#define CALIBRATION_SECONDS 180
#define RESULT_SHOW_MS      3000

typedef enum { CAL_NONE, CAL_OK, CAL_FAILED, CAL_REFUSED, CAL_ABORTED } CAL_RESULT;

static bool running = false, from_menu = false;
static CAL_RESULT result = CAL_NONE;
static unsigned int runs = 0, cycle = 0, again = 0, co2_last = 0;
static unsigned long t_result = 0;

static unsigned int cycles(void)
{
  return CALIBRATION_SECONDS / ((features & FEATURE_SCD4X) ? 5 : MEASURE_INTERVAL); //SCD4x always 5 s
}

// The "enable" indicator and Quiet keep the LEDs, the service menu always shows the calibration
static bool show(void)
{
  return from_menu || (!settings.quiet && !remote_on);
}

static void finish(CAL_RESULT r, uint32_t color, const char *text)
{
  running = false;
  result = r;
  t_result = millis();
  if(show())
  {
    leds(color);
    if(from_menu || settings.buzzer)
    {
      buzzer(500);
    }
  }
  if(features & FEATURE_USB)
  {
    Serial.println(text);
  }
}

bool calibration_available(void)
{
  return (features & (FEATURE_SCD30 | FEATURE_SCD4X)) != 0;
}

void calibration_start(bool menu)
{
  from_menu = menu;
  running = true;
  result = CAL_NONE;
  runs = cycle = again = 0;
  co2_last = co2_sensor();
  co2_set_asc(settings.asc); //ASC from the settings (co2.asc)
  buzzer(0);
  if(show())
  {
    led_calibration(co2_last);
  }
}

void calibration_abort(void)
{
  if(running)
  {
    running = false;
    result = CAL_ABORTED;
    if(features & FEATURE_USB)
    {
      Serial.println("Calibration aborted");
    }
  }
}

bool calibration_running(void)
{
  return running;
}

void calibration_reading(void)
{
  if(!running)
  {
    return;
  }

  unsigned int co2 = co2_sensor();
  if((co2 >= 200) && (co2 <= 800) &&
     (co2 + 30 >= co2_last) && (co2 <= co2_last + 30)) //+/-30ppm tolerance to the previous value
  {
    cycle++;
    again = 0;
  }
  else //sensor badly calibrated
  {
    again++;
    if(again > 3)
    {
      again = 1;
      cycle++;
    }
  }
  co2_last = co2;

  if(show())
  {
    led_calibration(co2);
  }
  if(features & FEATURE_USB)
  {
    Serial.print("loop: ");
    Serial.println(cycle);
  }

  if(cycle < cycles())
  {
    return;
  }

  if(!co2_force_recalibration()) //an unstable run applies it too, as in the original firmware
  {
    finish(CAL_REFUSED, COLOR_RED, "Calibration failed: the sensor refused the new value");
  }
  else if(again == 0)
  {
    finish(CAL_OK, COLOR_BLUE, "Calibration OK");
  }
  else if(++runs >= CALIBRATION_RUNS)
  {
    finish(CAL_FAILED, COLOR_RED, "Calibration failed: readings not stable");
  }
  else
  {
    if(features & FEATURE_USB)
    {
      Serial.println("Restart calibration");
    }
    cycle = again = 0;
    co2_last = co2_sensor();
  }
}

bool calibration_leds(void)
{
  return running || ((result != CAL_NONE) && (result != CAL_ABORTED) && ((millis() - t_result) < RESULT_SHOW_MS));
}

bool calibration_status(char *buf, size_t len)
{
  if(running)
  {
    snprintf(buf, len, "running, step %u of %u, run %u of %u", cycle, cycles(), runs + 1, CALIBRATION_RUNS);
    return true;
  }
  if(result == CAL_NONE)
  {
    return false;
  }
  snprintf(buf, len, "%s", (result == CAL_OK) ? "OK" : (result == CAL_FAILED) ? "failed, readings not stable" :
           (result == CAL_REFUSED) ? "failed, sensor refused the new value" : "aborted");
  return true;
}
