#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"
#include "./feature_slot.h"

// Settings-driven feature layout. This module decides which cached slots
// are active and where their boxes live. It does not compute feature values
// or draw pixels.
void feature_layout_recompute(FeaturesState *state);

// Shared digital clock/feature geometry. Both the clock renderer and the
// digital-mode feature layout use these helpers so their horizontal bands
// cannot drift apart.
void feature_layout_digital_clock_area(uint8_t bottom_style, int16_t screen_w,
                                       int16_t *out_x, int16_t *out_w);
uint8_t feature_layout_digital_side_mode(uint8_t bottom_style);
bool feature_layout_is_digital_top_layout(uint8_t bottom_style);
