#include "./primitive_catalogue.h"
#include "../feature_slot.h"

// --- content -> primitive ID -------------------------------------------
//
// Mirrors the exact segment structure each feature_value_*_compute() case
// already builds (see FEATURE_PRIMITIVE_REFACTOR_REPORT_REVISED.md Section
// 15b for the source inventory this was built from). Where a case's segment
// *count* varies with runtime data (a "N/A" fallback, or color-mode
// splitting one text into two), `slot->segment_count` disambiguates, exactly
// the same information feature_render_draw_slot() already relies on.

static PrimitiveId date_time_primitive_for(uint8_t content, int seg_index, const FeatureSlot *slot) {
  // Atomic decomposition (Phase 8): several date contents are now several
  // primitives rather than one composed string -- see
  // feature_value_date_compute() (feature_value_time.c) for the exact
  // segment order each of these mirrors.
  if (content >= 44 && content <= 62) { // timezone cluster: abbr, space, time, (+ampm in 12h style)
    static const PrimitiveId ids24[3] = { PRIM_TIMEZONE_ABBR, PRIM_SPACE, PRIM_TIMEZONE_TIME };
    static const PrimitiveId ids12[4] = { PRIM_TIMEZONE_ABBR, PRIM_SPACE, PRIM_TIMEZONE_TIME, PRIM_AMPM };
    if (slot->segment_count == 4) return (seg_index >= 0 && seg_index < 4) ? ids12[seg_index] : PRIM_ID_NONE;
    return (seg_index >= 0 && seg_index < 3) ? ids24[seg_index] : PRIM_ID_NONE;
  }
  switch (content) {
    case 12: { // weekday(mixed), space, day
      static const PrimitiveId ids[3] = { PRIM_WEEKDAY_SHORT_MIXED, PRIM_SPACE, PRIM_DAY_OF_MONTH };
      return (seg_index >= 0 && seg_index < 3) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 19: { // "WK" label, space, number
      static const PrimitiveId ids[3] = { PRIM_LABEL_WK_PREFIX, PRIM_SPACE, PRIM_WEEK_NUMBER };
      return (seg_index >= 0 && seg_index < 3) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 21: { // month(upper), space, day
      static const PrimitiveId ids[3] = { PRIM_MONTH_SHORT_UPPER, PRIM_SPACE, PRIM_DAY_OF_MONTH };
      return (seg_index >= 0 && seg_index < 3) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 27: { // day, slash, month
      static const PrimitiveId ids[3] = { PRIM_DAY_OF_MONTH, PRIM_SLASH, PRIM_MONTH_NUMBER };
      return (seg_index >= 0 && seg_index < 3) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 28: { // month, slash, day
      static const PrimitiveId ids[3] = { PRIM_MONTH_NUMBER, PRIM_SLASH, PRIM_DAY_OF_MONTH };
      return (seg_index >= 0 && seg_index < 3) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 29: { // day, slash, month, slash, year(full)
      static const PrimitiveId ids[5] = { PRIM_DAY_OF_MONTH, PRIM_SLASH, PRIM_MONTH_NUMBER, PRIM_SLASH, PRIM_YEAR_FULL };
      return (seg_index >= 0 && seg_index < 5) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 30: { // month, slash, day, slash, year(short)
      static const PrimitiveId ids[5] = { PRIM_MONTH_NUMBER, PRIM_SLASH, PRIM_DAY_OF_MONTH, PRIM_SLASH, PRIM_YEAR_SHORT };
      return (seg_index >= 0 && seg_index < 5) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 98: { // weekday(upper), space, day, slash, month
      static const PrimitiveId ids[5] = { PRIM_WEEKDAY_SHORT_UPPER, PRIM_SPACE, PRIM_DAY_OF_MONTH, PRIM_SLASH, PRIM_MONTH_NUMBER };
      return (seg_index >= 0 && seg_index < 5) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 99: { // weekday(upper), space, month, slash, day
      static const PrimitiveId ids[5] = { PRIM_WEEKDAY_SHORT_UPPER, PRIM_SPACE, PRIM_MONTH_NUMBER, PRIM_SLASH, PRIM_DAY_OF_MONTH };
      return (seg_index >= 0 && seg_index < 5) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 106: { // weekday(mixed), space, day, space, month(mixed), space, "WK" label, week number
      static const PrimitiveId ids[8] = { PRIM_WEEKDAY_SHORT_MIXED, PRIM_SPACE, PRIM_DAY_OF_MONTH, PRIM_SPACE,
                                           PRIM_MONTH_SHORT_MIXED, PRIM_SPACE, PRIM_LABEL_WK_PREFIX, PRIM_WEEK_NUMBER };
      return (seg_index >= 0 && seg_index < 8) ? ids[seg_index] : PRIM_ID_NONE;
    }
    default: break;
  }
  if (seg_index != 0) return PRIM_ID_NONE;
  switch (content) {
    case 18: return PRIM_TIME_HM;
    case 22: return PRIM_DAY_OF_MONTH;
    case 23: return PRIM_WEEKDAY_SHORT_UPPER;
    case 24: return PRIM_WEEKDAY_LONG;
    case 25: return PRIM_MONTH_SHORT_UPPER;
    case 26: return PRIM_MONTH_LONG;
    case 63: return PRIM_TIME_HMS;
    case 64: return PRIM_HOUR_24_PAD;
    case 65: return PRIM_HOUR_24;
    case 66: return PRIM_HOUR_12;
    case 67: return PRIM_MINUTE;
    case 68: return PRIM_MINUTE_PAD;
    case 69: return PRIM_SECOND;
    case 70: return PRIM_SECOND_PAD;
    case 71: return PRIM_SECOND_TENS;
    case 72: return PRIM_SECOND_ONES;
    case 86: return PRIM_AMPM;
    default:
      return PRIM_ID_NONE;
  }
}

static PrimitiveId health_primitive_for(uint8_t content, int seg_index) {
  switch (content) {
    case 1: return seg_index == 0 ? PRIM_HEART_RATE_ICON : seg_index == 1 ? PRIM_HEART_RATE_VALUE : PRIM_ID_NONE;
    case 2: case 3:
      return seg_index == 0 ? PRIM_STEPS_ICON
           : seg_index == 1 ? (content == 2 ? PRIM_STEPS_VALUE : PRIM_STEP_GOAL_PCT)
           : PRIM_ID_NONE;
    case 10: return seg_index == 0 ? PRIM_BATTERY_ICON : seg_index == 1 ? PRIM_BATTERY_PCT_TEXT : PRIM_ID_NONE;
    case 17: return seg_index == 0 ? PRIM_BATTERY_LOGO_ICON : PRIM_ID_NONE;
    case 20: return seg_index == 0 ? PRIM_BLUETOOTH_ICON : seg_index == 1 ? PRIM_BLUETOOTH_STATUS_TEXT : PRIM_ID_NONE;
    case 78: return seg_index == 0 ? PRIM_BLUETOOTH_ICON : PRIM_ID_NONE;
    case 108: return seg_index == 0 ? PRIM_QUIET_TIME_ICON : PRIM_ID_NONE;
    case 109: return seg_index == 0 ? PRIM_QUIET_TIME_ICON : seg_index == 1 ? PRIM_QUIET_TIME_STATUS_TEXT : PRIM_ID_NONE;
    case 39: return seg_index == 0 ? PRIM_SLEEP_TOTAL_ICON : seg_index == 1 ? PRIM_SLEEP_TOTAL_TEXT : PRIM_ID_NONE;
    case 40: return seg_index == 0 ? PRIM_SLEEP_RESTFUL_ICON : seg_index == 1 ? PRIM_SLEEP_RESTFUL_TEXT : PRIM_ID_NONE;
    case 41: return seg_index == 0 ? PRIM_SLEEP_QUALITY_ICON : seg_index == 1 ? PRIM_SLEEP_QUALITY_PCT : PRIM_ID_NONE;
    case 42: return seg_index == 0 ? PRIM_BED_TIME_ICON : seg_index == 1 ? PRIM_BED_TIME_TEXT : PRIM_ID_NONE;
    case 43: return seg_index == 0 ? PRIM_WAKE_TIME_ICON : seg_index == 1 ? PRIM_WAKE_TIME_TEXT : PRIM_ID_NONE;
    default: return PRIM_ID_NONE;
  }
}

static PrimitiveId sky_primitive_for(uint8_t content, int seg_index) {
  switch (content) {
    case 11: return seg_index == 0 ? PRIM_MOON_PHASE_ICON : seg_index == 1 ? PRIM_MOON_PHASE_NAME : PRIM_ID_NONE;
    case 13: return seg_index == 0 ? PRIM_LOCATION_ICON : seg_index == 1 ? PRIM_LOCATION_NAME : PRIM_ID_NONE;
    case 16: return seg_index == 0 ? PRIM_SUN_EVENT_ICON : seg_index == 1 ? PRIM_SUN_EVENT_TIME : PRIM_ID_NONE;
    case 79: return seg_index == 0 ? PRIM_PLANETS_VISIBLE_ICON : seg_index == 1 ? PRIM_PLANETS_VISIBLE_COUNT : PRIM_ID_NONE;
    case 80: return seg_index == 0 ? PRIM_METEOR_SHOWER_NAME : PRIM_ID_NONE;
    case 81: return seg_index == 0 ? PRIM_SATURN_RINGS_ICON : seg_index == 1 ? PRIM_SATURN_RINGS_PCT : PRIM_ID_NONE;
    case 82: return seg_index == 0 ? PRIM_NEXT_PLANET_RISE : PRIM_ID_NONE;
    case 83: return seg_index == 0 ? PRIM_ISS_ICON : seg_index == 1 ? PRIM_ISS_NEXT_PASS : PRIM_ID_NONE;
    case 84: return seg_index == 0 ? PRIM_AURORA_ICON : seg_index == 1 ? PRIM_AURORA_KP : PRIM_ID_NONE;
    case 85: return seg_index == 0 ? PRIM_COMPASS_ICON : seg_index == 1 ? PRIM_COMPASS_DIR_TEXT : PRIM_ID_NONE;
    default: return PRIM_ID_NONE;
  }
}

static PrimitiveId weather_primitive_for(uint8_t content, int seg_index, const FeatureSlot *slot) {
  switch (content) {
    case 4:
      // Atomic decomposition (Phase 8): the plain (non-color_mode-3) branch
      // is now 5 primitives (H, value, space, L, value); the color_mode==3
      // branch is now 4 (H, value, L, value) -- see feature_value_weather.c.
      if (slot->segment_count == 5) {
        static const PrimitiveId ids[5] = { PRIM_LABEL_HIGH_PREFIX, PRIM_TEMP_HIGH_TEXT, PRIM_SPACE, PRIM_LABEL_LOW_PREFIX, PRIM_TEMP_LOW_TEXT };
        return (seg_index >= 0 && seg_index < 5) ? ids[seg_index] : PRIM_ID_NONE;
      }
      if (slot->segment_count == 4) {
        static const PrimitiveId ids[4] = { PRIM_LABEL_HIGH_PREFIX, PRIM_TEMP_HIGH_TEXT, PRIM_LABEL_LOW_PREFIX, PRIM_TEMP_LOW_TEXT };
        return (seg_index >= 0 && seg_index < 4) ? ids[seg_index] : PRIM_ID_NONE;
      }
      return seg_index == 0 ? PRIM_TEMP_HIGH_LOW_TEXT : PRIM_ID_NONE; // dead/defensive fallback
    case 5: return seg_index == 0 ? PRIM_CURRENT_CONDITIONS_TEXT : PRIM_ID_NONE;
    case 6: return seg_index == 0 ? PRIM_UV_DAILY_TEXT : PRIM_ID_NONE;
    case 107: return seg_index == 0 ? PRIM_UV_CURRENT_TEXT : PRIM_ID_NONE;
    case 7: return seg_index == 0 ? PRIM_RAIN_CHANCE_ICON : seg_index == 1 ? PRIM_RAIN_CHANCE_TEXT : PRIM_ID_NONE;
    case 8: return seg_index == 0 ? PRIM_HUMIDITY_ICON : seg_index == 1 ? PRIM_HUMIDITY_TEXT : PRIM_ID_NONE;
    case 9: return seg_index == 0 ? PRIM_WIND_SPEED_ICON : seg_index == 1 ? PRIM_WIND_SPEED_TEXT : PRIM_ID_NONE;
    case 14: return seg_index == 0 ? PRIM_VISIBILITY_ICON : seg_index == 1 ? PRIM_VISIBILITY_TEXT : PRIM_ID_NONE;
    case 15: return seg_index == 0 ? PRIM_CLOUD_COVER_ICON : seg_index == 1 ? PRIM_CLOUD_COVER_TEXT : PRIM_ID_NONE;
    case 31: return seg_index == 0 ? PRIM_WEATHER_CONDITION_ICON : PRIM_ID_NONE;
    case 32: return seg_index == 0 ? PRIM_WEATHER_CONDITION_ICON : seg_index == 1 ? PRIM_CURRENT_TEMP_TEXT : PRIM_ID_NONE;
    case 34: return seg_index == 0 ? PRIM_PRESSURE_TREND_ICON : seg_index == 1 ? PRIM_PRESSURE_TEXT : PRIM_ID_NONE;
    case 35: return seg_index == 0 ? PRIM_WIND_DIRECTION_ICON : seg_index == 1 ? PRIM_WIND_DIRECTION_TEXT : PRIM_ID_NONE;
    case 36: return seg_index == 0 ? PRIM_AQI_TEXT : PRIM_ID_NONE;
    case 37: return seg_index == 0 ? PRIM_HUMIDITY_ICON : seg_index == 1 ? PRIM_DEW_POINT_TEXT : PRIM_ID_NONE;
    case 38: return seg_index == 0 ? PRIM_ALTITUDE_ICON : seg_index == 1 ? PRIM_ALTITUDE_TEXT : PRIM_ID_NONE;
    case 73: return seg_index == 0 ? PRIM_CURRENT_TEMP_ONLY_TEXT : PRIM_ID_NONE;
    case 74: return seg_index == 0 ? PRIM_TEMP_HIGH_ONLY_TEXT : PRIM_ID_NONE;
    case 75: return seg_index == 0 ? PRIM_TEMP_LOW_ONLY_TEXT : PRIM_ID_NONE;
    case 77: return seg_index == 0 ? PRIM_FEELS_LIKE_TEXT : PRIM_ID_NONE;
    case 76: { // Atomic decomposition (Phase 8): icon + 7 text primitives instead of icon + 1 composed string
      static const PrimitiveId ids[8] = { PRIM_WEATHER_CONDITION_ICON, PRIM_CURRENT_TEMP_TEXT, PRIM_SPACE,
                                           PRIM_LABEL_HIGH_PREFIX, PRIM_TEMP_HIGH_TEXT, PRIM_SPACE,
                                           PRIM_LABEL_LOW_PREFIX, PRIM_TEMP_LOW_TEXT };
      return (seg_index >= 0 && seg_index < 8) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 87: case 88: case 89: case 90: case 91: case 92:
    case 93: case 94: case 95:
      if (slot->segment_count == 2) return seg_index == 0 ? PRIM_FORECAST_OFFSET_ICON : seg_index == 1 ? PRIM_FORECAST_TEMP_TEXT : PRIM_ID_NONE;
      return seg_index == 0 ? PRIM_FORECAST_OFFSET_ICON : seg_index == 1 ? PRIM_WEATHER_CONDITION_ICON : seg_index == 2 ? PRIM_FORECAST_TEMP_TEXT : PRIM_ID_NONE;
    case 96: return seg_index == 0 ? PRIM_WEATHER_LAST_UPDATE_LONG_TEXT : PRIM_ID_NONE;
    case 97: return seg_index == 0 ? PRIM_WEATHER_REFRESH_ICON : seg_index == 1 ? PRIM_WEATHER_LAST_UPDATE_SHORT_TEXT : PRIM_ID_NONE;
    default: return PRIM_ID_NONE;
  }
}

static PrimitiveId composite_primitive_for(uint8_t content, int seg_index, const FeatureSlot *slot) {
  switch (content) {
    case 100: {
      static const PrimitiveId ids[4] = { PRIM_HEART_RATE_ICON, PRIM_HEART_RATE_VALUE, PRIM_STEPS_ICON, PRIM_STEPS_VALUE };
      return (seg_index >= 0 && seg_index < 4) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 101: { // Atomic decomposition (Phase 8): bed time, then (if sleep data exists) slash + wake time
      if (slot->segment_count == 4) {
        static const PrimitiveId ids[4] = { PRIM_SLEEP_TOTAL_ICON, PRIM_BED_TIME_TEXT, PRIM_SLASH, PRIM_WAKE_TIME_TEXT };
        return (seg_index >= 0 && seg_index < 4) ? ids[seg_index] : PRIM_ID_NONE;
      }
      static const PrimitiveId ids[2] = { PRIM_SLEEP_TOTAL_ICON, PRIM_BED_TIME_TEXT }; // N/A case, no wake time
      return (seg_index >= 0 && seg_index < 2) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 102: {
      static const PrimitiveId ids[2] = { PRIM_BATTERY_ICON, PRIM_BLUETOOTH_ICON };
      return (seg_index >= 0 && seg_index < 2) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 103: {
      static const PrimitiveId ids[3] = { PRIM_BATTERY_ICON, PRIM_BATTERY_PCT_TEXT, PRIM_BLUETOOTH_ICON };
      return (seg_index >= 0 && seg_index < 3) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 112: {
      static const PrimitiveId ids[3] = { PRIM_BATTERY_ICON, PRIM_BLUETOOTH_ICON, PRIM_QUIET_TIME_ICON };
      return (seg_index >= 0 && seg_index < 3) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 113: {
      static const PrimitiveId ids[6] = { PRIM_BATTERY_ICON, PRIM_BATTERY_PCT_TEXT, PRIM_QUIET_TIME_ICON,
                                           PRIM_QUIET_TIME_STATUS_TEXT, PRIM_BLUETOOTH_ICON, PRIM_BLUETOOTH_STATUS_TEXT };
      return (seg_index >= 0 && seg_index < 6) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 118: {
      static const PrimitiveId ids[4] = { PRIM_BATTERY_ICON, PRIM_BATTERY_PCT_TEXT, PRIM_BLUETOOTH_ICON, PRIM_QUIET_TIME_ICON };
      return (seg_index >= 0 && seg_index < 4) ? ids[seg_index] : PRIM_ID_NONE;
    }
    case 104:
      if (slot->segment_count == 2) return seg_index == 0 ? PRIM_SLEEP_TOTAL_ICON : seg_index == 1 ? PRIM_SLEEP_TOTAL_TEXT : PRIM_ID_NONE;
      { // available: icon, total, "(restful)", quality%
        static const PrimitiveId ids[4] = { PRIM_SLEEP_TOTAL_ICON, PRIM_SLEEP_TOTAL_TEXT, PRIM_SLEEP_RESTFUL_PAREN_TEXT, PRIM_SLEEP_QUALITY_PCT };
        return (seg_index >= 0 && seg_index < 4) ? ids[seg_index] : PRIM_ID_NONE;
      }
    case 105: { // Atomic decomposition (Phase 8): date half is now 5 primitives, then icon + time = 7 total
      static const PrimitiveId ids[7] = { PRIM_WEEKDAY_SHORT_MIXED, PRIM_SPACE, PRIM_DAY_OF_MONTH, PRIM_SPACE,
                                           PRIM_MONTH_SHORT_MIXED, PRIM_SUN_EVENT_ICON, PRIM_SUN_EVENT_TIME };
      return (seg_index >= 0 && seg_index < 7) ? ids[seg_index] : PRIM_ID_NONE;
    }
    default: return PRIM_ID_NONE;
  }
}

PrimitiveId primitive_catalogue_id_for(uint8_t content, int seg_index, const FeatureSlot *slot) {
  switch (content) {
    case 100: case 101: case 102: case 103: case 104: case 105:
    case 112: case 113: case 118:
      return composite_primitive_for(content, seg_index, slot);
    case 11: case 13: case 16: case 79: case 80: case 81: case 82: case 83: case 84: case 85:
      return sky_primitive_for(content, seg_index);
    case 1: case 2: case 3: case 10: case 17: case 20: case 39: case 40: case 41: case 42: case 43: case 78:
    case 108: case 109:
      return health_primitive_for(content, seg_index);
    case 4: case 5: case 6: case 7: case 8: case 9: case 14: case 15: case 31: case 32: case 34:
    case 35: case 36: case 37: case 38: case 73: case 74: case 75: case 76: case 77: case 87: case 88:
    case 89: case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97: case 107:
      return weather_primitive_for(content, seg_index, slot);
    default:
      // Date/time content (12,18,19,21-30,63-72,86,98,99,106) and timezone
      // content (44-62) both fall through to here and are handled by the
      // same helper (date_time_primitive_for checks the 44-62 range itself).
      return date_time_primitive_for(content, seg_index, slot);
  }
}

// --- primitive ID -> source / refresh class -----------------------------

PrimitiveSource primitive_catalogue_source_for(PrimitiveId id) {
  // Compass is watch-local even though it lives in the astronomy ID range
  // (Section 5b's astronomy addendum explicitly calls this exception out).
  if (id == PRIM_COMPASS_ICON || id == PRIM_COMPASS_DIR_TEXT) return PRIMITIVE_SOURCE_WATCH_RUNTIME;
  // "H"/"L" prefix labels (Phase 8's atomic decomposition of content 4) are
  // constant watch-side glyphs, not a PKJS-supplied value, even though they
  // live in the weather ID range next to the values they label.
  if (id == PRIM_LABEL_HIGH_PREFIX || id == PRIM_LABEL_LOW_PREFIX) return PRIMITIVE_SOURCE_WATCH_RUNTIME;

  if (id >= PRIM_GROUP_ASTRO_FIRST && id <= PRIM_GROUP_ASTRO_LAST) return PRIMITIVE_SOURCE_PKJS_VALUE;
  if (id >= PRIM_GROUP_WEATHER_FIRST && id <= PRIM_GROUP_WEATHER_LAST) return PRIMITIVE_SOURCE_PKJS_VALUE;

  // Universal spacers, date/time, health/status: watch-runtime (Section 5b).
  return PRIMITIVE_SOURCE_WATCH_RUNTIME;
}

PrimitiveRefreshClass primitive_catalogue_refresh_class_for(PrimitiveId id) {
  if (primitive_catalogue_source_for(id) == PRIMITIVE_SOURCE_PKJS_VALUE) return PRIMITIVE_REFRESH_PKJS_VALUE;

  switch (id) {
    case PRIM_LABEL_HIGH_PREFIX: case PRIM_LABEL_LOW_PREFIX: case PRIM_LABEL_WK_PREFIX:
      return PRIMITIVE_REFRESH_STATIC; // never changes, no refresh needed at all
    case PRIM_SECOND: case PRIM_SECOND_PAD: case PRIM_SECOND_TENS: case PRIM_SECOND_ONES:
    case PRIM_TIME_HMS:
      return PRIMITIVE_REFRESH_SECOND;
    case PRIM_COMPASS_ICON: case PRIM_COMPASS_DIR_TEXT:
      return PRIMITIVE_REFRESH_FAST_DYNAMIC;
    case PRIM_BLUETOOTH_ICON: case PRIM_BLUETOOTH_STATUS_TEXT:
    case PRIM_QUIET_TIME_ICON: case PRIM_QUIET_TIME_STATUS_TEXT:
    case PRIM_BATTERY_ICON: case PRIM_BATTERY_PCT_TEXT: case PRIM_BATTERY_LOGO_ICON:
      return PRIMITIVE_REFRESH_EVENT;
    default:
      // Everything else in the watch-runtime date/time/health range ticks
      // on the shared minute refresh (Section 14: "a feature should only be
      return PRIMITIVE_REFRESH_MINUTE;
  }
}
