#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"
#include "feature_slot.h"

void __attribute__((noinline)) feature_value_date_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data, uint8_t color_mode, GColor main_color, GColor accent_color, time_t now, struct tm *t);
void __attribute__((noinline)) feature_value_timezone_compute(FeatureSlot *slot, uint8_t content, uint8_t color_mode, GColor main_color, GColor accent_color, time_t now);
void __attribute__((noinline)) feature_value_sky_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data, uint8_t color_mode, GColor main_color, GColor accent_color, time_t now);
