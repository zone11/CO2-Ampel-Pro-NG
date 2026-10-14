#include "settings_registry.h"

#include "app_globals.h"
#include "json_parser.h"
#include "led_control.h"
#include "mqtt_service.h"
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

static bool apply_mqtt_ha(void *user, const cfg_item_t *item)
{
  (void)user;
  (void)item;
  mqtt_ha_update();
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
  { "sys.password",     CFG_STRING, settings.password,      0, 0, sizeof(settings.password) - 1, NULL, CFG_SECRET|CFG_LOCAL },
  { "sys.buzzer",       CFG_U32,   &settings.buzzer,         0, 1,   0, NULL },
  { "co2.t1",           CFG_U32,   &settings.range[0],       400, 10000, 0, NULL },
  { "co2.t2",           CFG_U32,   &settings.range[1],       400, 10000, 0, NULL },
  { "co2.t3",           CFG_U32,   &settings.range[2],       400, 10000, 0, NULL },
  { "co2.t4",           CFG_U32,   &settings.range[3],       400, 10000, 0, NULL },
  { "co2.t5",           CFG_U32,   &settings.range[4],       400, 10000, 0, NULL },
  { "co2.asc",          CFG_BOOL,  &settings.asc,            0, 0, 0, apply_asc },
  { "led.brightness",   CFG_U32,   &settings.brightness,     1, 255, 0, apply_brightness },
  { "led.brightness_dark", CFG_U32, &settings.brightness_dark, 1, 255, 0, apply_brightness },
  { "led.auto",         CFG_BOOL,  &settings.led_auto,       0, 0, 0, apply_brightness },
  { "led.disable",      CFG_BOOL,  &settings.led_disable,    0, 0, 0, apply_led_disable, CFG_CONTROL },
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
#if WITH_HA
  { "mqtt.ha",          CFG_BOOL,  &settings.mqtt_ha,        0, 0, 0, apply_mqtt_ha, CFG_LOCAL },
#endif
#if WITH_MQTT_CONFIG
  { "mqtt.config",      CFG_BOOL,  &settings.mqtt_config,    0, 0, 0, NULL, CFG_LOCAL },
#endif
  { "mqtt.interval",    CFG_U32,   &settings.mqtt_interval,  10, 3600, 0, NULL },
};
static const size_t settings_items_count = sizeof(settings_items) / sizeof(settings_items[0]);

bool settings_save(void)
{
  settings.valid = true;
  settings_write(&settings);
  return true;
}

bool settings_password_set(void)
{
  return settings.password[0] != 0;
}

bool settings_enable(const char *password, Print *out)
{
  static unsigned int fails = 0;
  static unsigned long t_fail = 0, lock_ms = 0;

  if(!settings_password_set())
  {
    if(out) { out->println("OK"); }
    return true;
  }
  if((password == NULL) || (*password == 0))
  {
    if(out) { out->println("ERROR: Password required (enable <password>)"); }
    return false;
  }
  if((fails > 0) && ((millis() - t_fail) < lock_ms))
  {
    if(out)
    {
      out->print("ERROR: Locked after a wrong password, retry in ");
      out->print((lock_ms - (millis() - t_fail) + 999) / 1000);
      out->println(" s");
    }
    return false;
  }

  //compare the full length (run time independent of the position of the first difference)
  uint8_t diff = (strlen(password) >= sizeof(settings.password)) ? 1 : 0;
  for(size_t i = 0; i < sizeof(settings.password); i++)
  {
    char a = (i <= strlen(password)) ? password[i] : 0;
    diff |= (uint8_t)(a ^ settings.password[i]);
    if(settings.password[i] == 0)
    {
      break;
    }
  }
  if(diff == 0)
  {
    fails = 0;
    if(out) { out->println("OK"); }
    return true;
  }

  fails++;
  t_fail = millis();
  lock_ms = (fails < 6) ? (1000UL << (fails - 1)) : 30000UL; //1, 2, 4, 8, 16, 30 s
  if(lock_ms > 30000UL) { lock_ms = 30000UL; }
  if(out) { out->println("ERROR: Wrong password"); }
  return false;
}

bool settings_handle_line(char *line, Print *out, bool remote_on, bool local)
{
  serial_settings_ctx_t ctx;
  ctx.user = NULL;
  ctx.remote_on = remote_on;
  ctx.locked = settings_password_set() && !remote_on;
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

// Collects "key":"message" pairs for the import reply
class JsonPairs : public Print
{
public:
  bool empty(void) const { return len == 0; }
  const char *c_str(void) const { return buf; }
  void add(const char *key, const char *msg)
  {
    if((len + strlen(key) + strlen(msg) + 8) >= sizeof(buf))
    {
      return; //full: drop further messages
    }
    if(len > 0)
    {
      print(',');
    }
    print_json_string(this, key);
    print(':');
    print_json_string(this, msg);
  }
  size_t write(uint8_t c) override
  {
    if(len < (sizeof(buf) - 1))
    {
      buf[len++] = (char)c;
      buf[len] = 0;
    }
    return 1;
  }
private:
  char buf[320] = "";
  size_t len = 0;
};

typedef struct
{
  bool local;
  bool control_only;
  bool save;
  bool do_export;
  const SETTINGS *before;
  JsonPairs errors;
  JsonPairs warnings;
} import_ctx_t;

static size_t item_size(const cfg_item_t *item)
{
  switch(item->type)
  {
    case CFG_U8:     return 1;
    case CFG_U16:    return 2;
    case CFG_BOOL:   return sizeof(bool);
    case CFG_STRING: return item->max_len + 1;
    case CFG_IP:     return sizeof(IPAddress);
    default:         return 4;
  }
}

static bool import_value(void *user, const char *key, json_type_t type, const char *value)
{
  import_ctx_t *ic = (import_ctx_t *)user;

  if(strchr(key, '.') == NULL) //command words (settings always contain a dot)
  {
    bool on = (type == JSON_BOOL) && (strcmp(value, "true") == 0);
    if(ic->control_only && ((strcmp(key, "save") == 0) || (strcmp(key, "export") == 0)))
    {
      ic->errors.add(key, "Configuration over MQTT is off (mqtt.config)");
    }
    else if(strcmp(key, "save") == 0)   { ic->save = on; }
    else if(strcmp(key, "export") == 0) { ic->do_export = on; }
    else                                { ic->warnings.add(key, "Unknown key"); }
    return true;
  }

  const cfg_item_t *item = cfg_find_item(settings_items, settings_items_count, key);
  if(item == NULL)
  {
    ic->warnings.add(key, "Unknown key"); //e.g. an export of a newer firmware
    return true;
  }
  if(ic->control_only && !(item->flags & CFG_CONTROL))
  {
    ic->errors.add(key, "Configuration over MQTT is off (mqtt.config)");
    return true;
  }
  if((item->flags & CFG_SECRET) && (strcmp(value, "***") == 0))
  {
    return true; //masked value from an export: leave unchanged
  }
  if((type == JSON_NULL) || ((type == JSON_BOOL) && (item->type != CFG_BOOL)))
  {
    ic->errors.add(key, "Invalid value");
    return true;
  }

  const char *err = cfg_set_value(item, value, type == JSON_NUMBER, NULL);
  if(err != NULL)
  {
    ic->errors.add(key, err);
    return true;
  }
  if((item->flags & CFG_LOCAL) && !ic->local)
  {
    size_t off = (const uint8_t *)item->ptr - (const uint8_t *)&settings;
    if(memcmp(item->ptr, (const uint8_t *)ic->before + off, item_size(item)) != 0)
    {
      ic->errors.add(key, "Only via serial or web"); //unchanged is allowed (restoring an export)
    }
  }
  return true;
}

bool settings_json_import(const char *json, Print *out, bool local, bool enabled, bool control_only)
{
  SETTINGS before = settings;
  import_ctx_t ic;
  char err[64];

  ic.local = local;
  ic.control_only = control_only;
  ic.save = false;
  ic.do_export = false;
  ic.before = &before;

  if(!enabled)
  {
    ic.errors.add("enable", "Not enabled (send 'enable' first)");
  }
  else if(!json_parse(json, import_value, &ic, err, sizeof(err)))
  {
    ic.errors.add("json", err);
  }

  bool ok = ic.errors.empty();
  if(!ok) //all or nothing: restore the previous state
  {
    bool asc_changed = (settings.asc != before.asc);
    bool led_changed = (settings.led_disable != before.led_disable);
    bool ha_changed = (settings.mqtt_ha != before.mqtt_ha);
    settings = before;
    if(asc_changed)
    {
      co2_set_asc(settings.asc);
    }
    if(led_changed)
    {
      apply_led_disable(NULL, NULL);
    }
    if(ha_changed)
    {
      mqtt_ha_update();
    }
    apply_brightness(NULL, NULL);
  }
  else if(ic.save)
  {
    settings_save();
  }

  if(out)
  {
    out->print(ok ? "{\"result\":\"OK\"" : "{\"result\":\"ERROR\",\"errors\":{");
    if(!ok)
    {
      out->print(ic.errors.c_str());
      out->print('}');
    }
    else if(ic.save)
    {
      out->print(",\"saved\":true");
    }
    if(!ic.warnings.empty())
    {
      out->print(",\"warnings\":{");
      out->print(ic.warnings.c_str());
      out->print('}');
    }
    if(ic.do_export && ok)
    {
      serial_settings_ctx_t ctx;
      ctx.user = NULL;
      ctx.remote_on = true;
      ctx.locked = false;
      ctx.local = local;
      ctx.out = out;
      ctx.on_save = NULL;
      out->print(",\"config\":");
      cfg_print_json(settings_items, settings_items_count, &ctx);
    }
    out->println('}');
  }
  return ok;
}

size_t settings_item_count(void)
{
  return settings_items_count;
}

const cfg_item_t *settings_item(size_t index)
{
  return (index < settings_items_count) ? &settings_items[index] : NULL;
}

void settings_print_json(Print *out, bool mask_secrets)
{
  serial_settings_ctx_t ctx;
  ctx.user = NULL;
  ctx.remote_on = true;
  ctx.locked = false;
  ctx.local = !mask_secrets;
  ctx.out = out;
  ctx.on_save = NULL;
  cfg_print_json(settings_items, settings_items_count, &ctx);
}
