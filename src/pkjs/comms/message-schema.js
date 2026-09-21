// ---- AppMessage schema: message types + key-to-chunk map -----------------
//
// Moved to comms/message-schema.js (communication subsystem extraction).
// Behavior is unchanged; only the module boundary is new.
//
// The watch used to get one giant dictionary per refresh (every
// eclipse/weather/sky/settings/features key at once), which forced
// app_message_open() on the C side to reserve inbox/outbox buffers big
// enough for that whole payload. Instead, every send now goes through
// sendFlatDict() (comms/message-encoder.js), which buckets keys by subject into several
// smaller dictionaries and sends them one at a time, each tagged with
// MESSAGE_TYPE so the watch knows which subset of keys to expect. Keep
// this enum in sync with the MsgType enum in src/c/eclipse_data.h.
var MSG_TYPE = {
  STATUS: 0,      // DATA_VALID, ERROR_CODE, LOCATION_NAME
  ECLIPSE: 1,      // contact times, magnitude, separation/mag sample arrays
  WEATHER: 2,      // current conditions: temps, humidity, wind, AQI, ...
  ASTRONOMY: 3,    // sun/moon/planet/star position samples, rise/set times
  SKY_EFFECTS: 4,  // aurora, meteor shower, ISS pass
  FEATURES: 5,     // corner/edge content-slot selection + what feeds them
  SETTINGS: 6      // clock face cosmetics: hands, markers, colors, units, fonts
};

// Send order: STATUS/ECLIPSE first since DATA_VALID gates whether the
// rest of the face renders as "eclipse day" at all, then the other
// data chunks, then the purely cosmetic ones last.
var MSG_TYPE_SEND_ORDER = [
  MSG_TYPE.STATUS, MSG_TYPE.ECLIPSE, MSG_TYPE.WEATHER, MSG_TYPE.ASTRONOMY,
  MSG_TYPE.SKY_EFFECTS, MSG_TYPE.FEATURES, MSG_TYPE.SETTINGS
];

// Maps every AppMessage key (other than MESSAGE_TYPE itself) to the
// chunk it belongs in. Built once from a table rather than scattering
// the mapping across each dict-building function, so it's one place
// to check when a new key is added.
var KEY_TYPE_MAP = (function () {
  var map = {};
  function assign(type, keys) { keys.forEach(function (k) { map[k] = type; }); }

  assign(MSG_TYPE.STATUS, ['DATA_VALID', 'ERROR_CODE', 'LOCATION_NAME']);

  assign(MSG_TYPE.ECLIPSE, [
    'C1_TIME', 'C2_TIME', 'MAX_TIME', 'C3_TIME', 'C4_TIME', 'SUNSET_TIME',
    'MAGNITUDE', 'ECLIPSE_TYPE', 'POS_ANGLE',
    'SAMPLE_START', 'SAMPLE_INTERVAL', 'SAMPLE_COUNT',
    'SEP_SAMPLES', 'MAG_SAMPLES', 'RADIUS_RATIO_PCT'
  ]);

  assign(MSG_TYPE.WEATHER, [
    'CLOUD_COVER', 'VIS_SCORE', 'WEATHER_SOURCES', 'WEATHER_ERROR_CODE', 'WEATHER_CONDITION',
    'WEATHER_TEMP_C', 'WEATHER_TEMP_HIGH_C', 'WEATHER_TEMP_LOW_C', 'CURRENT_TEMP_DISPLAY', 'WEATHER_ICON_CATEGORY', 'WEATHER_ICON_COLOR',
    'TEMP_HIGH_DISPLAY', 'TEMP_LOW_DISPLAY', 'FEELS_LIKE_DISPLAY',
    'CURRENT_TEMP_COLOR', 'TEMP_HIGH_COLOR', 'TEMP_LOW_COLOR', 'FEELS_LIKE_COLOR',
    'UV_DAILY_DISPLAY', 'UV_DAILY_COLOR', 'UV_CURRENT_DISPLAY', 'UV_CURRENT_COLOR',
    'RAIN_CHANCE_DISPLAY', 'RAIN_CHANCE_COLOR', 'HUMIDITY_DISPLAY', 'HUMIDITY_COLOR',
    'WIND_SPEED_DISPLAY', 'WIND_SPEED_COLOR', 'CLOUD_COVER_DISPLAY', 'CLOUD_COVER_COLOR',
    'VIS_SCORE_DISPLAY', 'VIS_SCORE_COLOR', 'ALTITUDE_DISPLAY', 'ALTITUDE_COLOR',
    'DEW_POINT_DISPLAY', 'PRESSURE_DISPLAY', 'PRESSURE_COLOR', 'AQI_DISPLAY', 'AQI_COLOR',
    'UV_INDEX_X10', 'UV_INDEX_CURRENT_X10', 'RAIN_CHANCE_PCT', 'HUMIDITY_PCT', 'WIND_SPEED_KMH',
    'WIND_DIR_DEG', 'DEW_POINT_C', 'PRESSURE_HPA', 'PRESSURE_TREND',
    'AQI_US', 'AQI_EU', 'ALTITUDE_M', 'WEATHER_LAST_UPDATE',
    'FORECAST_TEMP_C', 'FORECAST_CONDITION', 'FORECAST_ICON_CATEGORIES',
    'FORECAST_TEMP_DISPLAY_0', 'FORECAST_TEMP_DISPLAY_1', 'FORECAST_TEMP_DISPLAY_2',
    'FORECAST_TEMP_DISPLAY_3', 'FORECAST_TEMP_DISPLAY_4', 'FORECAST_TEMP_DISPLAY_5',
    'FORECAST_TEMP_DISPLAY_6', 'FORECAST_TEMP_DISPLAY_7', 'FORECAST_TEMP_DISPLAY_8',
    'FORECAST_TEMP_COLORS'
  ]);

  assign(MSG_TYPE.ASTRONOMY, [
    'SKY_SAMPLE_START', 'SKY_SAMPLE_INTERVAL', 'SKY_SAMPLE_COUNT',
    'SUN_ALT_SAMPLES', 'SUN_AZ_SAMPLES', 'MOON_ALT_SAMPLES', 'MOON_AZ_SAMPLES',
    'PLANET_ALT_SAMPLES', 'PLANET_AZ_SAMPLES', 'PLANET_RISE', 'PLANET_SET',
    'SATURN_RING_OPEN_PCT', 'SKY_SCALE_MAX_ALT', 'SATURN_RINGS_DISPLAY',
    'CLOUD_SAMPLES', 'CLOUD_ALTITUDE_PCT',
    'MOON_PHASE_PCT', 'MOON_WAXING', 'MOON_PHASE_DISPLAY',
    'SUN_RISE', 'SUN_SET', 'SUN_RISE_TOMORROW', 'MOON_RISE', 'MOON_SET',
    'STAR_ALT_SAMPLES', 'STAR_AZ_SAMPLES'
  ]);

  assign(MSG_TYPE.SKY_EFFECTS, [
    'AURORA_KP_X10', 'AURORA_VISIBILITY_PCT', 'AURORA_ERROR_CODE', 'AURORA_KP_DISPLAY', 'AURORA_KP_COLOR',
    'METEOR_INTENSITY', 'METEOR_SHOWER_NAME', 'METEOR_DISPLAY', 'METEOR_COLOR',
    'ISS_ALT', 'ISS_AZ', 'ISS_COMPUTED_AT', 'ISS_NEXT_PASS', 'ISS_ERROR_CODE',
    'OVERHEAD_OBJECTS', 'OVERHEAD_OBJECT_COUNT', 'OVERHEAD_OBJECTS_COMPUTED_AT'
  ]);

  assign(MSG_TYPE.FEATURES, [
    'CORNER_FONT', 'CORNER_CONTENT', 'CORNER_COLOR_MODE', 'CORNER_PRIMITIVE_IDS', 'CORNER_PRIMITIVE_AUX',
    'EDGE_LINES', 'EDGE_LINE_PRIMITIVE_IDS', 'EDGE_LINE_PRIMITIVE_AUX',
    'SHOW_SUN_TIME', 'SHOW_ISS', 'SHOW_FLIGHTS', 'AURORA_ENABLED',
    'DAILY_STEP_GOAL'
  ]);

  assign(MSG_TYPE.SETTINGS, [
    'CLOCK_FONT', 'TEMP_UNIT', 'WIND_SPEED_UNIT', 'SHOW_SECONDS',
    'COLORS',
    'NIGHT_SCHEME_ENABLED',
    'BOTTOM_STYLE', 'SUN_MOON_SIZE_PCT',
    'SKY_MODE', 'SHOW_MAJOR_STARS', 'WEATHER_ICON_STYLE', 'AQI_UNIT', 'ALTITUDE_UNIT',
    'SHAKE_LABEL_SECONDS', 'LABEL_STYLE',
    'VIBRATE_ON_PHASE_CHANGE', 'STARTUP_CLOCK_ANIM_MODE',
    'BG_ANIM_MODE', 'SHAKE_ANIM_MODE', 'OUTLINE_ENABLED', 'BATTERY_SAVER_ENABLED',
    'SHADOW_TRANSLUCENT', 'SHADOW_ANGLE',
    'BIG_ANALOG_MARKER_STYLE', 'BITMAP_MARKER_TRANSPARENT', 'DRAW_FEATURES_BENEATH_HANDS',
    'MARKER_RINGS', 'MARKER_TEXT', 'HANDS',
    'CENTER_CIRCLE_RADIUS', 'CENTER_CIRCLE_COLOR', "DRAW_DEBUG",
    'CUSTOM_HOUR_INNER_THICKNESS', 'CUSTOM_SEC_INNER_THICKNESS',
    'HOURLY_VIBE_MODE', 'HOURLY_VIBE_INTERVAL_MIN', 'HOURLY_VIBE_PATTERN',
    'HOURLY_VIBE_START_MIN', 'HOURLY_VIBE_END_MIN', 'HOURLY_VIBE_DAYS_MASK',
    'HOURLY_VIBE_OVERRIDE_QUIET'
  ]);

  return map;
})();

module.exports = {
  MSG_TYPE: MSG_TYPE,
  MSG_TYPE_SEND_ORDER: MSG_TYPE_SEND_ORDER,
  KEY_TYPE_MAP: KEY_TYPE_MAP
};
