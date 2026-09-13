#pragma once

#include <pebble.h>
#include "eclipse_data.h"

typedef void (*HandsControllerInvalidateHandler)(void *context);

// Application-level controller for the big-analog hand layer and the
// one-shot startup clock animation. Low-level hand geometry remains in
// hand_layer.c.

void hands_controller_init(EclipseData *data, HandsControllerInvalidateHandler handler, void *context);
void hands_controller_deinit(void);

Layer *hands_controller_create_layer(GRect frame);
void hands_controller_destroy_layer(Layer *layer);

void hands_controller_start_startup_animation(void);
bool hands_controller_animation_active(void);
int32_t hands_controller_animation_progress_1000(void);
int32_t hands_controller_animation_eased_progress_1000(void);
