#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// The digital clock's own panel height, in px on this app's fixed
// 200x228 screen -- shared between background_layer.c (sizes/positions
// the sky canvas around it, and feeds it into the gradient math the two
// Digital layouts' panels use -- see eclipse_top_gradient_create()'s own
// comment below) and features_layer.c (corner/side-column geometry --
// see that file's own use of this same constant). Was a features_layer.c-
// local #define until Digital top needed it here too; kept the same
// name and value so nothing downstream had to change.
#define DIGITAL_PANEL_H 76

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

// Digital top layout only: a thin, always-gradient-only strip (see
// apply_layout()'s own DIGITAL_PANEL_H-tall frame for it) that fills
// the panel's reserved band at the screen's TOP with a plain
// continuation of the SAME sky gradient/flat-black-space-mode wash the
// main sky canvas below it draws -- no sun/moon/stars/clouds/markers,
// "only sky gradient" per the request. Deliberately its own tiny
// module rather than another eclipse_canvas_create() frame -- it needs
// none of that layer's astronomy/animation/caching machinery, just the
// current sky colors, recomputed fresh each call (no caching -- see
// the .c file's own compute_sky_wash() comment for why this is cheap
// enough not to need it). set_data re-reads whatever's currently in
// `data` on every call rather than storing a copy, same "no separate
// staleness to track" reasoning as the plain corner/edge text draws.
Layer *eclipse_top_gradient_create(GRect frame);
void eclipse_top_gradient_destroy(Layer *layer);
void eclipse_top_gradient_set_data(Layer *layer, EclipseData *data);

// Toggles the "Sun" / "Moon" / "Saturn" name labels shown briefly
// next to each visible body after a shake gesture. main.c calls this
// true on tap and false again after a few seconds via app_timer.
void eclipse_canvas_set_show_labels(Layer *layer, bool show);

// Drives the "animate background on start" effect -- active/
// elapsed_ms mirror pebble-eclipse-watch.c's own s_bg_anim_active/
// s_bg_anim_elapsed_ms exactly (see maybe_start_startup_background_
// animation() there); this just hands them to the canvas's own draw
// code (see canvas_update_proc's own comment on where they're used).
// Only forces an immediate full redraw on entering/leaving bg_anim_
// mode 2 ("Planets" -- its sky_now-driven gradient sweep genuinely
// changes every frame) or on the active/inactive transition for modes
// 1/3 (whose backdrop is static -- only the overlaid clouds/markers
// move -- so canvas_update_proc's own cache-blit path plus draw_bg_
// anim_clouds_overlay()/draw_bg_anim_markers_overlay() handle every
// frame in between cheaply); see this function's own body for the
// exact condition.
void eclipse_canvas_set_bg_anim(Layer *layer, bool active, uint16_t elapsed_ms);

// Drives "Planet seek" (shake_anim_mode 2 or 3) -- see its own comment in
// canvas_update_proc(), and shake_anim_mode's own comment in
// eclipse_data.h for the feature as a whole.
void eclipse_canvas_set_planet_seek(Layer *layer, bool active, uint16_t elapsed_ms, int32_t heading_deg);

// Call every second from the tick handler; the canvas's own internal
// once-a-minute throttle decides whether this actually triggers a
// redraw or just returns immediately, so this is always cheap to call.
void eclipse_canvas_tick(Layer *layer);

// Figures out which phase "now" falls into relative to the contact
// times in `data`, writes a short human label + countdown (e.g.
// "Totality in 12:34" / "Partial ends in 0:47") into buf, and
// returns the phase so the caller can decide how urgently to
// refresh the canvas. live_seconds should be whatever the caller is
// actually driving its own redraw cadence off of right now (see
// pebble-eclipse-watch.c's update_tick_subscription()) -- only used
// to decide the pre-eclipse "Starts in" countdown's own precision:
// full M:SS when the screen is already updating every second for
// some other reason, a plain whole-minutes(+hours) readout otherwise,
// so the displayed countdown never implies more precision than the
// redraw rate can actually keep up with.
EclipsePhase eclipse_get_status_text(const EclipseData *data, time_t now,
                                      char *buf, size_t buf_len, bool live_seconds);

// True if the sky is currently bright enough (day through civil
// twilight) that dark text reads better than light text on top of
// it. Cheap -- just interpolates the transmitted altitude samples,
// no drawing -- so it's safe to call every second.
bool eclipse_sky_is_bright(const EclipseData *data, time_t now);

// True from first contact up to (not including) last contact -- see
// the .c file's own comment for why every "is the eclipse happening
// right now" check in the app goes through this one function.
bool eclipse_is_active(const EclipseData *data, time_t now);

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

// The tightest (closest-to-center) inner-border reach among a
// procedural marker style's hour/second rings -- i.e. how far into
// the middle of the face that style's own artwork actually goes --
// plus the eccentricity of whichever ring that reach belongs to.
// Lets features_layer.c compute its inner-empty-area margins the same
// point_on_ring()-based way for the procedural presets (0/1/2) and
// "none" (9, no ring drawn -- reports a full 100% reach, so it never
// constrains anything beyond the caller's own floor margin) that it
// already does for custom rings, instead of the flat hardcoded
// numbers those styles used to be stuck with. Meaningless for bitmap
// styles (3-7, no ring geometry at all) and for custom (8, which
// reads its own live MarkerRingConfig directly instead) -- callers
// shouldn't call this for those.
void background_marker_inner_reach(uint8_t marker_style, uint8_t *out_pct, uint8_t *out_eccentricity);
