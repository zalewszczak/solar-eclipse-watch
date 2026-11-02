#include "./feature_color_helpers.h"
#include <stdio.h>

GColor feature_color_resolve_flat(uint8_t color_mode, GColor dynamic_color, GColor main_color, GColor accent_color) {
  switch (color_mode) {
    case 1: return accent_color;
    case 3: return dynamic_color;
    case 0:
    case 2:
    default: return main_color;
  }
}

// Phase 7 (Feature Primitive Refactor): unpacks a PKJS-sent GColor8 byte
// (see eclipse_data.h's current_temp_color et al.) the same way
// watch_ui.c's eclipse_ui_color_from_packed() does for the SETTINGS
// custom-color fields -- GColor8 IS its own wire byte, no conversion table
// needed, just reinterpreting the byte as the struct.
GColor feature_color_from_packed(uint8_t packed) {
  GColor color;
  color.argb = packed;
  return color;
}

