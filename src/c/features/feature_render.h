#pragma once

#include <pebble.h>
#include "feature_slot.h"

// Shared feature rendering primitives.  This module owns the pixel-level
// drawing and segment measurement used by the feature overlay; it does not
// decide which content belongs in a slot or where the slot itself lives.

GColor feature_render_contrasting_outline_color(GColor c);

void feature_render_draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                                       GTextOverflowMode overflow, GTextAlignment alignment,
                                       GColor color, uint8_t outline_style);

void feature_render_resolve_segment_offsets(FeatureSlot *slot, GFont font, int16_t font_h);

void feature_render_draw_slot(GContext *ctx, GRect bounds, const FeatureSlot *slot,
                              GFont font, int16_t font_h, int16_t font_offset,
                              uint8_t outline_style, uint8_t weather_icon_style,
                              bool draw_debug);
