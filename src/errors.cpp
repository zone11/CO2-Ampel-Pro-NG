#include "errors.h"

#include "system_control.h"

#define ERROR_HOLD 3600 //s an event stays listed

static_assert(ERR_COUNT <= 32, "the list is a 32-bit mask");

static const char *const error_names[ERR_COUNT] =
{
  "co2_missing", "co2_stale", "co2_read_failed", "co2_range", "co2_frozen",
  "temp_range", "humidity_range",
  "pressure_missing", "pressure_stale", "pressure_range", "pressure_offset",
  "i2c_bus", "i2c_stuck", "wifi_module_missing",
  "reset_hang", "reset_fault", "reset_brownout",
  "settings_lost", "settings_newer", "ram_low", "loop_slow",
  "wifi_lost", "wifi_reconnects", "wifi_weak", "setup_network",
  "mqtt_reconnects", "mqtt_connect", "mqtt_publish", "net_write_timeout",
  "cal_refused", "cal_failed",
};

static uint32_t active = 0, written = 0;
static uint32_t event_at[ERR_COUNT]; //uptime + 1, 0 = none

static uint32_t error_mask(void)
{
  uint32_t mask = active;
  uint32_t now = uptime_s();
  for(int i = 0; i < ERR_COUNT; i++)
  {
    if((event_at[i] != 0) && ((now + 1 - event_at[i]) < ERROR_HOLD))
    {
      mask |= 1UL << i;
    }
  }
  return mask;
}

void error_set(ERROR_CODE e, bool on)
{
  if(on)
  {
    active |= 1UL << e;
  }
  else
  {
    active &= ~(1UL << e);
  }
}

void error_event(ERROR_CODE e)
{
  event_at[e] = uptime_s() + 1;
}

bool errors_any(void)
{
  return error_mask() != 0;
}

bool errors_changed(void)
{
  return error_mask() != written;
}

size_t errors_json(char *buf, size_t size)
{
  static const char tail[] = "\"truncated\"]";
  uint32_t mask = error_mask();
  size_t n = 0;

  written = mask;
  if(size < sizeof(tail) + 2)
  {
    return 0;
  }
  buf[n++] = '[';
  bool first = true, cut = false;
  for(int i = 0; i < ERR_COUNT; i++)
  {
    if(mask & (1UL << i))
    {
      size_t len = strlen(error_names[i]) + 3; //quotes and comma
      if((n + len + sizeof(tail) + 1) > size)
      {
        cut = true;
        break;
      }
      n += snprintf(buf + n, size - n, "%s\"%s\"", first ? "" : ",", error_names[i]);
      first = false;
    }
  }
  if(cut)
  {
    n += snprintf(buf + n, size - n, "%s%s", first ? "" : ",", tail);
  }
  else
  {
    buf[n++] = ']';
    buf[n] = 0;
  }
  return n;
}
