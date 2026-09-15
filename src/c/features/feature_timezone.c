#include "./feature_timezone.h"

static const TimezoneInfo TIMEZONES[] = {
  { "LON",    0, 2 }, // London
  { "PAR",   60, 2 }, // Paris/Berlin/Madrid (Central European Time)
  { "CAI",  120, 0 }, // Cairo
  { "MOW",  180, 0 }, // Moscow
  { "DXB",  240, 0 }, // Dubai
  { "DEL",  330, 0 }, // Delhi/Mumbai (UTC+5:30)
  { "DAC",  360, 0 }, // Dhaka
  { "BKK",  420, 0 }, // Bangkok/Jakarta
  { "BJS",  480, 0 }, // Beijing/Shanghai/Singapore
  { "TOK",  540, 0 }, // Tokyo
  { "SYD",  600, 0 }, // Sydney (DST not modeled -- see note above)
  { "AKL",  720, 0 }, // Auckland (DST not modeled -- see note above)
  { "NYC", -300, 1 }, // New York
  { "CHI", -360, 1 }, // Chicago
  { "DEN", -420, 1 }, // Denver
  { "LAX", -480, 1 }, // Los Angeles
  { "ANC", -540, 1 }, // Anchorage
  { "HNL", -600, 0 }, // Honolulu
  { "SAO", -180, 0 }, // Sao Paulo
};
#define TIMEZONE_COUNT (int)(sizeof(TIMEZONES) / sizeof(TIMEZONES[0]))

// civil_from_days()/days_from_civil() -- the well-known constant-time
// Gregorian-calendar<->epoch-days conversion (Howard Hinnant's
// "civil_from_days"/"days_from_civil"), used instead of gmtime() so this
// doesn't depend on anything beyond plain integer arithmetic. Verified
// numerically against Python's datetime for round-trips across leap
// years and the epoch boundary before use.
static void civil_from_days(int32_t z, int *y, int *m, int *d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t year = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  uint32_t month = mp + (mp < 10 ? 3 : (uint32_t)-9);
  *y = year + (month <= 2 ? 1 : 0);
  *m = (int)month;
  *d = (int)day;
}

static int32_t days_from_civil(int y, int m, int d) {
  y -= (m <= 2) ? 1 : 0;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// 0=Sunday..6=Saturday -- day 0 (1970-01-01) was a Thursday.
static int day_of_week_from_days(int32_t days) {
  int32_t d = (days + 4) % 7;
  return (int)(d < 0 ? d + 7 : d);
}

// The Nth Sunday of a month as epoch days (nth=1 => first Sunday,
// nth=-1 => last Sunday).
static int32_t nth_sunday_epoch_days(int year, int month, int nth) {
  if (nth > 0) {
    int32_t d1 = days_from_civil(year, month, 1);
    int dow1 = day_of_week_from_days(d1);
    int first_sunday_day = (dow1 == 0) ? 1 : (8 - dow1);
    return days_from_civil(year, month, first_sunday_day + (nth - 1) * 7);
  }
  static const int DAYS_IN_MONTH[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int last_day = DAYS_IN_MONTH[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) last_day = 29;
  int32_t d_last = days_from_civil(year, month, last_day);
  return d_last - day_of_week_from_days(d_last);
}

// Whether US-rule DST is active at this exact UTC instant. Transition
// hours are approximated with a single fixed UTC hour common to
// continental US zones (2am local standard time is ~7am UTC for the
// March start, ~6am UTC for the November end) -- exact for the correct
// calendar day either way, could be off by up to a couple hours right
// at the transition instant itself for the westernmost zones.
static bool is_us_dst(int32_t epoch_days, int32_t secs_of_day, int year) {
  int32_t start = nth_sunday_epoch_days(year, 3, 2) * 86400 + 7 * 3600;
  int32_t end = nth_sunday_epoch_days(year, 11, 1) * 86400 + 6 * 3600;
  int32_t now = epoch_days * 86400 + secs_of_day;
  return now >= start && now < end;
}

// EU-rule DST -- exact, since the EU rule is itself defined in UTC
// terms (01:00 UTC on the last Sunday of March/October).
static bool is_eu_dst(int32_t epoch_days, int32_t secs_of_day, int year) {
  int32_t start = nth_sunday_epoch_days(year, 3, -1) * 86400 + 3600;
  int32_t end = nth_sunday_epoch_days(year, 10, -1) * 86400 + 3600;
  int32_t now = epoch_days * 86400 + secs_of_day;
  return now >= start && now < end;
}

// Resolves a TimezoneInfo's actual current UTC offset in minutes,
// including DST if applicable right now.
int16_t feature_timezone_current_offset_min(const TimezoneInfo *tz, time_t utc_now) {
  int32_t epoch_days = (int32_t)(utc_now / 86400);
  int32_t secs_of_day = (int32_t)(utc_now % 86400);
  int y, m, d;
  civil_from_days(epoch_days, &y, &m, &d);
  bool dst = false;
  if (tz->dst_rule == 1) dst = is_us_dst(epoch_days, secs_of_day, y);
  else if (tz->dst_rule == 2) dst = is_eu_dst(epoch_days, secs_of_day, y);
  return tz->base_offset_min + (dst ? 60 : 0);
}

// Discrete three-band read of a remote timezone's local hour: white
// through the day, black overnight, and a light-gray "twilight" band
// around sunrise/sunset -- deliberately a simple fixed-hour heuristic
// (06:00-08:00 sunrise, 18:00-20:00 sunset) rather than real sun-
// altitude astronomy, which isn't available for an arbitrary remote
// timezone the way it is for the user's own location via
// sky_layer_is_bright().
GColor feature_timezone_daylight_color(int local_hour24) {
  if (local_hour24 >= 8 && local_hour24 < 18) return GColorWhite;  // day
  if (local_hour24 < 6 || local_hour24 >= 20) return GColorBlack;  // night
  return GColorLightGray; // 06-08 sunrise, 18-20 sunset -- twilight
}


const TimezoneInfo *feature_timezone_get(uint8_t index) {
  if (index >= TIMEZONE_COUNT) return NULL;
  return &TIMEZONES[index];
}

