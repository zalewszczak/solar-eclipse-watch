#include <pebble.h>
#include "hourly_vibration.h"

static EclipseData *s_data;

static bool time_in_range(int minute_of_day) {
  int start = s_data->hourly_vibe_start_min;
  int end = s_data->hourly_vibe_end_min;

  // Equal endpoints, including the default 0 == 0, mean all 24 hours.
  if (start == end) return true;

  // A range that crosses midnight is represented by start > end.
  if (start < end) {
    return minute_of_day >= start && minute_of_day <= end;
  }
  return minute_of_day >= start || minute_of_day <= end;
}

static void trigger_vibration(void) {
  switch (s_data->hourly_vibe_pattern) {
    case 1:
      vibes_double_pulse();
      break;
    case 2:
      vibes_long_pulse();
      break;
    default:
      // 0, and any unrecognized value, retain the original short pulse.
      vibes_short_pulse();
      break;
  }
}

void hourly_vibration_init(EclipseData *data) {
  s_data = data;
}

void hourly_vibration_deinit(void) {
  s_data = NULL;
}

void hourly_vibration_handle_minute(struct tm *tick_time) {
  if (!s_data || !tick_time || s_data->hourly_vibe_mode == 0) return;
  if (!((s_data->hourly_vibe_days_mask >> tick_time->tm_wday) & 1)) return;

  int minute_of_day = tick_time->tm_hour * 60 + tick_time->tm_min;
  if (!time_in_range(minute_of_day)) return;

  bool should_fire;
  if (s_data->hourly_vibe_mode == 1) {
    should_fire = tick_time->tm_min == 0;
  } else {
    int interval = s_data->hourly_vibe_interval_min > 0
                     ? s_data->hourly_vibe_interval_min
                     : 30;
    should_fire = (minute_of_day % interval) == 0;
  }

  if (!should_fire) return;
  if (!s_data->hourly_vibe_override_quiet && quiet_time_is_active()) return;

  trigger_vibration();
}
