#ifndef SCD4X_CONVERT_H
#define SCD4X_CONVERT_H

#include <stdint.h>

// SCD4x raw values, in float: the library's own conversions use double and pull in the soft double code (6 KB)
static inline float scd4x_temperature(uint16_t raw) { return -45.0f + (175.0f * raw) / 65535.0f; }
static inline float scd4x_humidity(uint16_t raw) { return (100.0f * raw) / 65535.0f; }
static inline float scd4x_offset(uint16_t raw) { return (175.0f * raw) / 65535.0f; }
static inline uint16_t scd4x_offset_raw(float offset) { return (uint16_t)((offset * 65535.0f) / 175.0f + 0.5f); }

#endif
