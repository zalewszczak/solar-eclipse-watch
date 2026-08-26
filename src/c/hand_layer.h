#pragma once

#include <pebble.h>

// ---------------------------------------------------------------------------
// Custom hour/minute/second hand system (big_analog_hand_style == 4,
// "custom"). Own file, mirroring marker_layer.{c,h} and eclipse_layer.{c,h}.
//
// Unlike the custom marker ring (marker_layer.c), there's no cached-bitmap
// trick here -- and deliberately so. A hand's on-screen angle changes on
// essentially every redraw (the second hand every tick, the minute hand
// every minute), so there's no fixed set of pixels to precompute once and
// reuse; the rotation itself IS the per-tick work, same as the original
// draw_big_hand()/gpath_rotate_to() approach already did. What this file
// actually separates out is the SHAPE math (dot/triangle/square, from the
// width/length/back_offset/outline settings) into its own reusable unit,
// used identically for all three hands, each with independent settings.
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
                                // applies to both the fill and the outline (if enabled).
} HandConfig;

// Draws one hand.
void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color);

// Shared (not per-hand) center decoration -- radius 0 means off.
void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color);
