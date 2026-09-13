#pragma once

#include <pebble.h>
#include "eclipse_data.h"
#include "feature_slot.h"

// Refresh orchestration for the feature-slot cache. This module coordinates
// value computation, segment measurement and the shared custom feature font,
// but leaves layout and pixel rendering to their dedicated modules.
void feature_controller_set_data(FeaturesState *state, EclipseData *data);
void feature_controller_refresh_values(FeaturesState *state);
void feature_controller_refresh_second_slots(FeaturesState *state);
void feature_controller_refresh_content(FeaturesState *state, uint8_t content);
bool feature_controller_content_in_use(const EclipseData *data, uint8_t content);

void feature_controller_ensure_corner_custom_font(uint8_t font_id);
void feature_controller_unload_fonts(void);
GFont feature_controller_font(uint8_t font_id);
int16_t feature_controller_font_height(uint8_t font_id);
int16_t feature_controller_font_y_offset(uint8_t font_id);
