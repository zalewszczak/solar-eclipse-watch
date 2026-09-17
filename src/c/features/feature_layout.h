#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"
#include "./feature_slot.h"

// Settings-driven feature layout. This module decides which cached slots
void feature_layout_recompute(FeaturesState *state);

// bottom_style value for the "Big Digital" layout -- 4 tall digit bitmaps
// + a procedural colon (see big_digital_display.h), corner features plus
// one top-center and one bottom-center feature only (no sides, no seconds).
// A plain new value for the existing field rather than a new one, same as
// every other bottom_style meaning already living alongside it.
#define BOTTOM_STYLE_BIG_DIGITAL 10

// bottom_style value for the "Grid" layout -- a 4x4 grid of single
// characters (see grid_display.h) over a full-screen sky, same corner +
// top-center/bottom-center-only feature set as Big Digital above (see
// feature_layout_recompute()'s shared branch for both).
#define BOTTOM_STYLE_GRID 11

// Shared digital clock/feature geometry. Both the clock renderer and the
void feature_layout_digital_clock_area(uint8_t bottom_style, int16_t screen_w,
                                       int16_t *out_x, int16_t *out_w);
uint8_t feature_layout_digital_side_mode(uint8_t bottom_style);
bool feature_layout_is_digital_top_layout(uint8_t bottom_style);
