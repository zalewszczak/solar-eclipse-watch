#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"

typedef void (*HandsControllerInvalidateHandler)(void *context);

// Controls the big-analog hand layer and one-shot startup animation.


void hands_controller_init(EclipseData *data, HandsControllerInvalidateHandler handler, void *context);
void hands_controller_deinit(void);

Layer *hands_controller_create_layer(GRect frame);
void hands_controller_destroy_layer(Layer *layer);

