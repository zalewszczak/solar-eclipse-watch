#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"
#include "./feature_slot.h"
#include "./primitives/primitive_ids.h"

// Refresh orchestration for the feature-slot cache. This module coordinates
void feature_controller_set_data(FeaturesState *state, EclipseData *data);
void feature_controller_refresh_values(FeaturesState *state);
void feature_controller_refresh_second_slots(FeaturesState *state);
void feature_controller_refresh_primitive(FeaturesState *state, PrimitiveId primitive);
bool feature_controller_primitive_in_use(const EclipseData *data, PrimitiveId primitive);

void feature_controller_ensure_corner_custom_font(uint8_t font_id);
void feature_controller_unload_fonts(void);
GFont feature_controller_font(uint8_t font_id);
