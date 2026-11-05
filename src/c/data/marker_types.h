#pragma once

#include <pebble.h>

// Configuration for one procedural marker ring.
typedef struct {
  uint8_t style;
  uint8_t thickness;
  uint8_t inner_eccentricity;
  uint8_t outer_eccentricity;
  uint8_t inner_border_pct;
  uint8_t outer_border_pct;
  bool translucent;
  uint8_t color;
} MarkerRingConfig;

