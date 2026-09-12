#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// Digital clock panel height on the fixed 200x228 display.
#define DIGITAL_PANEL_H 76

// Main cached sky/composition canvas.
Layer *background_layer_create(GRect frame);
void background_layer_destroy(Layer *layer);
void background_layer_set_data(Layer *layer, EclipseData *data);

// Toggles the "Sun" / "Moon" / "Saturn" name labels shown briefly
// next to each visible body after a shake gesture. pebble-eclipse-watch.c calls this
// true on tap and false again after a few seconds via app_timer.
void background_layer_set_labels_visible(Layer *layer, bool show);

// Drives the "animate background on start" effect -- active/
// elapsed_ms mirror the application-owned animation controller; this forwards the
// current animation state to the canvas renderer.
// Only forces an immediate full redraw on entering/leaving bg_anim_
// mode 2 ("Planets" -- its sky_now-driven gradient sweep genuinely
// changes every frame) or on the active/inactive transition for modes
// 1/3 (whose backdrop is static -- only the overlaid clouds/markers
// move -- so the canvas update procedure's own cache-blit path plus draw_bg_
// anim_clouds_overlay()/draw_bg_anim_markers_overlay() handle every
// frame in between cheaply); see this function's own body for the
// exact condition.
void background_layer_set_background_animation(Layer *layer, bool active, uint16_t elapsed_ms);

// Drives "Planet seek" (shake_anim_mode 2 or 3) -- see its own comment in
// the canvas update procedure, and shake_anim_mode's own comment in
// eclipse_data.h for the feature as a whole.
void background_layer_set_planet_seek(Layer *layer, bool active, uint16_t elapsed_ms, int32_t heading_deg);

// Call every second from the tick handler; the canvas's own internal
// once-a-minute throttle decides whether this actually triggers a
// redraw or just returns immediately, so this is always cheap to call.
void background_layer_tick(Layer *layer);

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

// True from first contact up to (not including) last contact -- see
// the .c file's own comment for why every "is the eclipse happening
// right now" check in the app goes through this one function.
bool eclipse_is_active(const EclipseData *data, time_t now);



