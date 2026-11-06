#include "calibration.h"
#include "errors.h"
#include "health.h"
#include "lora_service.h"
#include "system_control.h"

#include <Arduino.h>
#include <malloc.h>

#define RAM_PATTERN 0xA5A5A5A5
#define RAM_LOW_BYTES  4096
#define LOOP_SLOW_MS   20000UL
#define I2C_REPEATS    3 //per 10 minutes
#define CONNECT_REPEATS 3 //per hour

extern "C" char *sbrk(int incr);

HEALTH_COUNTERS health = { 0, 0, 0, 0, 0, 0 };

static uint32_t loop_last = 0, loop_max = 0; //ms

static uint32_t *heap_top(void)
{
  return (uint32_t *)(((uint32_t)sbrk(0) + 3) & ~3UL);
}

void health_init(void)
{
  //free RAM between heap and stack; the stack (or a growing heap) overwrites the pattern
  uint32_t *end = (uint32_t *)__get_MSP() - 16; //keep the current stack frame
  for(uint32_t *p = heap_top(); p < end; p++)
  {
    *p = RAM_PATTERN;
  }
}

void health_loop(void)
{
  uint32_t now = millis();
  if((loop_last != 0) && ((now - loop_last) > loop_max))
  {
    loop_max = now - loop_last;
  }
  if((loop_last != 0) && ((now - loop_last) > LOOP_SLOW_MS))
  {
    error_event(ERR_LOOP_SLOW);
  }
  loop_last = now;
}

static uint32_t ram_free_min(void);

// Once per second from loop(). shortcut: fixed windows, so repeats across a window edge can be missed; fine for "keeps happening"
void health_error_check(void)
{
  static uint32_t ticks = 0, i2c_seen = 0, wifi_seen = 0, mqtt_seen = 0, net_seen = 0;

  ticks++;
  if(health.net_write_timeouts != net_seen)
  {
    net_seen = health.net_write_timeouts;
    error_event(ERR_NET_WRITE_TIMEOUT);
  }
  if((ticks % 60) == 0)
  {
    error_set(ERR_RAM_LOW, ram_free_min() < RAM_LOW_BYTES);
  }
  if((ticks % 600) == 0)
  {
    if((health.i2c_errors - i2c_seen) >= I2C_REPEATS)
    {
      error_event(ERR_I2C_BUS);
    }
    i2c_seen = health.i2c_errors;
  }
  if((ticks % 3600) == 0)
  {
    if((health.wifi_connects - wifi_seen) >= CONNECT_REPEATS)
    {
      error_event(ERR_WIFI_RECONNECTS);
    }
    if((health.mqtt_connects - mqtt_seen) >= CONNECT_REPEATS)
    {
      error_event(ERR_MQTT_RECONNECTS);
    }
    wifi_seen = health.wifi_connects;
    mqtt_seen = health.mqtt_connects;
  }
}

// Free RAM never touched since boot: pattern words above the heap top
static uint32_t ram_free_min(void)
{
  uint32_t *end = (uint32_t *)__get_MSP();
  uint32_t *p = heap_top();
  while((p < end) && (*p == RAM_PATTERN))
  {
    p++;
  }
  return (uint32_t)p - (uint32_t)heap_top();
}

uint32_t health_ram_free(void)
{
  return __get_MSP() - (uint32_t)heap_top();
}

size_t health_json(char *buf, size_t size)
{
  const RESET_INFO *r = last_run();
  struct mallinfo mi = mallinfo();
  uint32_t ram_free = health_ram_free();
  char cal[48];
  if(!calibration_status(cal, sizeof(cal)))
  {
    strcpy(cal, "none");
  }

  int n = snprintf(buf, size,
    "{\"uptime\":%lu,\"resets\":%lu,\"reset\":\"%s\","
    "\"last_event\":\"%s\",\"last_phase\":\"%s\",\"last_pc\":\"0x%08lX\",\"last_lr\":\"0x%08lX\",\"last_uptime\":%lu,"
    "\"ram_free\":%lu,\"ram_free_min\":%lu,\"heap_free\":%lu,\"loop_max_ms\":%lu,"
    "\"wifi_connects\":%lu,\"mqtt_connects\":%lu,\"mqtt_failures\":%lu,\"mqtt_error\":%d,\"i2c_errors\":%lu,\"net_write_timeouts\":%lu,\"co2_failures\":%lu,\"pressure_offset\":%lu,\"calibration\":\"%s\",",
    (unsigned long)uptime_s(), (unsigned long)reset_count(), reset_cause(),
    r->valid ? event_name(r->event) : "none", r->valid ? phase_name(r->phase) : "none",
    (unsigned long)(r->valid ? r->pc : 0), (unsigned long)(r->valid ? r->lr : 0),
    (unsigned long)(r->valid ? r->uptime : 0),
    (unsigned long)ram_free, (unsigned long)ram_free_min(), (unsigned long)mi.fordblks, (unsigned long)loop_max,
    (unsigned long)health.wifi_connects, (unsigned long)health.mqtt_connects,
    (unsigned long)health.mqtt_failures, health.mqtt_error, (unsigned long)health.i2c_errors,
    (unsigned long)health.net_write_timeouts, (unsigned long)health.co2_failures, (unsigned long)health.pressure_offset, cal);
  if((n < 0) || ((size_t)n >= size))
  {
    return (n < 0) ? 0 : size - 1;
  }
  n += lora_json(buf + n, size - n);
  static const char key[] = "\"errors\":"; //errors last: it truncates itself when short
  if((n + sizeof(key) + 16) >= size) //no room for the list: close after the last value (it ends with a comma)
  {
    buf[n - 1] = '}';
    return n;
  }
  memcpy(buf + n, key, sizeof(key));
  n += sizeof(key) - 1;
  n += errors_json(buf + n, size - n - 1);
  buf[n++] = '}';
  buf[n] = 0;
  return n;
}
