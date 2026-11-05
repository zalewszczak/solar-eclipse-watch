#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"
#include "../../data/marker_types.h"
#include "../../fonts/font_lookup.h"
#include "../../graphics/subpixel.h"

// State owned exclusively by the marker renderer. The background canvas
typedef struct {
  GBitmap *bitmap;
  uint8_t bitmap_style;
  bool bitmap_tinted;
  GColor bitmap_tint_color;
  bool bitmap_tint_transparent;
} MarkerLayerState;

void marker_layer_init(MarkerLayerState *state);
void marker_layer_deinit(MarkerLayerState *state);

void marker_layer_draw(GContext *ctx, MarkerLayerState *state, GPoint center, GRect screen,
                       const EclipseData *data, GColor main_color, GColor accent_color,
                       GColor bg_color);

GPoint marker_layer_point_on_ring(GPoint center, GRect screen, int32_t angle,
                     uint8_t pct, uint8_t eccentricity_pct);

