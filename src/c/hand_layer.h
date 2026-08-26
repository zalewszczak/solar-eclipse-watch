#pragma once

#include <pebble.h>

// ---------------------------------------------------------------------------
// Custom hour/minute/second hand system (big_analog_hand_style == 4,
// "custom") -- and, since pebble-eclipse-watch.c now routes ALL 5 hand
// styles through hand_layer_draw() (styles 0-3 via hardcoded presets, see
// HAND_STYLE_PRESETS there), the only hand-drawing code left in this
// project. Own file, mirroring background_layer.{c,h}.
//
// Unlike the custom marker ring (background_layer.c), there's no cached-
// bitmap trick here -- and deliberately so. A hand's on-screen angle
// changes on essentially every redraw (the second hand every tick, the
// minute hand every minute), so there's no fixed set of pixels to
// precompute once and reuse; the rotation itself IS the per-tick work.
// What this file actually separates out is the SHAPE math (dot/triangle/
// square, from the width/length/back_offset/outline settings) into its
// own reusable unit, used identically for all three hands and both the
// custom and (now) procedural-preset paths.
// ---------------------------------------------------------------------------

typedef struct {
  uint8_t style;        // 0=dot (round caps), 1=triangle (tapers to a point), 2=square (flat caps)
  uint8_t width;         // 1-40 px, thickness across the hand (ignored -- tip only -- for triangle's tip)
  uint8_t length;         // 10-100 px, how far the hand extends outward from center
  int8_t back_offset;      // -40..40 px, extension on the far side of center, opposite the hand's
                             // direction. Positive = a tail sticking out behind the pivot; negative =
                             // the hand starts that far short of center instead (a detached gap).
  uint8_t color;            // 0=main, 1=accent, 2=background (from the active color scheme),
                              // 3=none -- skips drawing the hand's fill entirely (the outline, if
                              // enabled, still draws -- a way to get a "hollow" or ghosted look
                              // without needing real alpha blending).
  bool outline_enabled;      // draws a 1px-shifted 4-direction underlay in outline_color first,
                               // same "outline" technique the original draw_big_hand_outlined() used
  uint8_t outline_color;      // 0=main, 1=accent, 2=background
  bool translucent;           // per-hand ~50% transparency, via the same Bayer-dithered stipple
                                // fill_polygon_dithered() already uses elsewhere in this project --
                                // applies to both the fill and the outline (if enabled). Takes
                                // priority over hollow below when both are set, same as the original
                                // procedural hands did (transparent always won over style==2's hollow
                                // rendering).
  bool hollow;                 // draw the shape's own 1px stroke outline instead of a filled shape,
                                 // in `color` -- distinct from outline_enabled's shifted-copy underlay,
                                 // which still layers normally underneath a hollow shape if both are on.
                                 // Used by the "modern" procedural hand preset (see pebble-eclipse-watch.c),
                                 // which always rendered hollow when not transparent.
} HandConfig;

// Draws one hand.
void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color);

// Shared (not per-hand) center decoration -- radius 0 means off.
void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color);
