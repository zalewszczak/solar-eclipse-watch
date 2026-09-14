#pragma once

#include <pebble.h>

// Configuration for one procedural marker ring. Rendering code consumes this
// type, but the configuration itself belongs to the watch data model.
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

// Numeric/Roman marker-label configuration. One configuration selects either
// the hour ring or the second ring; both cannot be rendered simultaneously.
typedef struct {
  uint8_t target;
  uint8_t font_choice;
  int8_t offset_px;
  uint16_t hour_mask;
  uint16_t second_mask;
  bool roman_numerals;
} MarkerTextConfig;
