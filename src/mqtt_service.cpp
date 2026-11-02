#include "mqtt_service.h"
#include "sensors.h"
#include "settings_registry.h"
#include "system_control.h"
#include "health.h"
#include "wifi_manager.h"

#if WITH_MQTT_CONFIG
#define MQTT_CMD_MAX   1024 //max. JSON on <base>/cmd (read buffer in main.cpp)
#define MQTT_REPLY_MAX 1400 //max. reply on <base>/resp (write buffer 1536)

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
      memcpy(&buf[len], truncated, sizeof(truncated)); //incl. terminator
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

// Unique chip ID of the SAMD21
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

static int state_published = -1; //quiet mode last published on <base>/state, -1 = not yet

// Prefix and client id of the current connection: all topics use them until the next connect,
// a change of the settings reconnects (mqtt_service)
static char used_prefix[sizeof(settings.mqtt_topic_prefix)];
static char used_client_id[sizeof(settings.mqtt_client_id)];

// Topic <prefix>/<device id>/<suffix>
static void mqtt_topic(char *buffer, size_t buffer_size, const char *suffix)
{
  char device_id[16];
  get_device_id(device_id, sizeof(device_id));
  snprintf(buffer, buffer_size, "%s/%s/%s", used_prefix, device_id, suffix);
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
  (void)topic; //only <base>/cmd is subscribed

  if(length <= 0) //empty = our own clearing of a retained command
  {
    return;
  }
  if(cmd_state != CMD_NONE) //previous command not handled yet
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

// Runs the pending JSON command through settings_json_import() (as a remote caller:
// secrets masked, CFG_LOCAL keys refused) and publishes the reply on <base>/resp.
static void mqtt_handle_command(void)
{
  char topic[96];

  //clear the retained command so it doesn't run again on every connect
  mqtt_topic(topic, sizeof(topic), "cmd");
  mqttClient.publish(topic, "", true, 0);

  //JSON only (like "json import"): settings without a password, protected by the broker ACLs;
  //connection settings (CFG_LOCAL) stay locked
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
    settings_json_import(json, &reply, false, true, !settings.mqtt_config); //off: control only (sys.quiet)
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
  bool text;                // text sensor (no state_class)
} HA_ENTITY;

static const HA_ENTITY ha_entities[] =
{
  { "co2",           "CO2",                         "sensors", "carbon_dioxide",       "ppm", false, 0, false },
  { "temperature",   "Temperature",                 "sensors", "temperature",          "°C",  false, 0, false },
  { "humidity",      "Humidity",                    "sensors", "humidity",             "%",   false, 0, false },
  { "pressure",      "Pressure",                    "sensors", "atmospheric_pressure", "hPa", false, FEATURE_LPS22HB|FEATURE_BMP280, false },
  { "pressure_temp", "Pressure sensor temperature", "sensors", "temperature",          "°C",  true,  FEATURE_LPS22HB|FEATURE_BMP280, false },
  { "light",         "Light",                       "sensors", NULL,                   NULL,  false, 0, false },
  { "rssi",          "WiFi signal",                 "wifi",    "signal_strength",      "dBm", true,  0, false },
  { "uptime",        "Uptime",                      "diag",    "duration",             "s",   true,  0, false },
  { "reset",         "Reset cause",                 "diag",    NULL,                   NULL,  true,  0, true  },
  { "last_phase",    "Last reset phase",            "diag",    NULL,                   NULL,  true,  0, true  },
  { "ram_free_min",  "Free RAM minimum",            "diag",    "data_size",            "B",   true,  0, false },
  { "loop_max_ms",   "Longest loop",                "diag",    "duration",             "ms",  true,  0, false },
};

static bool ha_published = false; //configs published since boot (for the removal)

// Appends the HA device block and closes the config object; returns the new length
static size_t ha_device(char *json, size_t size, size_t n, const char *device_id, const char *name)
{
  IPAddress ip = WiFi.localIP();
  return n + snprintf(json + n, size - n,
    ",\"device\":{\"identifiers\":[\"co2ampel_%s\"],\"name\":\"%s\","
    "\"model\":\"" PRODUCT_NAME "\",\"manufacturer\":\"" MANUFACTURER "\",\"sw_version\":\"%s\","
    "\"configuration_url\":\"http://%u.%u.%u.%u/\"}}",
    device_id, name, VERSION, ip[0], ip[1], ip[2], ip[3]);
}

static void mqtt_publish_discovery(void)
{
  char device_id[16], topic[96], base[64], json[768], name[2*32+1];

  get_device_id(device_id, sizeof(device_id));
  json_escape(name, sizeof(name), system_name());
  snprintf(base, sizeof(base), "%s/%s", used_prefix, device_id);

  for(const HA_ENTITY &e : ha_entities)
  {
    snprintf(topic, sizeof(topic), MQTT_DISCOVERY_PREFIX "/sensor/%s/%s/config", device_id, e.key);

    //off or sensor missing: an empty retained message removes the entity in Home Assistant
    if(!settings.mqtt_ha || (e.needs && !(features & e.needs)))
    {
      mqttClient.publish(topic, "", true, 0);
      continue;
    }

    size_t n = snprintf(json, sizeof(json),
      "{\"name\":\"%s\",\"unique_id\":\"co2ampel_%s_%s\","
      "\"state_topic\":\"%s/%s\",\"value_template\":\"{{ value_json.%s }}\","
      "\"availability_topic\":\"%s/status\"%s",
      e.name, device_id, e.key, base, e.state, e.key, base, e.text ? "" : ",\"state_class\":\"measurement\"");
    if(e.device_class) { n += snprintf(json + n, sizeof(json) - n, ",\"device_class\":\"%s\"", e.device_class); }
    if(e.unit)         { n += snprintf(json + n, sizeof(json) - n, ",\"unit_of_measurement\":\"%s\"", e.unit); }
    if(e.diagnostic)   { n += snprintf(json + n, sizeof(json) - n, ",\"entity_category\":\"diagnostic\""); }
    n = ha_device(json, sizeof(json), n, device_id, name);

    if(n >= sizeof(json))
    {
      continue; //too long (should not happen)
    }
    mqttClient.publish(topic, json, true, 0);
  }

  //switches from <base>/state, commands on <base>/cmd (also with mqtt.config=0, RAM only)
  //"Quiet mode": on = sys.quiet true
  static const struct { const char *key, *name, *icon, *on, *off, *state; } switches[] =
  {
    { "quiet", "Quiet mode", "mdi:sleep", "{\\\"sys.quiet\\\":true}", "{\\\"sys.quiet\\\":false}",
      "{{ 'ON' if value_json.quiet else 'OFF' }}" },
  };
  //switches of 0.6/0.7 development builds: remove them from Home Assistant
  for(const char *old : { "led", "buzzer" })
  {
    snprintf(topic, sizeof(topic), MQTT_DISCOVERY_PREFIX "/switch/%s/%s/config", device_id, old);
    mqttClient.publish(topic, "", true, 0);
  }
  for(const auto &sw : switches)
  {
    snprintf(topic, sizeof(topic), MQTT_DISCOVERY_PREFIX "/switch/%s/%s/config", device_id, sw.key);
#if WITH_MQTT_CONFIG
    if(settings.mqtt_ha)
    {
      size_t n = snprintf(json, sizeof(json),
        "{\"name\":\"%s\",\"unique_id\":\"co2ampel_%s_%s\",\"icon\":\"%s\","
        "\"command_topic\":\"%s/cmd\",\"payload_on\":\"%s\",\"payload_off\":\"%s\","
        "\"state_topic\":\"%s/state\",\"value_template\":\"%s\","
        "\"state_on\":\"ON\",\"state_off\":\"OFF\"," //without them HA compares the template result with payload_on/off
        "\"availability_topic\":\"%s/status\"",
        sw.name, device_id, sw.key, sw.icon, base, sw.on, sw.off, base, sw.state, base);
      n = ha_device(json, sizeof(json), n, device_id, name);
      if(n < sizeof(json))
      {
        mqttClient.publish(topic, json, true, 0);
      }
      continue;
    }
#endif
    mqttClient.publish(topic, "", true, 0); //off (or no command topic): remove the switch
  }
}
#endif //WITH_HA

void mqtt_ha_update(void)
{
#if WITH_HA
  if(!mqttClient.connected())
  {
    return; //on the next connect (only if on)
  }
  if(settings.mqtt_ha || ha_published)
  {
    mqtt_publish_discovery(); //on: configs, off: empty messages once
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
  Serial.println(settings.mqtt_config ? "on (<prefix>/<id>/cmd)" : "off (<prefix>/<id>/cmd: sys.quiet only)");
#endif
  if(settings.mqtt_enabled && (features & FEATURE_WINC1500))
  {
    Serial.print("WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    Serial.print("Status: ");
    Serial.println(mqttClient.connected() ? "Connected" : "Disconnected");
  }
}

static boolean mqtt_try_connect(const char *client_id)
{
  if(strlen(settings.mqtt_user) > 0)
  {
    return mqttClient.connect(client_id, settings.mqtt_user, settings.mqtt_pass); //with authentication
  }
  return mqttClient.connect(client_id); //without authentication
}

// Result of the last failed connect (library error and broker return code)
static int last_err = LWMQTT_SUCCESS, last_rc = LWMQTT_CONNECTION_ACCEPTED;
static unsigned long last_publish_ms = 0; //last sensors publish, 0 = none yet

static const char *err_text(int err)
{
  switch(err)
  {
    case LWMQTT_NETWORK_FAILED_CONNECT: return "Network connection failed";
    case LWMQTT_NETWORK_TIMEOUT:        return "Network timeout";
    case LWMQTT_NETWORK_FAILED_READ:    return "Network read failed";
    case LWMQTT_NETWORK_FAILED_WRITE:   return "Network write failed";
    case LWMQTT_CONNECTION_DENIED:      return "Connection denied by broker";
    default:                            return NULL;
  }
}

static const char *rc_text(int rc)
{
  switch(rc)
  {
    case LWMQTT_UNACCEPTABLE_PROTOCOL:    return "Unacceptable protocol version";
    case LWMQTT_IDENTIFIER_REJECTED:      return "Client ID rejected";
    case LWMQTT_SERVER_UNAVAILABLE:       return "Server unavailable";
    case LWMQTT_BAD_USERNAME_OR_PASSWORD: return "Bad username or password";
    case LWMQTT_NOT_AUTHORIZED:           return "Not authorized";
    default:                              return NULL;
  }
}

static void print_code(const char *text, int code)
{
  if(text)
  {
    Serial.println(text);
  }
  else
  {
    Serial.print("Code ");
    Serial.println(code);
  }
}

const char *mqtt_error_text(void)
{
  static char code[24];
  if(last_rc != LWMQTT_CONNECTION_ACCEPTED) //the broker's answer says more than the library error
  {
    const char *t = rc_text(last_rc);
    if(t) { return t; }
    snprintf(code, sizeof(code), "Return code %d", last_rc);
    return code;
  }
  if(last_err != LWMQTT_SUCCESS)
  {
    const char *t = err_text(last_err);
    if(t) { return t; }
    snprintf(code, sizeof(code), "Error %d", last_err);
    return code;
  }
  return "";
}

long mqtt_publish_age(void)
{
  return (last_publish_ms == 0) ? -1 : (long)((millis() - last_publish_ms) / 1000);
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
    return; //no WiFi hardware
  }

  if(WiFi.status() != WL_CONNECTED)
  {
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT: WiFi not connected");
    }
    return; //no WiFi connection
  }

  //generate the MQTT client ID if empty
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

  //set up the MQTT client
  char topic[96];
  strcpy(used_prefix, settings.mqtt_topic_prefix);
  strcpy(used_client_id, settings.mqtt_client_id);
  Client &net = settings.mqtt_tls ? (Client &)mqttSslClient : (Client &)mqttWifiClient;
  mqttClient.begin(settings.mqtt_broker, settings.mqtt_port, net);
  mqtt_topic(topic, sizeof(topic), "status");
  mqttClient.setWill(topic, "offline", true, 1); //broker reports "offline" when the connection drops

  //connect (DNS, TCP and TLS can block up to ~40 s)
  watchdog_feed();
  phase_set(PHASE_MQTT_CONNECT);
  unsigned long t_connect = millis();
  boolean connected = mqtt_try_connect(client_id);
  if(!connected && (mqttClient.lastError() == LWMQTT_NETWORK_FAILED_CONNECT) && ((millis() - t_connect) < 2000))
  {
    //refused at once: after a quick restart the broker may still hold the previous connection,
    //a new attempt (new socket) a moment later usually works
    last_err = LWMQTT_NETWORK_FAILED_CONNECT;
    last_rc = LWMQTT_CONNECTION_ACCEPTED;
    health.mqtt_failures++;
    health.mqtt_error = last_err;
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT connection refused, retrying in 2 s");
    }
    delay(2000);
    watchdog_feed();
    connected = mqtt_try_connect(client_id);
  }

  if(connected)
  {
    health.mqtt_connects++;
    mqttClient.publish(topic, "online", true, 1);
    state_published = -1; //publish <base>/state again
#if WITH_HA
    ha_published = false;
    if(settings.mqtt_ha) //off: send nothing (removal only when switched off, see mqtt_ha_update)
    {
      mqtt_publish_discovery();
      ha_published = true;
    }
#endif
#if WITH_MQTT_CONFIG
    //always subscribed: control (sys.quiet) also works with mqtt.config=0, configuration only with 1
    mqttClient.onMessageAdvanced(mqtt_message);
    mqtt_topic(topic, sizeof(topic), "cmd");
    mqttClient.subscribe(topic, 0);
#endif
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT connected successfully");
    }
  }
  else
  {
    last_err = mqttClient.lastError();
    last_rc = mqttClient.returnCode();
    health.mqtt_failures++;
    health.mqtt_error = last_err;
    if(features & FEATURE_USB)
    {
      Serial.print("MQTT connection failed - ");

      //detailed error output
      if(last_err != LWMQTT_SUCCESS)
      {
        Serial.print("Error: ");
        print_code(err_text(last_err), last_err);
        if((last_err == LWMQTT_NETWORK_FAILED_CONNECT) && settings.mqtt_tls)
        {
          Serial.println("  TLS: root CA stored in the WINC1500? Does mqtt.broker match the certificate? (see README)");
        }
      }
      if(last_rc != LWMQTT_CONNECTION_ACCEPTED)
      {
        Serial.print("Return Code: ");
        print_code(rc_text(last_rc), last_rc);
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

  //retry every 30 seconds only
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

  //readings as one JSON (one consistent record per interval)
  //only real readings: without a CO2 sensor (or before the first reading) co2/temperature/humidity are missing
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
  if(mqttClient.publish(topic, json, false, 0))
  {
    last_publish_ms = millis() | 1; //never 0
  }

  //device: firmware version and detected sensors (retained, so always available)
  char name[2*32+1];
  json_escape(name, sizeof(name), system_name());
  n = snprintf(json, sizeof(json), "{\"name\":\"%s\",\"version\":\"%s\",\"reset\":\"%s\",\"sensors\":[", name, VERSION, reset_cause());
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

  //health (retained: the last state before a hang stays on the broker)
  char diag[512];
  health_json(diag, sizeof(diag));
  mqtt_topic(topic, sizeof(topic), "diag");
  mqttClient.publish(topic, diag, true, 0);

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
  phase_set(PHASE_MQTT_LOOP);
  mqttClient.loop();

#if WITH_MQTT_CONFIG
  //handle the command from <base>/cmd (outside the callback)
  if(cmd_state != CMD_NONE)
  {
    if(mqttClient.connected())
    {
      phase_set(PHASE_MQTT_CMD);
      mqtt_handle_command();
    }
    else
    {
      cmd_state = CMD_NONE;
    }
  }
#endif

  //check the connection
  if(!mqttClient.connected())
  {
    mqtt_reconnect();
    return;
  }

  //topic prefix or client id changed: reconnect, so LWT, subscription and HA discovery use the new topics
  if((strcmp(used_prefix, settings.mqtt_topic_prefix) != 0) || (strcmp(used_client_id, settings.mqtt_client_id) != 0))
  {
    if(features & FEATURE_USB)
    {
      Serial.println("MQTT topic prefix or client ID changed, reconnecting");
    }
    mqtt_shutdown(); //"offline" on the old status topic
    mqtt_connect();
    return;
  }

  //control state (retained): on connect and right after every change of quiet mode
  if(state_published != (int)settings.quiet)
  {
    char topic[96];
    state_published = settings.quiet;
    mqtt_topic(topic, sizeof(topic), "state");
    mqttClient.publish(topic, settings.quiet ? "{\"quiet\":true}" : "{\"quiet\":false}", true, 0);
  }

  //periodic publishing
  if((millis() - last_publish) > (settings.mqtt_interval * 1000UL))
  {
    last_publish = millis();
    phase_set(PHASE_MQTT_PUBLISH);
    mqtt_publish_sensors();
  }
}

void mqtt_shutdown(void)
{
  if(!mqttClient.connected())
  {
    return;
  }
  char topic[96];
  mqtt_topic(topic, sizeof(topic), "status");
  mqttClient.publish(topic, "offline", true, 1); //a clean disconnect sends no LWT
  mqttClient.disconnect();
}
