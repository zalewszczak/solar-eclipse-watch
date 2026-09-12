#pragma once

#include <pebble.h>

GColor feature_colors_seven_stop_gradient(int32_t value, int32_t min_v, int32_t max_v);
GColor feature_colors_white_to_turquoise_gradient(int32_t value, int32_t min_v, int32_t max_v);
GColor feature_colors_seven_stop_gradient_reversed(int32_t value, int32_t min_v, int32_t max_v);
GColor feature_colors_red_green_gradient(uint8_t pct);
GColor feature_colors_white_to_red_gradient(uint8_t kp_x10);
GColor feature_colors_heart_rate_gradient(int bpm);
GColor feature_colors_altitude_gradient(int16_t altitude_m);
GColor feature_colors_meteor_intensity_gradient(uint8_t pct);
GColor feature_colors_daynight_gradient(time_t at, time_t sun_rise, time_t sun_set);
GColor feature_colors_linear_white_black(int32_t value, int32_t min_v, int32_t max_v);
GColor feature_colors_date_year_progress_gradient(const struct tm *t);
GColor feature_colors_weather_staleness_gradient(time_t now, time_t last_update);
GColor feature_colors_sunny_yellow_white_gradient(uint8_t cloud_pct);
GColor feature_colors_overcast_gray_gradient(uint8_t cloud_pct);
GColor feature_colors_snow_white_gradient(uint8_t cloud_pct);
GColor feature_colors_weather_condition_color(uint8_t condition, uint8_t cloud_pct, uint8_t rain_chance_pct);
