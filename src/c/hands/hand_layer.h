#pragma once

#include <pebble.h>
#include "../graphics/subpixel.h"
#include "../data/hand_types.h"


// Draw a configured hand with fixed-point geometry and optional effects.
void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color,
                      bool shadow_translucent_style, uint16_t shadow_angle_deg,
                      uint16_t length_scale_1000);


// Draw the center pivot using the selected color scheme.
void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color);
