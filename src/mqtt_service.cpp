#include "mqtt_service.h"

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
  mqttClient.begin(settings.mqtt_broker, settings.mqtt_port, mqttWifiClient);

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
  char topic[128];
  char value[64];
  char device_id[32];
  char chip_id[40];

  if(!settings.mqtt_enabled)
  {
    return;
  }

  if(!mqttClient.connected())
  {
    return;
  }

  //Device-ID aus MAC-Adresse generieren
  get_device_id(device_id, sizeof(device_id));

 /*
  //Chip-ID holen
  get_chip_id(chip_id, sizeof(chip_id));


  //Chip-ID publizieren
  sprintf(topic, "%s/%s/chipid", settings.mqtt_topic_prefix, device_id);
  mqttClient.publish(topic, chip_id, true, 0); //retained, damit immer verfugbar
  */

  //CO2
  sprintf(topic, "%s/%s/co2", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%d", co2_value);
  mqttClient.publish(topic, value, false, 0);

  //Temperatur
  sprintf(topic, "%s/%s/temperature", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%.1f", temp_value);
  mqttClient.publish(topic, value, false, 0);

  //Luftfeuchtigkeit
  sprintf(topic, "%s/%s/humidity", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%.1f", humi_value);
  mqttClient.publish(topic, value, false, 0);

  //Lichtsensor
  sprintf(topic, "%s/%s/light", settings.mqtt_topic_prefix, device_id);
  sprintf(value, "%d", light_value);
  mqttClient.publish(topic, value, false, 0);

  //Druck (nur Pro Version)
  if(features & (FEATURE_LPS22HB|FEATURE_BMP280))
  {
    sprintf(topic, "%s/%s/pressure", settings.mqtt_topic_prefix, device_id);
    sprintf(value, "%.1f", pres_value);
    mqttClient.publish(topic, value, false, 0);
  }

  if(features & FEATURE_USB)
  {
    Serial.print("MQTT published to ");
    Serial.print(settings.mqtt_topic_prefix);
    Serial.print("/");
    Serial.println(device_id);
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
