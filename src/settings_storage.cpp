#include "settings_storage.h"

#include "app_config.h"

#include <Arduino.h>
#include <string.h>

// Settings live in the last 2KB of flash (0x3F800-0x3FFFF). The linker script
// reserves this area, and scripts/upload_samba.py reads it before the erase and
// writes it back with the firmware, so settings survive firmware updates.
#define SETTINGS_FLASH_ADDR  0x0003F800UL
#define SETTINGS_MAGIC       0x434F3241UL // "CO2A"
#define SETTINGS_VERSION     1            // bump on any SETTINGS layout change

#define FLASH_PAGE_SIZE      64
#define FLASH_ROW_SIZE       256

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;      // sizeof(SETTINGS) when written
  SETTINGS data;
  uint32_t crc;       // CRC32 over magic..data
} SETTINGS_RECORD;

// Rounded up to whole pages so page writes never read past the buffer
#define RECORD_BYTES  (((sizeof(SETTINGS_RECORD) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE)

static uint32_t crc32(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  while(len--)
  {
    crc ^= *data++;
    for(int i = 0; i < 8; i++)
    {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    }
  }
  return ~crc;
}

static uint32_t record_crc(const SETTINGS_RECORD *rec)
{
  return crc32((const uint8_t *)rec, offsetof(SETTINGS_RECORD, crc));
}

static void copy_str(char *dst, size_t size, const char *src)
{
  strncpy(dst, src, size - 1);
  dst[size - 1] = 0;
}

void settings_defaults(SETTINGS *data)
{
  memset(data, 0, sizeof(SETTINGS));
  data->valid         = true;
  data->serial_output = false;
  data->brightness    = HELLIGKEIT;
  data->range[0]      = DEFAULT_T1;
  data->range[1]      = DEFAULT_T2;
  data->range[2]      = DEFAULT_T3;
  data->range[3]      = DEFAULT_T4;
  data->range[4]      = DEFAULT_T5;
  data->buzzer        = BUZZER;
  copy_str(data->wifi_ssid, sizeof(data->wifi_ssid), WIFI_SSID);
  copy_str(data->wifi_code, sizeof(data->wifi_code), WIFI_CODE);

  //MQTT Standardeinstellungen
  data->mqtt_enabled  = MQTT_ENABLED;
  copy_str(data->mqtt_broker, sizeof(data->mqtt_broker), MQTT_BROKER);
  data->mqtt_port     = MQTT_PORT;
  copy_str(data->mqtt_user, sizeof(data->mqtt_user), MQTT_USER);
  copy_str(data->mqtt_pass, sizeof(data->mqtt_pass), MQTT_PASS);
  copy_str(data->mqtt_client_id, sizeof(data->mqtt_client_id), MQTT_CLIENT_ID);
  copy_str(data->mqtt_topic_prefix, sizeof(data->mqtt_topic_prefix), MQTT_TOPIC_PREFIX);
  data->mqtt_interval = MQTT_INTERVAL;

  //LED Color Defaults
  data->color_t1      = DEFAULT_COLOR_T1;
  data->color_t2      = DEFAULT_COLOR_T2;
  data->color_t3      = DEFAULT_COLOR_T3;
  data->color_t4      = DEFAULT_COLOR_T4;
}

// Bools read from flash may hold any byte value; normalize via the raw byte
static bool to_bool(const boolean *b)
{
  return *(const volatile uint8_t *)b != 0;
}

// Replaces out-of-range values with defaults; returns true if anything was changed
static bool settings_validate(SETTINGS *data)
{
  SETTINGS def;
  bool changed = false;
  bool ranges_ok = true;

  settings_defaults(&def);

  data->valid         = true;
  data->serial_output = to_bool(&data->serial_output);
  data->mqtt_enabled  = to_bool(&data->mqtt_enabled);

  if(data->brightness > 255)                                { data->brightness = def.brightness; changed = true; }
  if(data->buzzer > 1)                                      { data->buzzer = def.buzzer; changed = true; }
  if((data->mqtt_port < 1) || (data->mqtt_port > 65535))    { data->mqtt_port = def.mqtt_port; changed = true; }
  if((data->mqtt_interval < 10) || (data->mqtt_interval > 3600)) { data->mqtt_interval = def.mqtt_interval; changed = true; }

  for(int i = 0; i < 5; i++)
  {
    if((data->range[i] < 400) || (data->range[i] > 10000) || ((i > 0) && (data->range[i] < data->range[i-1])))
    {
      ranges_ok = false;
    }
  }
  if(!ranges_ok)
  {
    memcpy(data->range, def.range, sizeof(def.range));
    changed = true;
  }

  uint32_t *colors[] = { &data->color_t1, &data->color_t2, &data->color_t3, &data->color_t4 };
  const uint32_t def_colors[] = { def.color_t1, def.color_t2, def.color_t3, def.color_t4 };
  for(int i = 0; i < 4; i++)
  {
    if(*colors[i] > 0xFFFFFF) { *colors[i] = def_colors[i]; changed = true; }
  }

  // Strings must be terminated inside their buffers
  data->wifi_ssid[sizeof(data->wifi_ssid) - 1] = 0;
  data->wifi_code[sizeof(data->wifi_code) - 1] = 0;
  data->mqtt_broker[sizeof(data->mqtt_broker) - 1] = 0;
  data->mqtt_user[sizeof(data->mqtt_user) - 1] = 0;
  data->mqtt_pass[sizeof(data->mqtt_pass) - 1] = 0;
  data->mqtt_client_id[sizeof(data->mqtt_client_id) - 1] = 0;
  data->mqtt_topic_prefix[sizeof(data->mqtt_topic_prefix) - 1] = 0;

  return changed;
}

// Pre-versioning firmware stored a raw SETTINGS struct at the same address
static bool legacy_plausible(const SETTINGS *old)
{
  return (*(const volatile uint8_t *)&old->valid == 1) &&
         (old->brightness <= 255) &&
         (old->range[0] >= 100) && (old->range[0] <= 10000) &&
         (memchr(old->wifi_ssid, 0, sizeof(old->wifi_ssid)) != NULL) &&
         (memchr(old->mqtt_broker, 0, sizeof(old->mqtt_broker)) != NULL);
}

SETTINGS_LOAD settings_read(SETTINGS *data)
{
  SETTINGS_RECORD rec;

  memcpy(&rec, (const void *)SETTINGS_FLASH_ADDR, sizeof(rec));

  if((rec.magic == SETTINGS_MAGIC) && (rec.size == sizeof(SETTINGS)) &&
     (rec.version == SETTINGS_VERSION) && (rec.crc == record_crc(&rec)))
  {
    memcpy(data, &rec.data, sizeof(SETTINGS));
    settings_validate(data);
    return SETTINGS_LOADED;
  }

  if(rec.magic != SETTINGS_MAGIC)
  {
    SETTINGS old;
    memcpy(&old, (const void *)SETTINGS_FLASH_ADDR, sizeof(old));
    if(legacy_plausible(&old))
    {
      memcpy(data, &old, sizeof(SETTINGS));
      settings_validate(data);
      return SETTINGS_MIGRATED;
    }
  }

  settings_defaults(data);
  return SETTINGS_DEFAULTS;
}

// Schreibt Einstellungen in Flash
void settings_write(const SETTINGS *data)
{
  // SAMD21 Flash-Programmierung
  // Page-Groesse: 64 Bytes (Row = 256 Bytes = 4 Pages)
  uint32_t buf[RECORD_BYTES / 4];
  SETTINGS_RECORD *rec = (SETTINGS_RECORD *)buf;

  memset(buf, 0xFF, sizeof(buf));
  rec->magic   = SETTINGS_MAGIC;
  rec->version = SETTINGS_VERSION;
  rec->size    = sizeof(SETTINGS);
  memcpy(&rec->data, data, sizeof(SETTINGS));
  rec->crc     = record_crc(rec);

  const uint32_t num_rows  = (RECORD_BYTES + FLASH_ROW_SIZE - 1) / FLASH_ROW_SIZE;
  const uint32_t num_pages = RECORD_BYTES / FLASH_PAGE_SIZE;
  const uint32_t *src = buf;

  // Disable interrupts during flash operations
  __disable_irq();

  // Erase rows first
  for(uint32_t i = 0; i < num_rows; i++)
  {
    NVMCTRL->ADDR.reg = (SETTINGS_FLASH_ADDR + (i * FLASH_ROW_SIZE)) / 2; // Address must be divided by 2
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
    while(!NVMCTRL->INTFLAG.bit.READY);
  }

  // Write data page by page
  for(uint32_t page = 0; page < num_pages; page++)
  {
    // Clear page buffer
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
    while(!NVMCTRL->INTFLAG.bit.READY);

    // Fill page buffer (16 words = 64 bytes per page)
    volatile uint32_t *dst = (volatile uint32_t *)(SETTINGS_FLASH_ADDR + (page * FLASH_PAGE_SIZE));
    for(uint32_t i = 0; i < (FLASH_PAGE_SIZE / 4); i++)
    {
      dst[i] = *src++;
    }

    // Execute "Write Page" command
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
    while(!NVMCTRL->INTFLAG.bit.READY);
  }

  // Re-enable interrupts
  __enable_irq();
}
