// Only the type names that include/app_globals.h declares; the settings code doesn't use these objects
#ifndef LIB_STUBS_H
#define LIB_STUBS_H

#include <Arduino.h>

class SensirionI2cScd30 {};
class SensirionI2cScd4x {};
class Adafruit_BMP280 {};
class LPS22HBClass {};
class Adafruit_NeoPixel {};
class WiFiClient {};
class WiFiSSLClient : public WiFiClient {};
class WiFiServer {};
class MQTTClient {};

// include/tls_client.h needs the real WiFi101
#define TLS_CLIENT_H
class VerifiedSSLClient {};

#endif
