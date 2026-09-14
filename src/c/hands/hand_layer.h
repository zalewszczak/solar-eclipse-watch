#pragma once

#include <pebble.h>
#include "../graphics/subpixel.h"
#include "../data/hand_types.h"

// ---------------------------------------------------------------------------
// Hour/minute/second hand system -- the only hand-drawing code in this
// project. Every hand the watch draws (whether the person picked one
// of pkjs's built-in preset buttons or hand-edited hour/minute/second
// themselves -- see config-page.js's hand style picker popup) arrives
// here as one of these HandConfig field sets; there's no separate
// "preset" mode on the watch side to route around this. Own file,
// mirroring background_layer.{c,h}.
//
// Unlike the custom marker ring (background_layer.c), there's no cached-
// bitmap trick here -- and deliberately so. A hand's on-screen angle
// changes on essentially every redraw (the second hand every tick, the
// minute hand every minute), so there's no fixed set of pixels to
// precompute once and reuse; the rotation itself IS the per-tick work.
// What this file actually separates out is the SHAPE math (dot/triangle/
// square/dauphine/sword/spade/arrow/pomme, from the width/length/
// back_offset/middle_offset/secondary_width settings) into its
// own reusable unit, used identically for all three hands.
//
// The sub-pixel fixed-point coordinate system and generic fill/stroke
// The fixed-point rasterizers are shared with the background marker ring,
// which uses the same rotating dial geometry.
// ---------------------------------------------------------------------------




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
// compute_startup_hand_anim() in hands_controller.c. Pass 1000
// for a normal, non-animated draw. The "on shake" outline gradient
// (if active) is applied automatically, per pixel, whenever
// cfg->outline_enabled -- see shake_gradient_active() in
// eclipse_data.h/hands_controller.c and draw_hand_outline_from_geometry()
// in hand_layer.c; no separate parameter needed for it here.
void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color,
                      bool shadow_translucent_style, uint16_t shadow_angle_deg,
                      uint16_t length_scale_1000);

// Shared (not per-hand) center decoration -- radius 0 means off.
void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color);
