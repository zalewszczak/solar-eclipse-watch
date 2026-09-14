#include "feature_rules.h"

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

bool feature_rules_hourly_vibe_is_scheduled_now(const EclipseData *d, time_t now) {
  if (d->hourly_vibe_mode == 0) return false;
  struct tm *t = localtime(&now);
  if (!((d->hourly_vibe_days_mask >> t->tm_wday) & 1)) return false;
  int minute_of_day = t->tm_hour * 60 + t->tm_min;
  int start = d->hourly_vibe_start_min, end = d->hourly_vibe_end_min;
  if (start == end) return true;
  if (start < end) return minute_of_day >= start && minute_of_day <= end;
  return minute_of_day >= start || minute_of_day <= end;
}

bool feature_rules_content_is_weather_derived(uint8_t content) {
  switch (content) {
    case 4: case 5: case 6: case 104: case 7: case 8: case 9:
    case 14: case 15: case 31: case 32: case 34: case 35: case 37:
    case 73: case 76: case 77:
    case 87: case 88: case 89: case 90: case 91: case 92:
      return true;
    default:
      return false;
  }
}

bool feature_rules_content_needs_second_refresh(uint8_t content) {
  switch (content) {
    case 63:
    case 69: case 70: case 71: case 72:
      return true;
    default:
      return false;
  }
}
