#include "wifi_manager.h"
#include "led_control.h"

void get_device_id(char *buffer, size_t buffer_size)
{
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(buffer, buffer_size, "%02X%02X%02X%02X%02X%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

const char *system_name(void)
{
  static char name[sizeof(settings.name)];
  char id[16];

  if(settings.name[0] != 0)
  {
    return settings.name;
  }
  get_device_id(id, sizeof(id));
  snprintf(name, sizeof(name), PRODUCT_NAME " %s", id);
  return name;
}

void system_hostname(char *buffer, size_t buffer_size)
{
  const char *name = system_name();
  size_t n = 0;

  for(; (*name != 0) && (n < 32) && (n + 1 < buffer_size); name++)
  {
    char c = *name;
    bool ok = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'));
    if(ok)
    {
      buffer[n++] = c;
    }
    else if((n > 0) && (buffer[n - 1] != '-')) //no leading dash, no runs
    {
      buffer[n++] = '-';
    }
  }
  while((n > 0) && (buffer[n - 1] == '-')) //no trailing dash
  {
    n--;
  }
  buffer[n] = 0;

  if(n == 0) //nothing usable left: default name
  {
    char id[16];
    get_device_id(id, sizeof(id));
    snprintf(buffer, buffer_size, "%s-%s", PRODUCT_NAME, id);
  }
}

void print_ip_address_line(const IPAddress &ip)
{
  Serial.print(ip[0]);
  Serial.print(".");
  Serial.print(ip[1]);
  Serial.print(".");
  Serial.print(ip[2]);
  Serial.print(".");
  Serial.println(ip[3]);
}

void print_wifi_status(void)
{
  if(features & FEATURE_WINC1500)
  {
    char hostname[33];
    system_hostname(hostname, sizeof(hostname));
    Serial.print("Name: ");
    Serial.println(system_name());
    Serial.print("Hostname: ");
    Serial.println(hostname);
    Serial.print("WiFi SSID: ");
    Serial.println(strlen(settings.wifi_ssid) > 0 ? settings.wifi_ssid : "(not configured)");
    Serial.print("WiFi Password: ");
    Serial.println(strlen(settings.wifi_code) > 0 ? "***" : "(not configured)");
    Serial.print("WiFi Status: ");

    int status = WiFi.status();
    switch(status)
    {
      case WL_CONNECTED:
        Serial.print("Connected to ");
        Serial.println(WiFi.SSID());
        Serial.print("IP Address: ");
        print_ip_address_line(WiFi.localIP());
        Serial.print("Signal Strength: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        break;
      case WL_NO_SHIELD:
        Serial.println("No WiFi hardware");
        break;
      case WL_IDLE_STATUS:
        Serial.println("Idle");
        break;
      case WL_NO_SSID_AVAIL:
        Serial.println("SSID not available");
        break;
      case WL_SCAN_COMPLETED:
        Serial.println("Scan completed");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("Connection failed");
        break;
      case WL_CONNECTION_LOST:
        Serial.println("Connection lost");
        break;
      case WL_DISCONNECTED:
        Serial.println("Disconnected");
        break;
      case WL_AP_LISTENING:
        Serial.print("Access Point Mode - SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("AP IP Address: ");
        print_ip_address_line(WiFi.localIP());
        break;
      default:
        Serial.print("Unknown (");
        Serial.print(status);
        Serial.println(")");
        break;
    }
  }
  else
  {
    Serial.println("WiFi hardware not available");
  }
}

unsigned int wifi_start_ap(void)
{
  char hostname[33];

  if(features & FEATURE_USB)
  {
    Serial.println("WiFi AP start...");
  }

  system_hostname(hostname, sizeof(hostname));

  if(WiFi.status() != WL_IDLE_STATUS)
  {
    WiFi.end(); //WiFi.disconnect();
    //reset_mcu();
  }

  WiFi.hostname(hostname);
  if(WiFi.beginAP(system_name()) != WL_AP_LISTENING)
  {
    WiFi.end();
    return 1;
  }

  delay(5000);

#if WITH_WEB
  server.begin(); //start the web server
#endif

  return 0;
}

unsigned int wifi_start(void)
{
  char hostname[33];

  if(settings.wifi_ssid[0] == 0) //no credentials
  {
    if(features & FEATURE_USB)
    {
      Serial.println("WiFi not configured (ssid empty)");
    }
    return 1;
  }

  if(features & FEATURE_USB)
  {
    Serial.println("WiFi connect...");
  }

  system_hostname(hostname, sizeof(hostname));

  if(WiFi.status() != WL_IDLE_STATUS)
  {
    WiFi.end(); //WiFi.disconnect();
    //reset_mcu();
  }

  WiFi.hostname(hostname);
  if(strlen(settings.wifi_code) > 0) //password
  {
    WiFi.begin(settings.wifi_ssid, settings.wifi_code);
  }
  else
  {
    WiFi.begin(settings.wifi_ssid); //open network
  }

  //wait for the connection
  for(unsigned int t=0; WiFi.status() == WL_IDLE_STATUS; t++)
  {
    if(t >= 6) //6s
    {
      break;
    }
    status_led(1000);
  }

  if(!(WiFi.status() == WL_CONNECTED)) //connection failed
  {
    return 1;
  }

#if WITH_WEB
  server.begin(); //start the web server
#endif

  return 0;
}
