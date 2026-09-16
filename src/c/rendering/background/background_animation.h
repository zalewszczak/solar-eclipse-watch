#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"

// Duration of the startup background sweep. The hands can use the same
#define BACKGROUND_ANIMATION_DURATION_MS 1400

// Starts the one-shot startup background animation when the current settings
void background_animation_start(EclipseData *data, Layer *canvas_layer);

// Cancels any pending animation timer. Call during app shutdown.
void background_animation_deinit(void);

// True while the startup background animation is actively running.
bool background_animation_is_active(void);

// Returns the current animation progress in the range 0..1000.
int32_t background_animation_progress_1000(void);
