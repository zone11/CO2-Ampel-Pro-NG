#include "serial_console.h"

#include "app_globals.h"
#include "i2c_utils.h"
#include "led_control.h"
#include "mqtt_service.h"
#include "sensors.h"
#include "wifi_manager.h"

#include "settings_registry.h"
#include "winc_bridge.h"

#include <ctype.h>

#define LINE_IDLE_MS 500 //Zeile ohne CR/LF gilt nach 500ms Pause als fertig
#define IMPORT_MAX   2048    //max. JSON bei "json import"
#define IMPORT_IDLE  30000UL //Abbruch nach 30s ohne Eingabe

// "json import": collects raw characters (no line limit) until the closing brace of the object
static struct
{
  char *buf;          //nur waehrend des Imports belegt
  size_t len;
  int depth;
  bool in_string, escape;
  unsigned long t_last;
} imp;

static void import_end(void)
{
  free(imp.buf);
  imp.buf = NULL;
}

// Returns true when the object is complete (or the import was aborted)
static bool import_feed(char c)
{
  imp.t_last = millis();
  if(imp.len == 0)
  {
    if(isspace((unsigned char)c))
    {
      return false;
    }
    if(c != '{')
    {
      Serial.println("{\"result\":\"ERROR\",\"errors\":{\"json\":\"expected '{'\"}}");
      import_end();
      return true;
    }
  }
  if(imp.len >= (IMPORT_MAX - 1))
  {
    Serial.println("{\"result\":\"ERROR\",\"errors\":{\"json\":\"too long (max 2048 bytes)\"}}");
    import_end();
    return true;
  }
  imp.buf[imp.len++] = c;
  imp.buf[imp.len] = 0;

  if(imp.in_string)
  {
    if(imp.escape)          { imp.escape = false; }
    else if(c == '\\')     { imp.escape = true; }
    else if(c == '"')       { imp.in_string = false; }
  }
  else if(c == '"')         { imp.in_string = true; }
  else if(c == '{')         { imp.depth++; }
  else if(c == '}')         { imp.depth--; }

  if(imp.depth == 0)
  {
    settings_json_import(imp.buf, &Serial, true, remote_on != 0);
    import_end();
    return true;
  }
  return false;
}

static void import_begin(const char *rest)
{
  imp.buf = (char *)malloc(IMPORT_MAX);
  if(imp.buf == NULL)
  {
    Serial.println("{\"result\":\"ERROR\",\"errors\":{\"json\":\"out of memory\"}}");
    return;
  }
  imp.len = 0;
  imp.depth = 0;
  imp.in_string = false;
  imp.escape = false;
  imp.t_last = millis();
  while(*rest && (imp.buf != NULL))
  {
    import_feed(*rest++);
  }
}

static void import_service(void)
{
  while(Serial.available() && (imp.buf != NULL))
  {
    import_feed((char)Serial.read());
  }
  if((imp.buf != NULL) && ((millis() - imp.t_last) > IMPORT_IDLE))
  {
    Serial.println("{\"result\":\"ERROR\",\"errors\":{\"json\":\"timeout, nothing changed\"}}");
    import_end();
  }
}

// Collects input without blocking. A line ends on CR or LF (so CR-only terminals work
// and CRLF just adds an empty line), after LINE_IDLE_MS without new characters, or when the
// buffer is full (the rest stays unread).
static bool read_line(char *buf, size_t size)
{
  static size_t len = 0;
  static unsigned long t_last = 0;

  while(Serial.available())
  {
    char c = Serial.read();
    t_last = millis();
    if((c == '\r') || (c == '\n'))
    {
      buf[len] = 0;
      len = 0;
      return true;
    }
    buf[len++] = c;
    if(len >= (size - 1)) //voll: Rest bleibt im Puffer (z.B. langes JSON nach "json import")
    {
      buf[len] = 0;
      len = 0;
      return true;
    }
  }

  if((len > 0) && ((millis() - t_last) > LINE_IDLE_MS))
  {
    buf[len] = 0;
    len = 0;
    return true;
  }

  return false;
}

void serial_service(void)
{
  static char line_buf[192];

  if((features & FEATURE_USB) == 0)
  {
    return;
  }

  if(imp.buf != NULL) //"json import" sammelt das JSON-Objekt
  {
    import_service();
    return;
  }

  if(!read_line(line_buf, sizeof(line_buf)))
  {
    return;
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


  if((strncasecmp(line, "enable", 6) == 0) && ((line[6] == 0) || (line[6] == ' ')))
  {
    if(settings_enable(line + ((line[6] == ' ') ? 7 : 6), &Serial)) //antwortet OK / ERROR
    {
      remote_on = 1;
      buzzer(0);
      led_brightness_update();
      leds(COLOR_MENU);
    }
    return;
  }
  if(strcasecmp(line, "exit") == 0)
  {
    remote_on = 0;
    led_brightness_update();
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
    print_sensor_info();
    print_led_status();
    print_wifi_status();
    print_mqtt_status();
    return;
  }
  if(strcasecmp(line, "winc bridge") == 0) //WiFi-Modul-Flash fuer scripts/co2ampel_tool.py (nur lokal)
  {
    if(!remote_on)
    {
      Serial.println("ERROR: Not enabled (send 'enable' first)");
      return;
    }
    winc_bridge(); //kehrt nicht zurueck (Neustart)
  }
  if((strncasecmp(line, "json import", 11) == 0) && ((line[11] == 0) || isspace((unsigned char)line[11])))
  {
    if(!remote_on)
    {
      Serial.println("{\"result\":\"ERROR\",\"errors\":{\"enable\":\"Not enabled (send 'enable' first)\"}}");
      return;
    }
    import_begin(line + 11); //JSON auf derselben Zeile oder in den folgenden
    return;
  }
  if(strcasecmp(line, "i2c scan") == 0)
  {
    i2c_scan(Wire, "Wire (SERCOM0)", Serial);
    i2c_scan(Wire1, "Wire1 (SERCOM2)", Serial);
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

  if(settings_handle_line(line, &Serial, remote_on != 0, true))
  {
    return;
  }

  Serial.println("ERROR: Unknown command");
}
