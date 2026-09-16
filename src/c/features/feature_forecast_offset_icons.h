#pragma once
#include <pebble.h>

// Draws the "+X hour"/"+X day" half-icon that sits right before the weather
// condition icon in the "Weather in N hours/days" feature slots -- see
// feature_forecast_offset_icons.c for why it's drawn as a full 16x16 icon
// but only occupies half that width in the layout.
// offset: 1-6 = "+1h".."+6h", 7-9 = "+1 day".."+3 days" (content-86 either way).
void feature_forecast_offset_icons_draw_with_outline(GContext *ctx, GPoint top_left, uint8_t offset,
                                                      uint8_t style, uint8_t outline_style,
                                                      GColor outline_color, GColor color);
