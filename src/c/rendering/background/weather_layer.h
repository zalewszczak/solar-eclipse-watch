#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"

// Weather and atmospheric effects rendered over the sky.
// The background canvas remains responsible for deciding visibility and
// supplying the current Sun color; this module owns the procedural artwork.
void weather_layer_draw_clouds(GContext *ctx, GRect bounds, uint8_t cloud_pct,
                               uint8_t cloud_altitude_pct, uint8_t visibility_pct,
                               bool stormy, GPoint sun_center, bool sun_up,
                               bool flash_active, int16_t sun_alt_decideg,
                               uint8_t sun_r, uint8_t sun_g, uint8_t sun_b);

// Returns whether weather-derived feature slots should display an error
// instead of stale data after repeated failed refreshes.
bool weather_layer_should_show_error(const EclipseData *data);

// Compact condition label suitable for narrow feature slots.
const char *weather_layer_short_condition_text(uint8_t weather_condition, uint8_t cloud_pct);
