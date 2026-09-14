#pragma once
#include <pebble.h>
#include "../../data/eclipse_data.h"
#include "./marker_layer.h"
void background_overlays_draw_marker_animation(GContext *ctx, MarkerLayerState *markers,
                                                const EclipseData *data, GRect bounds, time_t now,
                                                uint16_t elapsed_ms, uint16_t duration_ms);
