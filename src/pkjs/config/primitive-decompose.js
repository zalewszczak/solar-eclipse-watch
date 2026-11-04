// Primitive-strip preview and phone-side feature compiler.
//
// Feature IDs exist only in settings and this module. Before communication,
// every selectable feature is compiled into a bounded primitive-ID sequence
// plus auxiliary parameters. The watch receives no feature/content IDs and
// has no legacy content resolver or rendering fallback.
//
// PRIMITIVE_PALETTE below exposes only IDs supported by primitive_resolver.c.
// The preview uses the same sequence tables that the message encoder sends.
// Dynamic values (time, battery, health, weather and astronomy) are resolved
// by generic primitive handlers on the watch or supplied by PKJS as data.

var presetsLookups = require('../presets-lookups');
var CORNER_CATEGORIES = presetsLookups.CORNER_CATEGORIES;
var PRIM = require('./primitive-ids-generated');

var CONTENT_ITEM_BY_ID = {};
CORNER_CATEGORIES.forEach(function (cat) {
  cat.items.forEach(function (item) { CONTENT_ITEM_BY_ID[item.id] = item; });
});

function label(text) { return { kind: 'label', text: text }; }
function value(text) { return { kind: 'value', text: text }; }
function space() { return { kind: 'space', text: ' ' }; }
function slash() { return { kind: 'slash', text: '/' }; }
function icon(text) { return { kind: 'icon', text: text }; }

// The feature-to-sequence table below is the phone-side compiler source of
// truth. Preview chips are derived from these same sequences later in this
// module, so the UI and wire payload cannot drift independently.
//
// Numeric primitive IDs are generated from the C primitive enum. Every
// selectable feature is represented here; zero is reserved for an empty slot.
var PRIMITIVE_TRANSPORT_SEQUENCES = {
  4: [PRIM.PRIM_LABEL_HIGH_PREFIX, PRIM.PRIM_TEMP_HIGH_TEXT, PRIM.PRIM_SPACE, PRIM.PRIM_LABEL_LOW_PREFIX, PRIM.PRIM_TEMP_LOW_TEXT],
  12: [PRIM.PRIM_WEEKDAY_SHORT_MIXED, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH],
  21: [PRIM.PRIM_MONTH_SHORT_UPPER, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH],
  27: [PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_MONTH_NUMBER],
  28: [PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_DAY_OF_MONTH],
  29: [PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_YEAR_FULL],
  30: [PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_YEAR_SHORT],
  98: [PRIM.PRIM_WEEKDAY_SHORT_UPPER, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_MONTH_NUMBER],
  99: [PRIM.PRIM_WEEKDAY_SHORT_UPPER, PRIM.PRIM_SPACE, PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_DAY_OF_MONTH],

  // Unblocked by the color-ownership principle (see this file's header and
  // IMPLEMENTATION_NOTES.md's Phase 3 update): these all used to need a
  // *different* canonical color for the same primitive ID depending on
  // which content composed it. Now every date-component primitive shares
  // one watch-decided gradient regardless of context, and every weather
  // primitive resolves its own PKJS-decided color regardless of context --
  // so there's no more ambiguity to avoid.
  19: [PRIM.PRIM_LABEL_WK_PREFIX, PRIM.PRIM_SPACE, PRIM.PRIM_WEEK_NUMBER],
  22: [PRIM.PRIM_DAY_OF_MONTH],
  23: [PRIM.PRIM_WEEKDAY_SHORT_UPPER],
  24: [PRIM.PRIM_WEEKDAY_LONG],
  25: [PRIM.PRIM_MONTH_SHORT_UPPER],
  26: [PRIM.PRIM_MONTH_LONG],
  76: [PRIM.PRIM_WEATHER_CONDITION_ICON, PRIM.PRIM_CURRENT_TEMP_TEXT, PRIM.PRIM_SPACE,
       PRIM.PRIM_LABEL_HIGH_PREFIX, PRIM.PRIM_TEMP_HIGH_TEXT, PRIM.PRIM_SPACE,
       PRIM.PRIM_LABEL_LOW_PREFIX, PRIM.PRIM_TEMP_LOW_TEXT],
  106: [PRIM.PRIM_WEEKDAY_SHORT_MIXED, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SPACE,
        PRIM.PRIM_MONTH_SHORT_MIXED, PRIM.PRIM_SPACE, PRIM.PRIM_LABEL_WK_PREFIX, PRIM.PRIM_WEEK_NUMBER],

  // Health/status cluster (all watch-runtime, no color-ownership conflicts
  // -- see the former health feature-value path and primitive_resolver.c's own
  // per-case comments). Each is icon+text (or icon-only/text-only) exactly
  // as the former segment helper's legacy slot setter/
  // legacy segment setters
  // pairs already produce, so no further decomposition was needed --
  // these were already atomic in Phase 8's sense.
  1: [PRIM.PRIM_HEART_RATE_ICON, PRIM.PRIM_HEART_RATE_VALUE],
  2: [PRIM.PRIM_STEPS_ICON, PRIM.PRIM_STEPS_VALUE],
  3: [PRIM.PRIM_STEPS_ICON, PRIM.PRIM_STEP_GOAL_PCT],
  10: [PRIM.PRIM_BATTERY_ICON, PRIM.PRIM_BATTERY_PCT_TEXT],
  17: [PRIM.PRIM_BATTERY_LOGO_ICON],
  20: [PRIM.PRIM_BLUETOOTH_ICON, PRIM.PRIM_BLUETOOTH_STATUS_TEXT],
  78: [PRIM.PRIM_BLUETOOTH_ICON],
  108: [PRIM.PRIM_QUIET_TIME_ICON],
  109: [PRIM.PRIM_QUIET_TIME_PLAIN_ICON, PRIM.PRIM_QUIET_TIME_STATUS_TEXT],
  39: [PRIM.PRIM_SLEEP_TOTAL_ICON, PRIM.PRIM_SLEEP_TOTAL_TEXT],
  40: [PRIM.PRIM_SLEEP_RESTFUL_ICON, PRIM.PRIM_SLEEP_RESTFUL_TEXT],
  41: [PRIM.PRIM_SLEEP_QUALITY_ICON, PRIM.PRIM_SLEEP_QUALITY_PCT],
  42: [PRIM.PRIM_BED_TIME_ICON, PRIM.PRIM_BED_TIME_TEXT],
  43: [PRIM.PRIM_WAKE_TIME_ICON, PRIM.PRIM_WAKE_TIME_TEXT],
  // Fixed primitive-only equivalents for legacy composites 101 and 104.
  // When sleep data is unavailable, individual resolvers render their N/A state.
  101: [PRIM.PRIM_BED_TIME_ICON, PRIM.PRIM_BED_TIME_TEXT, PRIM.PRIM_SLASH, PRIM.PRIM_WAKE_TIME_TEXT],
  // 104 keeps Eclipz's look "icon 7h 30m (2h 10m) 28%": text runs sit directly against
  // each other on the watch, so the gaps and the parentheses are explicit elements.
  104: [PRIM.PRIM_SLEEP_TOTAL_ICON, PRIM.PRIM_SLEEP_TOTAL_TEXT, PRIM.PRIM_SPACE, PRIM.PRIM_PAREN_OPEN,
        PRIM.PRIM_SLEEP_RESTFUL_TEXT, PRIM.PRIM_PAREN_CLOSE, PRIM.PRIM_SPACE, PRIM.PRIM_SLEEP_QUALITY_PCT],

  // Weather singles (legacy weather feature code cases 73/74/75/77) -- each a
  // distinct primitive ID from content 4/32/76's, so no conflict with the
  // temperature cluster above.
  73: [PRIM.PRIM_CURRENT_TEMP_ONLY_TEXT],
  74: [PRIM.PRIM_TEMP_HIGH_ONLY_TEXT],
  75: [PRIM.PRIM_TEMP_LOW_ONLY_TEXT],
  77: [PRIM.PRIM_FEELS_LIKE_TEXT],

  // Astronomy cluster (Update 24). Every one resolves on the watch
  // (primitive_resolver.c): PKJS-supplied display strings (11/80/81/84) or
  // watch-runtime selections over PKJS data (16/79/82/83), plus the live
  // compass sensor (85) and a passthrough name (13). Color rules copied
  // 1:1 from feature_value_sky_compute().
  11: [PRIM.PRIM_MOON_PHASE_ICON, PRIM.PRIM_MOON_PHASE_NAME],
  13: [PRIM.PRIM_LOCATION_ICON, PRIM.PRIM_LOCATION_NAME],
  16: [PRIM.PRIM_SUN_EVENT_ICON, PRIM.PRIM_SUN_EVENT_TIME],
  79: [PRIM.PRIM_PLANETS_VISIBLE_ICON, PRIM.PRIM_PLANETS_VISIBLE_COUNT],
  80: [PRIM.PRIM_METEOR_SHOWER_NAME],
  81: [PRIM.PRIM_SATURN_RINGS_ICON, PRIM.PRIM_SATURN_RINGS_PCT],
  82: [PRIM.PRIM_NEXT_PLANET_RISE],
  83: [PRIM.PRIM_ISS_ICON, PRIM.PRIM_ISS_NEXT_PASS],
  84: [PRIM.PRIM_AURORA_ICON, PRIM.PRIM_AURORA_KP],
  85: [PRIM.PRIM_COMPASS_ICON, PRIM.PRIM_COMPASS_DIR_TEXT],

  // Composites (Update 24): fixed sequences over primitives that already
  // resolve on their own. Not covered: 101 (bed/wake) and 104 (sleep
  // times) -- their sequence LENGTH depends on whether sleep data exists
  // (legacy drops the wake time / restful / quality parts when there is
  // none), which a fixed transported sequence cannot express. See
  // IMPLEMENTATION_NOTES.md Update 24.
  // Time cluster (Update 25): plain clock/second/date-field contents. All
  // watch-runtime.
  18: [PRIM.PRIM_TIME_HM],
  63: [PRIM.PRIM_TIME_HMS],
  64: [PRIM.PRIM_HOUR_24_PAD],
  65: [PRIM.PRIM_HOUR_24],
  66: [PRIM.PRIM_HOUR_12],
  67: [PRIM.PRIM_MINUTE],
  68: [PRIM.PRIM_MINUTE_PAD],
  69: [PRIM.PRIM_SECOND],
  70: [PRIM.PRIM_SECOND_PAD],
  71: [PRIM.PRIM_SECOND_TENS],
  72: [PRIM.PRIM_SECOND_ONES],
  86: [PRIM.PRIM_AMPM],

  // Weather cluster (Update 25). Every one PKJS-value-sourced with a
  // watch-side fallback, same as the temperature/UV contents already
  // transported.
  5: [PRIM.PRIM_CURRENT_CONDITIONS_TEXT],
  6: [PRIM.PRIM_UV_DAILY_TEXT],
  107: [PRIM.PRIM_UV_CURRENT_TEXT],
  7: [PRIM.PRIM_RAIN_CHANCE_ICON, PRIM.PRIM_RAIN_CHANCE_TEXT],
  8: [PRIM.PRIM_HUMIDITY_ICON, PRIM.PRIM_HUMIDITY_TEXT],
  9: [PRIM.PRIM_WIND_SPEED_ICON, PRIM.PRIM_WIND_SPEED_TEXT],
  14: [PRIM.PRIM_VISIBILITY_ICON, PRIM.PRIM_VISIBILITY_TEXT],
  15: [PRIM.PRIM_CLOUD_COVER_ICON, PRIM.PRIM_CLOUD_COVER_TEXT],
  31: [PRIM.PRIM_WEATHER_CONDITION_ICON],
  32: [PRIM.PRIM_WEATHER_CONDITION_ICON, PRIM.PRIM_CURRENT_TEMP_TEXT],
  34: [PRIM.PRIM_PRESSURE_TEXT],
  35: [PRIM.PRIM_WIND_DIRECTION_TEXT],
  36: [PRIM.PRIM_AQI_TEXT],
  37: [PRIM.PRIM_HUMIDITY_ICON, PRIM.PRIM_DEW_POINT_TEXT],
  38: [PRIM.PRIM_ALTITUDE_ICON, PRIM.PRIM_ALTITUDE_TEXT],
  76: [PRIM.PRIM_WEATHER_CONDITION_ICON, PRIM.PRIM_CURRENT_TEMP_TEXT, PRIM.PRIM_SPACE,
       PRIM.PRIM_LABEL_HIGH_PREFIX, PRIM.PRIM_TEMP_HIGH_TEXT, PRIM.PRIM_SPACE,
       PRIM.PRIM_LABEL_LOW_PREFIX, PRIM.PRIM_TEMP_LOW_TEXT],
  96: [PRIM.PRIM_WEATHER_LAST_UPDATE_LONG_TEXT],
  97: [PRIM.PRIM_WEATHER_REFRESH_ICON, PRIM.PRIM_WEATHER_LAST_UPDATE_SHORT_TEXT],
  // 87-95 (forecast 1-6h / 1-3 days) all use the same two-primitive
  // sequence; only the aux (forecast slot index, 0-8) differs. Composer
  // items for these therefore need aux support the same way timezone
  // primitives do -- see PRIMITIVE_PALETTE's 'Forecast' group below.
  87: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  88: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  89: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  90: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  91: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  92: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  93: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  94: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],
  95: [PRIM.PRIM_FORECAST_OFFSET_ICON, PRIM.PRIM_FORECAST_TEMP_TEXT],

  100: [PRIM.PRIM_HEART_RATE_ICON, PRIM.PRIM_HEART_RATE_VALUE, PRIM.PRIM_STEPS_ICON, PRIM.PRIM_STEPS_VALUE],
  102: [PRIM.PRIM_BATTERY_ICON, PRIM.PRIM_BLUETOOTH_ICON],
  103: [PRIM.PRIM_BATTERY_ICON, PRIM.PRIM_BATTERY_PCT_TEXT, PRIM.PRIM_BLUETOOTH_ICON],
  105: [PRIM.PRIM_WEEKDAY_SHORT_MIXED, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SPACE,
        PRIM.PRIM_MONTH_SHORT_MIXED, PRIM.PRIM_SUN_EVENT_ICON, PRIM.PRIM_SUN_EVENT_TIME],
  112: [PRIM.PRIM_BATTERY_ICON, PRIM.PRIM_BLUETOOTH_ICON, PRIM.PRIM_QUIET_TIME_ICON],
  113: [PRIM.PRIM_BATTERY_ICON, PRIM.PRIM_BATTERY_PCT_TEXT, PRIM.PRIM_QUIET_TIME_PLAIN_ICON,
        PRIM.PRIM_QUIET_TIME_STATUS_TEXT, PRIM.PRIM_BLUETOOTH_ICON, PRIM.PRIM_BLUETOOTH_ONOFF_TEXT],
  118: [PRIM.PRIM_BATTERY_ICON, PRIM.PRIM_BATTERY_PCT_TEXT, PRIM.PRIM_BLUETOOTH_ICON, PRIM.PRIM_QUIET_TIME_ICON]
};

// Timezone cluster (content 44-62): the one case in this table where a
// primitive needs a per-position *parameter*, not just an ID -- which of
// the 19 zones. Every entry uses the identical 5-primitive shape (abbr,
// space, time, space, am/pm; the watch drops empty AM/PM and the spacer before it); PRIM_TIMEZONE_AMPM resolves to an empty,
// zero-width primitive in 24h clock style rather than being included or
// omitted conditionally, since the sequence itself can't react to a
// runtime setting once sent -- see primitive_resolver.c's own comment.
// Built programmatically (19 near-identical rows) rather than hand-
// written, same reasoning as generate-primitive-ids.js: fewer places for
// the zone-index arithmetic to drift.
for (var tzContentId = 44; tzContentId <= 62; tzContentId++) {
  PRIMITIVE_TRANSPORT_SEQUENCES[tzContentId] = [
    PRIM.PRIM_TIMEZONE_ABBR, PRIM.PRIM_SPACE, PRIM.PRIM_TIMEZONE_TIME, PRIM.PRIM_SPACE, PRIM.PRIM_TIMEZONE_AMPM
  ];
}

// Per-position aux byte for the sequences above -- see
// eclipse_data.h's corner_primitive_aux comment. Sparse: only content ids
// whose primitives need a non-zero aux appear here; every other content
// id (and every position within it) is implicitly all-zero, which is
// exactly right since none of the primitives used elsewhere in this file
// read their aux at all.
var PRIMITIVE_TRANSPORT_AUX = {};
for (var tzAuxContentId = 44; tzAuxContentId <= 62; tzAuxContentId++) {
  var zoneIdx = tzAuxContentId - 44; // matches feature_timezone_get()'s own index space
  PRIMITIVE_TRANSPORT_AUX[tzAuxContentId] = [zoneIdx, 0, zoneIdx, 0, zoneIdx, 0, 0, 0, 0, 0];
}

// Content 87-95 (Update 25): each is [PRIM_FORECAST_OFFSET_ICON,
// PRIM_FORECAST_TEMP_TEXT] with a fixed forecast-slot aux (0-8, matching
// forecast_temp_c[]'s own index space) baked in per content id -- the
// content number itself picks the slot, same idea as a timezone content id
// picking a zone.
for (var fcContentId = 87; fcContentId <= 95; fcContentId++) {
  var fcIdx = fcContentId - 87;
  PRIMITIVE_TRANSPORT_AUX[fcContentId] = [fcIdx, fcIdx, 0, 0, 0, 0, 0, 0, 0, 0];
}

// Pebble plugin data (content ids 200-211 = plugin slot 0-11): one generic text
// primitive whose aux byte is the slot the source was assigned (see
// plugins/plugin-manager.js).
var PLUGIN_CONTENT_FIRST = 200;
var PLUGIN_SLOT_COUNT = 12;
for (var plugSlot = 0; plugSlot < PLUGIN_SLOT_COUNT; plugSlot++) {
  PRIMITIVE_TRANSPORT_SEQUENCES[PLUGIN_CONTENT_FIRST + plugSlot] = [PRIM.PRIM_PLUGIN_TEXT];
  PRIMITIVE_TRANSPORT_AUX[PLUGIN_CONTENT_FIRST + plugSlot] = [plugSlot, 0, 0, 0, 0, 0, 0, 0, 0, 0];
}

var PRIMITIVE_SEQUENCE_SLOT_COUNT = 10; // must match PRIMITIVE_FEATURE_MAX (primitives/primitive_storage.h)
var PRIMITIVE_TRANSPORT_CONTENT_IDS = Object.keys(PRIMITIVE_TRANSPORT_SEQUENCES).map(Number);

// Zero-padded to PRIMITIVE_SEQUENCE_SLOT_COUNT bytes. Primitive ID zero
// terminates a sequence and an all-zero sequence means the slot is disabled.
function primitiveIdSequenceForContent(contentId) {
  var seq = PRIMITIVE_TRANSPORT_SEQUENCES[parseInt(contentId, 10)] || [];
  var out = seq.slice(0, PRIMITIVE_SEQUENCE_SLOT_COUNT);
  while (out.length < PRIMITIVE_SEQUENCE_SLOT_COUNT) out.push(0);
  return out;
}

// Companion to primitiveIdSequenceForContent(): the per-position aux byte
// for that same content id (see PRIMITIVE_TRANSPORT_AUX above). All-zero
// for every content id with no entry there, which is correct by
// construction -- those primitives never read their aux.
function primitiveAuxSequenceForContent(contentId) {
  var aux = PRIMITIVE_TRANSPORT_AUX[parseInt(contentId, 10)] || [];
  var out = aux.slice(0, PRIMITIVE_SEQUENCE_SLOT_COUNT);
  while (out.length < PRIMITIVE_SEQUENCE_SLOT_COUNT) out.push(0);
  return out;
}

// Clamps/pads an arbitrary (person-composed) array of numeric primitive
// IDs to exactly PRIMITIVE_SEQUENCE_SLOT_COUNT bytes the same way -- used
// for a custom sequence (Phase 9's composer, config-page.js) instead of
// one derived from a single content id.
function normalizePrimitiveSequence(ids) {
  var out = (ids || []).slice(0, PRIMITIVE_SEQUENCE_SLOT_COUNT);
  while (out.length < PRIMITIVE_SEQUENCE_SLOT_COUNT) out.push(0);
  return out;
}

// ---- Phase 9: composable primitive palette --------------------------
//
// Only primitive IDs primitive_resolver.c actually resolves -- offering
// anything else would let a person build a sequence that silently falls
// back to a blank/legacy-content render on the watch (primitive_transport.c
// bails the whole slot the moment one ID can't be resolved), which would
// look like a bug rather than a real limit. Grouped and labeled for the
// palette UI; kept in the same file as the transport sequences above so
// adding resolver coverage for a new primitive is a one-place update
// (add it here, and to primitive_resolver.c on the watch side).
var PRIMITIVE_PALETTE = [
  { group: 'Spacers', items: [
    { id: PRIM.PRIM_SPACE, label: 'Space' },
    { id: PRIM.PRIM_SLASH, label: 'Slash (/)' },
    { id: PRIM.PRIM_DASH, label: 'Dash (-)' },
    { id: PRIM.PRIM_PAREN_OPEN, label: 'Parenthesis open "("' },
    { id: PRIM.PRIM_PAREN_CLOSE, label: 'Parenthesis close ")"' },
    { id: PRIM.PRIM_DOT, label: 'Dot (.)' },
    { id: PRIM.PRIM_COMMA, label: 'Comma (,)' }
  ]},
  { group: 'Date & time', items: [
    { id: PRIM.PRIM_WEEKDAY_SHORT_MIXED, label: 'Weekday (Mon)' },
    { id: PRIM.PRIM_WEEKDAY_SHORT_UPPER, label: 'Weekday (MON)' },
    { id: PRIM.PRIM_WEEKDAY_LONG, label: 'Weekday (Monday)' },
    { id: PRIM.PRIM_MONTH_SHORT_UPPER, label: 'Month (SEP)' },
    { id: PRIM.PRIM_MONTH_SHORT_MIXED, label: 'Month (Sep)' },
    { id: PRIM.PRIM_MONTH_LONG, label: 'Month (September)' },
    { id: PRIM.PRIM_DAY_OF_MONTH, label: 'Day of month' },
    { id: PRIM.PRIM_MONTH_NUMBER, label: 'Month number' },
    { id: PRIM.PRIM_YEAR_FULL, label: 'Year (2026)' },
    { id: PRIM.PRIM_YEAR_SHORT, label: 'Year (26)' },
    { id: PRIM.PRIM_LABEL_WK_PREFIX, label: '"WK" label' },
    { id: PRIM.PRIM_WEEK_NUMBER, label: 'Week number' }
  ]},
  // Parametrized primitives: `param: 'timezone'` tells the composer UI to
  // show a zone picker for this item; the chosen zone index (0-18, same
  // index space as feature_timezone_get()) travels as the primitive's aux
  // byte (CORNER_PRIMITIVE_AUX) -- see primitive_resolver.c.
  { group: 'Time zone', items: [
    { id: PRIM.PRIM_TIMEZONE_ABBR, label: 'Zone name (LON)', param: 'timezone' },
    { id: PRIM.PRIM_TIMEZONE_TIME, label: 'Zone time', param: 'timezone' },
    { id: PRIM.PRIM_TIMEZONE_AMPM, label: 'Zone AM/PM', param: 'timezone' }
  ]},
  { group: 'Clock', items: [
    { id: PRIM.PRIM_TIME_HM, label: 'Time (H:MM)' },
    { id: PRIM.PRIM_TIME_HMS, label: 'Time with seconds' },
    { id: PRIM.PRIM_HOUR_24_PAD, label: 'Hour (24h, padded)' },
    { id: PRIM.PRIM_HOUR_24, label: 'Hour (24h)' },
    { id: PRIM.PRIM_HOUR_12, label: 'Hour (12h)' },
    { id: PRIM.PRIM_MINUTE, label: 'Minute' },
    { id: PRIM.PRIM_MINUTE_PAD, label: 'Minute (padded)' },
    { id: PRIM.PRIM_SECOND, label: 'Second' },
    { id: PRIM.PRIM_SECOND_PAD, label: 'Second (padded)' },
    { id: PRIM.PRIM_SECOND_TENS, label: 'Second, tens digit' },
    { id: PRIM.PRIM_SECOND_ONES, label: 'Second, ones digit' },
    { id: PRIM.PRIM_AMPM, label: 'AM/PM' }
  ]},
  // Every element here is ONE value (or one icon / one label / one unit):
  // nothing bundles a legend, unit or prefix with its number. Units and
  // legends are separate elements ("Temp unit", "Wind unit", "H" label, ...)
  // so a feature shows exactly what the person put in it. The bundled legacy
  // primitives ("H 28", "UV6", "1013 hPa", "AQI 42", ...) still exist for the
  // prebuilt features, they are just not offered for building.
  { group: 'Weather', items: [
    { id: PRIM.PRIM_WEATHER_CONDITION_ICON, label: 'Weather icon' },
    { id: PRIM.PRIM_CURRENT_TEMP_TEXT, label: 'Current temp' },
    { id: PRIM.PRIM_TEMP_HIGH_TEXT, label: 'Daily high temp' },
    { id: PRIM.PRIM_TEMP_LOW_TEXT, label: 'Daily low temp' },
    { id: PRIM.PRIM_FEELS_LIKE_VALUE, label: 'Feels-like temp' },
    { id: PRIM.PRIM_DEW_POINT_TEXT, label: 'Dew point' },
    { id: PRIM.PRIM_TEMP_UNIT_TEXT, label: 'Temp unit (C/F/K)' },
    { id: PRIM.PRIM_CURRENT_CONDITIONS_TEXT, label: 'Condition text' },
    { id: PRIM.PRIM_UV_DAILY_VALUE, label: 'UV index (daily max)' },
    { id: PRIM.PRIM_UV_CURRENT_VALUE, label: 'UV index (current)' },
    { id: PRIM.PRIM_RAIN_CHANCE_ICON, label: 'Rain chance icon' },
    { id: PRIM.PRIM_RAIN_CHANCE_TEXT, label: 'Rain chance %' },
    { id: PRIM.PRIM_HUMIDITY_ICON, label: 'Humidity icon' },
    { id: PRIM.PRIM_HUMIDITY_TEXT, label: 'Humidity %' },
    { id: PRIM.PRIM_WIND_SPEED_ICON, label: 'Wind speed icon' },
    { id: PRIM.PRIM_WIND_SPEED_TEXT, label: 'Wind speed' },
    { id: PRIM.PRIM_WIND_UNIT_TEXT, label: 'Wind unit (km/h, mph...)' },
    { id: PRIM.PRIM_WIND_DIRECTION_TEXT, label: 'Wind direction' },
    { id: PRIM.PRIM_VISIBILITY_ICON, label: 'Visibility icon' },
    { id: PRIM.PRIM_VISIBILITY_TEXT, label: 'Visibility %' },
    { id: PRIM.PRIM_CLOUD_COVER_ICON, label: 'Cloud cover icon' },
    { id: PRIM.PRIM_CLOUD_COVER_TEXT, label: 'Cloud cover %' },
    { id: PRIM.PRIM_PRESSURE_VALUE, label: 'Pressure' },
    { id: PRIM.PRIM_PRESSURE_UNIT_TEXT, label: 'Pressure unit (hPa)' },
    { id: PRIM.PRIM_AQI_VALUE, label: 'Air quality index' },
    { id: PRIM.PRIM_ALTITUDE_ICON, label: 'Altitude icon' },
    { id: PRIM.PRIM_ALTITUDE_VALUE, label: 'Altitude' },
    { id: PRIM.PRIM_ALTITUDE_UNIT_TEXT, label: 'Altitude unit (m/ft)' },
    { id: PRIM.PRIM_WEATHER_REFRESH_ICON, label: 'Weather refresh icon' },
    { id: PRIM.PRIM_WEATHER_LAST_UPDATE_SHORT_TEXT, label: 'Weather updated (time)' }
  ]},
  { group: 'Forecast', items: [
    { id: PRIM.PRIM_FORECAST_OFFSET_ICON, label: 'Forecast offset icon', param: 'forecast' },
    { id: PRIM.PRIM_FORECAST_TEMP_TEXT, label: 'Forecast temp', param: 'forecast' }
  ]},
  { group: 'Health & status', items: [
    { id: PRIM.PRIM_HEART_RATE_ICON, label: 'Heart rate icon' },
    { id: PRIM.PRIM_HEART_RATE_VALUE, label: 'Heart rate BPM' },
    { id: PRIM.PRIM_STEPS_ICON, label: 'Steps icon' },
    { id: PRIM.PRIM_STEPS_VALUE, label: 'Steps count' },
    { id: PRIM.PRIM_STEP_GOAL_PCT, label: 'Step goal %' },
    { id: PRIM.PRIM_BATTERY_ICON, label: 'Battery icon' },
    { id: PRIM.PRIM_BATTERY_PCT_TEXT, label: 'Battery %' },
    { id: PRIM.PRIM_BATTERY_LOGO_ICON, label: 'Pebble battery logo' },
    { id: PRIM.PRIM_BLUETOOTH_ICON, label: 'Bluetooth icon' },
    { id: PRIM.PRIM_BLUETOOTH_STATUS_TEXT, label: 'Bluetooth status' },
    { id: PRIM.PRIM_BLUETOOTH_ONOFF_TEXT, label: 'Bluetooth ON/OFF' },
    { id: PRIM.PRIM_QUIET_TIME_ICON, label: 'Quiet Time icon (shows state)' },
    { id: PRIM.PRIM_QUIET_TIME_PLAIN_ICON, label: 'Quiet Time icon (plain)' },
    { id: PRIM.PRIM_QUIET_TIME_STATUS_TEXT, label: 'Quiet Time ON/OFF' },
    { id: PRIM.PRIM_SLEEP_TOTAL_ICON, label: 'Sleep icon' },
    { id: PRIM.PRIM_SLEEP_TOTAL_TEXT, label: 'Sleep duration' },
    { id: PRIM.PRIM_SLEEP_RESTFUL_ICON, label: 'Restful sleep icon' },
    { id: PRIM.PRIM_SLEEP_RESTFUL_TEXT, label: 'Restful sleep duration' },
    { id: PRIM.PRIM_SLEEP_QUALITY_ICON, label: 'Sleep quality icon' },
    { id: PRIM.PRIM_SLEEP_QUALITY_PCT, label: 'Sleep quality %' },
    { id: PRIM.PRIM_BED_TIME_ICON, label: 'Bed time icon' },
    { id: PRIM.PRIM_BED_TIME_TEXT, label: 'Bed time' },
    { id: PRIM.PRIM_WAKE_TIME_ICON, label: 'Wake time icon' },
    { id: PRIM.PRIM_WAKE_TIME_TEXT, label: 'Wake time' }
  ]},
  { group: 'Astronomy', items: [
    { id: PRIM.PRIM_MOON_PHASE_ICON, label: 'Moon phase icon' },
    { id: PRIM.PRIM_MOON_PHASE_NAME, label: 'Moon phase name' },
    { id: PRIM.PRIM_SUN_EVENT_ICON, label: 'Sunrise/sunset icon' },
    { id: PRIM.PRIM_SUN_EVENT_TIME, label: 'Next sunrise/sunset time' },
    { id: PRIM.PRIM_PLANETS_VISIBLE_ICON, label: 'Planets icon' },
    { id: PRIM.PRIM_PLANETS_VISIBLE_VALUE, label: 'Planets visible (count)' },
    { id: PRIM.PRIM_NEXT_PLANET_NAME, label: 'Next planet rising (name)' },
    { id: PRIM.PRIM_NEXT_PLANET_TIME, label: 'Next planet rising (time)' },
    { id: PRIM.PRIM_METEOR_SHOWER_NAME, label: 'Meteor shower' },
    { id: PRIM.PRIM_SATURN_RINGS_ICON, label: 'Saturn icon' },
    { id: PRIM.PRIM_SATURN_RINGS_VALUE, label: 'Saturn ring angle %' },
    { id: PRIM.PRIM_ISS_ICON, label: 'ISS icon' },
    { id: PRIM.PRIM_ISS_NEXT_PASS, label: 'Next ISS pass' },
    { id: PRIM.PRIM_AURORA_ICON, label: 'Aurora icon' },
    { id: PRIM.PRIM_AURORA_KP_VALUE, label: 'Aurora Kp index' },
    { id: PRIM.PRIM_COMPASS_ICON, label: 'Compass icon' },
    { id: PRIM.PRIM_COMPASS_DIR_TEXT, label: 'Compass direction' },
    { id: PRIM.PRIM_LOCATION_ICON, label: 'Location icon' },
    { id: PRIM.PRIM_LOCATION_NAME, label: 'Location name' }
  ]},
  // Free text typed by the user (e.g. "H", "L", "UV"). Travels as a fixed-text
  // plugin slot (PRIM_PLUGIN_TEXT, slot key "text|<string>"), see plugin-manager.js.
  { group: 'Text', items: [
    { id: PRIM.PRIM_PLUGIN_TEXT, key: 'customtext', label: 'Custom text', customText: true }
  ]},
  // One entry per plugin data source: the picker is filled at run time from the
  // sources the Pebble app reports (see the 'plugin' param in config-composer.js).
  { group: 'Plugins', items: [
    { id: PRIM.PRIM_PLUGIN_TEXT, label: 'Plugin data', param: 'plugin' }
  ]}
];
var PRIMITIVE_LABEL_BY_ID = {};
PRIMITIVE_PALETTE.forEach(function (g) {
  g.items.forEach(function (it) { PRIMITIVE_LABEL_BY_ID[it.id] = it.label; });
});

// Zone picker options for parametrized primitives (index == content id - 44,
// matching feature_timezone_get()'s own table and PRIMITIVE_TRANSPORT_AUX
// above).
var TIMEZONE_ZONES = [];
(function () {
  for (var cid = 44; cid <= 62; cid++) {
    var item = CONTENT_ITEM_BY_ID[cid];
    // abbr: the 3-letter code the existing zone previews already use
    // ("DAC 18:34" -> "DAC"); shown on composer chips and in the preview.
    var abbr = item && item.preview ? String(item.preview).split(' ')[0] : ('Z' + (cid - 44));
    TIMEZONE_ZONES.push({ index: cid - 44, label: item ? item.label : ('Zone ' + (cid - 44)), abbr: abbr });
  }
})();

// Custom-sequence setting format (Phase 9 composer): comma-separated
// entries, each `id` or `id:aux`. Plain `id` (the original text-box format)
// still parses, with aux 0, so anything saved by the earlier composer keeps
// working. Invalid entries are dropped, never guessed at. The result is NOT
// truncated to 10 here -- callers normalize (the settings page rejects >10
// on save; the encoder clamps defensively).
function parseCustomPrimsSetting(raw) {
  var ids = [];
  var aux = [];
  if (!raw) return { ids: ids, aux: aux };
  String(raw).split(',').forEach(function (entry) {
    var parts = entry.split(':');
    var id = parseInt(parts[0], 10);
    if (isNaN(id) || id <= 0 || id > 255) return;
    var a = parts.length > 1 ? parseInt(parts[1], 10) : 0;
    if (isNaN(a) || a < 0 || a > 255) a = 0;
    ids.push(id);
    aux.push(a);
  });
  return { ids: ids, aux: aux };
}

// Same as normalizePrimitiveSequence(), for the aux array.
function normalizePrimitiveAux(aux) {
  return normalizePrimitiveSequence(aux);
}

// Sample text per primitive id, for the settings page's watch-face preview
// canvas when a corner has a custom sequence (drawCornerSlot()). Values
// deliberately match the existing CORNER_PREVIEW_LABELS samples (Monday,
// SEP, day 15, 72/58 F, ...) so a custom sequence that reproduces a
// prebuilt feature previews identically to it. Time-zone primitives are
// zone-dependent and handled in composerPreviewLabel() from the aux byte,
// so they are intentionally absent here. Icons use the same "(name)" style
// the existing labels use for icon-only content.
var PRIMITIVE_PREVIEW_TEXT = {};
(function () {
  var t = PRIMITIVE_PREVIEW_TEXT;
  t[PRIM.PRIM_SPACE] = ' ';
  t[PRIM.PRIM_SLASH] = '/';
  t[PRIM.PRIM_DASH] = '-';
  t[PRIM.PRIM_WEEKDAY_SHORT_MIXED] = 'Mon';
  t[PRIM.PRIM_WEEKDAY_SHORT_UPPER] = 'MON';
  t[PRIM.PRIM_WEEKDAY_LONG] = 'Monday';
  t[PRIM.PRIM_MONTH_SHORT_UPPER] = 'SEP';
  t[PRIM.PRIM_MONTH_SHORT_MIXED] = 'Sep';
  t[PRIM.PRIM_MONTH_LONG] = 'September';
  t[PRIM.PRIM_DAY_OF_MONTH] = '15';
  t[PRIM.PRIM_MONTH_NUMBER] = '9';
  t[PRIM.PRIM_YEAR_FULL] = '2026';
  t[PRIM.PRIM_YEAR_SHORT] = '26';
  t[PRIM.PRIM_LABEL_WK_PREFIX] = 'WK';
  t[PRIM.PRIM_WEEK_NUMBER] = '34';
  t[PRIM.PRIM_WEATHER_CONDITION_ICON] = '(weather)';
  t[PRIM.PRIM_CURRENT_TEMP_TEXT] = '22';
  t[PRIM.PRIM_LABEL_HIGH_PREFIX] = 'H';
  t[PRIM.PRIM_TEMP_HIGH_TEXT] = '72';
  t[PRIM.PRIM_LABEL_LOW_PREFIX] = 'L';
  t[PRIM.PRIM_TEMP_LOW_TEXT] = '58';
  t[PRIM.PRIM_CURRENT_TEMP_ONLY_TEXT] = '22C';
  t[PRIM.PRIM_TEMP_HIGH_ONLY_TEXT] = 'H 28C';
  t[PRIM.PRIM_TEMP_LOW_ONLY_TEXT] = 'L 11C';
  t[PRIM.PRIM_FEELS_LIKE_TEXT] = 'FL 20C';
  t[PRIM.PRIM_HEART_RATE_ICON] = '(hr)';
  t[PRIM.PRIM_HEART_RATE_VALUE] = '72';
  t[PRIM.PRIM_STEPS_ICON] = '(steps)';
  t[PRIM.PRIM_STEPS_VALUE] = '8421';
  t[PRIM.PRIM_STEP_GOAL_PCT] = '84%';
  t[PRIM.PRIM_BATTERY_ICON] = '(batt)';
  t[PRIM.PRIM_BATTERY_PCT_TEXT] = '80%';
  t[PRIM.PRIM_BATTERY_LOGO_ICON] = 'LOGO';
  t[PRIM.PRIM_BLUETOOTH_ICON] = '(bt)';
  t[PRIM.PRIM_BLUETOOTH_STATUS_TEXT] = 'BT';
  t[PRIM.PRIM_QUIET_TIME_ICON] = '(quiet)';
  t[PRIM.PRIM_QUIET_TIME_STATUS_TEXT] = 'OFF';
  t[PRIM.PRIM_SLEEP_TOTAL_ICON] = '(sleep)';
  t[PRIM.PRIM_SLEEP_TOTAL_TEXT] = '7h30';
  t[PRIM.PRIM_SLEEP_RESTFUL_ICON] = '(rest)';
  t[PRIM.PRIM_SLEEP_RESTFUL_TEXT] = '2h10';
  t[PRIM.PRIM_SLEEP_QUALITY_ICON] = '(qual)';
  t[PRIM.PRIM_SLEEP_QUALITY_PCT] = '85%';
  t[PRIM.PRIM_BED_TIME_ICON] = '(bed)';
  t[PRIM.PRIM_BED_TIME_TEXT] = '23:10';
  t[PRIM.PRIM_WAKE_TIME_ICON] = '(wake)';
  t[PRIM.PRIM_WAKE_TIME_TEXT] = '06:45';
  t[PRIM.PRIM_QUIET_TIME_PLAIN_ICON] = '(quiet)';
  t[PRIM.PRIM_BLUETOOTH_ONOFF_TEXT] = 'ON';
  t[PRIM.PRIM_MOON_PHASE_ICON] = '(moon)';
  t[PRIM.PRIM_MOON_PHASE_NAME] = 'Full';
  t[PRIM.PRIM_SUN_EVENT_ICON] = '(sun)';
  t[PRIM.PRIM_SUN_EVENT_TIME] = '19:42';
  t[PRIM.PRIM_PLANETS_VISIBLE_ICON] = '(planets)';
  t[PRIM.PRIM_PLANETS_VISIBLE_COUNT] = '3 planets';
  t[PRIM.PRIM_NEXT_PLANET_RISE] = 'VEN 18:32';
  t[PRIM.PRIM_METEOR_SHOWER_NAME] = 'Perseids';
  t[PRIM.PRIM_SATURN_RINGS_ICON] = '(saturn)';
  t[PRIM.PRIM_SATURN_RINGS_PCT] = 'Rings 12%';
  t[PRIM.PRIM_ISS_ICON] = '(iss)';
  t[PRIM.PRIM_ISS_NEXT_PASS] = '22:47';
  t[PRIM.PRIM_AURORA_ICON] = '(aurora)';
  t[PRIM.PRIM_AURORA_KP] = 'Kp 4.3';
  t[PRIM.PRIM_COMPASS_ICON] = '(compass)';
  t[PRIM.PRIM_COMPASS_DIR_TEXT] = 'NNW';
  t[PRIM.PRIM_LOCATION_ICON] = '(pin)';
  t[PRIM.PRIM_LOCATION_NAME] = 'Innsbruck';
  t[PRIM.PRIM_TIME_HM] = '9:41';
  t[PRIM.PRIM_TIME_HMS] = '9:41:07';
  t[PRIM.PRIM_HOUR_24_PAD] = '09';
  t[PRIM.PRIM_HOUR_24] = '9';
  t[PRIM.PRIM_HOUR_12] = '9';
  t[PRIM.PRIM_MINUTE] = '41';
  t[PRIM.PRIM_MINUTE_PAD] = '41';
  t[PRIM.PRIM_SECOND] = '7';
  t[PRIM.PRIM_SECOND_PAD] = '07';
  t[PRIM.PRIM_SECOND_TENS] = '0';
  t[PRIM.PRIM_SECOND_ONES] = '7';
  t[PRIM.PRIM_AMPM] = 'AM';
  t[PRIM.PRIM_CURRENT_CONDITIONS_TEXT] = 'Partly Cloudy';
  t[PRIM.PRIM_UV_DAILY_TEXT] = 'UV6';
  t[PRIM.PRIM_UV_CURRENT_TEXT] = 'UV4';
  t[PRIM.PRIM_RAIN_CHANCE_ICON] = '(rain)';
  t[PRIM.PRIM_RAIN_CHANCE_TEXT] = '20%';
  t[PRIM.PRIM_HUMIDITY_ICON] = '(humid)';
  t[PRIM.PRIM_HUMIDITY_TEXT] = '55%';
  t[PRIM.PRIM_WIND_SPEED_ICON] = '(wind)';
  t[PRIM.PRIM_WIND_SPEED_TEXT] = '12';
  t[PRIM.PRIM_VISIBILITY_ICON] = '(vis)';
  t[PRIM.PRIM_VISIBILITY_TEXT] = '90%';
  t[PRIM.PRIM_CLOUD_COVER_ICON] = '(cloud)';
  t[PRIM.PRIM_CLOUD_COVER_TEXT] = '35%';
  t[PRIM.PRIM_PRESSURE_TREND_ICON] = '(pressure)';
  t[PRIM.PRIM_PRESSURE_TEXT] = '1013 hPa';
  t[PRIM.PRIM_WIND_DIRECTION_ICON] = '(compass)';
  t[PRIM.PRIM_WIND_DIRECTION_TEXT] = 'NW';
  t[PRIM.PRIM_AQI_TEXT] = 'AQI 42';
  t[PRIM.PRIM_DEW_POINT_TEXT] = '14';
  t[PRIM.PRIM_ALTITUDE_ICON] = '(altitude)';
  t[PRIM.PRIM_ALTITUDE_TEXT] = '120m';
  t[PRIM.PRIM_WEATHER_REFRESH_ICON] = '(refresh)';
  t[PRIM.PRIM_WEATHER_LAST_UPDATE_LONG_TEXT] = 'Last updated 09:30';
  t[PRIM.PRIM_WEATHER_LAST_UPDATE_SHORT_TEXT] = '09:30';
  t[PRIM.PRIM_FORECAST_OFFSET_ICON] = '(+Nh)';
  t[PRIM.PRIM_FORECAST_TEMP_TEXT] = '19';
  t[PRIM.PRIM_PAREN_OPEN] = '(';
  t[PRIM.PRIM_PAREN_CLOSE] = ')';
  t[PRIM.PRIM_DOT] = '.';
  t[PRIM.PRIM_COMMA] = ',';
  t[PRIM.PRIM_FEELS_LIKE_VALUE] = '20';
  t[PRIM.PRIM_UV_DAILY_VALUE] = '6';
  t[PRIM.PRIM_UV_CURRENT_VALUE] = '4';
  t[PRIM.PRIM_PRESSURE_VALUE] = '1013';
  t[PRIM.PRIM_AQI_VALUE] = '42';
  t[PRIM.PRIM_ALTITUDE_VALUE] = '120';
  t[PRIM.PRIM_PLANETS_VISIBLE_VALUE] = '3';
  t[PRIM.PRIM_SATURN_RINGS_VALUE] = '12%';
  t[PRIM.PRIM_AURORA_KP_VALUE] = '4.3';
  t[PRIM.PRIM_NEXT_PLANET_NAME] = 'VEN';
  t[PRIM.PRIM_NEXT_PLANET_TIME] = '18:32';
  t[PRIM.PRIM_TEMP_UNIT_TEXT] = 'C';
  t[PRIM.PRIM_WIND_UNIT_TEXT] = 'km/h';
  t[PRIM.PRIM_PRESSURE_UNIT_TEXT] = 'hPa';
  t[PRIM.PRIM_ALTITUDE_UNIT_TEXT] = 'm';
  t[PRIM.PRIM_PLUGIN_TEXT] = 'PLG';
})();

// Forecast slot options for PRIM_FORECAST_OFFSET_ICON/PRIM_FORECAST_TEMP_TEXT
// (param: 'forecast'): index matches forecast_temp_c[]'s own space, same
// idea as TIMEZONE_ZONES above -- 0-5 are 1-6 hours ahead, 6-8 are 1-3 days
// ahead (content 87-95's own order), 9-14 are 7-12 hours ahead (added later,
// so they come after the day slots to keep saved indices valid). The list is
// ordered for the picker, NOT by index: always look a slot up by .index.
var FORECAST_SLOTS = [
  { index: 0, label: '1 hour ahead' }, { index: 1, label: '2 hours ahead' },
  { index: 2, label: '3 hours ahead' }, { index: 3, label: '4 hours ahead' },
  { index: 4, label: '5 hours ahead' }, { index: 5, label: '6 hours ahead' },
  { index: 9, label: '7 hours ahead' }, { index: 10, label: '8 hours ahead' },
  { index: 11, label: '9 hours ahead' }, { index: 12, label: '10 hours ahead' },
  { index: 13, label: '11 hours ahead' }, { index: 14, label: '12 hours ahead' },
  { index: 6, label: '1 day ahead' }, { index: 7, label: '2 days ahead' },
  { index: 8, label: '3 days ahead' }
];

// ---- Phase 10: preview chips derived from the real transport sequence ----
//
// Reverse of PRIM (name -> id) and a param lookup built from
// PRIMITIVE_PALETTE (the same "which primitives take an aux parameter, and
// which" the composer itself uses) -- both are classification tables built
// once from data that already exists elsewhere in this file, not
// hand-duplicated.
var PRIM_NAME_BY_ID = {};
Object.keys(PRIM).forEach(function (name) { PRIM_NAME_BY_ID[PRIM[name]] = name; });
var PRIM_PARAM_BY_ID = {};
PRIMITIVE_PALETTE.forEach(function (g) { g.items.forEach(function (it) { if (it.param) PRIM_PARAM_BY_ID[it.id] = it.param; }); });

// A chip's display kind, from the primitive's own name -- every primitive
// ID in primitive_ids.h follows one consistent naming convention (an
// _ICON suffix for icons, PRIM_LABEL_* for the constant-glyph prefixes),
// so this needs no separate hand-kept id->kind table.
function primitiveKindForId(id) {
  if (id === PRIM.PRIM_SPACE) return 'space';
  if (id === PRIM.PRIM_SLASH || id === PRIM.PRIM_DASH) return 'slash';
  var name = PRIM_NAME_BY_ID[id] || '';
  if (name.indexOf('PRIM_LABEL_') === 0) return 'label';
  if (name.slice(-5) === '_ICON') return 'icon';
  return 'value';
}

// Sample text for a parametrized primitive at a given aux value (zone name
// or forecast-slot label), or undefined for a non-parametrized primitive --
// same substitution composerPreviewLabel() does for a person's custom
// sequence, extracted here so both the reference preview (content-derived,
// this file) and the composer (person-composed, config-composer.js) show
// the identical label for the identical {id, aux} pair.
function paramPrimitiveLabel(id, aux) {
  var param = PRIM_PARAM_BY_ID[id];
  if (!param) return undefined;
  if (param === 'timezone') {
    var zone = TIMEZONE_ZONES[aux];
    var zoneText = zone ? (zone.abbr || zone.label.split(' ')[0]) : ('zone ' + aux);
    // Exact-id checks, not name substring matching -- every timezone
    // primitive's name starts with "PRIM_TIMEZONE_", which itself contains
    // "TIME", so a substring check on TIME/AMPM would misfire on the abbr
    // primitive too.
    if (id === PRIM.PRIM_TIMEZONE_TIME) return '12:00';
    if (id === PRIM.PRIM_TIMEZONE_AMPM) return 'PM';
    return zoneText; // PRIM_TIMEZONE_ABBR
  }
  if (param === 'forecast') return PRIMITIVE_PREVIEW_TEXT[id]; // slot-independent sample text
  return undefined;
}

// Builds the chip list for an already-resolved {ids, aux} pair -- shared by
// primitiveChipsForContent() (a content id's own sequence) and available for
// a person-composed custom sequence too, so both paths render chips through
// the same code. ids/aux are expected zero-padded (as
// primitiveIdSequenceForContent()/primitiveAuxSequenceForContent() and
// normalizePrimitiveSequence()/normalizePrimitiveAux() all produce); a
// PRIM_ID_NONE (0) entry ends the sequence.
function chipsFromSequence(ids, aux) {
  var chips = [];
  for (var i = 0; i < ids.length && ids[i] !== 0; i++) {
    var text = paramPrimitiveLabel(ids[i], aux[i]);
    if (text === undefined) text = PRIMITIVE_PREVIEW_TEXT[ids[i]];
    if (text === undefined) text = '?'; // a resolver-covered id with no PRIMITIVE_PREVIEW_TEXT entry -- see this file's own header note
    chips.push({ kind: primitiveKindForId(ids[i]), text: text });
  }
  return chips;
}

// One chip per non-zero primitive in the phone-compiled sequence. Content
// IDs are only local compiler inputs and never cross the wire.
function primitiveChipsForContent(contentId) {
  var id = parseInt(contentId, 10);
  if (id === 0) return [];
  if (PRIMITIVE_TRANSPORT_SEQUENCES[id]) {
    return chipsFromSequence(primitiveIdSequenceForContent(id), primitiveAuxSequenceForContent(id));
  }
  var item = CONTENT_ITEM_BY_ID[id];
  if (!item) return [];
  return [value(item.preview || item.label)];
}

module.exports = {
  PRIMITIVE_PREVIEW_TEXT: PRIMITIVE_PREVIEW_TEXT,
  FORECAST_SLOTS: FORECAST_SLOTS,
  TIMEZONE_ZONES: TIMEZONE_ZONES,
  parseCustomPrimsSetting: parseCustomPrimsSetting,
  normalizePrimitiveAux: normalizePrimitiveAux,
  primitiveChipsForContent: primitiveChipsForContent,
  chipsFromSequence: chipsFromSequence,
  primitiveKindForId: primitiveKindForId,
  paramPrimitiveLabel: paramPrimitiveLabel,
  PRIMITIVE_TRANSPORT_CONTENT_IDS: PRIMITIVE_TRANSPORT_CONTENT_IDS,
  PRIMITIVE_TRANSPORT_SEQUENCES: PRIMITIVE_TRANSPORT_SEQUENCES,
  PRIMITIVE_TRANSPORT_AUX: PRIMITIVE_TRANSPORT_AUX,
  primitiveIdSequenceForContent: primitiveIdSequenceForContent,
  primitiveAuxSequenceForContent: primitiveAuxSequenceForContent,
  normalizePrimitiveSequence: normalizePrimitiveSequence,
  PRIMITIVE_PALETTE: PRIMITIVE_PALETTE,
  PRIMITIVE_LABEL_BY_ID: PRIMITIVE_LABEL_BY_ID
};
