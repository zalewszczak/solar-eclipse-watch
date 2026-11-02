#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"
#include "./primitives/primitive_ids.h"

// Pebble Layer wrapper for the always-on-top feature-slot overlay.

#define FEATURES_MAX_SLOTS 12

Layer *features_layer_create(GRect frame);
void features_layer_destroy(Layer *layer);

// Recomputes layout and values from the supplied application data.
void features_layer_set_data(Layer *layer, EclipseData *data);

// Recomputes all active slot values without changing layout.
void features_layer_refresh_values(Layer *layer);

// Recomputes only slots containing second-refresh primitives.
void features_layer_refresh_second_slots(Layer *layer);

// Recomputes slots containing the supplied primitive ID.
void features_layer_refresh_primitive(Layer *layer, PrimitiveId primitive);
