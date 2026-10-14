#ifndef SERIAL_SETTINGS_H
#define SERIAL_SETTINGS_H

#include <Arduino.h>

typedef enum
{
  CFG_U8,
  CFG_U16,
  CFG_U32,
  CFG_BOOL,
  CFG_STRING,
  CFG_COLOR,
  CFG_IP
} cfg_type_t;

// Item flags
#define CFG_SECRET  (1<<0) // value masked unless the caller is local (serial dump keeps it for backups)
#define CFG_LOCAL   (1<<1) // may only be changed locally (serial, web), e.g. connection credentials
#define CFG_CONTROL (1<<2) // runtime control (e.g. led.disable): also over MQTT with mqtt.config=0, RAM only

typedef struct cfg_item cfg_item_t;
typedef bool (*cfg_apply_fn)(void *user, const cfg_item_t *item);

struct cfg_item
{
  const char *key;
  cfg_type_t type;
  void *ptr;
  uint32_t min_val;
  uint32_t max_val;
  size_t max_len; // Only used for CFG_STRING.
  cfg_apply_fn apply;
  uint8_t flags;  // CFG_SECRET, CFG_LOCAL
};

typedef struct
{
  void *user;
  bool remote_on;
  bool locked;    // password set and not enabled: get/dump refused too
  bool local;     // serial/web: may see secrets and change CFG_LOCAL items
  Print *out;
  bool (*on_save)(void *user);
} serial_settings_ctx_t;

// Sets one item from text with the same parsing, range check and apply callback as "set"
// (decimal: a JSON number, also for colors). Returns NULL or an error message. No access checks.
const char *cfg_set_value(const cfg_item_t *item, const char *value, bool decimal, void *user);
const cfg_item_t *cfg_find_item(const cfg_item_t *items, size_t count, const char *key);

// Current value as text, as "get" prints it (colors 0xRRGGBB); secrets are not masked here.
void cfg_print_value(Print *out, const cfg_item_t *item);

// All items as one JSON object (no line end); secrets masked unless ctx->local.
void cfg_print_json(const cfg_item_t *items, size_t item_count, const serial_settings_ctx_t *ctx);
void print_json_string(Print *out, const char *str);

// Parses and handles one line. The line buffer is modified in-place.
// Returns true if the line was handled, false if it was not recognized.
bool serial_settings_handle_line(char *line,
                                 const cfg_item_t *items,
                                 size_t item_count,
                                 const serial_settings_ctx_t *ctx);

#endif
