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
};

typedef struct
{
  void *user;
  bool remote_on;
  Print *out;
  bool (*on_save)(void *user);
} serial_settings_ctx_t;

// Parses and handles one line. The line buffer is modified in-place.
// Returns true if the line was handled, false if it was not recognized.
bool serial_settings_handle_line(char *line,
                                 const cfg_item_t *items,
                                 size_t item_count,
                                 const serial_settings_ctx_t *ctx);

#endif
