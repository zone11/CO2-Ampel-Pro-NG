#include "serial_console.h"

#include "app_globals.h"
#include "led_control.h"
#include "mqtt_service.h"
#include "sensors.h"
#include "settings_storage.h"
#include "wifi_manager.h"

#include "serial_settings.h"

#include <ctype.h>

static bool on_save_settings(void *user)
{
  (void)user;
  settings.valid = true;
  settings_write(&settings);
  return true;
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
  { "led.color.t1",     CFG_COLOR, &settings.color_t1,       0, 0xFFFFFF, 0, NULL },
  { "led.color.t2",     CFG_COLOR, &settings.color_t2,       0, 0xFFFFFF, 0, NULL },
  { "led.color.t3",     CFG_COLOR, &settings.color_t3,       0, 0xFFFFFF, 0, NULL },
  { "led.color.t4",     CFG_COLOR, &settings.color_t4,       0, 0xFFFFFF, 0, NULL },
  { "wifi.ssid",        CFG_STRING, settings.wifi_ssid,      0, 0, sizeof(settings.wifi_ssid) - 1, NULL },
  { "wifi.pass",        CFG_STRING, settings.wifi_code,      0, 0, sizeof(settings.wifi_code) - 1, NULL },
  { "mqtt.enabled",     CFG_BOOL,  &settings.mqtt_enabled,   0, 0, 0, NULL },
  { "mqtt.broker",      CFG_STRING, settings.mqtt_broker,    0, 0, sizeof(settings.mqtt_broker) - 1, NULL },
  { "mqtt.port",        CFG_U32,   &settings.mqtt_port,      1, 65535, 0, NULL },
  { "mqtt.user",        CFG_STRING, settings.mqtt_user,      0, 0, sizeof(settings.mqtt_user) - 1, NULL },
  { "mqtt.pass",        CFG_STRING, settings.mqtt_pass,      0, 0, sizeof(settings.mqtt_pass) - 1, NULL },
  { "mqtt.client_id",   CFG_STRING, settings.mqtt_client_id, 0, 0, sizeof(settings.mqtt_client_id) - 1, NULL },
  { "mqtt.topic_prefix",CFG_STRING, settings.mqtt_topic_prefix, 0, 0, sizeof(settings.mqtt_topic_prefix) - 1, NULL },
  { "mqtt.interval",    CFG_U32,   &settings.mqtt_interval,  10, 3600, 0, NULL },
};
static const size_t settings_items_count = sizeof(settings_items) / sizeof(settings_items[0]);

void serial_service(void)
{
  char line_buf[192];
  size_t len;

  if((features & FEATURE_USB) == 0)
  {
    return;
  }

  if(Serial.available() == 0)
  {
    return;
  }

  len = Serial.readBytesUntil('\n', line_buf, sizeof(line_buf) - 1);
  line_buf[len] = 0;
  if(len > 0 && line_buf[len - 1] == '\r')
  {
    line_buf[len - 1] = 0;
  }

  char *line = line_buf;
  while(*line && isspace((unsigned char)*line))
  {
    line++;
  }

  if(*line == 0)
  {
    return;
  }

  if(strcasecmp(line, "remote on") == 0)
  {
    remote_on = 1;
    buzzer(0);
    ws2812.setBrightness(30);
    leds(COLOR_MENU);
    Serial.println("OK");
    return;
  }
  if(strcasecmp(line, "remote off") == 0)
  {
    remote_on = 0;
    ws2812.setBrightness(settings.brightness);
    Serial.println("OK");
    return;
  }
  if(strcasecmp(line, "version") == 0)
  {
    Serial.println(VERSION);
    return;
  }
  if(strcasecmp(line, "status") == 0)
  {
    print_measurements();
    print_wifi_status();
    print_mqtt_status();
    return;
  }
  if(strcasecmp(line, "reset") == 0)
  {
    if(remote_on)
    {
      Serial.println("OK");
      leds(0);
      Serial.flush();
      Serial.end();
      delay(20);
    }
    NVIC_SystemReset();
    while(1);
  }

  serial_settings_ctx_t ctx;
  ctx.user = NULL;
  ctx.remote_on = (remote_on != 0);
  ctx.out = &Serial;
  ctx.on_save = on_save_settings;
  if(serial_settings_handle_line(line, settings_items, settings_items_count, &ctx))
  {
    return;
  }

  Serial.println("ERROR: Unknown command");
}
