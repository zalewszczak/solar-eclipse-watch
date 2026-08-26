#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// The sun/moon graphic. Owns its own draw state; call
// eclipse_canvas_set_data() whenever a fresh EclipseData arrives or
// once a minute so the moon's position animates smoothly.
Layer *eclipse_canvas_create(GRect frame);
void eclipse_canvas_destroy(Layer *layer);
void eclipse_canvas_set_data(Layer *layer, EclipseData *data);

// Toggles the "Sun" / "Moon" / "Saturn" name labels shown briefly
// next to each visible body after a shake gesture. main.c calls this
// true on tap and false again after a few seconds via app_timer.
void eclipse_canvas_set_show_labels(Layer *layer, bool show);

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
