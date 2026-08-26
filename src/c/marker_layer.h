#pragma once

#include <pebble.h>

// ---------------------------------------------------------------------------
// Custom hour/second marker system (big_analog_marker_style == 8, "custom").
//
// Own file, mirroring how eclipse_layer.{c,h} own the sun/moon graphic --
// see marker_layer.c for the full design writeup, in particular the note
// on why this is a *cached bitmap*, not a second Pebble Layer (Pebble has
// no public API to obtain a GContext for an arbitrary offscreen GBitmap,
// so "draw once, blit every tick" is implemented as manual pixel rasterizing
// into a GBitmapFormat8Bit buffer -- same technique pebble-eclipse-watch.c
// already uses in tint_marker_bitmap()).
// ---------------------------------------------------------------------------

// One ring's procedural shape -- used twice (hour ring, second ring). Each
// mark is now drawn directly BETWEEN its inner and outer border points --
// there's no separate "length": the border points themselves are the
// mark's start and end, so eccentricity ("bending" the ring from a circle
// towards the screen-fitted rectangle) directly changes how long each mark
// is as it goes around, same as it changes the ring's overall shape.
typedef struct {
  uint8_t style;          // 0=dot (round caps), 1=line (thin stroke), 2=square (sharp caps)
  uint8_t thickness;      // width of the mark, across the ring, in px.
                           // Hour: 1-20. Second: 1-10 (clamped by the caller/settings UI).
  uint8_t inner_eccentricity; // 0-100: 0 = the inner edge follows a circle, 100 = it follows
                               // the screen-fitted rectangle (see marker_layer.c:
                               // point_on_ring()) at the inner_border_pct "reach".
  uint8_t outer_eccentricity; // same, for the outer edge, at outer_border_pct.
  uint8_t inner_border_pct; // 0-100: how far out the inner edge sits. 0% = the largest circle
                              // guaranteed to stay fully on-screen (min(screen_w,screen_h)/2),
                              // 100% = the screen-fitted rectangle's own far edge
                              // (max(screen_w,screen_h)/2, along its dominant axis) -- see
                              // marker_border_reach_px() in marker_layer.c for the exact mapping.
  uint8_t outer_border_pct; // ring's outer reach, same 0-100% scale. Never allowed below
                              // inner_border_pct (enforced defensively again in marker_layer.c).
} MarkerRingConfig;

// Text-numeral overlay -- hour and second markers share ONE of these
// (mutually exclusive by design), since drawing both at once was
// explicitly ruled out.
typedef struct {
  uint8_t target;       // 0=off, 1=numerals on the hour ring, 2=numerals on the second ring
                          // (every 5s, drawn at the same 12 angular slots hour numerals use)
  uint8_t font_choice;   // 0-2: system GOTHIC_14 / GOTHIC_14_BOLD / GOTHIC_18_BOLD (all <25px)
                          // 3-6: custom Digital/Minecraft/Pixelate/Miso (all <25px) --
                          // same encoding as corner_custom_font/corner_font_size combined,
                          // see marker_text_font_resource_id() in marker_layer.c
  int8_t offset_px;      // -50..50 -- radial nudge of the text away from (positive) or
                          // towards (negative) the line/dot/square marker it's paired with,
                          // so the two can be visually independent instead of overlapping.
  uint16_t hour_mask;    // bit h (0-11) set => draw a numeral at that hour position
  uint16_t second_mask;  // bit i (0-11) set => draw a numeral at second-slot i (= i*5 seconds)
} MarkerTextConfig;

// Rebuilds the cached ring bitmap if, and only if, hour_cfg/second_cfg/color
// have changed since the last call (cheap no-op otherwise) -- call this
// once per tick, before draw_ring(). Owns its cache internally; nothing
// to destroy explicitly (it's freed via marker_layer_deinit()).
void marker_layer_ensure_ring_cache(GRect bounds, const MarkerRingConfig *hour_cfg,
                                     const MarkerRingConfig *second_cfg, GColor color);

// Blits the cached ring bitmap (built by ensure_ring_cache) centered in
// bounds. Cheap -- this is the part that runs every tick without redoing
// any of the angle/eccentricity math.
void marker_layer_draw_ring(GContext *ctx, GRect bounds);

// Draws the (independent, live -- Pebble has no offscreen text rendering
// API, so this can't be cached the same way) text-numeral overlay, if
// text_cfg->target != 0. Needs hour_cfg/second_cfg only to read each
// ring's border_px/eccentricity (numerals sit relative to whichever ring
// they're paired with, before text_cfg->offset_px is applied).
void marker_layer_draw_text(GContext *ctx, GRect bounds, const MarkerTextConfig *text_cfg,
                             const MarkerRingConfig *hour_cfg, const MarkerRingConfig *second_cfg,
                             GColor color);

// Frees the cached ring bitmap and any loaded custom text font. Call from
// the window's unload handler, same lifecycle as the other cached bitmaps
// in pebble-eclipse-watch.c.
void marker_layer_deinit(void);
