#include "settings_registry.h"

#include "app_globals.h"
#include "led_control.h"
#include "sensors.h"
#include "serial_settings.h"
#include "settings_storage.h"

#include <stdio.h>

static bool apply_asc(void *user, const cfg_item_t *item)
{
  (void)user;
  (void)item;
  co2_set_asc(settings.asc);
  return true;
}

static bool on_save_settings(void *user)
{
  (void)user;
  return settings_save();
}

static const cfg_item_t settings_items[] =
{
  { "sys.serial_output", CFG_BOOL, &settings.serial_output, 0, 0, 0, NULL },
  { "sys.brightness",   CFG_U32,   &settings.brightness,     0, 255, 0, apply_brightness },
  { "sys.buzzer",       CFG_U32,   &settings.buzzer,         0, 1,   0, NULL },
  { "co2.t1",           CFG_U32,   &settings.range[0],       400, 10000, 0, NULL },
  { "co2.t2",           CFG_U32,   &settings.range[1],       400, 10000, 0, NULL },
  { "co2.t3",           CFG_U32,   &settings.range[2],       400, 10000, 0, NULL },
  { "co2.t4",           CFG_U32,   &settings.range[3],       400, 10000, 0, NULL },
  { "co2.t5",           CFG_U32,   &settings.range[4],       400, 10000, 0, NULL },
  { "co2.asc",          CFG_BOOL,  &settings.asc,            0, 0, 0, apply_asc },
  { "led.color.t1",     CFG_COLOR, &settings.color_t1,       0, 0xFFFFFF, 0, NULL },
  { "led.color.t2",     CFG_COLOR, &settings.color_t2,       0, 0xFFFFFF, 0, NULL },
  { "led.color.t3",     CFG_COLOR, &settings.color_t3,       0, 0xFFFFFF, 0, NULL },
  { "led.color.t4",     CFG_COLOR, &settings.color_t4,       0, 0xFFFFFF, 0, NULL },
  { "wifi.ssid",        CFG_STRING, settings.wifi_ssid,      0, 0, sizeof(settings.wifi_ssid) - 1, NULL, CFG_LOCAL },
  { "wifi.pass",        CFG_STRING, settings.wifi_code,      0, 0, sizeof(settings.wifi_code) - 1, NULL, CFG_SECRET|CFG_LOCAL },
  { "mqtt.enabled",     CFG_BOOL,  &settings.mqtt_enabled,   0, 0, 0, NULL, CFG_LOCAL },
  { "mqtt.broker",      CFG_STRING, settings.mqtt_broker,    0, 0, sizeof(settings.mqtt_broker) - 1, NULL, CFG_LOCAL },
  { "mqtt.port",        CFG_U32,   &settings.mqtt_port,      1, 65535, 0, NULL, CFG_LOCAL },
  { "mqtt.tls",         CFG_BOOL,  &settings.mqtt_tls,       0, 0, 0, NULL, CFG_LOCAL },
  { "mqtt.user",        CFG_STRING, settings.mqtt_user,      0, 0, sizeof(settings.mqtt_user) - 1, NULL, CFG_LOCAL },
  { "mqtt.pass",        CFG_STRING, settings.mqtt_pass,      0, 0, sizeof(settings.mqtt_pass) - 1, NULL, CFG_SECRET|CFG_LOCAL },
  { "mqtt.client_id",   CFG_STRING, settings.mqtt_client_id, 0, 0, sizeof(settings.mqtt_client_id) - 1, NULL },
  { "mqtt.topic_prefix",CFG_STRING, settings.mqtt_topic_prefix, 0, 0, sizeof(settings.mqtt_topic_prefix) - 1, NULL },
  { "mqtt.ha",          CFG_BOOL,  &settings.mqtt_ha,        0, 0, 0, NULL, CFG_LOCAL },
  { "mqtt.config",      CFG_BOOL,  &settings.mqtt_config,    0, 0, 0, NULL, CFG_LOCAL },
  { "mqtt.interval",    CFG_U32,   &settings.mqtt_interval,  10, 3600, 0, NULL },
};
static const size_t settings_items_count = sizeof(settings_items) / sizeof(settings_items[0]);

bool settings_save(void)
{
  settings.valid = true;
  settings_write(&settings);
  return true;
}

bool settings_handle_line(char *line, Print *out, bool remote_on, bool local)
{
  serial_settings_ctx_t ctx;
  ctx.user = NULL;
  ctx.remote_on = remote_on;
  ctx.local = local;
  ctx.out = out;
  ctx.on_save = on_save_settings;
  return serial_settings_handle_line(line, settings_items, settings_items_count, &ctx);
}

// Swallows the handler's reply; "set" answers either "OK" or "ERROR: ..."
class ReplySink : public Print
{
public:
  char first = 0;
  size_t write(uint8_t c) override
  {
    if(first == 0) { first = (char)c; }
    return 1;
  }
};

bool settings_set(const char *key, const char *value)
{
  char line[16 + 32 + 3*64 + 1];
  ReplySink sink;

  if(snprintf(line, sizeof(line), "set %s=%s", key, value) >= (int)sizeof(line))
  {
    return false;
  }
  settings_handle_line(line, &sink, true, true);
  return sink.first == 'O';
}
