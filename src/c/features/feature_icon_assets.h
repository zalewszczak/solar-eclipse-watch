#pragma once
#include <pebble.h>

// One icon, drawn in each of the 3 "Icons style" looks (see eclipse_data.h's
// icon_style / former weather_icon_style): 0=simple, 1=hollow, 2=full color.
// A resource id of 0 means "no art for this style yet" and is skipped rather
// than crashing -- lets new icon identities be wired up before every style's
// artwork exists (see feature_icons.c / feature_weather_icons.c tables).
typedef struct {
  uint32_t simple;
  uint32_t hollow;
  uint32_t fullcolor;
} IconResourceSet;

void feature_icon_assets_get_outline_offsets(uint8_t style, const GPoint **offsets, int *count);
void feature_icon_assets_draw_tiny(GContext *ctx, GPoint top_left, const uint8_t *pattern, int rows, int width, GColor color);

// Generic style-aware icon loader/drawer -- the single code path behind
// every fixed-shape icon (weather condition AND plain feature icons alike).
// Resolves `set` against `style` (falling back to hollow for an unknown
// style, matching the settings page's own 3 options), tints for
// simple/hollow, draws full color art natively (no tint) for style 2, and,
// when outline_style != 0, draws an outline pass first -- reusing the
// hollow-style art for that pass when style is full color, exactly like the
// weather icon's own outline used to. One function replaces what used to be
// a separate switch/case per style per icon, which is what actually keeps
// the compiled binary small as more icons and styles are added.
uint32_t feature_icon_assets_resolve_styled(const IconResourceSet *set, uint8_t style);
void feature_icon_assets_draw_styled(GContext *ctx, GPoint top_left, const IconResourceSet *set, uint8_t style, GColor color, int16_t w, int16_t h);
void feature_icon_assets_draw_styled_with_outline(GContext *ctx, GPoint pos, const IconResourceSet *set, uint8_t style, uint8_t outline_style, GColor outline_color, GColor color, int16_t w, int16_t h);
