#include "winc_bridge.h"

#include "app_globals.h"
#include "led_control.h"

#include <spi_flash/include/spi_flash.h>

// Protocol of the WiFi101 FirmwareUpdater sketch: header (big endian) command u8, address u32,
// argument u32, payload length u16, then the payload. Replies are raw data and/or "OK" / "ER".
#define CMD_READ        0x01 //address, arg = length -> data + OK
#define CMD_WRITE       0x02 //address, payload      -> OK
#define CMD_ERASE       0x03 //address, arg = length -> OK
#define CMD_MAX_PAYLOAD 0x50 //                      -> u16 (big endian)
#define CMD_RESTART     0x7F //                      -> OK, then restart (not in the original sketch)
#define CMD_HELLO       0x99 //0x11223344, 0x55667788 -> "v10000"

#define MAX_PAYLOAD     1024
#define HEADER_SIZE     11
#define WRITE_START     0x4000  //only the root certificate store may be changed
#define WRITE_END       0x5000
#define IDLE_TIMEOUT    60000UL //restart after 60s without a valid command
#define RESYNC_GAP      50      //ms of silence after which a broken packet is dropped

static unsigned long t_valid; //last valid command

static void restart(void)
{
  Serial.flush();
  delay(100);
  NVIC_SystemReset();
  while(1);
}

// Reads exactly len bytes; restarts the device IDLE_TIMEOUT after the last valid command
// (also when only garbage arrives, e.g. a terminal typing into the bridge)
static void read_bytes(uint8_t *buf, size_t len)
{
  while(len > 0)
  {
    int c = Serial.read();
    if(c < 0)
    {
      if((millis() - t_valid) > IDLE_TIMEOUT)
      {
        restart();
      }
      continue;
    }
    *buf++ = (uint8_t)c;
    len--;
  }
}

// Invalid packet: drops input until RESYNC_GAP ms of silence, so the next packet starts in sync
static void resync(void)
{
  unsigned long t = millis();
  while((millis() - t) < RESYNC_GAP)
  {
    if(Serial.read() >= 0)
    {
      t = millis();
    }
    if((millis() - t_valid) > IDLE_TIMEOUT)
    {
      restart();
    }
  }
  Serial.print("ER");
}

static uint32_t be32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool write_allowed(uint32_t addr, uint32_t len)
{
  return (addr >= WRITE_START) && (len <= (WRITE_END - addr));
}

static void reply(bool ok)
{
  Serial.print(ok ? "OK" : "ER");
}

void winc_bridge(void)
{
  uint8_t hdr[HEADER_SIZE];
  uint8_t payload[MAX_PAYLOAD];

  Serial.println("OK bridge");
  Serial.flush();
  leds(COLOR_BLUE);
  t_valid = millis();

  WiFi.end();
  nm_bsp_init();
  if(m2m_wifi_download_mode() != M2M_SUCCESS)
  {
    Serial.println("ERROR: WiFi module flash not accessible");
    restart();
  }

  while(1)
  {
    read_bytes(hdr, sizeof(hdr));
    uint8_t cmd = hdr[0];
    uint32_t addr = be32(&hdr[1]);
    uint32_t arg = be32(&hdr[5]);
    uint16_t len = ((uint16_t)hdr[9] << 8) | hdr[10];

    if((len > MAX_PAYLOAD) || ((cmd != CMD_WRITE) && (len != 0))) //not a valid packet
    {
      resync();
      continue;
    }
    read_bytes(payload, len);

    switch(cmd)
    {
      case CMD_HELLO:
        if((addr != 0x11223344) || (arg != 0x55667788))
        {
          resync();
          continue;
        }
        Serial.print("v10000");
        break;
      case CMD_MAX_PAYLOAD:
        Serial.write((uint8_t)(MAX_PAYLOAD >> 8));
        Serial.write((uint8_t)(MAX_PAYLOAD & 0xFF));
        break;
      case CMD_READ:
        if((arg > MAX_PAYLOAD) || (spi_flash_read(payload, addr, arg) != M2M_SUCCESS))
        {
          reply(false);
        }
        else
        {
          Serial.write(payload, arg);
          reply(true);
        }
        break;
      case CMD_WRITE:
        reply(write_allowed(addr, len) && (spi_flash_write(payload, addr, len) == M2M_SUCCESS));
        break;
      case CMD_ERASE:
        reply(write_allowed(addr, arg) && (spi_flash_erase(addr, arg) == M2M_SUCCESS));
        break;
      case CMD_RESTART:
        reply(true);
        restart();
        break;
      default:
        resync();
        continue;
    }
    t_valid = millis();
  }
}
