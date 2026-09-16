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

// Configuration for hour/second marker labels.
typedef struct {
  uint8_t target;
  uint8_t font_choice;
  int8_t offset_px;
  uint16_t hour_mask;
  uint16_t second_mask;
  bool roman_numerals;
} MarkerTextConfig;
