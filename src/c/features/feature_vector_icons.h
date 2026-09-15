#pragma once

#include <pebble.h>

// Procedural feature icons that are small vector drawings rather than
// resource-backed bitmaps. The dispatcher owns placement and outlines;
// this module owns the icon geometry itself.
void feature_vector_icons_battery(GContext *ctx, GPoint top_left, GColor color, int charge);
void feature_vector_icons_pressure_trend(GContext *ctx, GPoint top_left, uint8_t trend, GColor color);
void feature_vector_icons_wind_direction(GContext *ctx, GPoint top_left, int16_t from_deg, GColor color);
void feature_vector_icons_compass(GContext *ctx, GPoint top_left, int16_t heading_deg,
                                  GColor north_color, GColor other_color);
void feature_vector_icons_compass_sleep(GContext *ctx, GPoint top_left, GColor color);
void feature_vector_icons_mountain(GContext *ctx, GPoint top_left, GColor color);
