#include <pebble.h>
#include "./hourly_vibration.h"

#define VIBE_FLASH_DURATION_MS 10000
#define VIBE_FLASH_STEP_MS 1000

static EclipseData *s_data;

static AppTimer *s_flash_timer;
static bool s_flash_active;
static bool s_flash_inverted;
static uint16_t s_flash_elapsed_ms;
static bool s_flash_targets_hour_hand;
static HourlyVibrationFlashHandler s_flash_handler;
static void *s_flash_context;

// Equal endpoints mean all day; start > end denotes a midnight-crossing range.
static bool time_in_range(int minute_of_day) {
  int start = s_data->hourly_vibe_start_min;
  int end = s_data->hourly_vibe_end_min;

  if (start == end) return true;

  if (start < end) {
    return minute_of_day >= start && minute_of_day <= end;
  }
  return minute_of_day >= start || minute_of_day <= end;
}

// Unknown vibration patterns fall back to the original short pulse.
static void trigger_vibration(void) {
  switch (s_data->hourly_vibe_pattern) {
    case 1:
      vibes_double_pulse();
      break;
    case 2:
      vibes_long_pulse();
      break;
    default:

      vibes_short_pulse();
      break;
  }
}

static void flash_timer_callback(void *context) {
  (void)context;
  s_flash_elapsed_ms += VIBE_FLASH_STEP_MS;
  if (s_flash_elapsed_ms >= VIBE_FLASH_DURATION_MS) {
    s_flash_active = false;
    s_flash_inverted = false;
    s_flash_timer = NULL;
  } else {
    s_flash_inverted = !s_flash_inverted;
    s_flash_timer = app_timer_register(VIBE_FLASH_STEP_MS, flash_timer_callback, NULL);
  }
  if (s_flash_handler) s_flash_handler(s_flash_context);
}

// this_fire_interval_min is 0 for mode 1 ("on full hours" -- always a
// 60-minute cadence); mode 2 passes its own configured interval.
static void start_flash(int this_fire_interval_min) {
  if (s_flash_timer) app_timer_cancel(s_flash_timer);
  s_flash_active = true;
  s_flash_inverted = true; // starts inverted for the first second
  s_flash_elapsed_ms = 0;
  s_flash_targets_hour_hand = (this_fire_interval_min == 0) || (this_fire_interval_min == 60);
  s_flash_timer = app_timer_register(VIBE_FLASH_STEP_MS, flash_timer_callback, NULL);
  if (s_flash_handler) s_flash_handler(s_flash_context);
}

void hourly_vibration_init(EclipseData *data, HourlyVibrationFlashHandler flash_handler, void *flash_context) {
  s_data = data;
  s_flash_handler = flash_handler;
  s_flash_context = flash_context;
}

void hourly_vibration_deinit(void) {
  if (s_flash_timer) {
    app_timer_cancel(s_flash_timer);
    s_flash_timer = NULL;
  }
  s_flash_active = false;
  s_flash_inverted = false;
  s_data = NULL;
  s_flash_handler = NULL;
  s_flash_context = NULL;
}

void hourly_vibration_handle_minute(struct tm *tick_time) {
  if (!s_data || !tick_time || s_data->hourly_vibe_mode == 0) return;
  if (!((s_data->hourly_vibe_days_mask >> tick_time->tm_wday) & 1)) return;

  int minute_of_day = tick_time->tm_hour * 60 + tick_time->tm_min;
  if (!time_in_range(minute_of_day)) return;

  bool should_fire;
  int interval = s_data->hourly_vibe_interval_min > 0
                   ? s_data->hourly_vibe_interval_min
                   : 30;
  if (s_data->hourly_vibe_mode == 1) {
    should_fire = tick_time->tm_min == 0;
  } else {
    should_fire = (minute_of_day % interval) == 0;
  }

  if (!should_fire) return;
  if (!s_data->hourly_vibe_override_quiet && quiet_time_is_active()) return;

  trigger_vibration();
  start_flash(s_data->hourly_vibe_mode == 1 ? 0 : interval);
}

bool hourly_vibration_flash_is_active(void) {
  return s_flash_active;
}

bool hourly_vibration_flash_is_inverted(void) {
  return s_flash_active && s_flash_inverted;
}

bool hourly_vibration_flash_targets_hour_hand(void) {
  return s_flash_targets_hour_hand;
}
