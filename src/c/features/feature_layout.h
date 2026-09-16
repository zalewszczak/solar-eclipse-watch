#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"
#include "./feature_slot.h"

// Settings-driven feature layout. This module decides which cached slots
void feature_layout_recompute(FeaturesState *state);

// Shared digital clock/feature geometry. Both the clock renderer and the
void feature_layout_digital_clock_area(uint8_t bottom_style, int16_t screen_w,
                                       int16_t *out_x, int16_t *out_w);
uint8_t feature_layout_digital_side_mode(uint8_t bottom_style);
bool feature_layout_is_digital_top_layout(uint8_t bottom_style);
