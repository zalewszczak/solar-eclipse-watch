#pragma once

#include <pebble.h>
#include "eclipse_data.h"
#include "eclipse_status.h"

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
