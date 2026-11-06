#ifndef LORA_SERVICE_H
#define LORA_SERVICE_H

#include <Arduino.h>

// LoRaWAN over the RFM95 (MCCI LMIC, TTN, EU868): OTAA join after a random delay, then one uplink per
// lora.interval, stretched so the airtime stays under the TTN fair use limit (30 s per day)
bool lora_radio_found(void); // RFM95 answers on SPI (version register 0x12)
void lora_setup(void);       // after the radio and WiFi module detection (the DevEUI default comes from the WiFi MAC)
void lora_service(void);     // every loop pass
bool lora_busy(void);        // a frame is on air or its receive windows are still open: no blocking WiFi/MQTT connect now

typedef void (*lora_row_fn)(void *ctx, const char *name, const char *value);
void lora_status(lora_row_fn row, void *ctx); // status rows for serial and web; nothing without a radio
size_t lora_json(char *buf, size_t size);     // "lora":{...}, for diag; 0 without a radio

#endif
