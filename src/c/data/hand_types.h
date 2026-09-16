#pragma once

#include <pebble.h>

// Configuration shared by hand data, geometry, rendering and decoding.
typedef struct {
  uint8_t style;
  uint8_t width;
  uint8_t length;
  int8_t back_offset;
  int8_t middle_offset;
  uint8_t secondary_width;
  uint8_t color;
  bool outline_enabled;
  uint8_t outline_color;
  bool translucent;
  bool hollow;
  uint8_t hollow_thickness;
  bool shadow_enabled;
  uint8_t shadow_distance_px;
} HandConfig;
