#include "./feature_support.h"
#include "../rendering/background/sky_layer.h"
#include "../rendering/render_math.h"

// Planets count as visible from just before rising until just after setting.
#define PLANET_RISE_SET_MARGIN_S 180

void feature_draw_moon_phase(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                             uint8_t phase_pct, bool waxing, GColor lit_color) {
  int32_t k100 = phase_pct, side = waxing ? 1 : -1;
  int16_t x0 = center.x - radius, x1 = center.x + radius;
  int16_t y0 = center.y - radius, y1 = center.y + radius;
  for (int16_t y = y0; y <= y1; y++) {
    int16_t dy = y - center.y;
    int32_t term = (int32_t)radius * radius - (int32_t)dy * dy;
    if (term < 0) continue;
    int16_t half_width = (int16_t)render_math_isqrt32(term);
    bool gibbous = k100 > 50;
    int32_t a = gibbous ? (radius * (2 * k100 - 100)) / 100 : (radius * (100 - 2 * k100)) / 100;
    int32_t ellipse_w = (a * half_width) / (radius == 0 ? 1 : radius);
    for (int16_t x = x0 + (radius - half_width); x <= x1 - (radius - half_width); x++) {
      if (x < bounds.origin.x || x >= bounds.origin.x + bounds.size.w ||
          y < bounds.origin.y || y >= bounds.origin.y + bounds.size.h) continue;
      int16_t dx = x - center.x;
      bool lit = gibbous ? (side * dx > -ellipse_w) : (side * dx >= ellipse_w);
      graphics_context_set_fill_color(ctx, lit ? lit_color : GColorDarkGray);
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, center, radius);
}

const char *feature_moon_phase_short_name(uint8_t pct, bool waxing) {
  if (pct <= 2) return "New";
  if (pct >= 98) return "Full";
  if (waxing) {
    if (pct < 48) return "WxCr";
    if (pct <= 52) return "1stQ";
    return "WxGb";
  }
  if (pct > 52) return "WnGb";
  if (pct >= 48) return "3rdQ";
  return "WnCr";
}

uint8_t feature_count_visible_planets(const EclipseData *d, time_t now) {
  if (sky_layer_is_bright(d, now)) return 0;
  uint8_t count = 0;
  for (int p = 0; p < PLANET_COUNT; p++) {
    time_t rise = d->planet_rise[p], set = d->planet_set[p];
    if (rise == 0 || set == 0) continue;
    if (now >= rise - PLANET_RISE_SET_MARGIN_S && now <= set + PLANET_RISE_SET_MARGIN_S) count++;
  }
  return count;
}

const char *feature_weather_short_condition_text(uint8_t weather_condition, uint8_t cloud_pct) {
  switch (weather_condition) {
    case 1: return "Fog";
    case 2: return "Rain";
    case 3: return "Snow";
    case 4: return "Storm";
    default:
      if (cloud_pct < 20) return "Sunny";
      if (cloud_pct < 60) return "P.Cloudy";
      if (cloud_pct < 90) return "Cloudy";
      return "Overcast";
  }
}
