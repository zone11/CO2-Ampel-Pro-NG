#include "webserver.h"
#include "sensors.h"
#include "settings_registry.h"
#include "wifi_manager.h"
#include "health.h"
#include "mqtt_service.h"
#include "system_control.h"
#include "calibration.h"

#include <ctype.h>

#if WITH_WEB

#define CONFIG_BODY_MAX 3072 //max. POST to /config/... (form or password + JSON)
#define CONFIG_JSON_MAX 2048 //JSON built from the form

static void urldecode(char *src);
static bool restart_pending = false; //restart after the reply has been sent

// sys.nobuttons (public areas): falsified readings must need a password
static bool calibration_allowed(void)
{
  return !settings.nobuttons || (settings.password[0] != 0);
}

// Writes text into a fixed buffer (for the JSON import reply on the page)
class BufPrint : public Print
{
public:
  BufPrint(char *b, size_t n) : buf(b), size(n) { buf[0] = 0; }
  size_t write(uint8_t c) override
  {
    if(len < (size - 1))
    {
      buf[len++] = (char)c;
      buf[len] = 0;
    }
    return 1;
  }
  size_t length(void) const { return len; }
private:
  char *buf;
  size_t size;
  size_t len = 0;
};

// Collects output and sends it in larger blocks (each client.print() is its own TCP packet)
class ChunkPrint : public Print
{
public:
  ChunkPrint(WiFiClient &c) : client(c) {}
  ~ChunkPrint() { flush(); }
  size_t write(uint8_t c) override
  {
    buf[len++] = (char)c;
    if(len == sizeof(buf))
    {
      flush();
    }
    return 1;
  }
  void flush(void) override
  {
    if(len > 0)
    {
      if(!client.getWriteError()) //after a write timeout: don't wait again for every block of the page
      {
        client.write((const uint8_t *)buf, len);
      }
      len = 0;
    }
  }
private:
  WiFiClient &client;
  char buf[512] = {};
  size_t len = 0;
};

// HTML-safe output of user/device text; attr additionally escapes quotes (for value='...')
static void print_html(Print &out, const char *text, bool attr = false)
{
  for(; *text; text++)
  {
    if(*text == '<')                     { out.print("&lt;"); }
    else if(*text == '>')                { out.print("&gt;"); }
    else if(*text == '&')                { out.print("&amp;"); }
    else if(attr && (*text == '\''))     { out.print("&#39;"); }
    else if(attr && (*text == '"'))      { out.print("&quot;"); }
    else                                 { out.print(*text); }
  }
}

static char *read_body(WiFiClient &client, size_t content_length)
{
  if((content_length == 0) || (content_length >= CONFIG_BODY_MAX))
  {
    return NULL;
  }
  char *body = (char *)malloc(content_length + 1);
  if(body == NULL)
  {
    return NULL;
  }
  size_t n = 0;
  unsigned long t = millis();
  while((n < content_length) && client.connected() && ((millis() - t) < 3000UL))
  {
    if(client.available())
    {
      body[n++] = client.read();
    }
  }
  if(n < content_length) //never act on a partial form
  {
    free(body);
    return NULL;
  }
  body[n] = 0;
  return body;
}

#define FORM_MAX_FIELDS 40

typedef struct
{
  char *name[FORM_MAX_FIELDS];
  char *value[FORM_MAX_FIELDS];
  size_t count;
} form_t;

// Splits an urlencoded body (name=value&...) in place and decodes names and values
static void form_parse(char *body, form_t *form)
{
  form->count = 0;
  char *p = body;
  while((p != NULL) && (*p != 0) && (form->count < FORM_MAX_FIELDS))
  {
    char *next = strchr(p, '&');
    if(next != NULL)
    {
      *next++ = 0;
    }
    char *eq = strchr(p, '=');
    if(eq != NULL)
    {
      *eq = 0;
      urldecode(p);
      urldecode(eq + 1);
      form->name[form->count] = p;
      form->value[form->count] = eq + 1;
      form->count++;
    }
    p = next;
  }
}

static const char *form_value(const form_t *form, const char *name)
{
  for(size_t i = 0; i < form->count; i++)
  {
    if(strcmp(form->name[i], name) == 0)
    {
      return form->value[i];
    }
  }
  return NULL;
}

static void print_field(Print &out, const cfg_item_t *item)
{
  char val[72];
  BufPrint vb(val, sizeof(val));

  out.print("<label for='");
  out.print(item->key);
  out.print("'>");
  out.print(item->key);
  out.print("</label> ");
  switch(item->type)
  {
    case CFG_BOOL:
    {
      bool on = *(bool *)item->ptr;
      out.print("<select id='"); out.print(item->key); out.print("' name='"); out.print(item->key); out.print("'>");
      out.print(on ? "<option value=0>0 off</option><option value=1 selected>1 on</option>"
                   : "<option value=0 selected>0 off</option><option value=1>1 on</option>");
      out.print("</select>");
      break;
    }
    case CFG_COLOR:
    {
      snprintf(val, sizeof(val), "#%06lX", (unsigned long)*(uint32_t *)item->ptr);
      out.print("<input id='"); out.print(item->key); out.print("' name='"); out.print(item->key);
      out.print("' type=color value='"); out.print(val); out.print("'>");
      break;
    }
    case CFG_STRING:
      out.print("<input id='"); out.print(item->key); out.print("' name='"); out.print(item->key);
      out.print("' maxlength="); out.print((unsigned int)item->max_len);
      if(item->flags & CFG_SECRET) //never shown; empty = unchanged
      {
        out.print(" type=password placeholder='(unchanged)'>");
      }
      else
      {
        out.print(" value='"); print_html(out, (const char *)item->ptr, true); out.print("'>");
      }
      break;
    default: //numbers, IP
      cfg_print_value(&vb, item);
      out.print("<input id='"); out.print(item->key); out.print("' name='"); out.print(item->key); out.print("'");
      if((item->type != CFG_IP) && (item->min_val <= item->max_val))
      {
        out.print(" type=number min="); out.print(item->min_val); out.print(" max="); out.print(item->max_val);
      }
      out.print(" value='"); print_html(out, val, true); out.print("'>");
      break;
  }
  out.print("<br/>\r\n");
}

// Page title "CO2-Ampel-NG | <sys.name>[ | <page>]"
static void page_title(Print &out, const char *page)
{
  out.print(PRODUCT_NAME " | ");
  print_html(out, system_name(), false);
  if(page)
  {
    out.print(" | ");
    out.print(page);
  }
}

// HTTP header, HTML head and heading shared by the pages (same style); page = NULL for the main page,
// extra = additional head elements
static void page_head(Print &out, const char *page, const char *extra)
{
  out.print("HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\r\n<html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>\r\n"
            "<title>");
  page_title(out, page);
  out.print("</title>\r\n"
            "<link rel=icon href='data:image/gif;base64,R0lGODlhAQABAAAAACwAAAAAAQABAAA='>\r\n"
            "<style>body{font-family:Lato,sans-serif;padding:10px;max-width:42em}"
            "label{display:inline-block;width:14em}fieldset{margin-bottom:10px}"
            "input,select{margin:2px 0}textarea{width:100%}pre{white-space:pre-wrap}</style>\r\n");
  out.print(extra);
  out.print("</head><body>\r\n<h2>");
  page_title(out, page);
  out.print("</h2>\r\n");
}

// GET /config: all settings as a form (from the key table), JSON upload/download, restart
static void config_page(WiFiClient &client, const char *result)
{
  ChunkPrint out(client);
  bool pw = settings_password_set();

  page_head(out, "Configuration",
            "<script>function pw(f){var p=document.getElementById('pw'),h=f.elements[0];if(p)h.value=p.value;}\r\n"
            "function ld(i){var r=new FileReader();r.onload=function(){document.getElementById('j').value=r.result;};"
            "r.readAsText(i.files[0]);}</script>\r\n");
  if((result != NULL) && result[0])
  {
    out.print("<pre>");
    print_html(out, result);
    out.print("</pre>\r\n");
  }
  if(pw)
  {
    out.print("<p><label for=pw>Password</label> <input id=pw type=password maxlength=32></p>\r\n");
  }

  //settings, grouped by prefix (sys, co2, led, wifi, mqtt)
  out.print("<form method=post action=/config/save onsubmit='pw(this)'><input type=hidden name=_pw>\r\n");
  char group[8] = "";
  for(size_t i = 0; i < settings_item_count(); i++)
  {
    const cfg_item_t *item = settings_item(i);
    size_t g = strcspn(item->key, ".");
    if((g >= sizeof(group)) || (strncmp(group, item->key, g) != 0) || (group[g] != 0))
    {
      if(group[0])
      {
        out.print("</fieldset>\r\n");
      }
      memcpy(group, item->key, (g < sizeof(group)) ? g : sizeof(group) - 1);
      group[(g < sizeof(group)) ? g : sizeof(group) - 1] = 0;
      out.print("<fieldset><legend>");
      out.print(group);
      out.print("</legend>\r\n");
    }
    print_field(out, item);
  }
  out.print("</fieldset>\r\n<input type=submit value='Save'> "
            "(saved immediately; WiFi/MQTT apply after a restart)</form>\r\n");

  //JSON
  out.print("<h3>JSON</h3>\r\n<a href='/config.json'>Download</a> (passwords masked as ***)\r\n");
  if(pw)
  {
    out.print("<form method=post action=/config/export onsubmit='pw(this)'><input type=hidden name=_pw>"
              "<input type=submit value='Download with passwords'></form>\r\n");
  }
  out.print("<form method=post action=/config/import enctype=text/plain onsubmit='pw(this)'><input type=hidden name=p>\r\n"
            "<p>Upload: <input type=file accept='.json,application/json' onchange='ld(this)'></p>\r\n"
            "<textarea id=j name=j rows=8 placeholder='{\"co2.t1\":600, \"led.brightness\":100}'></textarea><br/>\r\n"
            "<input type=submit value='Import'> (all or nothing, saved)</form>\r\n");

  //calibration
  if(calibration_running())
  {
    out.print("<h3>Calibration</h3>\r\n<form method=post action=/config/calabort onsubmit='pw(this)'><input type=hidden name=_pw>"
              "<p>Running, the main page shows the progress.</p>"
              "<input type=submit value='Abort calibration'></form>\r\n");
  }
  else if(calibration_available() && !calibration_allowed())
  {
    out.print("<h3>Calibration</h3>\r\n<p>Set sys.password to start it here.</p>\r\n");
  }
  else if(calibration_available())
  {
    out.print("<h3>Calibration</h3>\r\n<form method=post action=/config/calibrate onsubmit='pw(this)'><input type=hidden name=_pw>"
              "<p>Only with the device in fresh air for at least 2 minutes: it sets the current reading to 400 ppm. "
              "Takes 3 minutes or more; the device stays online.</p>"
              "<input type=submit value='Start calibration'></form>\r\n");
  }

  //restart
  out.print("<h3>Restart</h3>\r\n<form method=post action=/restart onsubmit='pw(this)'><input type=hidden name=_pw>"
            "<input type=submit value='Restart'></form>\r\n"
            "<p><a href='/'>Back</a></p>\r\n</body></html>\r\n");
}

// One line "<label>name</label> value" (value HTML-escaped)
static void row(Print &out, const char *label, const char *value)
{
  out.print("<label>");
  out.print(label);
  out.print("</label> ");
  print_html(out, value);
  out.print("<br/>\r\n");
}

static const char *fmt_duration(char *buf, size_t size, unsigned long s)
{
  if(s >= 86400UL)
  {
    snprintf(buf, size, "%lud %02lu:%02lu:%02lu", s / 86400UL, (s / 3600UL) % 24, (s / 60) % 60, s % 60);
  }
  else
  {
    snprintf(buf, size, "%02lu:%02lu:%02lu", s / 3600UL, (s / 60) % 60, s % 60);
  }
  return buf;
}

// GET /: names as in Home Assistant; readings, WiFi (with the login form), MQTT and system status in sections like /config
static void main_page(WiFiClient &client)
{
  ChunkPrint out(client);
  bool pw = settings_password_set(); //with a password: no SSID, broker or topic on this page
  char v[96], f1[16], f2[16];

  page_head(out, NULL, "<meta http-equiv=refresh content=30>\r\n");
  //measurements: without a CO2 sensor "--" instead of start values; pressure only with a pressure sensor
  out.print("<fieldset><legend>Measurements</legend>\r\n");
  uint32_t shown = ws2812.getPixelColor(0);
  snprintf(v, sizeof(v), "%u ppm", co2_value);
  out.print("<label>CO2</label> ");
  out.print(co2_valid ? v : "--");
  if(shown != 0)
  {
    snprintf(v, sizeof(v), " <span style='color:#%06lX'>&#9679;</span>", (unsigned long)shown);
    out.print(v);
  }
  out.print("<br/>\r\n");
  snprintf(v, sizeof(v), "%s °C", fmt1(f1, temp_value));
  row(out, "Temperature", co2_valid ? v : "--");
  snprintf(v, sizeof(v), "%s %%", fmt1(f1, humi_value));
  row(out, "Humidity", co2_valid ? v : "--");
  if(pres_valid)
  {
    snprintf(v, sizeof(v), "%s hPa", fmt1(f1, pres_value));
    row(out, "Pressure", v);
    snprintf(v, sizeof(v), "%s °C", fmt1(f1, temp2_value));
    row(out, "Pressure sensor temperature", v);
  }
  snprintf(v, sizeof(v), "%u", light_value);
  row(out, "Light", (light_value < 1024) ? v : "--"); //1024 = no reading yet
  out.print("</fieldset>\r\n");

  //WiFi
  int status = WiFi.status();
  IPAddress ip = WiFi.localIP();
  byte mac[6];
  WiFi.macAddress(mac);
  out.print("<fieldset><legend>WiFi</legend>\r\n");
  row(out, "Status", (status == WL_CONNECTED) ? "connected" : (status == WL_AP_LISTENING) ? "access point" : "disconnected");
  if(!pw && (status == WL_CONNECTED))
  {
    row(out, "SSID", WiFi.SSID());
  }
  snprintf(v, sizeof(v), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  row(out, "IP address", v);
  if(status == WL_CONNECTED)
  {
    snprintf(v, sizeof(v), "%ld dBm", (long)WiFi.RSSI());
    row(out, "WiFi signal", v);
  }
  snprintf(v, sizeof(v), "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  row(out, "MAC", v);
  String fv = WiFi.firmwareVersion();
  row(out, "WINC1500 firmware", fv.c_str());
  out.print("</fieldset>\r\n");

  //MQTT
  out.print("<fieldset><legend>MQTT</legend>\r\n");
  if(!settings.mqtt_enabled)
  {
    row(out, "Status", "off");
  }
  else
  {
    const char *err = mqtt_error_text();
    bool connected = mqttClient.connected();
    if(!connected && err[0])
    {
      snprintf(v, sizeof(v), "disconnected (%s)", err);
      row(out, "Status", v);
    }
    else
    {
      row(out, "Status", connected ? "connected" : "disconnected");
    }
    if(!pw)
    {
      snprintf(v, sizeof(v), "%s:%u%s", settings.mqtt_broker, settings.mqtt_port, settings.mqtt_tls ? " (TLS)" : "");
      row(out, "Broker", v);
      char id[16];
      get_device_id(id, sizeof(id));
      snprintf(v, sizeof(v), "%s/%s", settings.mqtt_topic_prefix, id);
      row(out, "Base topic", v);
    }
    long age = mqtt_publish_age();
    if(age >= 0)
    {
      snprintf(v, sizeof(v), "%ld s ago (every %u s)", age, settings.mqtt_interval);
    }
    else
    {
      snprintf(v, sizeof(v), "not yet (every %u s)", settings.mqtt_interval);
    }
    row(out, "Last publish", v);
    snprintf(v, sizeof(v), "%lu / %lu", (unsigned long)health.mqtt_connects, (unsigned long)health.mqtt_failures);
    row(out, "Connects / failures", v);
    if(connected && err[0])
    {
      row(out, "Last error", err);
    }
#if WITH_HA
    row(out, "Home Assistant", settings.mqtt_ha ? "on" : "off");
#endif
#if WITH_MQTT_CONFIG
    row(out, "Remote config", settings.mqtt_config ? "on" : "off (Quiet only)");
#endif
  }
  out.print("</fieldset>\r\n");

  //system
  const RESET_INFO *r = last_run();
  out.print("<fieldset><legend>System</legend>\r\n");
  row(out, "Firmware", VERSION);
  row(out, "Uptime", fmt_duration(v, sizeof(v), uptime_s()));
  row(out, "Quiet", settings.quiet ? "on (LEDs and buzzer off)" : "off");
  row(out, "Reset cause", reset_cause());
  if(r->valid && (r->event != EVENT_NONE))
  {
    snprintf(v, sizeof(v), "%s in %s after %s", event_name(r->event), phase_name(r->phase), fmt_duration(f2, sizeof(f2), r->uptime));
    row(out, "Last reset", v);
  }
  if(calibration_status(v, sizeof(v)))
  {
    row(out, "Calibration", v);
  }
  snprintf(v, sizeof(v), "%lu B", (unsigned long)health_ram_free());
  row(out, "Free RAM", v);
  row(out, "Built with", WITH_HA ? (WITH_MQTT_CONFIG ? "HA, MQTT config" : "HA") : (WITH_MQTT_CONFIG ? "MQTT config" : "no options"));
  out.print("</fieldset>\r\n");

  out.print("<a href='/json'>JSON</a> - <a href='/diag'>Diagnostics</a> - <a href='/config'>Configuration</a> - <a href='https://github.com/zone11/CO2-Ampel-Pro-NG' target=_blank>GitHub</a>\r\n"
            "</body></html>\r\n");
}

// GET /config.json (masked) or POST /config/export (with password: full backup)
static void config_download(WiFiClient &client, bool full)
{
  ChunkPrint out(client);
  byte mac[6];
  char name[40];

  WiFi.macAddress(mac);
  snprintf(name, sizeof(name), "co2ampel-%02X%02X%02X%02X%02X%02X.json", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  out.print("HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Disposition: attachment; filename=");
  out.print(name);
  out.print("\r\nConnection: close\r\n\r\n");
  settings_print_json(&out, !full);
  out.print("\r\n");
}

// Password check (shared lockout); only the error text goes to the page, not the "OK"
static bool password_ok(const char *pw, Print &res)
{
  char msg[80];
  BufPrint m(msg, sizeof(msg));
  if(settings_enable(pw, &m))
  {
    return true;
  }
  res.print(msg);
  return false;
}

// POST /config/save|import|export, /restart: password check, then the action. Returns true if the
// response was already sent (download), otherwise the config page shows result.
static bool config_post(WiFiClient &client, const char *path, size_t content_length, char *result, size_t size)
{
  BufPrint res(result, size);
  char *body = read_body(client, content_length);
  if(body == NULL)
  {
    res.print("{\"result\":\"ERROR\",\"errors\":{\"form\":\"empty, incomplete or too long (max 3 KB)\"}}");
    return false;
  }

  if(strcmp(path, "import") == 0) //text/plain: "p=<password>\r\nj=<json>"
  {
    const char *pw = "";
    char *json = body;
    if(strncmp(body, "p=", 2) == 0)
    {
      pw = body + 2;
      json = strstr(body, "\r\n");
      if(json != NULL)
      {
        *json = 0;
        json += 2;
      }
    }
    if((json == NULL) || (strncmp(json, "j=", 2) != 0))
    {
      res.print("{\"result\":\"ERROR\",\"errors\":{\"json\":\"no JSON\"}}");
    }
    else if(password_ok(pw, res))
    {
      BufPrint reply(result, size);
      if(settings_json_import(json + 2, &reply, true, true))
      {
        settings_save(); //web import is always saved
      }
    }
    free(body);
    return false;
  }

  //urlencoded forms
  form_t form;
  form_parse(body, &form);
  const char *pw = form_value(&form, "_pw");
  if(!password_ok(pw ? pw : "", res)) //always allowed without sys.password
  {
    free(body);
    return false;
  }

  if(strcmp(path, "export") == 0)
  {
    free(body);
    config_download(client, true);
    return true;
  }
  if(strcmp(path, "restart") == 0)
  {
    free(body);
    res.print("{\"result\":\"OK\",\"restart\":true}");
    restart_pending = true;
    return false;
  }

  if(strcmp(path, "calibrate") == 0)
  {
    free(body);
    if(!calibration_allowed())
    {
      res.print("{\"result\":\"ERROR\",\"errors\":{\"calibration\":\"needs sys.password\"}}");
    }
    else if(calibration_running())
    {
      res.print("{\"result\":\"ERROR\",\"errors\":{\"calibration\":\"already running\"}}");
    }
    else if(calibration_available())
    {
      res.print("{\"result\":\"OK\",\"calibration\":true}");
      calibration_start(false);
    }
    else
    {
      res.print("{\"result\":\"ERROR\",\"errors\":{\"calibration\":\"no CO2 sensor\"}}");
    }
    return false;
  }

  if(strcmp(path, "calabort") == 0)
  {
    free(body);
    calibration_abort();
    res.print("{\"result\":\"OK\"}");
    return false;
  }

  if(strcmp(path, "save") != 0)
  {
    res.print("{\"result\":\"ERROR\",\"errors\":{\"form\":\"unknown action\"}}");
    free(body);
    return false;
  }

  //save: form fields -> JSON (all values as strings) -> settings_json_import (all or nothing)
  char *json = (char *)malloc(CONFIG_JSON_MAX);
  if(json == NULL)
  {
    res.print("{\"result\":\"ERROR\",\"errors\":{\"form\":\"out of memory\"}}");
    free(body);
    return false;
  }
  BufPrint jb(json, CONFIG_JSON_MAX);
  jb.print('{');
  bool first = true;
  for(size_t i = 0; i < settings_item_count(); i++)
  {
    const cfg_item_t *item = settings_item(i);
    const char *v = form_value(&form, item->key);
    if((v == NULL) || ((item->flags & CFG_SECRET) && (*v == 0))) //missing or empty password = unchanged
    {
      continue;
    }
    if(!first)
    {
      jb.print(',');
    }
    first = false;
    print_json_string(&jb, item->key);
    jb.print(':');
    print_json_string(&jb, v);
  }
  jb.print('}');
  if(jb.length() >= (CONFIG_JSON_MAX - 1))
  {
    res.print("{\"result\":\"ERROR\",\"errors\":{\"form\":\"too long\"}}");
  }
  else
  {
    BufPrint reply(result, size);
    if(settings_json_import(json, &reply, true, true))
    {
      settings_save();
    }
  }
  free(json);
  free(body);
  return false;
}

static void urldecode(char *src) //decode URL parameters
{
  char a, b, *dst = src;

  while(*src) 
  {
    if((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((uint8_t)a) && isxdigit((uint8_t)b))) 
    {
      if (a >= 'a')
        a -= 'a' - 'A';

      if (a >= 'A')
        a -= 'A' - 10;
      else
        a -= '0';

      if (b >= 'a')
        b -= 'a' - 'A';

      if (b >= 'A')
        b -= 'A' - 10;
      else
        b -= '0';

      *dst++ = 16 * a + b;
      src += 3;
    }
    else if (*src == '+')
    {
      *dst++ = ' ';
      src++;
    }
    else
    {
      *dst++ = *src++;
    }
  }
  *dst++ = '\0';

  return;
}

void webserver_service(void)
{
  unsigned int status;

  if((features & FEATURE_WINC1500) == 0)
  {
    return;
  }

  status = WiFi.status();

  if(status == WL_IDLE_STATUS) //connecting
  {
    return;
  }
  else if((status == WL_CONNECT_FAILED) ||
          (status == WL_CONNECTION_LOST) || 
          (status == WL_DISCONNECTED)) //connection lost: loop() reconnects
  {
    return;
  }

  WiFiClient client = server.available();
  if(!client) //no client
  {
    return;
  }
  //if(features & FEATURE_USB)
  //{
  //  Serial.println("WiFi client connected");
  //}
  boolean currentLineIsBlank=true;
  unsigned int pos=0;
  char buf[640]; //replies of /json, /diag and errors (the pages use ChunkPrint)
  char req[3*64+1]; //HTTP request
  char f1[16], f2[16], f3[16], f4[16]; //readings with one decimal (fmt1)
  char hline[24]; //current header line (only for Content-Length)
  size_t hlen = 0, content_length = 0;
  req[0] = 0;
  unsigned long t_start = millis();
  bool request_started = false;
  while(client.connected())
  {
    //browsers open connections in advance and send nothing: don't stall the loop for those
    if((millis()-t_start) > (request_started ? 5000UL : 1000UL))
    {
      break;
    }
    if(client.available())
    {
      request_started = true;
      char c = client.read();
      if(c == '\n' && currentLineIsBlank) //end of header
      {
        if(strncmp(req, "GET ", 4) && strncmp(req, "POST ", 5)) //not GET or POST
        {
          sprintf(buf, 
              "HTTP/1.1 400 Bad Request\r\n" \
              "Content-Type: text/plain\r\n" \
              "Connection: close\r\n" \
              "\r\n" \
              "400 Bad Request\r\n"
          );
          client.print(buf);
        }
        else if(strncmp(req, "GET /json", 9) == 0) //JSON
        {
          //only real readings (no c/t/h without a CO2 sensor, no p/u without a pressure sensor)
          int n = sprintf(buf,
              "HTTP/1.1 200 OK\r\n" \
              "Content-Type: application/json\r\n" \
              "Connection: close\r\n" \
              "\r\n" \
              "{\r\n");
          if(co2_valid)
          {
            n += sprintf(buf + n, " \"c\": %u,\r\n \"t\": %s,\r\n \"h\": %s,\r\n",
                         co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value));
          }
          if(pres_valid)
          {
            n += sprintf(buf + n, " \"p\": %s,\r\n \"u\": %s,\r\n", fmt1(f3, pres_value), fmt1(f4, temp2_value));
          }
          sprintf(buf + n, " \"l\": %u\r\n}\r\n", light_value);
          client.print(buf);
        }
        else if(strncmp(req, "GET /diag", 9) == 0) //health JSON
        {
          int n = sprintf(buf,
              "HTTP/1.1 200 OK\r\n" \
              "Content-Type: application/json\r\n" \
              "Connection: close\r\n" \
              "\r\n");
          n += health_json(buf + n, sizeof(buf) - n - 2);
          strcpy(buf + n, "\r\n");
          client.print(buf);
        }
        else if(strncmp(req, "GET /config.json", 16) == 0) //settings, passwords masked
        {
          config_download(client, false);
        }
        else if(strncmp(req, "GET /config", 11) == 0)
        {
          config_page(client, NULL);
        }
        else if((strncmp(req, "POST /config/", 13) == 0) || (strncmp(req, "POST /restart", 13) == 0))
        {
          char result[400], path[12];
          const char *p = (req[6] == 'r') ? "restart" : req + 13; //save, import, export
          size_t n = strcspn(p, " ?");
          snprintf(path, sizeof(path), "%.*s", (int)((n < sizeof(path)) ? n : sizeof(path) - 1), p);
          if(!config_post(client, path, content_length, result, sizeof(result)))
          {
            config_page(client, result);
          }
        }
        else if(strncmp(req, "GET /favicon", 12) == 0) //favicon
        {
          sprintf(buf, 
              "HTTP/1.1 404 Not Found\r\n" \
              "Content-Type: text/plain\r\n" \
              "Connection: close\r\n" \
              "\r\n" \
              "404 Not Found\r\n"
          );
          client.print(buf);
        }
        else
        {
          main_page(client);
        }
        break;
      }
      else //save request
      {
        if(pos < (sizeof(req)-1))
        {
          req[pos++] = c;
          req[pos] = 0;
        }
      }
      if(c == '\n')
      {
        hline[hlen] = 0;
        if(strncasecmp(hline, "Content-Length:", 15) == 0)
        {
          content_length = (size_t)atol(hline + 15);
        }
        hlen = 0;
        currentLineIsBlank = true;
      }
      else if(c != '\r')
      {
        if(hlen < (sizeof(hline) - 1))
        {
          hline[hlen++] = c;
        }
        currentLineIsBlank = false;
      }
    }
  }

  delay(20); //give the client time to receive
  client.stop();

  if(restart_pending) //restart button on /config: only after the page has been sent
  {
    mqtt_shutdown();
    delay(500);
    NVIC_SystemReset();
  }

  return;
}

#endif //WITH_WEB
