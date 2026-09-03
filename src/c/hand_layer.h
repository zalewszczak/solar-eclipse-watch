#pragma once

#include <pebble.h>
#include "subpixel.h"

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
// rasterizers this shape math is built on now live in subpixel.h --
// shared with background_layer.c's marker ring, which rotates around the
// dial the same way hands do and used to be drawn with coarser plain-
// integer math instead.
// ---------------------------------------------------------------------------


typedef struct {
  uint8_t style;        // 0=dot (round caps), 1=triangle (tapers to a point), 2=square (flat caps),
                          // 3=dauphine, 4=sword, 5=spade, 6=arrow, 7=pomme, 8=leaf, 9=syringe,
                          // 10=serpentine -- see HandGeometry/compute_hand_geometry_fp() in
                          // hand_layer.c for what each shape does with middle_offset/secondary_width
                          // below. leaf (8) only uses middle_offset (its peak position), not
                          // secondary_width; syringe (9) and serpentine (10) use both, same as
                          // 3-7.
  uint8_t width;         // 1-40 px, thickness across the hand (ignored -- tip only -- for triangle's tip)
  uint8_t length;         // 10-100 px, how far the hand extends outward from center
  int8_t back_offset;      // -40..40 px, extension on the far side of center, opposite the hand's
                             // direction. Positive = a tail sticking out behind the pivot; negative =
                             // the hand starts that far short of center instead (a detached gap).
  // Both only meaningful for styles 3-10 (dauphine/sword/spade/arrow/
  // pomme/leaf/syringe/serpentine) -- ignored entirely by styles 0-2,
  // same as width is already ignored by triangle's tip. secondary_width
  // specifically is further ignored by leaf (8), which only uses
  // middle_offset. Same ranges/units as back_offset and width
  // respectively (a position along the hand's own axis, and a sideways
  // thickness) -- what each one actually controls is style-specific,
  // see compute_hand_geometry_fp() in hand_layer.c.
  int8_t middle_offset;    // -40..40 px, axial position of a style's "middle" feature (dauphine's
                             // side points, sword's side bulge, spade/arrow's tip-ornament height,
                             // pomme's thick/thin joint) -- measured from center like back_offset,
                             // positive = toward the tip.
  uint8_t secondary_width;  // 1-40 px, a style's secondary thickness (sword's mid-bulge width,
                              // spade's droplet/arrow's tip-triangle width, pomme's thin-tail width)
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
  bool hollow;                 // draw an INLINE stroke of the shape's own outline instead of a filled
                                 // shape, in `color` -- i.e. within the shape's own bounds, as opposed
                                 // to outline_enabled's shifted-copy underlay which marks the hand
                                 // OUTSIDE its bounds and still layers normally underneath a hollow
                                 // shape if both are on. hollow_thickness below sets how wide that
                                 // inline stroke is; hollow_thickness <= 1 draws a plain 1px
                                 // perimeter trace (stroke_polygon_fp()/stroke_circle_fp() in
                                 // hand_layer.c). A thickness too large for the shape to actually
                                 // contain just fills it solid instead (see
                                 // inset_convex_polygon_fp()'s own comment in subpixel.h).
  uint8_t hollow_thickness;    // 1-40 px, width of the inline stroke above when hollow is set and
                                 // this is > 1. Ignored otherwise.
  bool shadow_enabled;          // draws a drop shadow of the hand's own shape (translated, not rotated,
                                  // by shadow_distance_px in a single global direction shared by every
                                  // hand -- see EclipseData's shadow_angle_deg, not this struct)
                                  // UNDERNEATH everything else this hand draws -- outline and fill both
                                  // layer on top of it, same z-order a real shadow would have.
  uint8_t shadow_distance_px;   // 1-5 px.
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
// for a normal, non-animated draw. The "on shake" outline gradient
// (if active) is applied automatically, per pixel, whenever
// cfg->outline_enabled -- see shake_gradient_active() in
// eclipse_data.h/pebble-eclipse-watch.c and draw_hand_outline_once_fp()
// in hand_layer.c; no separate parameter needed for it here.
void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color,
                      bool shadow_translucent_style, uint16_t shadow_angle_deg,
                      uint16_t length_scale_1000);

// Shared (not per-hand) center decoration -- radius 0 means off.
void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color);
