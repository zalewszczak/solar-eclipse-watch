#pragma once

#include <pebble.h>

GColor feature_color_resolve_flat(uint8_t color_mode, GColor dynamic_color, GColor main_color, GColor accent_color);
GColor feature_color_from_packed(uint8_t packed);
