#include "./primitive_catalogue.h"

// --- primitive ID -> source / refresh class -----------------------------

PrimitiveSource primitive_catalogue_source_for(PrimitiveId id) {
  // Compass is watch-local even though it lives in the astronomy ID range
  // (Section 5b's astronomy addendum explicitly calls this exception out).
  if (id == PRIM_COMPASS_ICON || id == PRIM_COMPASS_DIR_TEXT) return PRIMITIVE_SOURCE_WATCH_RUNTIME;
  // "H"/"L" prefix labels (Phase 8's atomic decomposition of content 4) are
  // constant watch-side glyphs, not a PKJS-supplied value, even though they
  // live in the weather ID range next to the values they label.
  if (id == PRIM_LABEL_HIGH_PREFIX || id == PRIM_LABEL_LOW_PREFIX) return PRIMITIVE_SOURCE_WATCH_RUNTIME;
  // Builder additions (appended after the original catalogue): bare values
  // and unit symbols derived from PKJS-supplied data change only when PKJS
  // pushes; the planet-based ones depend on the watch's own clock.
  switch (id) {
    case PRIM_FEELS_LIKE_VALUE: case PRIM_UV_DAILY_VALUE: case PRIM_UV_CURRENT_VALUE:
    case PRIM_PRESSURE_VALUE: case PRIM_AQI_VALUE: case PRIM_ALTITUDE_VALUE:
    case PRIM_SATURN_RINGS_VALUE: case PRIM_AURORA_KP_VALUE:
    case PRIM_TEMP_UNIT_TEXT: case PRIM_WIND_UNIT_TEXT: case PRIM_PRESSURE_UNIT_TEXT:
    case PRIM_ALTITUDE_UNIT_TEXT: case PRIM_PLUGIN_TEXT:
      return PRIMITIVE_SOURCE_PKJS_VALUE;
    case PRIM_PLANETS_VISIBLE_VALUE: case PRIM_NEXT_PLANET_NAME: case PRIM_NEXT_PLANET_TIME:
      return PRIMITIVE_SOURCE_WATCH_RUNTIME;
    default: break;
  }
  // Time-dependent selections over PKJS data (Update 6): which sun event /
  // planet is "next" changes with the watch's own clock, so the watch owns
  // them even though they sit in the astronomy ID range.
  if (id == PRIM_SUN_EVENT_ICON || id == PRIM_SUN_EVENT_TIME ||
      id == PRIM_PLANETS_VISIBLE_ICON || id == PRIM_PLANETS_VISIBLE_COUNT ||
      id == PRIM_NEXT_PLANET_RISE) return PRIMITIVE_SOURCE_WATCH_RUNTIME;

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
    case PRIM_QUIET_TIME_ICON: case PRIM_QUIET_TIME_PLAIN_ICON: case PRIM_QUIET_TIME_STATUS_TEXT:
    case PRIM_BLUETOOTH_ONOFF_TEXT:
    case PRIM_BATTERY_ICON: case PRIM_BATTERY_PCT_TEXT: case PRIM_BATTERY_LOGO_ICON:
      return PRIMITIVE_REFRESH_EVENT;
    default:
      // Everything else in the watch-runtime date/time/health range ticks
      // on the shared minute refresh (Section 14: "a feature should only be
      return PRIMITIVE_REFRESH_MINUTE;
  }
}

