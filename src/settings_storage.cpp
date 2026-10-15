#include "settings_storage.h"

#include "app_config.h"

#include <Arduino.h>
#include <string.h>

// Settings live in the last 2KB of flash (0x3F800-0x3FFFF). The linker script
// reserves this area, and scripts/upload_samba.py reads it before the erase and
// writes it back with the firmware, so settings survive firmware updates.
#define SETTINGS_FLASH_ADDR  0x0003F800UL
#define SETTINGS_MAGIC       0x434F3241UL // "CO2A"
#define SETTINGS_VERSION     7            // bump on any SETTINGS change and add a migration in settings_read()

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

#define RECORD_HEADER     offsetof(SETTINGS_RECORD, data)
#define SETTINGS_V1_SIZE  388 // layout size of settings v1-v4 and of the pre-versioning raw struct

// Older records are read with their own size: the CRC follows their (shorter) data
static_assert(offsetof(SETTINGS_RECORD, crc) == RECORD_HEADER + sizeof(SETTINGS), "crc must follow data");
static_assert(sizeof(SETTINGS) >= SETTINGS_V1_SIZE, "fields may only be appended");
static_assert(offsetof(SETTINGS, brightness_dark) == 392, "v6 fields must start after the v5 record (392 bytes)");
static_assert(offsetof(SETTINGS, name) == 431, "v7 name starts in the last padding byte of v6 (see default_padding_fields)");

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
  data->brightness    = BRIGHTNESS;
  data->range[0]      = DEFAULT_T1;
  data->range[1]      = DEFAULT_T2;
  data->range[2]      = DEFAULT_T3;
  data->range[3]      = DEFAULT_T4;
  data->range[4]      = DEFAULT_T5;
  data->buzzer        = BUZZER;
  copy_str(data->wifi_ssid, sizeof(data->wifi_ssid), WIFI_SSID);
  copy_str(data->wifi_code, sizeof(data->wifi_code), WIFI_CODE);

  //MQTT defaults
  data->mqtt_enabled  = MQTT_ENABLED;
  copy_str(data->mqtt_broker, sizeof(data->mqtt_broker), MQTT_BROKER);
  data->mqtt_port     = MQTT_PORT;
  copy_str(data->mqtt_user, sizeof(data->mqtt_user), MQTT_USER);
  copy_str(data->mqtt_pass, sizeof(data->mqtt_pass), MQTT_PASS);
  copy_str(data->mqtt_client_id, sizeof(data->mqtt_client_id), MQTT_CLIENT_ID);
  copy_str(data->mqtt_topic_prefix, sizeof(data->mqtt_topic_prefix), MQTT_TOPIC_PREFIX);
  data->mqtt_interval = MQTT_INTERVAL;

  //LED color defaults
  data->color_t1      = DEFAULT_COLOR_T1;
  data->color_t2      = DEFAULT_COLOR_T2;
  data->color_t3      = DEFAULT_COLOR_T3;
  data->color_t4      = DEFAULT_COLOR_T4;

  data->asc           = CO2_ASC;
  data->mqtt_config   = MQTT_CONFIG;
  data->mqtt_ha       = MQTT_HA;
  data->mqtt_tls      = MQTT_TLS;

  data->brightness_dark = BRIGHTNESS_DARK;
  data->led_auto      = BRIGHTNESS_AUTO;
  data->led_disable   = false;
  data->password[0]   = 0;
  data->name[0]       = 0;
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
  data->asc           = to_bool(&data->asc);
  data->mqtt_config   = to_bool(&data->mqtt_config);
  data->mqtt_ha       = to_bool(&data->mqtt_ha);
  data->mqtt_tls      = to_bool(&data->mqtt_tls);
  data->led_auto      = to_bool(&data->led_auto);
  data->led_disable   = to_bool(&data->led_disable);

  if((data->brightness < 1) || (data->brightness > 255))    { data->brightness = def.brightness; changed = true; }
  if((data->brightness_dark < 1) || (data->brightness_dark > 255)) { data->brightness_dark = def.brightness_dark; changed = true; }
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
  data->password[sizeof(data->password) - 1] = 0;
  data->name[sizeof(data->name) - 1] = 0;

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

// Fields that lived in struct padding before they existed (v1-v4 share one 388-byte layout)
static void default_padding_fields(SETTINGS *data, uint16_t version)
{
  if(version < 2) { data->asc         = CO2_ASC; }
  if(version < 3) { data->mqtt_config = MQTT_CONFIG; }
  if(version < 4) { data->mqtt_ha     = MQTT_HA; }
  if(version < 7) { data->name[0]     = 0; } //byte 431 was padding in v6
}

SETTINGS_LOAD settings_read(SETTINGS *data)
{
  const uint8_t *flash = (const uint8_t *)SETTINGS_FLASH_ADDR;
  uint32_t magic;
  uint16_t version, size;

  memcpy(&magic, flash, sizeof(magic));
  memcpy(&version, flash + 4, sizeof(version));
  memcpy(&size, flash + 6, sizeof(size));

  if(magic == SETTINGS_MAGIC)
  {
    if(version > SETTINGS_VERSION) //after a downgrade: don't overwrite, so the newer firmware finds them again
    {
      settings_defaults(data);
      return SETTINGS_NEWER;
    }
    if((version >= 1) && (size >= SETTINGS_V1_SIZE) && (size <= sizeof(SETTINGS)) && ((size % 4) == 0))
    {
      uint32_t crc;
      memcpy(&crc, flash + RECORD_HEADER + size, sizeof(crc));
      if(crc == crc32(flash, RECORD_HEADER + size))
      {
        settings_defaults(data);                       //fields missing in shorter records
        memcpy(data, flash + RECORD_HEADER, size);
        default_padding_fields(data, version);
        settings_validate(data);
        return (version == SETTINGS_VERSION) ? SETTINGS_LOADED : SETTINGS_MIGRATED;
      }
    }
  }
  else
  {
    // Pre-versioning firmware stored the raw v1 layout at the same address
    SETTINGS old;
    settings_defaults(&old);
    memcpy(&old, flash, SETTINGS_V1_SIZE);
    if(legacy_plausible(&old))
    {
      memcpy(data, &old, sizeof(SETTINGS));
      default_padding_fields(data, 0);
      settings_validate(data);
      return SETTINGS_MIGRATED;
    }
  }

  settings_defaults(data);
  return SETTINGS_DEFAULTS;
}

// Writes the settings to flash
void settings_write(const SETTINGS *data)
{
  // SAMD21 flash programming
  // page size: 64 bytes (row = 256 bytes = 4 pages)
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
