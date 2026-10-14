#include "webserver.h"
#include "sensors.h"
#include "settings_registry.h"
#include "wifi_manager.h"

#include <ctype.h>

#if WITH_WEB

#define CONFIG_BODY_MAX 3072 //max. POST to /config/... (form or password + JSON)
#define CONFIG_JSON_MAX 2048 //JSON built from the form

static void urldecode(char *src);
static bool restart_pending = false; //restart after the reply has been sent

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
      client.write((const uint8_t *)buf, len);
      len = 0;
    }
  }
private:
  WiFiClient &client;
  char buf[512];
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

// GET /config: all settings as a form (from the key table), JSON upload/download, restart
static void config_page(WiFiClient &client, const char *result)
{
  ChunkPrint out(client);
  bool pw = settings_password_set();

  out.print("HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!DOCTYPE html>\r\n<html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>\r\n"
            "<title>CO2-Ampel Config</title>\r\n"
            "<style>body{font-family:Lato,sans-serif;padding:10px;max-width:42em}"
            "label{display:inline-block;width:12em}fieldset{margin-bottom:10px}"
            "input,select{margin:2px 0}textarea{width:100%}pre{white-space:pre-wrap}</style>\r\n"
            "<script>function pw(f){var p=document.getElementById('pw'),h=f.elements[0];if(p)h.value=p.value;}\r\n"
            "function ld(i){var r=new FileReader();r.onload=function(){document.getElementById('j').value=r.result;};"
            "r.readAsText(i.files[0]);}</script>\r\n"
            "</head><body>\r\n<h2>CO2-Ampel Konfiguration / configuration</h2>\r\n");
  if((result != NULL) && result[0])
  {
    out.print("<pre>");
    print_html(out, result);
    out.print("</pre>\r\n");
  }
  if(pw)
  {
    out.print("<p><label for=pw>Passwort / password</label> <input id=pw type=password maxlength=32></p>\r\n");
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
  out.print("</fieldset>\r\n<input type=submit value='Speichern / save'> "
            "(sofort gespeichert; WiFi/MQTT nach Neustart / saved now, WiFi/MQTT after a restart)</form>\r\n");

  //JSON
  out.print("<h3>JSON</h3>\r\n<a href='/config.json'>Download</a> (Passwoerter als *** / passwords masked)\r\n");
  if(pw)
  {
    out.print("<form method=post action=/config/export onsubmit='pw(this)'><input type=hidden name=_pw>"
              "<input type=submit value='Download mit Passwoertern / with passwords'></form>\r\n");
  }
  out.print("<form method=post action=/config/import enctype=text/plain onsubmit='pw(this)'><input type=hidden name=p>\r\n"
            "<p>Upload: <input type=file accept='.json,application/json' onchange='ld(this)'></p>\r\n"
            "<textarea id=j name=j rows=8 placeholder='{\"co2.t1\":600, \"led.brightness\":100}'></textarea><br/>\r\n"
            "<input type=submit value='Import'> (alles oder nichts, wird gespeichert / all or nothing, saved)</form>\r\n");

  //restart
  out.print("<h3>Neustart / restart</h3>\r\n<form method=post action=/restart onsubmit='pw(this)'><input type=hidden name=_pw>"
            "<input type=submit value='Neustart / restart'></form>\r\n"
            "<p><a href='/'>Zurueck / back</a></p>\r\n</body></html>\r\n");
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
    res.print("{\"result\":\"ERROR\",\"errors\":{\"form\":\"empty or too long (max 3 KB)\"}}");
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
  static unsigned long t_check=0;
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
          (status == WL_DISCONNECTED)) //connection lost
  {
    if((millis()-t_check) > (1*60000UL)) //1min
    {
      t_check = millis();
      wifi_start();
    }
    return;
  }

  t_check = millis(); //for the reconnect after 1min

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
  char buf[1024];
  char req[3][3*64+1]; //HTTP request / POST values SSID, code, password (URL-encoded, up to 3 chars per byte)
  const char *form_msg = "";
  char f1[16], f2[16], f3[16], f4[16]; //readings with one decimal (fmt1)
  char hline[24]; //current header line (only for Content-Length)
  size_t hlen = 0, content_length = 0;
  req[0][0] = 0;
  while(client.connected())
  {
    if((millis()-t_check) > (5*1000UL)) //stop after 5s
    {
      break;
    }
    if(client.available())
    {
      char c = client.read();
      if(c == '\n' && currentLineIsBlank) //end of header
      {
        if(strncmp(req[0], "GET ", 4) && strncmp(req[0], "POST ", 5)) //not GET or POST
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
        else if(strncmp(req[0], "GET /json", 9) == 0) //JSON
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
        else if(strncmp(req[0], "GET /cmk-agent", 14) == 0) //Checkmk agent
        {
          //This way CO2-Ampel devices can be added directly to the monitoring of checkmk.com
          //without any plugins.
          //As HTTP is the transport, use a "data source":
          //wget -O - http://ip_address/cmk-agent
          //See: https://docs.checkmk.com/latest/en/datasource_programs.html
          int n = sprintf(buf,
              "HTTP/1.1 200 OK\r\n" \
              "Content-Type: text/plain\r\n" \
              "Connection: close\r\n" \
              "\r\n" \
              //plain text in the format Checkmk expects
              //See: https://docs.checkmk.com/latest/en/devel_check_plugins.html
              "<<<check_mk>>>\r\n" \
              "AgentOS: arduino\r\n" \
              //needs a check plugin on the server to evaluate the metrics
              "<<<watterott_co2ampel_plugin>>>\r\n");
          if(co2_valid)
          {
            n += sprintf(buf + n, "co2 %u\r\ntemp %s\r\nhumidity %s\r\n", co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value));
          }
          n += sprintf(buf + n, "lighting %u\r\n", light_value);
          if(pres_valid)
          {
            n += sprintf(buf + n, "pressure %s\r\ntemp2 %s\r\n", fmt1(f3, pres_value), fmt1(f4, temp2_value));
          }
          //ad-hoc check without a server plugin, uses the traffic light thresholds.
          //Note: one line only - the Checkmk server rates the value itself based on
          //the thresholds passed along, which we read from the device here:
          if(co2_valid)
          {
            sprintf(buf + n, "<<<local:sep(0)>>>\r\n" \
                "P \"CO2 level (ppm)\" co2ppm=%u;%u;%u CO2/ventilation control with Watterott CO2-Ampel, thresholds taken from sensor board.\r\n",
                co2_value, settings.range[1], settings.range[2]);
          }
          else
          {
            sprintf(buf + n, "<<<local:sep(0)>>>\r\n" \
                "3 \"CO2 level (ppm)\" - CO2 sensor not found\r\n");
          }
          client.print(buf);
        }
        else if(strncmp(req[0], "GET /config.json", 16) == 0) //settings, passwords masked
        {
          config_download(client, false);
        }
        else if(strncmp(req[0], "GET /config", 11) == 0)
        {
          config_page(client, NULL);
        }
        else if((strncmp(req[0], "POST /config/", 13) == 0) || (strncmp(req[0], "POST /restart", 13) == 0))
        {
          char result[400], path[8];
          const char *p = (req[0][6] == 'r') ? "restart" : req[0] + 13; //save, import, export
          size_t n = strcspn(p, " ?");
          snprintf(path, sizeof(path), "%.*s", (int)((n < sizeof(path)) ? n : sizeof(path) - 1), p);
          if(!config_post(client, path, content_length, result, sizeof(result)))
          {
            config_page(client, result);
          }
        }
        else if(strncmp(req[0], "GET /favicon", 12) == 0) //favicon
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
          //HTTP POST data
          if((strncmp(req[0], "POST ", 5) == 0) && client.available())
          {
            bool too_long = false;
            req[0][0] = 0; //SSID
            req[1][0] = 0; //Code
            req[2][0] = 0; //password (sys.password)
            for(unsigned int r=0, i=0, last_c=0; client.available();)
            {
              c = client.read();
              if(c == '&') //format: 1=xxx&2=yyy
              {
                r = 0;
              }
              else if((c == '=') && isdigit(last_c)) //1=xxx
              {
                r = last_c-'0';
                i = 0;
              }
              else if((r > 0) && (r < 4)) //1 to 3
              {
                if(i < (sizeof(req[0]) - 1))
                {
                  req[r-1][i++] = c;
                  req[r-1][i] = 0;
                }
                else
                {
                  too_long = true; //value too long, not saved
                }
              }
              last_c = c;
            }
            urldecode(req[0]); //Serial.println(req[0]);
            urldecode(req[1]); //Serial.println(req[1]);
            urldecode(req[2]);
            if((strlen(req[0]) >= sizeof(settings.wifi_ssid)) || (strlen(req[1]) >= sizeof(settings.wifi_code)))
            {
              too_long = true;
            }
            if(!settings_enable(req[2], NULL)) //with sys.password: password required
            {
              form_msg = "<b>Falsches Passwort / wrong password</b><br/>\r\n";
            }
            else if(!too_long && (strcmp(req[0], settings.wifi_ssid) || strcmp(req[1], settings.wifi_code)))
            {
              if(settings_set("wifi.ssid", req[0]) && settings_set("wifi.pass", req[1]))
              {
                settings_save();
                form_msg = "<b>Gespeichert / saved</b><br/>\r\n";
              }
            }
          }
          //HTTP header + data
          sprintf(buf,
              "HTTP/1.1 200 OK\r\n" \
              "Content-Type: text/html\r\n" \
              "Connection: close\r\n" \
              "\r\n"
              "<!DOCTYPE html>\r\n" \
              "<html>\r\n" \
              "<head>\r\n" \
              "<meta charset=utf-8>\r\n" \
              "<meta http-equiv=refresh content=120>\r\n" \
              "<title>CO2-Ampel</title>\r\n" \
              "<link rel=icon href=\"data:image/gif;base64,R0lGODlhAQABAAAAACwAAAAAAQABAAA=\">\r\n" \
              "<style>\r\n" \
              "body { font-size:1.0em; font-family:Lato,sans-serif; padding:10px; }\r\n" \
              "#data { font-size:3.0em; }\r\n" \
              "#wifi { font-size:1.0em; display:none; }\r\n" \
              "#info { font-size:0.9em; }\r\n" \
              "</style>\r\n" \
              "<script>\r\n" \
              "function wifi() {\r\n" \
              "var box = document.getElementById('wifi');\r\n" \
              "if(box.style.display != 'block') { box.style.display = 'block'; }\r\n" \
              "else { box.style.display = 'none'; }\r\n" \
              "}\r\n" \
              "</script>\r\n" \
              "</head>\r\n" \
              "<body>\r\n"
          );
          client.print(buf);

          //without a CO2 sensor "--" instead of start values; pressure lines only with a pressure sensor
          char co2_text[12];
          sprintf(co2_text, "%u", co2_value);
          int n = sprintf(buf,
              "<div id=data>\r\n" \
              "CO2 (ppm): %s<br/>\r\n" \
              "Temperatur (&deg;C): %s<br/>\r\n" \
              "Luftfeuchte (%% rel): %s<br/>\r\n",
              co2_valid ? co2_text : "--", co2_valid ? fmt1(f1, temp_value) : "--", co2_valid ? fmt1(f2, humi_value) : "--");
          if(pres_valid)
          {
            n += sprintf(buf + n,
                "Druck (hPa): %s<br/>\r\n" \
                "Temperatur (&deg;C): %s<br/>\r\n",
                fmt1(f3, pres_value), fmt1(f4, temp2_value));
          }
          sprintf(buf + n, "</div>\r\n");
          client.print(buf);

          String fv = WiFi.firmwareVersion();
          byte mac[6];
          WiFi.macAddress(mac);
          sprintf(buf,
              "<br/><br/>\r\n" \
              "%s" \
              "<a href='/json'>JSON</a> - <a href='/cmk-agent'>Checkmk</a> - <a href='#' onclick='wifi();'>WiFi Login</a> - <a href='/config'>Config</a>\r\n" \
              "<br/><br/>\r\n" \
              "<div id=wifi>\r\n" \
              "<form method=post>\r\n" \
              "SSID <input name=1 size=30 maxlength=64 placeholder=SSID value='%s'><br/>\r\n" \
              "Code <input name=2 size=30 maxlength=64 placeholder=Password value=''><br/>\r\n" \
              "%s" \
              "<input type=submit> (Neustart erforderlich, requires reboot)<br/>\r\n" \
              "</form><br/>\r\n" \
              "<div id=info>\r\n" \
              "Firmware: v" VERSION ", \r\n" \
              "WINC1500: %s, \r\n" \
              "MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n" \
              "</div>\r\n" \
              "</div>\r\n" \
              "</body>\r\n" \
              "</html>\r\n",
              form_msg,
              settings_password_set() ? "" : settings.wifi_ssid, /*settings.wifi_code, */
              settings_password_set() ? "Passwort <input name=3 type=password size=30 maxlength=32><br/>\r\n" : "",
              fv.c_str(), 
              mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]
          );
          client.print(buf);
        }
        break;
      }
      else //save request
      {
        if(pos < (sizeof(req[0])-1))
        {
          req[0][pos++] = c;
          req[0][pos] = 0;
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
    delay(500);
    NVIC_SystemReset();
  }

  return;
}

#endif //WITH_WEB
