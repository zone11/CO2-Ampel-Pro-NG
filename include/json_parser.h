#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stdbool.h>
#include <stddef.h>

// Small JSON object parser without heap use. The text must be one object; nested objects are
// flattened into dotted keys ({"co2":{"t1":600}} -> "co2.t1"), arrays are not supported.
// Every value is passed to the callback as decoded text ("600", "true", unescaped string).

typedef enum { JSON_STRING, JSON_NUMBER, JSON_BOOL, JSON_NULL } json_type_t;

#define JSON_MAX_KEY   48  // full dotted key
#define JSON_MAX_VALUE 128 // decoded value
#define JSON_MAX_DEPTH 4

// Returns false to stop parsing (json_parse() then returns false with err "stopped").
typedef bool (*json_value_fn)(void *user, const char *key, json_type_t type, const char *value);

// Returns true if the whole text was parsed; otherwise err holds e.g. "at 37: expected ':'".
bool json_parse(const char *text, json_value_fn cb, void *user, char *err, size_t err_size);

#endif
