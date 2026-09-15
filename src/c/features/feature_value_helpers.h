#pragma once

#include <pebble.h>
#include "./feature_slot.h"

GColor feature_value_resolve_flat_color(uint8_t color_mode, GColor dynamic_color, GColor main_color, GColor accent_color);
void feature_value_set_icon_segment(FeatureSlot *slot, int i, uint8_t icon_kind, GColor color);
void feature_value_set_text_segment(FeatureSlot *slot, int i, const char *text, GColor color);
void feature_value_slot_set(FeatureSlot *slot, uint8_t icon_kind, const char *text, GColor color);
