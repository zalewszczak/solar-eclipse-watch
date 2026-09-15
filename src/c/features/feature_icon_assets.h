#pragma once
#include <pebble.h>

void feature_icon_assets_get_outline_offsets(uint8_t style, const GPoint **offsets, int *count);
void feature_icon_assets_draw_tiny(GContext *ctx, GPoint top_left, const uint8_t *pattern, int rows, int width, GColor color);
void feature_icon_assets_draw_resource(GContext *ctx, GPoint top_left, uint32_t resource_id, GColor color);
void feature_icon_assets_draw_resource_with_outline_sized(GContext *ctx, GPoint pos, uint32_t resource_id, uint8_t outline_style, GColor outline_color, GColor color, int16_t w, int16_t h);
void feature_icon_assets_draw_resource_with_outline(GContext *ctx, GPoint pos, uint32_t resource_id, uint8_t outline_style, GColor outline_color, GColor color);
void feature_icon_assets_draw_resource_native(GContext *ctx, GPoint top_left, uint32_t resource_id);
