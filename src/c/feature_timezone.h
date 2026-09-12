#pragma once

#include <pebble.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
  const char *abbr;
  int16_t base_offset_min;
  uint8_t dst_rule;
} TimezoneInfo;

const TimezoneInfo *feature_timezone_get(uint8_t index);
uint8_t feature_timezone_count(void);
int16_t feature_timezone_current_offset_min(const TimezoneInfo *tz, time_t utc_now);
GColor feature_timezone_daylight_color(int local_hour24);
