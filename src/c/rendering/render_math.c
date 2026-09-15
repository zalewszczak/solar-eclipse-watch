#include "./render_math.h"

#define RENDER_GROUND_H 18

uint8_t render_math_lerp8(uint8_t a, uint8_t b, int32_t num, int32_t den) {
  if (den == 0) return a;
  return (uint8_t)(a + ((int32_t)(b - a) * num) / den);
}

uint8_t render_math_dither_channel(uint8_t continuous_255, uint8_t bayer_0_15) {
  int32_t scaled = (int32_t)continuous_255 * 3;
  int32_t level = scaled / 255;
  int32_t rem = scaled - level * 255;
  int32_t threshold = (bayer_0_15 * 255) / 16;
  if (rem > threshold && level < 3) level++;
  return (uint8_t)level;
}

GColor render_math_dither_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t bayer_0_15) {
  return GColorFromRGB(render_math_dither_channel(r, bayer_0_15) * 85,
                       render_math_dither_channel(g, bayer_0_15) * 85,
                       render_math_dither_channel(b, bayer_0_15) * 85);
}

uint16_t render_math_isqrt32(int32_t v) {
  if (v <= 0) return 0;
  uint32_t x = (uint32_t)v;
  uint32_t res = 0;
  uint32_t bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= res + bit) {
      x -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return (uint16_t)res;
}

int16_t render_math_cloud_band_y(GRect bounds, uint8_t cloud_altitude_pct) {
  int16_t half_h = bounds.size.h / 2;
  int16_t lower_top = bounds.origin.y + half_h;
  int16_t lower_bottom = bounds.origin.y + bounds.size.h - RENDER_GROUND_H;
  return lower_bottom - (((int32_t)(lower_bottom - lower_top) * cloud_altitude_pct) / 100);
}
