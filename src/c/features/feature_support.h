#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"

// Small helpers shared by the icons and primitive resolution.

// Draws the Moon with its lit portion for `phase_pct` (0-100, illuminated
// fraction) and whether it is waxing. Pixels outside `bounds` are skipped.
void feature_draw_moon_phase(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                             uint8_t phase_pct, bool waxing, GColor lit_color);

// Compact Moon-phase name ("New", "WxCr", "1stQ", ...).
const char *feature_moon_phase_short_name(uint8_t pct, bool waxing);

// Number of planets currently above the horizon while the sky is dark.
uint8_t feature_count_visible_planets(const EclipseData *data, time_t now);

// Compact condition label suitable for narrow feature slots.
const char *feature_weather_short_condition_text(uint8_t weather_condition, uint8_t cloud_pct);
