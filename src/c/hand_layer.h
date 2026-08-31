#pragma once

#include <pebble.h>
#include "subpixel.h"

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
//
// The sub-pixel fixed-point coordinate system and generic fill/stroke
// rasterizers this shape math is built on now live in subpixel.h --
// shared with background_layer.c's marker ring, which rotates around the
// dial the same way hands do and used to be drawn with coarser plain-
// integer math instead.
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
  bool outline_enabled;      // traces a genuine 1px perimeter stroke in outline_color underneath
                               // the fill (see draw_hand_outline_once_fp() in hand_layer.c)
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
  bool shadow_enabled;          // draws a drop shadow of the hand's own shape (translated, not rotated,
                                  // by shadow_distance_px in a single global direction shared by every
                                  // hand -- see EclipseData's shadow_angle_deg, not this struct)
                                  // UNDERNEATH everything else this hand draws -- outline and fill both
                                  // layer on top of it, same z-order a real shadow would have.
  uint8_t shadow_distance_px;   // 1-5 px. Editable in custom mode; procedural presets hardcode 2.
} HandConfig;

// Draws one hand using sub-pixel precision. shadow_translucent_style
// and shadow_angle_deg are both single global "Style" section settings
// (see s_data.shadow_translucent/shadow_angle_deg) -- not per-hand,
// unlike everything else in `cfg` -- so they're separate parameters
// rather than HandConfig fields. A shared angle makes sense (all 3
// hands' shadows come from the same one light source); a shared
// on/off + distance would not, hence those two staying in HandConfig
// itself. Translucent shadows draw at ~50% density, or ~25% when the
// hand itself (cfg->translucent) is also translucent, so a see-through
// hand's shadow doesn't end up reading darker/more solid than the hand
// it belongs to. Solid style always draws a fully opaque shadow
// regardless of cfg->translucent.
// length_scale_1000 (0-1000, 1000 = normal/full length) shrinks the
// hand's own length (not its width) proportionally -- used only by
// the startup animation's "grows out from a center dot" phase, see
// compute_startup_hand_anim() in pebble-eclipse-watch.c. Pass 1000
// for a normal, non-animated draw.
// shake_phase_pct (0-100) is this hand's own phase offset into the
// "on shake" outline color-cycle animation -- see shake_anim_color()
// in eclipse_data.h/pebble-eclipse-watch.c, which this passes
// straight through to when resolving the outline color below. Only
// matters if cfg->outline_enabled; pass anything (e.g. 0) otherwise.
void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color,
                      bool shadow_translucent_style, uint16_t shadow_angle_deg,
                      uint16_t length_scale_1000, uint8_t shake_phase_pct);

// Shared (not per-hand) center decoration -- radius 0 means off.
void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color);
