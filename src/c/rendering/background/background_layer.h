#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"
#include "../../data/eclipse_status.h"

// Digital clock panel height on the fixed 200x228 display.
#define DIGITAL_PANEL_H 76

// Main cached sky/composition canvas.
Layer *background_layer_create(GRect frame);
void background_layer_destroy(Layer *layer);
void background_layer_set_data(Layer *layer, EclipseData *data);

// Toggles the "Sun" / "Moon" / "Saturn" name labels shown briefly
// next to visible bodies after a shake gesture.
void background_layer_set_labels_visible(Layer *layer, bool show);

// Updates the startup background-animation state used by the canvas renderer.
// The animation controller owns elapsed-time policy; this layer only consumes
// the current state and decides whether its cached composition can be reused.
void background_layer_set_background_animation(Layer *layer, bool active, uint16_t elapsed_ms);

// Drives "Planet seek" (shake_anim_mode 2 or 3) -- see its own comment in
// the canvas update procedure, and shake_anim_mode's own comment in
// eclipse_data.h for the feature as a whole.
void background_layer_set_planet_seek(Layer *layer, bool active, uint16_t elapsed_ms, int32_t heading_deg);

// Call every second from the tick handler; the canvas's own internal
// once-a-minute throttle decides whether this actually triggers a
// redraw or just returns immediately, so this is always cheap to call.
void background_layer_tick(Layer *layer);
