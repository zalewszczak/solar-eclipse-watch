#pragma once
#include <pebble.h>

void feature_weather_icons_draw_hollow(GContext *ctx, GPoint top_left, uint8_t category, GColor color);
void feature_weather_icons_draw_simple(GContext *ctx, GPoint top_left, uint8_t category, GColor color);
void feature_weather_icons_draw_filled(GContext *ctx, GPoint top_left, uint8_t category, GColor color);
void feature_weather_icons_draw(GContext *ctx, GPoint top_left, uint8_t category, uint8_t style, GColor color);
