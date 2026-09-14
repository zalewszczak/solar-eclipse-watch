#pragma once

#include <pebble.h>
#include "eclipse_data.h"
#include "feature_slot.h"

void __attribute__((noinline)) feature_value_composite_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data, uint8_t color_mode, GColor main_color, time_t now);
