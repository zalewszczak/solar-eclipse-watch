#include "./feature_support.h"
#include "../rendering/background/sky_layer.h"
#include "../rendering/render_math.h"
#include "../rendering/background/celestial_layer.h"

// Planets count as visible from just before rising until just after setting.
#define PLANET_RISE_SET_MARGIN_S 180

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
