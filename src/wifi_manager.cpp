#include "wifi_manager.h"
#include "led_control.h"

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
  byte mac[6];
  char ssid[32];

  if(features & FEATURE_USB)
  {
    Serial.println("WiFi AP start...");
  }

  WiFi.macAddress(mac); //MAC-Adresse abfragen
  sprintf(ssid, "CO2AMPEL-%X-%X", mac[1], mac[0]);

  if(WiFi.status() != WL_IDLE_STATUS)
  {
    WiFi.end(); //WiFi.disconnect();
    //reset_mcu();
  }

  WiFi.hostname(ssid); //Hostname setzen
  if(WiFi.beginAP(ssid) != WL_AP_LISTENING)
  {
    WiFi.end();
    return 1;
  }

  delay(5000); //5s warten

  server.begin(); //starte Webserver

  return 0;
}

unsigned int wifi_start(void)
{
  byte mac[6];
  char name[32];

  if(settings.wifi_ssid[0] == 0) //keine Logindaten
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

  WiFi.macAddress(mac); //MAC-Adresse abfragen
  sprintf(name, "CO2AMPEL-%X-%X", mac[1], mac[0]);

  if(WiFi.status() != WL_IDLE_STATUS)
  {
    WiFi.end(); //WiFi.disconnect();
    //reset_mcu();
  }

  WiFi.hostname(name); //Hostname setzen
  if(strlen(settings.wifi_code) > 0) //Passwort
  {
    WiFi.begin(settings.wifi_ssid, settings.wifi_code); //verbinde WiFi Netzwerk mit Passwort
  }
  else
  {
    WiFi.begin(settings.wifi_ssid); //verbinde WiFi Netzwerk ohne Passwort
  }

  //auf Verbindung warten
  for(unsigned int t=0; WiFi.status() == WL_IDLE_STATUS; t++)
  {
    if(t >= 6) //6s
    {
      break;
    }
    status_led(1000); //Status-LED
  }

  if(!(WiFi.status() == WL_CONNECTED)) //Verbindung fehlgeschlagen
  {
    return 1;
  }

  server.begin(); //starte Webserver

  return 0;
}
