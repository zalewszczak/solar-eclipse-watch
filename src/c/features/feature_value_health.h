#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"
#include "./feature_slot.h"

void __attribute__((noinline)) feature_value_health_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data, uint8_t color_mode, GColor main_color, GColor accent_color);
