#include "health.h"
#include "system_control.h"

#include <Arduino.h>
#include <malloc.h>

#define RAM_PATTERN 0xA5A5A5A5

extern "C" char *sbrk(int incr);

HEALTH_COUNTERS health = { 0, 0, 0, 0, 0 };

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
  loop_last = now;
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

  int n = snprintf(buf, size,
    "{\"uptime\":%lu,\"resets\":%lu,\"reset\":\"%s\","
    "\"last_event\":\"%s\",\"last_phase\":\"%s\",\"last_pc\":\"0x%08lX\",\"last_lr\":\"0x%08lX\",\"last_uptime\":%lu,"
    "\"ram_free\":%lu,\"ram_free_min\":%lu,\"heap_free\":%lu,\"loop_max_ms\":%lu,"
    "\"wifi_connects\":%lu,\"mqtt_connects\":%lu,\"mqtt_failures\":%lu,\"mqtt_error\":%d,\"i2c_errors\":%lu}",
    (unsigned long)uptime_s(), (unsigned long)reset_count(), reset_cause(),
    r->valid ? event_name(r->event) : "none", r->valid ? phase_name(r->phase) : "none",
    (unsigned long)(r->valid ? r->pc : 0), (unsigned long)(r->valid ? r->lr : 0),
    (unsigned long)(r->valid ? r->uptime : 0),
    (unsigned long)ram_free, (unsigned long)ram_free_min(), (unsigned long)mi.fordblks, (unsigned long)loop_max,
    (unsigned long)health.wifi_connects, (unsigned long)health.mqtt_connects,
    (unsigned long)health.mqtt_failures, health.mqtt_error, (unsigned long)health.i2c_errors);
  return (n < 0) ? 0 : ((size_t)n < size) ? (size_t)n : size - 1;
}
