#pragma once
#include <pebble.h>

// Draws the "+X hour"/"+X day" half-icon that sits right before the weather
// condition icon in the "Weather in N hours/days" feature slots -- see
// feature_forecast_offset_icons.c for why it's drawn as a full 16x16 icon
// but only occupies half that width in the layout.
// idx: content id - 87, i.e. 0-5 = "+1h".."+6h", 6-8 = "+1 day".."+3 days" --
// the same index forecast_temp_c/forecast_condition in eclipse_data.h use.
void feature_forecast_offset_icons_draw_with_outline(GContext *ctx, GPoint top_left, uint8_t idx,
                                                      uint8_t style, uint8_t outline_style,
                                                      GColor outline_color, GColor color);
