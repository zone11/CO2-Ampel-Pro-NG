#ifndef SETTINGS_STORAGE_H
#define SETTINGS_STORAGE_H

#include "app_types.h"

typedef enum
{
  SETTINGS_LOADED,    // valid versioned record found
  SETTINGS_MIGRATED,  // pre-versioning layout found and taken over (not yet rewritten)
  SETTINGS_DEFAULTS,  // nothing usable found, defaults applied (not yet written)
  SETTINGS_NEWER,     // record from newer firmware: defaults in use, flash left untouched
} SETTINGS_LOAD;

SETTINGS_LOAD settings_read(SETTINGS *data);
void settings_defaults(SETTINGS *data);
void settings_write(const SETTINGS *data);

// MQTT topic prefix check: not empty, no wildcards (+ #), no empty levels (leading, trailing or double /)
bool settings_topic_valid(const char *prefix);

// Empty or exactly digits hex characters (LoRaWAN EUIs and keys)
bool settings_hex_valid(const char *s, size_t digits);

// CO2 thresholds in ascending order (co2.t1 <= .. <= co2.t5); an equal pair is allowed
bool settings_thresholds_ordered(const SETTINGS *data);

#endif
