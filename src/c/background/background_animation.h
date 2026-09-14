#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

// Duration of the startup background sweep. The hands can use the same
// normalized progress so their optional planet-sweep time shift stays
// synchronized with the background animation.
#define BACKGROUND_ANIMATION_DURATION_MS 1400

// Starts the one-shot startup background animation when the current settings
// allow it. Safe to call more than once; an animation is played at most once
// per app session.
void background_animation_start(EclipseData *data, Layer *canvas_layer);

// Cancels any pending animation timer. Call during app shutdown.
void background_animation_deinit(void);

// True while the startup background animation is actively running.
bool background_animation_is_active(void);

// Returns the current animation progress in the range 0..1000.
// Returns 1000 when no animation is active, so callers can use it as a
// conventional "fully settled" value without special casing normal draws.
int32_t background_animation_progress_1000(void);
