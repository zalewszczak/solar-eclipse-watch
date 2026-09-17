#include "./feature_colors.h"

GColor feature_colors_seven_stop_gradient(int32_t value, int32_t min_v, int32_t max_v) {
  static const uint8_t STOPS[7][3] = {
    {  64, 224, 208 },  // turquoise
    { 173, 216, 230 },  // light blue
    {   0, 200,   0 },  // green
    { 255, 220,   0 },  // yellow
    { 255, 140,   0 },  // orange
    { 220,  20,  20 },  // red
    { 148,   0, 211 },  // violet
  };
  if (max_v <= min_v || value <= min_v) return GColorFromRGB(STOPS[0][0], STOPS[0][1], STOPS[0][2]);
  if (value >= max_v) return GColorFromRGB(STOPS[6][0], STOPS[6][1], STOPS[6][2]);

  int32_t pos_x6000 = ((value - min_v) * 6000) / (max_v - min_v); // 0..6000 across 6 segments
  int seg = (int)(pos_x6000 / 1000);
  if (seg > 5) seg = 5;
  int32_t seg_frac = pos_x6000 - (int32_t)seg * 1000; // 0..1000 within the segment

  int16_t r = STOPS[seg][0] + (int16_t)(((STOPS[seg + 1][0] - STOPS[seg][0]) * seg_frac) / 1000);
  int16_t g = STOPS[seg][1] + (int16_t)(((STOPS[seg + 1][1] - STOPS[seg][1]) * seg_frac) / 1000);
  int16_t b = STOPS[seg][2] + (int16_t)(((STOPS[seg + 1][2] - STOPS[seg][2]) * seg_frac) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// Simple white (low) -> turquoise (high) gradient, used for humidity,
GColor feature_colors_white_to_turquoise_gradient(int32_t value, int32_t min_v, int32_t max_v) {
  if (max_v <= min_v) return GColorWhite;
  int32_t clamped = value < min_v ? min_v : (value > max_v ? max_v : value);
  int32_t frac1000 = ((clamped - min_v) * 1000) / (max_v - min_v);
  int16_t r = 255 - (int16_t)(((255 - 64) * frac1000) / 1000);
  int16_t g = 255 - (int16_t)(((255 - 224) * frac1000) / 1000);
  int16_t b = 255 - (int16_t)(((255 - 208) * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// Same 7-stop rainbow as feature_colors_seven_stop_gradient(), but reversed: the
GColor feature_colors_seven_stop_gradient_reversed(int32_t value, int32_t min_v, int32_t max_v) {
  return feature_colors_seven_stop_gradient(min_v + (max_v - value), min_v, max_v);
}

// Red (0%) -> green (100%+). Shared by "steps today"/"step goal %"
GColor feature_colors_red_green_gradient(uint8_t pct) {
  if (pct >= 100) return GColorFromRGB(0, 200, 0);
  int32_t frac1000 = ((int32_t)pct * 1000) / 100;
  int16_t r = 220 - (int16_t)((220 * frac1000) / 1000);
  int16_t g = (int16_t)((200 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, 0);
}

// White (Kp 0, geomagnetically quiet) -> red (Kp 9, extreme storm)
GColor feature_colors_white_to_red_gradient(uint8_t kp_x10) {
  if (kp_x10 >= 90) return GColorFromRGB(220, 0, 0);
  int32_t frac1000 = ((int32_t)kp_x10 * 1000) / 90;
  // Cap the green channel at 170. Pebble quantizes each RGB channel to
  int16_t g = 170 - (int16_t)((170 * frac1000) / 1000);
  int16_t b = g;
  return GColorFromRGB(255, (uint8_t)g, (uint8_t)b);
}

// Pink (calm/resting) -> red -> violet (dangerously high), scaled by
#define HR_LOW_BPM 60
#define HR_HIGH_BPM 120
#define HR_DANGER_BPM 180
GColor feature_colors_heart_rate_gradient(int bpm) {
  const int16_t pink[3]   = { 255, 105, 180 };
  const int16_t red[3]    = { 220,  20,  20 };
  const int16_t violet[3] = { 148,   0, 211 };
  const int16_t *from, *to;
  int32_t frac1000;
  if (bpm <= HR_LOW_BPM) {
    return GColorFromRGB((uint8_t)pink[0], (uint8_t)pink[1], (uint8_t)pink[2]);
  } else if (bpm >= HR_DANGER_BPM) {
    return GColorFromRGB((uint8_t)violet[0], (uint8_t)violet[1], (uint8_t)violet[2]);
  } else if (bpm <= HR_HIGH_BPM) {
    from = pink; to = red;
    frac1000 = ((int32_t)(bpm - HR_LOW_BPM) * 1000) / (HR_HIGH_BPM - HR_LOW_BPM);
  } else {
    from = red; to = violet;
    frac1000 = ((int32_t)(bpm - HR_HIGH_BPM) * 1000) / (HR_DANGER_BPM - HR_HIGH_BPM);
  }
  int16_t r = from[0] + (int16_t)(((to[0] - from[0]) * frac1000) / 1000);
  int16_t g = from[1] + (int16_t)(((to[1] - from[1]) * frac1000) / 1000);
  int16_t b = from[2] + (int16_t)(((to[2] - from[2]) * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// White (sea level) -> turquoise (high), for the "Altitude" content
#define ALTITUDE_GRADIENT_MAX_M 4000
GColor feature_colors_altitude_gradient(int16_t altitude_m) {
  return feature_colors_white_to_turquoise_gradient(altitude_m, 0, ALTITUDE_GRADIENT_MAX_M);
}

// White during the day, black at night, blending linearly across the
#define DAYNIGHT_TRANSITION_SECS 3600
GColor feature_colors_daynight_gradient(time_t at, time_t sun_rise, time_t sun_set) {
  if (sun_rise <= 0 || sun_set <= 0) return GColorWhite;
  bool is_daytime = (at >= sun_rise && at < sun_set);
  int32_t dist_to_rise = (int32_t)(at - sun_rise); // negative before rise, positive after
  int32_t dist_to_set  = (int32_t)(at - sun_set);
  int32_t abs_rise = dist_to_rise < 0 ? -dist_to_rise : dist_to_rise;
  int32_t abs_set  = dist_to_set  < 0 ? -dist_to_set  : dist_to_set;
  bool near_rise = abs_rise <= abs_set;
  int32_t dist = near_rise ? dist_to_rise : dist_to_set;
  int32_t abs_dist = near_rise ? abs_rise : abs_set;
  if (abs_dist >= DAYNIGHT_TRANSITION_SECS) return is_daytime ? GColorWhite : GColorBlack;
  // Within an hour of the nearer transition: blend across it. At
  int32_t frac1000 = ((dist + DAYNIGHT_TRANSITION_SECS) * 1000) / (2 * DAYNIGHT_TRANSITION_SECS);
  if (frac1000 < 0) frac1000 = 0;
  if (frac1000 > 1000) frac1000 = 1000;
  int16_t v = near_rise ? (int16_t)((255 * frac1000) / 1000) : 255 - (int16_t)((255 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// Plain black->white ramp across a field's own numeric range, no
GColor feature_colors_linear_white_black(int32_t value, int32_t min_v, int32_t max_v) {
  if (max_v <= min_v) return GColorWhite;
  int32_t clamped = value < min_v ? min_v : (value > max_v ? max_v : value);
  int32_t frac1000 = ((clamped - min_v) * 1000) / (max_v - min_v);
  int16_t v = (int16_t)((255 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// Multi-value dates (weekday+day, day/month, full dates, "long date",
GColor feature_colors_date_year_progress_gradient(const struct tm *t) {
  return feature_colors_seven_stop_gradient(t->tm_yday, 0, 365);
}

// White (just updated) -> full red at 2h+ stale, for the "last
#define WEATHER_STALE_RED_SECS (2 * 3600)
GColor feature_colors_weather_staleness_gradient(time_t now, time_t last_update) {
  if (last_update <= 0) return GColorRed; // never updated at all -- treat like fully stale
  int32_t age = (int32_t)(now - last_update);
  if (age <= 0) return GColorWhite;
  if (age >= WEATHER_STALE_RED_SECS) return GColorRed;
  int32_t frac1000 = (age * 1000) / WEATHER_STALE_RED_SECS;
  int16_t gb = 255 - (int16_t)((255 * frac1000) / 1000);
  return GColorFromRGB(255, (uint8_t)gb, (uint8_t)gb);
}

// The four weather-family color gradients "current conditions" (and

// Clear/sunny: white fading toward a warm golden-white as skies get
static GColor feature_colors_sunny_yellow_white_gradient(uint8_t cloud_pct) {
  uint8_t clamped = cloud_pct > OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct;
  int32_t frac1000 = ((int32_t)(OVERCAST_CLOUD_THRESHOLD - clamped) * 1000) / OVERCAST_CLOUD_THRESHOLD;
  int16_t b = 255 - (int16_t)((85 * frac1000) / 1000);
  return GColorFromRGB(255, 255, (uint8_t)b);
}

// Overcast/fog: light gray darkening toward a heavier gray as cloud
GColor feature_colors_overcast_gray_gradient(uint8_t cloud_pct) {
  uint8_t clamped = cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct;
  int32_t frac1000 = ((int32_t)(clamped - OVERCAST_CLOUD_THRESHOLD) * 1000) / (100 - OVERCAST_CLOUD_THRESHOLD);
  int16_t v = 200 - (int16_t)((115 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// Snow: white gaining a faint blue-white cast as it gets heavier
static GColor feature_colors_snow_white_gradient(uint8_t cloud_pct) {
  int32_t frac1000 = ((int32_t)cloud_pct * 1000) / 100;
  int16_t rg = 255 - (int16_t)((85 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)rg, (uint8_t)rg, 255);
}

// weather_condition: 0=clear/cloudy (cloud_pct alone decides sunny vs
GColor feature_colors_weather_condition_color(uint8_t condition, uint8_t cloud_pct, uint8_t rain_chance_pct) {
  if (condition == 4) return GColorFromRGB(255, 0, 0);
  if (condition == 3) return feature_colors_snow_white_gradient(cloud_pct);
  if (condition == 2) return feature_colors_white_to_turquoise_gradient(rain_chance_pct, 0, 100);
  if (condition == 1) return feature_colors_overcast_gray_gradient(cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct);
  return (cloud_pct < OVERCAST_CLOUD_THRESHOLD) ? feature_colors_sunny_yellow_white_gradient(cloud_pct) : feature_colors_overcast_gray_gradient(cloud_pct);
}

// temp_unit: 0=Celsius (input is already Celsius, passed through),
