#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

// Pebble Layer wrapper for the always-on-top feature-slot overlay.
// Slot layout, value computation, feature fonts and rendering are owned by
// the feature_* modules; this header exposes only the layer lifecycle and
// refresh operations needed by layout/application code.

#define FEATURES_MAX_SLOTS 12

Layer *features_layer_create(GRect frame);
void features_layer_destroy(Layer *layer);

// Recomputes layout and values from the supplied application data.
void features_layer_set_data(Layer *layer, EclipseData *data);

// Recomputes all active slot values without changing layout.
void features_layer_refresh_values(Layer *layer);

// Recomputes only slots whose content requires second-level updates.
void features_layer_refresh_second_slots(Layer *layer);

// Recomputes only slots currently showing the supplied content id.
void features_layer_refresh_content(Layer *layer, uint8_t content);
