#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

// Owns the application-level composition of the sky, clock, hands and
// feature layers for the three supported screen layouts.
void layout_controller_init(EclipseData *data, Window *window);
void layout_controller_deinit(void);

void layout_controller_apply(void);
void layout_controller_unload(void);
void layout_controller_handle_unobstructed(AnimationProgress progress);

Layer *layout_controller_canvas_layer(void);
Layer *layout_controller_top_gradient_layer(void);
Layer *layout_controller_hands_layer(void);
Layer *layout_controller_features_layer(void);
