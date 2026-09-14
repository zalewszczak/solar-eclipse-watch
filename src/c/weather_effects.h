#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// Atmospheric effects other than the procedural cloud masses.
// Weather/cloud geometry remains owned by weather_layer; this module owns
// precipitation, lightning, meteor showers, and aurora rendering.
void weather_effects_draw_effect(GContext *ctx, GRect bounds, uint8_t condition,
                                 uint8_t cloud_pct, uint8_t cloud_altitude_pct);
void weather_effects_draw_meteors(GContext *ctx, GRect bounds, uint8_t intensity);
void weather_effects_draw_aurora(GContext *ctx, GRect bounds,
                                 uint8_t visibility_pct, uint8_t kp_x10);
