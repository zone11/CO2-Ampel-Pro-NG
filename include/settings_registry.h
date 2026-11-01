#ifndef SETTINGS_REGISTRY_H
#define SETTINGS_REGISTRY_H

#include <Arduino.h>

// Single table of all user settings, shared by every front end (serial, web, later MQTT/LoRa).
// local = serial or web: may see secrets and change CFG_LOCAL items (credentials).

// Handles one settings-API line (get/set/save/dump/help). Returns false if not a settings command.
bool settings_handle_line(char *line, Print *out, bool remote_on, bool local);

// Enable password (sys.password; empty = none). Prints OK / ERROR to out (may be NULL) and returns
// true if the caller may enable. Wrong passwords lock further attempts for 1, 2, 4 ... 30 s
// (shared by serial, MQTT and web, without blocking the main loop).
bool settings_password_set(void);
bool settings_enable(const char *password, Print *out);

// Applies a JSON object of settings (as printed by "json export"), all or nothing: on any invalid
// value nothing changes. Command words without a dot: "save":true writes the flash, "export":true
// adds the full configuration to the reply. Unknown keys are warnings, "***" for secrets and
// unchanged CFG_LOCAL values from remote callers are skipped. Prints one JSON line as reply.
// control_only: only CFG_CONTROL keys (MQTT with mqtt.config=0), no save/export.
bool settings_json_import(const char *json, Print *out, bool local, bool enabled, bool control_only = false);

// Read access to the key table (e.g. to build the web configuration page).
#include "serial_settings.h"
size_t settings_item_count(void);
const cfg_item_t *settings_item(size_t index);

// All settings as one JSON object (as "json export"); mask_secrets prints passwords as "***".
void settings_print_json(Print *out, bool mask_secrets);

// Sets one item by key from text, with the same parsing/validation as "set". Local, no output.
bool settings_set(const char *key, const char *value);

// Writes the current settings to flash.
bool settings_save(void);

#endif
