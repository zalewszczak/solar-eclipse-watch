#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// The sky/sun/moon graphic AND, for big-analog mode, the hour/second
// markers drawn on top of it -- merged into one module (formerly
// eclipse_layer.c + marker_layer.c, two separate cached-drawing systems)
// so there's a single cached bitmap per redraw instead of two. Markers
// are drawn directly into the same live GContext as the sky, right
// before the frame gets captured into the cache -- see the design note
// at the top of background_layer.c for why that's both simpler and
// cheaper than the marker ring's previous standalone bitmap cache.
//
// Owns its own draw state; call eclipse_canvas_set_data() whenever a
// fresh EclipseData arrives, a marker/text-marker setting changes, or
// once a minute so the moon's position animates smoothly.
Layer *eclipse_canvas_create(GRect frame);
void eclipse_canvas_destroy(Layer *layer);
void eclipse_canvas_set_data(Layer *layer, EclipseData *data);

// Toggles the "Sun" / "Moon" / "Saturn" name labels shown briefly
// next to each visible body after a shake gesture. main.c calls this
// true on tap and false again after a few seconds via app_timer.
void eclipse_canvas_set_show_labels(Layer *layer, bool show);

// Drives the "animate background on start" effect -- active/
// elapsed_ms mirror pebble-eclipse-watch.c's own s_bg_anim_active/
// s_bg_anim_elapsed_ms exactly (see maybe_start_startup_background_
// animation() there); this just hands them to the canvas's own draw
// code (see canvas_update_proc's own comment on where they're used)
// and forces an immediate full redraw, same "force + mark dirty"
// shape eclipse_canvas_set_data() above already uses.
void eclipse_canvas_set_bg_anim(Layer *layer, bool active, uint16_t elapsed_ms);

// Drives "Planet seek" (shake_anim_mode 4) -- see its own comment in
// canvas_update_proc(), and shake_anim_mode's own comment in
// eclipse_data.h for the feature as a whole.
void eclipse_canvas_set_planet_seek(Layer *layer, bool active, uint16_t elapsed_ms, int32_t heading_deg);

// Drives "Paths" (shake_anim_mode 5) -- see its own comment in
// background_layer.c and shake_anim_mode's own comment in eclipse_data.h.
void eclipse_canvas_set_shake_paths(Layer *layer, bool active, uint16_t elapsed_ms);

// Call every second from the tick handler; the canvas's own internal
// once-a-minute throttle decides whether this actually triggers a
// redraw or just returns immediately, so this is always cheap to call.
void eclipse_canvas_tick(Layer *layer);

// Figures out which phase "now" falls into relative to the contact
// times in `data`, writes a short human label + countdown (e.g.
// "Totality in 12:34" / "Partial ends in 0:47") into buf, and
// returns the phase so the caller can decide how urgently to
// refresh the canvas.
EclipsePhase eclipse_get_status_text(const EclipseData *data, time_t now,
                                      char *buf, size_t buf_len);

// True if the sky is currently bright enough (day through civil
// twilight) that dark text reads better than light text on top of
// it. Cheap -- just interpolates the transmitted altitude samples,
// no drawing -- so it's safe to call every second.
bool eclipse_sky_is_bright(const EclipseData *data, time_t now);

// A short word ("Sunny", "Overcast", "Rain", ...) summarizing current
// conditions from weather_condition + cloud_cover_pct -- shared with
// the corners overlay's "current conditions" content type so both
// places agree on the same wording.
const char *short_condition_text(uint8_t weather_condition, uint8_t cloud_pct);

// Draws an accurately-shaped Moon phase disc (not just an icon glyph)
// at `center`/`radius`, tinted `lit_color` on the illuminated side --
// shared by the sky canvas's own Moon rendering (which always passes
// GColorWhite, preserving its usual look) and the corners overlay's
// "Moon phase" content type (which passes whatever color that
// corner's color mode calls for).
void draw_moon_phase(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                      uint8_t phase_pct, bool waxing, GColor lit_color);

// Compact ("WxGb", "Full", ...) Moon phase name for the corners
// overlay's tight box width -- see the .c file for the full set.
const char *moon_phase_short_name(uint8_t pct, bool waxing);

// How many of the 5 tracked naked-eye planets (Mercury/Venus/Mars/
// Jupiter/Saturn -- see PlanetId) are currently above the horizon,
// interpolated from the same planet_alt_decideg samples the sky
// canvas already animates their positions from. Used by the "Planets
// visible" corner content -- purely a re-read of data already being
// sent every refresh, no new phone-side computation.
uint8_t background_count_visible_planets(const EclipseData *d, time_t now);

// Where a mark on a ring (custom or preset) actually lands, given the
// ring's own inner/outer border percentage and eccentricity -- see the
// .c file's own comment on this function for why it's exposed here.
// angle is in native TRIG_MAX_ANGLE units, 0 = 12 o'clock, clockwise.
GPoint point_on_ring(GPoint center, GRect screen, int32_t angle,
                      uint8_t pct, uint8_t eccentricity_pct);
