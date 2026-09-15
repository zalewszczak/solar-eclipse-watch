#pragma once

#include <pebble.h>

// Small integer math shared by the background renderers. Keeping these
// routines in one translation unit avoids emitting the same raster math in
// sky, weather, celestial, and marker rendering code.
uint8_t render_math_lerp8(uint8_t a, uint8_t b, int32_t num, int32_t den);
uint8_t render_math_dither_channel(uint8_t continuous_255, uint8_t bayer_0_15);
GColor render_math_dither_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t bayer_0_15);
uint16_t render_math_isqrt32(int32_t v);
int16_t render_math_cloud_band_y(GRect bounds, uint8_t cloud_altitude_pct);
