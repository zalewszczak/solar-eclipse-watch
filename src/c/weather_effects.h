#pragma once
#include <pebble.h>
void weather_effects_draw_effect(GContext *ctx,GRect bounds,uint8_t condition,uint8_t cloud_pct,uint8_t cloud_altitude_pct);
void weather_effects_draw_meteors(GContext *ctx,GRect bounds,uint8_t intensity);
void weather_effects_draw_aurora(GContext *ctx,GRect bounds,uint8_t visibility_pct,uint8_t kp_x10);
