#include "./feature_rules.h"

int16_t feature_rules_convert_temp(int16_t celsius, uint8_t temp_unit) {
  if (temp_unit == 1) return (int16_t)((celsius * 9) / 5 + 32);
  if (temp_unit == 2) return (int16_t)(celsius + 273);
  return celsius;
}

int16_t feature_rules_apparent_temp_c(int16_t temp_c, int16_t wind_kmh, uint8_t humidity_pct) {
  if (temp_c <= 10 && wind_kmh > 4) {
    int16_t chill = (int16_t)((wind_kmh - 4) / 5);
    if (chill > 12) chill = 12;
    return temp_c - chill;
  }
  if (temp_c >= 27 && humidity_pct > 40) {
    int16_t bump = (int16_t)(((int32_t)(humidity_pct - 40) * 3) / 20);
    if (bump > 8) bump = 8;
    return temp_c + bump;
  }
  return temp_c;
}

int16_t feature_rules_convert_wind(int16_t kmh, uint8_t wind_speed_unit) {
  if (wind_speed_unit == 1) return (int16_t)((kmh * 621) / 1000);
  if (wind_speed_unit == 2) return (int16_t)((kmh * 1000) / 3600);
  if (wind_speed_unit == 3) return (int16_t)((kmh * 540) / 1000);
  return kmh;
}

void feature_rules_to_upper_str(char *s) {
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z') *s -= 32;
  }
}

