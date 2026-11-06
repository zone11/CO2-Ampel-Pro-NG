#include "lora_service.h"
#include "app_globals.h"

#include <SPI.h>

bool lora_radio_found(void)
{
  SPI.begin();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_RFM9X_CS, LOW);
  SPI.transfer(0x42); //version register
  byte version = SPI.transfer(0x00);
  digitalWrite(PIN_RFM9X_CS, HIGH);
  SPI.endTransaction();
  return version == 0x12;
}

#if WITH_LORA

#include "calibration.h"
#include "errors.h"
#include "system_control.h"

#include <lmic.h>
#include <hal/hal.h>

#define LORA_PORT        1      //uplink port of the sensor payload
#define LORA_PAYLOAD     10     //bytes, see payload()
#define LORA_JOIN_DELAY  30     //s, maximum random delay before the first join
#define FAIR_USE_FACTOR  2880UL //86400 s / 30 s: interval = airtime * factor stays under 30 s airtime per day

const lmic_pinmap lmic_pins = {
  PIN_RFM9X_CS, LMIC_UNUSED_PIN, LMIC_UNUSED_PIN, { PIN_RFM9X_DIO0, PIN_RFM9X_DIO1, LMIC_UNUSED_PIN },
  0, 0, 0, NULL
};

typedef enum { LORA_OFF, LORA_NO_RADIO, LORA_NO_DEVEUI, LORA_NO_APPKEY, LORA_WAITING, LORA_JOINING, LORA_JOINED } lora_state_t;
static const char *const state_names[] = { "off", "no radio", "no DevEUI", "no AppKey", "waiting to join", "joining", "joined" };

static bool radio = false;
static lora_state_t state = LORA_OFF;
static uint8_t deveui[8], joineui[8], appkey[16]; //EUIs LSB first (LMIC), key MSB first
static bool deveui_from_mac = false;
static unsigned long t_next = 0, t_last = 0;      //ms: next action (join or uplink), last uplink done
static unsigned long interval_ms = 0;             //current uplink interval incl. the fair use stretch
static uint32_t uplinks = 0, join_attempts = 0;
static int16_t dn_rssi = 0, dn_snr = 0;            //last downlink (snr in 0.25 dB)
static bool dn_seen = false;

// LMIC key callbacks
void os_getArtEui(u1_t *buf) { memcpy(buf, joineui, 8); }
void os_getDevEui(u1_t *buf) { memcpy(buf, deveui, 8); }
void os_getDevKey(u1_t *buf) { memcpy(buf, appkey, 16); }

// "70B3D57ED0012345" -> bytes; reverse = LSB first
static bool parse_hex(const char *s, uint8_t *out, size_t n, bool reverse)
{
  if(strlen(s) != 2 * n)
  {
    return false;
  }
  for(size_t i = 0; i < n; i++)
  {
    char b[3] = { s[2*i], s[2*i+1], 0 };
    out[reverse ? (n - 1 - i) : i] = (uint8_t)strtoul(b, NULL, 16);
  }
  return true;
}

static void print_eui(char *buf, const uint8_t *eui) //LSB first in, MSB first out
{
  for(int i = 0; i < 8; i++)
  {
    sprintf(buf + 2*i, "%02X", eui[7 - i]);
  }
}

static uint8_t spreading_factor(void)
{
  return 12 - LMIC.datarate; //EU868: DR0 = SF12 .. DR5 = SF7
}

static unsigned long airtime_ms(void)
{
  rps_t rps = makeRps((sf_t)(SF12 - LMIC.datarate), BW125, CR_4_5, 0, 0);
  return osticks2ms(calcAirTime(rps, 13 + LORA_PAYLOAD)); //13 bytes LoRaWAN header, port and MIC
}

// Port 1, big endian: CO2 ppm u16, temperature 0.1 C s16, humidity 0.5 % u8, pressure 0.1 hPa u16, light u16,
// flags u8 (bit0 quiet, bit1 any error, bit2 calibrating, bit4 buzzer); a missing value is all ones (0x7FFF for temperature)
static void payload(uint8_t *p)
{
  uint16_t co2 = co2_valid ? co2_value : 0xFFFF;
  int16_t t = co2_valid ? (int16_t)(temp_value * 10.0f + ((temp_value >= 0) ? 0.5f : -0.5f)) : 0x7FFF;
  uint8_t h = co2_valid ? (uint8_t)(humi_value * 2.0f + 0.5f) : 0xFF;
  uint16_t pr = pres_valid ? (uint16_t)(pres_value * 10.0f + 0.5f) : 0xFFFF;
  uint16_t l = (light_value != LIGHT_NONE) ? light_value : 0xFFFF;
  p[0] = co2 >> 8;          p[1] = co2 & 0xFF;
  p[2] = (uint16_t)t >> 8;  p[3] = (uint16_t)t & 0xFF;
  p[4] = h;
  p[5] = pr >> 8;           p[6] = pr & 0xFF;
  p[7] = l >> 8;            p[8] = l & 0xFF;
  p[9] = (settings.quiet ? 0x01 : 0) | (errors_any() ? 0x02 : 0) | (calibration_running() ? 0x04 : 0) | (settings.buzzer ? 0x10 : 0);
}

static void on_event(void *user, ev_t ev)
{
  (void)user;
  switch(ev)
  {
    case EV_JOINING:
      state = LORA_JOINING;
      break;
    case EV_JOIN_TXCOMPLETE: //no join accept, LMIC retries on its own with growing delays
      join_attempts++;
      break;
    case EV_JOINED:
      join_attempts++;
      state = LORA_JOINED;
      LMIC_setLinkCheckMode(0);
      t_next = millis(); //first uplink right away
      break;
    case EV_TXCOMPLETE:
      uplinks++;
      t_last = millis();
      if(LMIC.dataLen || (LMIC.txrxFlags & (TXRX_DNW1 | TXRX_DNW2)))
      {
        dn_seen = true;
        dn_rssi = LMIC.rssi - RSSI_OFF;
        dn_snr = LMIC.snr;
      }
      break;
    case EV_RESET:
    case EV_LINK_DEAD:
      state = LORA_JOINING; //LMIC joins again
      break;
    default:
      break;
  }
}

void lora_setup(void)
{
  radio = (features & FEATURE_RFM9X) != 0;
  if(!radio)
  {
    state = LORA_NO_RADIO;
    return;
  }

  //DevEUI and JoinEUI also with LoRa off, so the device can be registered first
  deveui_from_mac = (settings.lora_deveui[0] == 0);
  if(deveui_from_mac)
  {
    byte mac[6] = { 0 };
    if(features & FEATURE_WINC1500)
    {
      WiFi.macAddress(mac); //mac[5] is the first byte as shown
    }
    if((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0)
    {
      state = LORA_NO_DEVEUI; //no WiFi module: lora.deveui must be set
      return;
    }
    const uint8_t eui[8] = { mac[0], mac[1], mac[2], 0xFE, 0xFF, mac[3], mac[4], mac[5] }; //EUI-64 from MAC-48 (FFFE), LSB first
    memcpy(deveui, eui, 8);
  }
  else
  {
    parse_hex(settings.lora_deveui, deveui, 8, true);
  }
  parse_hex(settings.lora_joineui, joineui, 8, true);
  if(!settings.lora_enabled)
  {
    state = LORA_OFF;
    return;
  }
  if(!parse_hex(settings.lora_appkey, appkey, 16, false))
  {
    state = LORA_NO_APPKEY;
    return;
  }

  os_init();
  LMIC_reset();
  LMIC_registerEventCb(on_event, NULL);
  LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100); //no crystal: the RC clock may be off by up to 1 %
  state = LORA_WAITING;
  t_next = millis() + (unsigned long)(os_getRndU2() % (LORA_JOIN_DELAY * 1000)); //not all devices at once after a power cut
}

void lora_service(void)
{
  if((state != LORA_WAITING) && (state != LORA_JOINING) && (state != LORA_JOINED))
  {
    return;
  }
  phase_set(PHASE_LORA);
  os_runloop_once();

  if((long)(millis() - t_next) < 0)
  {
    return;
  }
  if(state == LORA_WAITING)
  {
    LMIC_startJoining();
    state = LORA_JOINING;
    t_next = millis() + 3600000UL; //EV_JOINED sets it
    return;
  }
  if((state == LORA_JOINED) && !(LMIC.opmode & OP_TXRXPEND))
  {
    uint8_t p[LORA_PAYLOAD];
    payload(p);
    LMIC_setTxData2(LORA_PORT, p, sizeof(p), 0);
    interval_ms = settings.lora_interval * 1000UL;
    unsigned long fair = airtime_ms() * FAIR_USE_FACTOR;
    if(fair > interval_ms)
    {
      interval_ms = fair;
    }
    t_next = millis() + interval_ms;
  }
}

void lora_status(lora_row_fn row, void *ctx)
{
  char v[48];
  if(!radio)
  {
    return;
  }
  row(ctx, "LoRaWAN", state_names[state]);
  if(state == LORA_NO_DEVEUI)
  {
    row(ctx, "DevEUI", "not set (no WiFi module, set lora.deveui)");
    return;
  }
  print_eui(v, deveui);
  if(deveui_from_mac)
  {
    strcat(v, " (from the MAC)");
  }
  row(ctx, "DevEUI", v);
  print_eui(v, joineui);
  row(ctx, "JoinEUI", v);
  row(ctx, "Frequency plan", LORA_PLAN);
  if(state == LORA_JOINING)
  {
    snprintf(v, sizeof(v), "%lu", (unsigned long)join_attempts);
    row(ctx, "Join attempts", v);
  }
  if(state == LORA_JOINED)
  {
    snprintf(v, sizeof(v), "%08lX", (unsigned long)LMIC.devaddr);
    row(ctx, "DevAddr", v);
    snprintf(v, sizeof(v), "SF%u", spreading_factor());
    row(ctx, "Spreading factor", v);
    if(uplinks > 0)
    {
      snprintf(v, sizeof(v), "%lu, last %lu s ago", (unsigned long)uplinks, (millis() - t_last) / 1000UL);
    }
    else
    {
      strcpy(v, "0");
    }
    row(ctx, "Uplinks", v);
    snprintf(v, sizeof(v), "in %ld s (every %lu s)", (long)(t_next - millis()) / 1000L, interval_ms / 1000UL);
    row(ctx, "Next uplink", v);
  }
  if(dn_seen)
  {
    snprintf(v, sizeof(v), "RSSI %d dBm, SNR %d dB", dn_rssi, dn_snr / 4);
    row(ctx, "Last downlink", v);
  }
}

size_t lora_json(char *buf, size_t size)
{
  char dev[17] = "", join[17] = "";
  if(!radio)
  {
    return 0;
  }
  if(state != LORA_NO_DEVEUI)
  {
    print_eui(dev, deveui);
    print_eui(join, joineui);
  }
  bool joined = (state == LORA_JOINED);
  int n = snprintf(buf, size,
    "\"lora\":{\"state\":\"%s\",\"deveui\":\"%s\",\"joineui\":\"%s\",\"plan\":\"" LORA_PLAN "\",\"devaddr\":\"%08lX\","
    "\"join_attempts\":%lu,\"uplinks\":%lu,\"uplink_age\":%ld,\"sf\":%u,\"interval\":%lu,\"rssi\":%d,\"snr\":%d},",
    state_names[state], dev, join, (unsigned long)(joined ? LMIC.devaddr : 0),
    (unsigned long)join_attempts, (unsigned long)uplinks, uplinks ? (long)((millis() - t_last) / 1000UL) : -1L,
    joined ? spreading_factor() : 0, interval_ms / 1000UL, dn_seen ? dn_rssi : 0, dn_seen ? dn_snr / 4 : 0);
  return ((n < 0) || ((size_t)n >= size)) ? 0 : (size_t)n;
}

#else

void lora_setup(void) {}
void lora_service(void) {}
void lora_status(lora_row_fn row, void *ctx) { (void)row; (void)ctx; }
size_t lora_json(char *buf, size_t size) { (void)buf; (void)size; return 0; }

#endif
