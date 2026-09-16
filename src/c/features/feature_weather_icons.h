#pragma once
#include <pebble.h>

// Draws a weather condition icon: picks day/night art (for the 2 categories
// that have both -- clear sky and partly cloudy) and, when outline_style !=
// 0, draws the outline pass itself -- see
// feature_icon_assets_draw_styled_with_outline().
void feature_weather_icons_draw_with_outline(GContext *ctx, GPoint top_left, uint8_t category, bool is_night,
                                             uint8_t style, uint8_t outline_style, GColor outline_color, GColor color);
