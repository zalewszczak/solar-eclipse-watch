#pragma once

#include <pebble.h>
#include "eclipse_data.h"
#include "input.h"

// Coordinates visual status updates and the presentation-side callbacks from
// the input subsystem. This module owns how application events invalidate or
// update visible layers; policy/state remains in the corresponding services.
void watchface_ui_init(EclipseData *data);
void watchface_ui_deinit(void);

void watchface_ui_refresh_status(bool force_canvas);
void watchface_ui_update_planet_seek_accuracy_label(bool active);

const InputCallbacks *watchface_ui_input_callbacks(void);
