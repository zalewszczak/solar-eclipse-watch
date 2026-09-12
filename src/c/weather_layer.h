#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// Weather and atmospheric effects rendered over the sky.
// The background canvas remains responsible for deciding visibility and
// supplying the current Sun color; this module owns the procedural artwork.
void weather_layer_draw_clouds(GContext *ctx, GRect bounds, uint8_t cloud_pct,
                               uint8_t cloud_altitude_pct, uint8_t visibility_pct,
                               bool stormy, GPoint sun_center, bool sun_up,
                               bool flash_active, int16_t sun_alt_decideg,
                               uint8_t sun_r, uint8_t sun_g, uint8_t sun_b);

void weather_layer_draw_effect(GContext *ctx, GRect bounds, uint8_t condition,
                               uint8_t cloud_pct, uint8_t cloud_altitude_pct);

void weather_layer_draw_meteors(GContext *ctx, GRect bounds, uint8_t intensity);

void weather_layer_draw_aurora(GContext *ctx, GRect bounds,
                               uint8_t visibility_pct, uint8_t kp_x10);

const char *short_condition_text(uint8_t weather_condition, uint8_t cloud_pct);
