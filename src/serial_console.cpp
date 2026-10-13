#include "serial_console.h"

#include "app_globals.h"
#include "i2c_utils.h"
#include "led_control.h"
#include "mqtt_service.h"
#include "sensors.h"
#include "wifi_manager.h"

#include "settings_registry.h"

#include <ctype.h>

#define LINE_IDLE_MS 500 //Zeile ohne CR/LF gilt nach 500ms Pause als fertig

// Collects input without blocking. A line ends on CR or LF (so CR-only terminals work
// and CRLF just adds an empty line), or after LINE_IDLE_MS without new characters.
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
    if(len < (size - 1))
    {
      buf[len++] = c;
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
    print_sensor_info();
    print_wifi_status();
    print_mqtt_status();
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
