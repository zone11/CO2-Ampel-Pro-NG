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

#endif
