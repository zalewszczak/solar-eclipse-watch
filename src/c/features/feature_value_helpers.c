#include "./feature_value_helpers.h"
#include <stdio.h>

GColor feature_value_resolve_flat_color(uint8_t color_mode, GColor dynamic_color, GColor main_color, GColor accent_color) {
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
// eclipse_ui.c's eclipse_ui_color_from_packed() does for the SETTINGS
// custom-color fields -- GColor8 IS its own wire byte, no conversion table
// needed, just reinterpreting the byte as the struct.
GColor feature_value_color_from_packed(uint8_t packed) {
  GColor color;
  color.argb = packed;
  return color;
}

void feature_value_set_icon_segment(FeatureSlot *slot, int i, uint8_t icon_kind, GColor color) {
  RenderSegment *g = &slot->segments[i];
  g->is_icon = true;
  g->icon_kind = icon_kind;
  g->icon_extra = 0;
  g->icon_flag = false;
  g->color = color;
  g->color2 = color;
}

void feature_value_set_text_segment(FeatureSlot *slot, int i, const char *text, GColor color) {
  RenderSegment *g = &slot->segments[i];
  g->is_icon = false;
  g->icon_kind = 0;
  g->icon_extra = 0;
  g->icon_flag = false;
  g->color = color;
  g->color2 = color;
  snprintf(g->text, sizeof(g->text), "%s", text);
}

void feature_value_slot_set(FeatureSlot *slot, uint8_t icon_kind, const char *text, GColor color) {
  if (icon_kind == 0) {
    slot->segment_count = 1;
    feature_value_set_text_segment(slot, 0, text, color);
  } else {
    slot->segment_count = 2;
    feature_value_set_icon_segment(slot, 0, icon_kind, color);
    feature_value_set_text_segment(slot, 1, text, color);
  }
}
