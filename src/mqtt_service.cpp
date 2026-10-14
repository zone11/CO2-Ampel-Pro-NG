#include "mqtt_service.h"
#include "sensors.h"
#include "settings_registry.h"

#if WITH_MQTT_CONFIG
#define MQTT_CMD_MAX   1024 //max. JSON auf <base>/cmd (Lesepuffer MQTT_READ_BUF in main.cpp)
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
#endif //WITH_MQTT_CONFIG

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

#if WITH_MQTT_CONFIG
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

  //retained Befehl loeschen, damit er nicht bei jeder Verbindung erneut ausgefuehrt wird
  mqtt_topic(topic, sizeof(topic), "cmd");
  mqttClient.publish(topic, "", true, 0);

  //nur JSON (wie "json import"): Einstellungen ohne Passwort, Schutz ueber die Broker-ACLs;
  //Verbindungsdaten (CFG_LOCAL) bleiben gesperrt
  reply.clear();
  const char *json = cmd_pending + strspn(cmd_pending, " \t\r\n");
  if(cmd_state == CMD_TOO_LONG)
  {
    reply.print("{\"result\":\"ERROR\",\"errors\":{\"payload\":\"too long (max ");
    reply.print(MQTT_CMD_MAX);
    reply.println(" bytes)\"}}");
  }
  else if(*json != '{')
  {
    reply.println("{\"result\":\"ERROR\",\"errors\":{\"payload\":\"JSON object expected\"}}");
  }
  else
  {
    settings_json_import(json, &reply, false, true);
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
    Serial.println(reply.c_str()[11] == 'O' ? "OK" : "ERROR"); //{"result":"OK"...
  }
}
#endif //WITH_MQTT_CONFIG

#if WITH_HA
// Home Assistant MQTT discovery: one retained config per entity under
// <discovery prefix>/sensor/<device id>/<key>/config, reading from the existing JSON topics.
typedef struct
{
  const char *key;          // JSON key in the state topic
  const char *name;         // entity name (shown after the device name)
  const char *state;        // state topic suffix: "sensors" or "wifi"
  const char *device_class; // NULL = none
  const char *unit;         // NULL = none
  bool diagnostic;
  unsigned int needs;       // required feature bits, 0 = always
} HA_ENTITY;

static const HA_ENTITY ha_entities[] =
{
  { "co2",           "CO2",                         "sensors", "carbon_dioxide",       "ppm", false, 0 },
  { "temperature",   "Temperature",                 "sensors", "temperature",          "°C",  false, 0 },
  { "humidity",      "Humidity",                    "sensors", "humidity",             "%",   false, 0 },
  { "pressure",      "Pressure",                    "sensors", "atmospheric_pressure", "hPa", false, FEATURE_LPS22HB|FEATURE_BMP280 },
  { "pressure_temp", "Pressure sensor temperature", "sensors", "temperature",          "°C",  true,  FEATURE_LPS22HB|FEATURE_BMP280 },
  { "light",         "Light",                       "sensors", NULL,                   NULL,  false, 0 },
  { "rssi",          "WiFi signal",                 "wifi",    "signal_strength",      "dBm", true,  0 },
};

static bool ha_published = false; //Konfigurationen seit dem Start veroeffentlicht (fuer das Entfernen)

static void mqtt_publish_discovery(void)
{
  char device_id[16], topic[96], base[64], json[768];
  IPAddress ip = WiFi.localIP();

  get_device_id(device_id, sizeof(device_id));
  snprintf(base, sizeof(base), "%s/%s", settings.mqtt_topic_prefix, device_id);

  for(const HA_ENTITY &e : ha_entities)
  {
    snprintf(topic, sizeof(topic), MQTT_DISCOVERY_PREFIX "/sensor/%s/%s/config", device_id, e.key);

    //aus oder Sensor fehlt: leere retained Nachricht entfernt die Entitaet in Home Assistant
    if(!settings.mqtt_ha || (e.needs && !(features & e.needs)))
    {
      mqttClient.publish(topic, "", true, 0);
      continue;
    }

    size_t n = snprintf(json, sizeof(json),
      "{\"name\":\"%s\",\"unique_id\":\"co2ampel_%s_%s\","
      "\"state_topic\":\"%s/%s\",\"value_template\":\"{{ value_json.%s }}\","
      "\"availability_topic\":\"%s/status\",\"state_class\":\"measurement\"",
      e.name, device_id, e.key, base, e.state, e.key, base);
    if(e.device_class) { n += snprintf(json + n, sizeof(json) - n, ",\"device_class\":\"%s\"", e.device_class); }
    if(e.unit)         { n += snprintf(json + n, sizeof(json) - n, ",\"unit_of_measurement\":\"%s\"", e.unit); }
    if(e.diagnostic)   { n += snprintf(json + n, sizeof(json) - n, ",\"entity_category\":\"diagnostic\""); }
    n += snprintf(json + n, sizeof(json) - n,
      ",\"device\":{\"identifiers\":[\"co2ampel_%s\"],\"name\":\"CO2-Ampel %s\","
      "\"model\":\"CO2-Ampel Pro\",\"manufacturer\":\"Watterott\",\"sw_version\":\"%s\","
      "\"configuration_url\":\"http://%u.%u.%u.%u/\"}}",
      device_id, device_id + 8, VERSION, ip[0], ip[1], ip[2], ip[3]);

    if(n >= sizeof(json))
    {
      continue; //zu lang (sollte nicht vorkommen)
    }
    mqttClient.publish(topic, json, true, 0);
  }
}
#endif //WITH_HA

void mqtt_ha_update(void)
{
#if WITH_HA
  if(!mqttClient.connected())
  {
    return; //beim naechsten Verbindungsaufbau (nur wenn an)
  }
  if(settings.mqtt_ha || ha_published)
  {
    mqtt_publish_discovery(); //an: Konfigurationen, aus: einmal leere Nachrichten
    ha_published = settings.mqtt_ha;
  }
#endif
}

void print_mqtt_status(void)
{
  Serial.print("MQTT Enabled: ");
  Serial.println(settings.mqtt_enabled ? "Yes" : "No");
  Serial.print("Broker: ");
  Serial.print(settings.mqtt_broker);
  Serial.print(":");
  Serial.print(settings.mqtt_port);
  Serial.println(settings.mqtt_tls ? " (TLS)" : "");
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
#if WITH_HA
  Serial.print("HA discovery: ");
  Serial.println(settings.mqtt_ha ? "on (" MQTT_DISCOVERY_PREFIX ")" : "off");
#endif
#if WITH_MQTT_CONFIG
  Serial.print("Remote config: ");
  Serial.println(settings.mqtt_config ? "on (<prefix>/<id>/cmd)" : "off");
#endif
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
    Serial.print(settings.mqtt_tls ? " (TLS)" : "");
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
  Client &net = settings.mqtt_tls ? (Client &)mqttSslClient : (Client &)mqttWifiClient;
  mqttClient.begin(settings.mqtt_broker, settings.mqtt_port, net);
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
#if WITH_HA
    ha_published = false;
    if(settings.mqtt_ha) //aus: nichts senden (Entfernen nur beim Ausschalten, siehe mqtt_ha_update)
    {
      mqtt_publish_discovery();
      ha_published = true;
    }
#endif
#if WITH_MQTT_CONFIG
    if(settings.mqtt_config)
    {
      mqttClient.onMessageAdvanced(mqtt_message);
      mqtt_topic(topic, sizeof(topic), "cmd");
      mqttClient.subscribe(topic, 0);
    }
#endif
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
          case LWMQTT_NETWORK_FAILED_CONNECT:
            Serial.println("Network connection failed");
            if(settings.mqtt_tls)
            {
              Serial.println("  TLS: root CA stored in the WINC1500? Does mqtt.broker match the certificate? (see README)");
            }
            break;
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
  //nur echte Messwerte: ohne CO2-Sensor (oder vor der ersten Messung) fehlen co2/temperature/humidity
  char f1[16], f2[16];
  n = snprintf(json, sizeof(json), "{\"light\":%u", light_value);
  if(co2_valid)
  {
    n += snprintf(json + n, sizeof(json) - n, ",\"co2\":%u,\"temperature\":%s,\"humidity\":%s",
                  co2_value, fmt1(f1, temp_value), fmt1(f2, humi_value));
  }
  if(pres_valid)
  {
    n += snprintf(json + n, sizeof(json) - n, ",\"pressure\":%s,\"pressure_temp\":%s", fmt1(f1, pres_value), fmt1(f2, temp2_value));
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

#if WITH_MQTT_CONFIG
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
#endif

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
