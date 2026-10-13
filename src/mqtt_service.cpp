#include "mqtt_service.h"
#include "settings_registry.h"

#define MQTT_CMD_MAX   400  //max. Laenge eines Befehls auf <base>/cmd (Lesepuffer 512)
#define MQTT_REPLY_MAX 1400 //max. Antwort auf <base>/resp (Schreibpuffer 1536)

typedef enum { CMD_NONE, CMD_READY, CMD_TOO_LONG } CMD_STATE;

static char cmd_pending[MQTT_CMD_MAX + 1];
static volatile CMD_STATE cmd_state = CMD_NONE;

// Collects a command reply as text; lines end with \n only
class ReplyBuffer : public Print
{
public:
  void clear(void) { len = 0; overflow = false; buf[0] = 0; }
  const char *c_str(void) const { return buf; }
  size_t write(uint8_t c) override
  {
    static const char truncated[] = "\n...truncated";
    if(c == '\r')
    {
      return 1;
    }
    if(len < (sizeof(buf) - sizeof(truncated)))
    {
      buf[len++] = (char)c;
      buf[len] = 0;
    }
    else if(!overflow)
    {
      overflow = true;
      memcpy(&buf[len], truncated, sizeof(truncated)); //inkl. Nullterminator
    }
    return 1;
  }
private:
  char buf[MQTT_REPLY_MAX + 1];
  size_t len = 0;
  bool overflow = false;
};

static ReplyBuffer reply;

// Holt die eindeutige Chip-ID des SAMD21
void get_chip_id(char *buffer, size_t buffer_size)
{
  // SAMD21G18A Unique Identifier (128-bit = 4x 32-bit words)
  volatile uint32_t *id0 = (volatile uint32_t *)0x0080A00C;
  volatile uint32_t *id1 = (volatile uint32_t *)0x0080A040;
  volatile uint32_t *id2 = (volatile uint32_t *)0x0080A044;
  volatile uint32_t *id3 = (volatile uint32_t *)0x0080A048;

  snprintf(buffer, buffer_size, "%08X%08X%08X%08X",
           (unsigned int)*id0, (unsigned int)*id1,
           (unsigned int)*id2, (unsigned int)*id3);
}

// Generiert Device-ID aus MAC-Adresse (volle MAC-Adresse)
void get_device_id(char *buffer, size_t buffer_size)
{
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(buffer, buffer_size, "%02X%02X%02X%02X%02X%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

// Topic <prefix>/<device id>/<suffix>
static void mqtt_topic(char *buffer, size_t buffer_size, const char *suffix)
{
  char device_id[16];
  get_device_id(device_id, sizeof(device_id));
  snprintf(buffer, buffer_size, "%s/%s/%s", settings.mqtt_topic_prefix, device_id, suffix);
}

// Copies src into dst as JSON string content (escapes " and \, drops control characters)
static void json_escape(char *dst, size_t size, const char *src)
{
  size_t n = 0;
  for(; *src && (n + 2 < size); src++)
  {
    if((*src == '"') || (*src == '\\'))
    {
      dst[n++] = '\\';
    }
    else if((unsigned char)*src < 0x20)
    {
      continue;
    }
    dst[n++] = *src;
  }
  dst[n] = 0;
}

// Called from mqttClient.loop(). The library must not publish from here, so the command is only
// stored and handled by mqtt_handle_command() after loop() returns.
static void mqtt_message(MQTTClient *client, char topic[], char bytes[], int length)
{
  (void)client;
  (void)topic; //nur <base>/cmd ist abonniert

  if(length <= 0) //leer = eigenes Loeschen eines retained Befehls
  {
    return;
  }
  if(cmd_state != CMD_NONE) //vorheriger Befehl noch nicht bearbeitet
  {
    return;
  }
  if(length > MQTT_CMD_MAX)
  {
    cmd_state = CMD_TOO_LONG;
    return;
  }
  memcpy(cmd_pending, bytes, length);
  cmd_pending[length] = 0;
  cmd_state = CMD_READY;
}

// Runs every line of the pending command through the settings API (as a remote caller:
// secrets masked, CFG_LOCAL keys refused) and publishes the reply on <base>/resp.
static void mqtt_handle_command(void)
{
  char topic[96];
  char first[32] = "";

  //retained Befehl loeschen, damit er nicht bei jeder Verbindung erneut ausgefuehrt wird
  mqtt_topic(topic, sizeof(topic), "cmd");
  mqttClient.publish(topic, "", true, 0);

  reply.clear();
  if(cmd_state == CMD_TOO_LONG)
  {
    reply.print("ERROR: Command too long (max ");
    reply.print(MQTT_CMD_MAX);
    reply.println(" bytes)");
  }
  else
  {
    for(char *line = strtok(cmd_pending, "\n"); line != NULL; line = strtok(NULL, "\n"))
    {
      size_t n = strlen(line);
      if((n > 0) && (line[n - 1] == '\r'))
      {
        line[n - 1] = 0;
      }
      if(first[0] == 0)
      {
        sscanf(line, "%31s", first); //nur das Kommando fuers Log, keine Werte
      }
      if(!settings_handle_line(line, &reply, true, false))
      {
        if(line[strspn(line, " \t")] != 0) //Leerzeilen ignorieren
        {
          reply.println("ERROR: Unknown command");
        }
      }
    }
  }
  cmd_state = CMD_NONE;

  if(reply.c_str()[0] != 0)
  {
    mqtt_topic(topic, sizeof(topic), "resp");
    mqttClient.publish(topic, reply.c_str(), false, 0);
  }

  if(features & FEATURE_USB)
  {
    Serial.print("MQTT cmd: ");
    Serial.println(first[0] ? first : "(too long)");
  }
}

void print_mqtt_status(void)
{
  Serial.print("MQTT Enabled: ");
  Serial.println(settings.mqtt_enabled ? "Yes" : "No");
  Serial.print("Broker: ");
  Serial.print(settings.mqtt_broker);
  Serial.print(":");
  Serial.println(settings.mqtt_port);
  Serial.print("Username: ");
  Serial.println(strlen(settings.mqtt_user) > 0 ? settings.mqtt_user : "(none)");
  Serial.print("Client ID: ");
  if(strlen(settings.mqtt_client_id) > 0)
  {
    Serial.println(settings.mqtt_client_id);
  }
  else
  {
    char device_id[32];
    get_device_id(device_id, sizeof(device_id));
    Serial.print(device_id);
    Serial.println(" (auto)");
  }
  Serial.print("Topic Prefix: ");
  Serial.println(settings.mqtt_topic_prefix);

  char chip_id[40];
  get_chip_id(chip_id, sizeof(chip_id));
  Serial.print("Chip ID: ");
  Serial.println(chip_id);

  Serial.print("Interval: ");
  Serial.print(settings.mqtt_interval);
  Serial.println("s");
  Serial.print("Remote config: ");
  Serial.println(settings.mqtt_config ? "on (<prefix>/<id>/cmd)" : "off");
  if(settings.mqtt_enabled && (features & FEATURE_WINC1500))
  {
    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    Serial.print("Status: ");
    Serial.println(mqttClient.connected() ? "Connected" : "Disconnected");
  }
}

void mqtt_connect(void)
{
  char client_id[48];

  if(!settings.mqtt_enabled)
  {
    return;
  }

  if((features & FEATURE_WINC1500) == 0)
  {
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT: WiFi hardware not available");
    }
    return; //keine WiFi-Hardware
  }

  if(WiFi.status() != WL_CONNECTED)
  {
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT: WiFi not connected");
    }
    return; //keine WiFi-Verbindung
  }

  //MQTT Client ID generieren wenn leer
  if(strlen(settings.mqtt_client_id) == 0)
  {
    get_device_id(client_id, sizeof(client_id));
  }
  else
  {
    strncpy(client_id, settings.mqtt_client_id, sizeof(client_id)-1);
    client_id[sizeof(client_id)-1] = '\0';
  }

  if(features & FEATURE_USB)
  {
    Serial.print("MQTT connecting to ");
    Serial.print(settings.mqtt_broker);
    Serial.print(":");
    Serial.print(settings.mqtt_port);
    Serial.print(" as '");
    Serial.print(client_id);
    Serial.print("'");
    if(strlen(settings.mqtt_user) > 0)
    {
      Serial.print(" with user '");
      Serial.print(settings.mqtt_user);
      Serial.print("'");
    }
    Serial.println();
  }

  //MQTT Client initialisieren
  char topic[96];
  mqttClient.begin(settings.mqtt_broker, settings.mqtt_port, mqttWifiClient);
  mqtt_topic(topic, sizeof(topic), "status");
  mqttClient.setWill(topic, "offline", true, 1); //Broker meldet "offline" bei Verbindungsabbruch

  //Verbinden
  boolean connected = false;
  if(strlen(settings.mqtt_user) > 0)
  {
    //Mit Authentifizierung
    connected = mqttClient.connect(client_id, settings.mqtt_user, settings.mqtt_pass);
  }
  else
  {
    //Ohne Authentifizierung
    connected = mqttClient.connect(client_id);
  }

  if(connected)
  {
    mqttClient.publish(topic, "online", true, 1);
    if(settings.mqtt_config)
    {
      mqttClient.onMessageAdvanced(mqtt_message);
      mqtt_topic(topic, sizeof(topic), "cmd");
      mqttClient.subscribe(topic, 0);
    }
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT connected successfully");
    }
  }
  else
  {
    if(features & FEATURE_USB)
    {
      Serial.print("MQTT connection failed - ");

      //Detaillierte Fehlerausgabe
      lwmqtt_err_t err = mqttClient.lastError();
      lwmqtt_return_code_t rc = mqttClient.returnCode();

      if(err != LWMQTT_SUCCESS)
      {
        Serial.print("Error: ");
        switch(err)
        {
          case LWMQTT_NETWORK_FAILED_CONNECT: Serial.println("Network connection failed"); break;
          case LWMQTT_NETWORK_TIMEOUT: Serial.println("Network timeout"); break;
          case LWMQTT_NETWORK_FAILED_READ: Serial.println("Network read failed"); break;
          case LWMQTT_NETWORK_FAILED_WRITE: Serial.println("Network write failed"); break;
          case LWMQTT_CONNECTION_DENIED: Serial.println("Connection denied by broker"); break;
          default: Serial.print("Code "); Serial.println(err); break;
        }
      }

      if(rc != LWMQTT_CONNECTION_ACCEPTED)
      {
        Serial.print("Return Code: ");
        switch(rc)
        {
          case LWMQTT_UNACCEPTABLE_PROTOCOL: Serial.println("Unacceptable protocol version"); break;
          case LWMQTT_IDENTIFIER_REJECTED: Serial.println("Client ID rejected"); break;
          case LWMQTT_SERVER_UNAVAILABLE: Serial.println("Server unavailable"); break;
          case LWMQTT_BAD_USERNAME_OR_PASSWORD: Serial.println("Bad username or password"); break;
          case LWMQTT_NOT_AUTHORIZED: Serial.println("Not authorized"); break;
          default: Serial.print("Code "); Serial.println(rc); break;
        }
      }
    }
  }
}

void mqtt_reconnect(void)
{
  static unsigned long last_attempt = 0;

  if(!settings.mqtt_enabled)
  {
    return;
  }

  //Nur alle 30 Sekunden neu versuchen
  if((millis() - last_attempt) < 30000UL)
  {
    return;
  }

  last_attempt = millis();
  mqtt_connect();
}

void mqtt_publish_sensors(void)
{
  char topic[96];
  char json[192];
  size_t n;

  if(!settings.mqtt_enabled)
  {
    return;
  }

  if(!mqttClient.connected())
  {
    return;
  }

  //Messwerte als ein JSON (ein konsistenter Datensatz pro Intervall)
  n = snprintf(json, sizeof(json), "{\"co2\":%u,\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%u",
               co2_value, temp_value, humi_value, light_value);
  if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
  {
    n += snprintf(json + n, sizeof(json) - n, ",\"pressure\":%.1f,\"pressure_temp\":%.1f", pres_value, temp2_value);
  }
  snprintf(json + n, sizeof(json) - n, "}");
  mqtt_topic(topic, sizeof(topic), "sensors");
  mqttClient.publish(topic, json, false, 0);

  //Geraet: Firmware-Version und erkannte Sensoren (retained, damit immer abrufbar)
  n = snprintf(json, sizeof(json), "{\"version\":\"%s\",\"sensors\":[", VERSION);
  const struct { unsigned int feature; const char *name; } sensor_names[] =
  {
    { FEATURE_SCD30, "SCD30" }, { FEATURE_SCD4X, "SCD4X" }, { FEATURE_LPS22HB, "LPS22HB" }, { FEATURE_BMP280, "BMP280" },
  };
  bool first = true;
  for(const auto &s : sensor_names)
  {
    if(features & s.feature)
    {
      n += snprintf(json + n, sizeof(json) - n, "%s\"%s\"", first ? "" : ",", s.name);
      first = false;
    }
  }
  snprintf(json + n, sizeof(json) - n, "]}");
  mqtt_topic(topic, sizeof(topic), "device");
  mqttClient.publish(topic, json, true, 0);

  //WiFi
  char ssid[2*32+1];
  IPAddress ip = WiFi.localIP();
  json_escape(ssid, sizeof(ssid), WiFi.SSID());
  mqtt_topic(topic, sizeof(topic), "wifi");
  snprintf(json, sizeof(json), "{\"ssid\":\"%s\",\"ip\":\"%u.%u.%u.%u\",\"rssi\":%ld}",
           ssid, ip[0], ip[1], ip[2], ip[3], (long)WiFi.RSSI());
  mqttClient.publish(topic, json, false, 0);

  if(features & FEATURE_USB)
  {
    mqtt_topic(topic, sizeof(topic), "sensors");
    Serial.print("MQTT published to ");
    Serial.println(topic);
  }
}

void mqtt_service(void)
{
  static unsigned long last_publish = 0;

  if(!settings.mqtt_enabled)
  {
    return;
  }

  if((features & FEATURE_WINC1500) == 0)
  {
    return;
  }

  //MQTT Loop (non-blocking)
  mqttClient.loop();

  //Befehl aus <base>/cmd bearbeiten (ausserhalb des Callbacks)
  if(cmd_state != CMD_NONE)
  {
    if(settings.mqtt_config && mqttClient.connected())
    {
      mqtt_handle_command();
    }
    else
    {
      cmd_state = CMD_NONE;
    }
  }

  //Verbindung pruefen
  if(!mqttClient.connected())
  {
    mqtt_reconnect();
    return;
  }

  //Periodisches Publishing
  if((millis() - last_publish) > (settings.mqtt_interval * 1000UL))
  {
    last_publish = millis();
    mqtt_publish_sensors();
  }
}
