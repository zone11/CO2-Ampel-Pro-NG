#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H

#include <WiFi101.h>

// TLS client that also checks the server certificate against the hostname.
// WiFiSSLClient (WiFi101 0.16) sends SNI but never enables the WINC1500's SNI validation,
// so any certificate from a trusted root CA would be accepted (verified on hardware).
// The socket is created here with both options and then handed to a WiFiClient.
class VerifiedSSLClient : public Client
{
public:
  int connect(IPAddress ip, uint16_t port) override; //no hostname to check: refused
  int connect(const char *host, uint16_t port) override;
  size_t write(uint8_t b) override { return client.write(b); }
  size_t write(const uint8_t *buf, size_t size) override { return client.write(buf, size); }
  int available() override { return client.available(); }
  int read() override { return client.read(); }
  int read(uint8_t *buf, size_t size) override { return client.read(buf, size); }
  int peek() override { return client.peek(); }
  void flush() override { client.flush(); }
  void stop() override { client.stop(); }
  uint8_t connected() override { return client.connected(); }
  operator bool() override { return (bool)client; }

private:
  WiFiClient client;
};

#endif
