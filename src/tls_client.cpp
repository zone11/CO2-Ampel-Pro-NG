#include "tls_client.h"

#include <utility/WiFiSocket.h>

int VerifiedSSLClient::connect(IPAddress ip, uint16_t port)
{
  (void)ip;
  (void)port;
  return 0;
}

int VerifiedSSLClient::connect(const char *host, uint16_t port)
{
  IPAddress ip;
  struct sockaddr_in addr;
  int enable = 1;

  if(client.connected())
  {
    client.stop();
  }
  if(!WiFi.hostByName(host, ip))
  {
    return 0;
  }

  SOCKET sock = WiFiSocket.create(AF_INET, SOCK_STREAM, SOCKET_FLAGS_SSL);
  if(sock < 0)
  {
    return 0;
  }
  WiFiSocket.setopt(sock, SOL_SSL_SOCKET, SO_SSL_SNI, host, strlen(host));
  WiFiSocket.setopt(sock, SOL_SSL_SOCKET, SO_SSL_ENABLE_SNI_VALIDATION, &enable, sizeof(enable));

  addr.sin_family = AF_INET;
  addr.sin_port = _htons(port);
  addr.sin_addr.s_addr = ip;
  if(!WiFiSocket.connect(sock, (struct sockaddr *)&addr, sizeof(addr)))
  {
    WiFiSocket.close(sock);
    return 0;
  }

  client = WiFiClient(sock);
  return 1;
}
