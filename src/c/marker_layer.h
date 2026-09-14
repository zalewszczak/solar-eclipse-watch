#pragma once

#include <pebble.h>
#include "eclipse_data.h"
#include "font_lookup.h"
#include "subpixel.h"

// State owned exclusively by the marker renderer. The background canvas
// embeds one instance so bitmap resources and the custom marker font remain
// tied to that canvas instance rather than becoming global application state.
typedef struct {
  GBitmap *bitmap;
  uint8_t bitmap_style;
  bool bitmap_tinted;
  GColor bitmap_tint_color;
  bool bitmap_tint_transparent;
  FontSlot text_font_slot;
} MarkerLayerState;

void marker_layer_init(MarkerLayerState *state);
void marker_layer_deinit(MarkerLayerState *state);

void marker_layer_draw(GContext *ctx, MarkerLayerState *state, GPoint center, GRect screen,
                       const EclipseData *data, GColor main_color, GColor accent_color,
                       GColor bg_color, bool anim_active, int32_t anim_progress_1000,
                       bool draw_debug);

GPoint marker_layer_point_on_ring(GPoint center, GRect screen, int32_t angle,
                     uint8_t pct, uint8_t eccentricity_pct);

// Shared marker-label/ring startup animation helpers.
int32_t marker_ease_in_1000(int32_t t);
int32_t marker_anim_mark_progress_1000_raw(int mark_index, int marks, int32_t overall_progress_1000);

void marker_layer_inner_reach(uint8_t marker_style, uint8_t *out_pct, uint8_t *out_eccentricity);
