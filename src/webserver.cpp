#include "webserver.h"
#include "sensors.h"
#include "settings_registry.h"
#include "wifi_manager.h"

#include <ctype.h>

#if WITH_WEB

static void urldecode(char *src) //URL Parameter dekodieren
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

  if(status == WL_IDLE_STATUS) //Verbindungsaufbau
  {
    return;
  }
  else if((status == WL_CONNECT_FAILED) ||
          (status == WL_CONNECTION_LOST) || 
          (status == WL_DISCONNECTED)) //Verbindungsabbruch
  {
    if((millis()-t_check) > (1*60000UL)) //1min
    {
      t_check = millis();
      wifi_start();
    }
    return;
  }

  t_check = millis(); //Zeit speichern fuer Neuverbindung nach 1min

  WiFiClient client = server.available();
  if(!client) //Client nicht verbunden
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
  char req[3][3*64+1]; //HTTP request / POST-Werte SSID, Code, Passwort (URL-kodiert bis 3 Zeichen pro Byte)
  const char *form_msg = "";
  char f1[16], f2[16], f3[16], f4[16]; //Messwerte mit einer Nachkommastelle (fmt1)
  req[0][0] = 0;
  while(client.connected())
  {
    if((millis()-t_check) > (5*1000UL)) //Stop nach 5s
    {
      break;
    }
    if(client.available())
    {
      char c = client.read();
      if(c == '\n' && currentLineIsBlank) //Header zu Ende
      {
        if(strncmp(req[0], "GET ", 4) && strncmp(req[0], "POST ", 5)) //kein GET oder POST
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
          if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: application/json\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                "{\r\n" \
                " \"c\": %i,\r\n" \
                " \"t\": %s,\r\n" \
                " \"h\": %s,\r\n" \
                " \"p\": %s,\r\n" \
                " \"u\": %s,\r\n" \
                " \"l\": %i\r\n" \
                "}\r\n",
                co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value), fmt1(f3, pres_value), fmt1(f4, temp2_value), light_value
            );
          }
          else
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: application/json\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                "{\r\n" \
                " \"c\": %i,\r\n" \
                " \"t\": %s,\r\n" \
                " \"h\": %s,\r\n" \
                " \"l\": %i\r\n" \
                "}\r\n",
                co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value), light_value
            );
          }
          client.print(buf);
        }
        else if(strncmp(req[0], "GET /cmk-agent", 14) == 0) //Checkmk Agent
        {
          //CO2-Ampeln koennen so direkt ins Monitoring von checkmk.com 
          //aufgenommen werden. Plugins sind nicht zwingend erforderlich.
          //Da HTTP als Uebertragungsweg genutzt wird, "Data Source" 
          //verwenden: wget -O - http://ip_address/cmk-agent
          //Siehe: https://docs.checkmk.com/latest/de/datasource_programs.html
          if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: text/plain\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                //Plaintext im von Checkmk erwarteten Format
                //Siehe: https://docs.checkmk.com/latest/en/devel_check_plugins.html
                "<<<check_mk>>>\r\n" \
                "AgentOS: arduino\r\n" \
                //Check-Plugin fuer den Server erforderlich, um die Metriken auszuwerten 
                "<<<watterott_co2ampel_plugin>>>\r\n" \
                "co2 %i\r\n" \
                "temp %s\r\n" \
                "humidity %s\r\n" \
                "lighting %i\r\n" \
                "pressure %s\r\n" \
                "temp2 %s\r\n" \
                //Ad-hoc Check, der kein Server-Plugin benoetigt, nutzt Schwellwerte der Ampel.
                //Achtung: Nur eine Zeile - der Checkmk-Server nimmt die Bewertung selbst an
                //Hand der uebergebenen Schwellwerte vor. Die lesen wir hier aus der Ampel aus:
                "<<<local:sep(0)>>>\r\n" \
                "P \"CO2 level (ppm)\" co2ppm=%i;%i;%i CO2/ventilation control with Watterott CO2-Ampel, thresholds taken from sensor board.\r\n",
                co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value), light_value, fmt1(f3, pres_value), fmt1(f4, temp2_value),
                co2_value, settings.range[1], settings.range[2]
            );
          }
          else
          {
            sprintf(buf,
                "HTTP/1.1 200 OK\r\n" \
                "Content-Type: text/plain\r\n" \
                "Connection: close\r\n" \
                "\r\n" \
                //Plaintext im von Checkmk erwarteten Format
                //Siehe: https://docs.checkmk.com/latest/en/devel_check_plugins.html
                "<<<check_mk>>>\r\n" \
                "AgentOS: arduino\r\n" \
                //Check-Plugin fuer den Server erforderlich, um die Metriken auszuwerten 
                "<<<watterott_co2ampel_plugin>>>\r\n" \
                "co2 %i\r\n" \
                "temp %s\r\n" \
                "humidity %s\r\n" \
                "lighting %i\r\n" \
                //Ad-hoc Check, der kein Server-Plugin benoetigt, nutzt Schwellwerte der Ampel.
                //Achtung: Nur eine Zeile - der Checkmk-Server nimmt die Bewertung selbst an
                //Hand der uebergebenen Schwellwerte vor. Die lesen wir hier aus der Ampel aus:
                "<<<local:sep(0)>>>\r\n" \
                "P \"CO2 level (ppm)\" co2ppm=%i;%i;%i CO2/ventilation control with Watterott CO2-Ampel, thresholds taken from sensor board.\r\n",
                co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value), light_value,
                co2_value, settings.range[1], settings.range[2]
            );
          }
          client.print(buf);
        }
        else if(strncmp(req[0], "GET /favicon", 12) == 0) //Favicon 
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
          //HTTP Post Daten verarbeiten
          if((strncmp(req[0], "POST ", 5) == 0) && client.available())
          {
            bool too_long = false;
            req[0][0] = 0; //SSID
            req[1][0] = 0; //Code
            req[2][0] = 0; //Passwort (sys.password)
            for(unsigned int r=0, i=0, last_c=0; client.available();)
            {
              c = client.read();
              if(c == '&') //Aufbau: 1=xxx&2=yyy
              {
                r = 0;
              }
              else if((c == '=') && isdigit(last_c)) //1=xxx
              {
                r = last_c-'0';
                i = 0;
              }
              else if((r > 0) && (r < 4)) //1 bis 3
              {
                if(i < (sizeof(req[0]) - 1))
                {
                  req[r-1][i++] = c;
                  req[r-1][i] = 0;
                }
                else
                {
                  too_long = true; //Wert zu lang, nicht speichern
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
            if(!settings_enable(req[2], NULL)) //mit sys.password: Passwort erforderlich
            {
              form_msg = "<b>Falsches Passwort / wrong password</b><br/>\r\n";
            }
            else if(!too_long && (strcmp(req[0], settings.wifi_ssid) || strcmp(req[1], settings.wifi_code)))
            {
              if(settings_set("wifi.ssid", req[0]) && settings_set("wifi.pass", req[1]))
              {
                settings_save(); //Einstellungen speichern
                form_msg = "<b>Gespeichert / saved</b><br/>\r\n";
              }
            }
          }
          //HTTP Header+Daten
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

          if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
          {
            sprintf(buf,
                "<div id=data>\r\n" \
                "CO2 (ppm): %i<br/>\r\n" \
                "Temperatur (&deg;C): %s<br/>\r\n" \
                "Luftfeuchte (%% rel): %s<br/>\r\n" \
                "Druck (hPa): %s<br/>\r\n" \
                "Temperatur (&deg;C): %s<br/>\r\n" \
                "</div>\r\n",
                co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value), fmt1(f3, pres_value), fmt1(f4, temp2_value)
            );
          }
          else
          {
            sprintf(buf,
                "<div id=data>\r\n" \
                "CO2 (ppm): %i<br/>\r\n" \
                "Temperatur (&deg;C): %s<br/>\r\n" \
                "Luftfeuchte (%% rel): %s<br/>\r\n" \
                "</div>\r\n",
                co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value)
            );
          }
          client.print(buf);

          String fv = WiFi.firmwareVersion();
          byte mac[6];
          WiFi.macAddress(mac);
          sprintf(buf,
              "<br/><br/>\r\n" \
              "%s" \
              "<a href='/json'>JSON</a> - <a href='/cmk-agent'>Checkmk</a> - <a href='#' onclick='wifi();'>WiFi Login</a>\r\n" \
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
        currentLineIsBlank = true;
      }
      else if(c != '\r')
      {
        currentLineIsBlank = false;
      }
    }
  }

  delay(20); //20ms warten zum Senden
  client.stop();

  return;
}

#endif //WITH_WEB
