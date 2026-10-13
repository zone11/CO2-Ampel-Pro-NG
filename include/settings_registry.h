#ifndef SETTINGS_REGISTRY_H
#define SETTINGS_REGISTRY_H

#include <Arduino.h>

// Single table of all user settings, shared by every front end (serial, web, later MQTT/LoRa).
// local = serial or web: may see secrets and change CFG_LOCAL items (credentials).

// Handles one settings-API line (get/set/save/dump/help). Returns false if not a settings command.
bool settings_handle_line(char *line, Print *out, bool remote_on, bool local);

// Sets one item by key from text, with the same parsing/validation as "set". Local, no output.
bool settings_set(const char *key, const char *value);

// Writes the current settings to flash.
bool settings_save(void);

#endif
