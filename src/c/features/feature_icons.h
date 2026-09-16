#pragma once

#include <pebble.h>

#define FEATURE_ICON_WIDTH 16
#define FEATURE_ICON_ROWS 12

uint8_t feature_icons_weather_category(uint8_t weather_condition, uint8_t cloud_pct);
int16_t feature_icons_plus_gap_width(int icon_kind);

void feature_icons_draw_render_icon(GContext *ctx,
                                    uint8_t icon_kind,
                                    int16_t icon_extra,
                                    bool icon_flag,
                                    GColor color,
                                    GColor color2,
                                    int16_t icon_x,
                                    int16_t box_y,
                                    int16_t row_height,
                                    uint8_t outline_style,
                                    uint8_t icon_style,
                                    bool draw_debug);
