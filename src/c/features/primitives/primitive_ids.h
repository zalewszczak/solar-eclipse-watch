#pragma once

// Consecutive primitive-ID catalogue. See FEATURE_PRIMITIVE_REFACTOR_REPORT_REVISED.md,
// Section 3 ("Each primitive has a unique consecutive ID") and Section 15b
// (the addendum inventory this catalogue was built from).
//
// One ID per *distinct atomic meaning*, not one ID per possible value or per
// legacy "content" number -- a shared icon/value (e.g. the steps "foot" icon
// used by both content 2 and content 3) keeps a single ID, matching Section 4's
// "there should not be separate primitive IDs for every possible value."
// Where the same primitive appears at different parametrizations (timezone
// index, forecast-hour index, moon phase percent...) it stays one ID and the
// parametrization travels in the primitive instance's `aux`/`aux_flag` fields,
// the same pattern the existing RenderSegment.icon_extra/icon_flag already use.
//
// Grouped by logical function per Section 3's suggested file layout; kept in
// one header for now since the catalogue is still small enough to review as a
// whole, and split into the primitive_<group>.c/h files only where actual
// per-group *behavior* (not just an ID) is needed.

typedef enum {
  PRIM_ID_NONE = 0,

  // ---- Universal spacers (Section 4) ----
  PRIM_SPACE,
  PRIM_SLASH,
  PRIM_DASH,

  // ---- Date / time (watch-runtime; Section 5b) ----
  PRIM_GROUP_DATETIME_FIRST,
  PRIM_DATE_WEEKDAY_DAY = PRIM_GROUP_DATETIME_FIRST,     // superseded by atomic decomposition (Phase 8) --
                                 // content 12 is now WEEKDAY_SHORT_MIXED + SPACE + DAY_OF_MONTH
  PRIM_TIME_HM,                 // "13:45" / "1:45 PM"               (content 18)
  PRIM_WEEK_NUMBER,             // "34" (just the number -- see PRIM_LABEL_WK_PREFIX; atomic content 19)
  PRIM_DATE_MONTH_DAY,          // superseded by atomic decomposition (Phase 8) --
                                 // content 21 is now MONTH_SHORT_UPPER + SPACE + DAY_OF_MONTH
  PRIM_DAY_OF_MONTH,            // "12"                              (content 22, and atomic content 27/28/29/98/99/106)
  PRIM_MONTH_NUMBER,            // "9" (numeric, not the letter abbreviations below) (atomic content 27/28/29/30/98/99)
  PRIM_WEEKDAY_SHORT_UPPER,     // "TUE"                             (content 23, and atomic content 98/99)
  PRIM_WEEKDAY_LONG,            // "Tuesday"                         (content 24)
  PRIM_MONTH_SHORT_UPPER,       // "SEP"                             (content 25, and atomic content 21)
  PRIM_MONTH_LONG,              // "September"                      (content 26)
  PRIM_WEEKDAY_SHORT_MIXED,     // "Mon" (mixed case, distinct from the UPPER variant above) (atomic content 12/106)
  PRIM_MONTH_SHORT_MIXED,       // "Sep" (mixed case, distinct from MONTH_SHORT_UPPER)       (atomic content 106)
  PRIM_YEAR_FULL,                // "2026"                                                    (atomic content 29)
  PRIM_YEAR_SHORT,               // "26" (2-digit)                                             (atomic content 30)
  PRIM_LABEL_WK_PREFIX,          // "WK" -- generic short static label, not a value (atomic content 19/106)
  PRIM_DATE_SLASH_DM,           // superseded by atomic decomposition (Phase 8) --
                                 // content 27 is now DAY + SLASH + MONTH_NUMBER, not one primitive
  PRIM_DATE_SLASH_MD,           // superseded by atomic decomposition (Phase 8) --
                                 // content 28 is now MONTH_NUMBER + SLASH + DAY, not one primitive
  PRIM_DATE_SLASH_DMY,          // superseded by atomic decomposition (Phase 8) --
                                 // content 29 is now DAY + SLASH + MONTH_NUMBER + SLASH + YEAR_FULL
  PRIM_DATE_SLASH_MDY_SHORTYEAR,// superseded by atomic decomposition (Phase 8) --
                                 // content 30 is now MONTH_NUMBER + SLASH + DAY + SLASH + YEAR_SHORT
  PRIM_TIME_HMS,                // "13:45:07"                        (content 63)
  PRIM_HOUR_24_PAD,             // "07"                              (content 64)
  PRIM_HOUR_24,                 // "7"                               (content 65)
  PRIM_HOUR_12,                 // "7"                               (content 66)
  PRIM_MINUTE,                  // "5"                               (content 67)
  PRIM_MINUTE_PAD,              // "05"                              (content 68)
  PRIM_SECOND,                  // "9"                               (content 69)
  PRIM_SECOND_PAD,              // "09"                              (content 70)
  PRIM_SECOND_TENS,             // "0"                               (content 71)
  PRIM_SECOND_ONES,             // "9"                               (content 72)
  PRIM_AMPM,                    // "AM"/"PM"                         (content 86)
  PRIM_DATE_WEEKDAY_DM_SLASH,   // superseded by atomic decomposition (Phase 8) --
                                 // content 98 is now WEEKDAY_SHORT_UPPER + SPACE + DAY_OF_MONTH + SLASH + MONTH_NUMBER
  PRIM_DATE_WEEKDAY_MD_SLASH,   // superseded by atomic decomposition (Phase 8) --
                                 // content 99 is now WEEKDAY_SHORT_UPPER + SPACE + MONTH_NUMBER + SLASH + DAY_OF_MONTH
  PRIM_DATE_LONG_WITH_WEEK,     // superseded by atomic decomposition (Phase 8) -- content 106 is now
                                 // WEEKDAY_SHORT_MIXED + SPACE + DAY + SPACE + MONTH_SHORT_MIXED + SPACE + WK_PREFIX + WEEK_NUMBER
  PRIM_DATE_LONG,               // superseded by atomic decomposition (Phase 8) -- content 105's date
                                 // half is now WEEKDAY_SHORT_MIXED + SPACE + DAY_OF_MONTH + SPACE + MONTH_SHORT_MIXED
  PRIM_TIMEZONE_CLOCK,          // superseded by atomic decomposition (Phase 8) -- content 44-62 is
                                 // now TIMEZONE_ABBR + SPACE + TIMEZONE_TIME (+ AMPM in 12h style)
  PRIM_TIMEZONE_ABBR,           // "PST" (aux = zone index)          (atomic content 44-62)
  PRIM_TIMEZONE_TIME,           // "07:45"/"7:45" (aux = zone index) (atomic content 44-62)
  PRIM_TIMEZONE_AMPM,           // "AM"/"PM" for a zone's *local* hour, not the watch's own
                                 // (aux = zone index) -- distinct from PRIM_AMPM (content 86)
                                 // because the value itself, not just the color, depends on
                                 // which zone (Section 3's Phase-3 timezone update)
  PRIM_GROUP_DATETIME_LAST = PRIM_TIMEZONE_AMPM,

  // ---- Health / status (watch-runtime; Section 5b) ----
  PRIM_GROUP_HEALTH_FIRST,
  PRIM_HEART_RATE_ICON = PRIM_GROUP_HEALTH_FIRST,
  PRIM_HEART_RATE_VALUE,
  PRIM_STEPS_ICON,
  PRIM_STEPS_VALUE,
  PRIM_STEP_GOAL_PCT,
  PRIM_BATTERY_ICON,
  PRIM_BATTERY_PCT_TEXT,
  PRIM_BATTERY_LOGO_ICON,       // Pebble-logo battery glyph (content 17)
  PRIM_BLUETOOTH_ICON,
  PRIM_BLUETOOTH_STATUS_TEXT,
  PRIM_QUIET_TIME_ICON,
  PRIM_QUIET_TIME_STATUS_TEXT,
  PRIM_SLEEP_TOTAL_ICON,
  PRIM_SLEEP_TOTAL_TEXT,
  PRIM_SLEEP_RESTFUL_ICON,
  PRIM_SLEEP_RESTFUL_TEXT,
  PRIM_SLEEP_RESTFUL_PAREN_TEXT, // "(1h 20m)" -- composite 104's parenthesized form
  PRIM_SLEEP_QUALITY_ICON,
  PRIM_SLEEP_QUALITY_PCT,
  PRIM_BED_TIME_ICON,
  PRIM_BED_TIME_TEXT,
  PRIM_WAKE_TIME_ICON,
  PRIM_WAKE_TIME_TEXT,
  PRIM_WAKE_TIME_SLASH_TEXT,    // superseded by atomic decomposition (Phase 8) -- composite 101's
                                 // wake-time half is now SLASH + WAKE_TIME_TEXT, not one "/22:10" primitive
  PRIM_GROUP_HEALTH_LAST = PRIM_WAKE_TIME_SLASH_TEXT,

  // ---- Astronomy (PKJS-value once Phase 7 lands; Section 5b) ----
  PRIM_GROUP_ASTRO_FIRST,
  PRIM_MOON_PHASE_ICON = PRIM_GROUP_ASTRO_FIRST,
  PRIM_MOON_PHASE_NAME,
  PRIM_LOCATION_ICON,
  PRIM_LOCATION_NAME,
  PRIM_SUN_EVENT_ICON,
  PRIM_SUN_EVENT_TIME,
  PRIM_PLANETS_VISIBLE_ICON,
  PRIM_PLANETS_VISIBLE_COUNT,
  PRIM_METEOR_SHOWER_NAME,
  PRIM_SATURN_RINGS_ICON,
  PRIM_SATURN_RINGS_PCT,
  PRIM_NEXT_PLANET_RISE,
  PRIM_ISS_ICON,
  PRIM_ISS_NEXT_PASS,
  PRIM_AURORA_ICON,
  PRIM_AURORA_KP,
  PRIM_COMPASS_ICON,            // heading itself is watch-runtime (Section 2.1) even
  PRIM_COMPASS_DIR_TEXT,        // though most of this cluster is PKJS-value
  PRIM_GROUP_ASTRO_LAST = PRIM_COMPASS_DIR_TEXT,

  // ---- Weather (PKJS-value once Phase 7 lands; Section 5) ----
  PRIM_GROUP_WEATHER_FIRST,
  PRIM_TEMP_HIGH_TEXT = PRIM_GROUP_WEATHER_FIRST,
  PRIM_TEMP_LOW_TEXT,
  PRIM_TEMP_HIGH_LOW_TEXT,      // superseded for content 4's plain-text branch by atomic
                                 // decomposition (Phase 8) -- see PRIM_LABEL_HIGH_PREFIX below;
                                 // still used by content 4's own color_mode==3 2-segment branch
  PRIM_LABEL_HIGH_PREFIX,       // "H" -- generic short static label, not a value (atomic content 4)
  PRIM_LABEL_LOW_PREFIX,        // "L" -- ditto (atomic content 4)
  PRIM_CURRENT_CONDITIONS_TEXT,
  PRIM_UV_DAILY_TEXT,
  PRIM_UV_CURRENT_TEXT,
  PRIM_RAIN_CHANCE_ICON,
  PRIM_RAIN_CHANCE_TEXT,
  PRIM_HUMIDITY_ICON,
  PRIM_HUMIDITY_TEXT,
  PRIM_WIND_SPEED_ICON,
  PRIM_WIND_SPEED_TEXT,
  PRIM_VISIBILITY_ICON,
  PRIM_VISIBILITY_TEXT,
  PRIM_CLOUD_COVER_ICON,
  PRIM_CLOUD_COVER_TEXT,
  PRIM_WEATHER_CONDITION_ICON,
  PRIM_CURRENT_TEMP_TEXT,
  PRIM_PRESSURE_TREND_ICON,
  PRIM_PRESSURE_TEXT,
  PRIM_WIND_DIRECTION_ICON,
  PRIM_WIND_DIRECTION_TEXT,
  PRIM_AQI_TEXT,
  PRIM_DEW_POINT_TEXT,
  PRIM_ALTITUDE_ICON,
  PRIM_ALTITUDE_TEXT,
  PRIM_CURRENT_TEMP_ONLY_TEXT,
  PRIM_TEMP_HIGH_ONLY_TEXT,
  PRIM_TEMP_LOW_ONLY_TEXT,
  PRIM_FEELS_LIKE_TEXT,
  PRIM_TEMP_CUR_HI_LO_TEXT,     // superseded by atomic decomposition (Phase 8) -- content 76's text
                                 // half is now CURRENT_TEMP_TEXT + SPACE + LABEL_HIGH_PREFIX +
                                 // TEMP_HIGH_TEXT + SPACE + LABEL_LOW_PREFIX + TEMP_LOW_TEXT
  PRIM_FORECAST_OFFSET_ICON,    // "+Xh"/"+X day" half-icon (aux = offset index)
  PRIM_FORECAST_TEMP_TEXT,
  PRIM_WEATHER_REFRESH_ICON,
  PRIM_WEATHER_LAST_UPDATE_LONG_TEXT,
  PRIM_WEATHER_LAST_UPDATE_SHORT_TEXT,
  PRIM_GROUP_WEATHER_LAST = PRIM_WEATHER_LAST_UPDATE_SHORT_TEXT,

  PRIM_ID_COUNT,
} PrimitiveId;
