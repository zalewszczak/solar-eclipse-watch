#pragma once

#include <pebble.h>
#include "eclipse_data.h"

typedef struct FeatureSlot FeatureSlot;

// Resolves the current content of a slot. Layout, font metrics, and segment
// offsets are intentionally left to features_layer.c.
void feature_values_compute_slot(FeatureSlot *slot, const EclipseData *data,
                                 GColor main_color, GColor accent_color, GColor bg_color,
                                 time_t now, struct tm *t);
