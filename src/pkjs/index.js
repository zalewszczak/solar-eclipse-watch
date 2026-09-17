var configPage = require('./config-page');
var servicelog = require('./servicelog');
var presetsLookups = require('./presets-lookups');

// astro/weather/geocode/iss requires removed here -- refreshAndSend() was
// their only caller in this file, and it moved to refresh-manager.js
// (refresh manager extraction); that module requires them directly now.

// TYPE_CODE moved to comms/message-encoder.js (its only callers).

// Both derived from their own source-of-truth arrays in presets-
// lookups.js (CORNER_CATEGORIES / FONT_LOOKUP) rather than hand-
// maintained here -- see that file's own comments on both constants
// for why a hardcoded copy of either is exactly the kind of thing that
// silently goes stale the moment someone adds a new font or content
// id without remembering this file also has a copy to bump.
var MAX_FEATURES = presetsLookups.MAX_FEATURES;
var FONT_MAX_CONTENT_ID = presetsLookups.FONT_MAX_CONTENT_ID;

// ---- migration: settings-key wire-format schema version ------------------
// Moved to settings/settings-migration.js (settings subsystem extraction) --
// self-executes on require, at the same point in startup it used to run inline.
require('./settings/settings-migration');

// ---- AppMessage chunking -------------------------------------------------
//
// The watch used to get one giant dictionary per refresh (every
// eclipse/weather/sky/settings/features key at once), which forced
// app_message_open() on the C side to reserve inbox/outbox buffers big
// enough for that whole payload. Instead, every send now goes through
// sendFlatDict() below, which buckets keys by subject into several
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
    'WEATHER_TEMP_C', 'WEATHER_TEMP_HIGH_C', 'WEATHER_TEMP_LOW_C',
    'UV_INDEX_X10', 'UV_INDEX_CURRENT_X10', 'RAIN_CHANCE_PCT', 'HUMIDITY_PCT', 'WIND_SPEED_KMH',
    'WIND_DIR_DEG', 'DEW_POINT_C', 'PRESSURE_HPA', 'PRESSURE_TREND',
    'AQI_US', 'AQI_EU', 'ALTITUDE_M', 'WEATHER_LAST_UPDATE',
    'FORECAST_TEMP_C', 'FORECAST_CONDITION'
  ]);

  assign(MSG_TYPE.ASTRONOMY, [
    'SKY_SAMPLE_START', 'SKY_SAMPLE_INTERVAL', 'SKY_SAMPLE_COUNT',
    'SUN_ALT_SAMPLES', 'SUN_AZ_SAMPLES', 'MOON_ALT_SAMPLES', 'MOON_AZ_SAMPLES',
    'PLANET_ALT_SAMPLES', 'PLANET_AZ_SAMPLES', 'PLANET_RISE', 'PLANET_SET',
    'SATURN_RING_OPEN_PCT', 'SKY_SCALE_MAX_ALT',
    'CLOUD_SAMPLES', 'CLOUD_ALTITUDE_PCT',
    'MOON_PHASE_PCT', 'MOON_WAXING',
    'SUN_RISE', 'SUN_SET', 'SUN_RISE_TOMORROW', 'MOON_RISE', 'MOON_SET',
    'STAR_ALT_SAMPLES', 'STAR_AZ_SAMPLES'
  ]);

  assign(MSG_TYPE.SKY_EFFECTS, [
    'AURORA_KP_X10', 'AURORA_VISIBILITY_PCT', 'AURORA_ERROR_CODE',
    'METEOR_INTENSITY', 'METEOR_SHOWER_NAME',
    'ISS_ALT', 'ISS_AZ', 'ISS_COMPUTED_AT', 'ISS_NEXT_PASS', 'ISS_ERROR_CODE',
    'OVERHEAD_OBJECTS', 'OVERHEAD_OBJECT_COUNT', 'OVERHEAD_OBJECTS_COMPUTED_AT'
  ]);

  assign(MSG_TYPE.FEATURES, [
    'CORNER_FONT', 'CORNER_CONTENT', 'CORNER_COLOR_MODE',
    'EDGE_LINES',
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
    'BG_ANIM_MODE', 'SHAKE_ANIM_MODE', 'OUTLINE_ENABLED',
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

// Only one AppMessage can be in flight at a time -- s_sendQueue holds
// the chunks still waiting to go out for the current send*() call(s),
// s_sendInFlight guards against overlapping Pebble.sendAppMessage()
// calls (which would otherwise error/clobber each other). Each queue
// entry is {dict, batchIndex, batchTotal} rather than the bare dict --
// batchIndex/batchTotal (1-based position / count within THIS
// enqueueFlatDict() call, e.g. "3/6") are never sent to the watch
// (only entry.dict is), just carried alongside for
// recordRawMessage()'s own log entries and the Testing section's
// per-chunk button labels in the settings page.
var s_sendQueue = [];
var s_sendInFlight = false;

// Nothing actually leaves the phone for the first few seconds after
// PKJS starts up, regardless of what queued it (the 'ready' handler's
// own first refresh below, a REQUEST_UPDATE reply, a settings-page
// Save) -- gated here, in the one place every send*() call already
// funnels through (see pumpSendQueue()), rather than in each
// individual caller, so nothing can slip through by some other path.
// Lets the watch's own screen finish laying out/settling first before
// a message starts changing what's on it -- the watch itself holds
// its own first outbound request back a few seconds for the same
// reason (see request_update()'s own comment in pebble-eclipse-watch.c),
// so this is the phone-side half of that same "let it settle first"
// intent, on its own longer timer since the phone doesn't know
// whether the watch's request has actually arrived yet.
var PHONE_STARTUP_SEND_DELAY_MS = 5000;
var s_phoneStartupSendDelayElapsed = false;
setTimeout(function () {
  s_phoneStartupSendDelayElapsed = true;
  pumpSendQueue(); // resume whatever queued up during the delay, if anything did
}, PHONE_STARTUP_SEND_DELAY_MS);

// Last RAW_MESSAGE_LOG_MAX individual AppMessage chunks actually sent
// (acked, not just attempted), for the Testing section's raw-message
// browser -- see recordRawMessage() below and buildConfigHtml()'s own
// rawMessageLog. Chunking (see the AppMessage chunking block above)
// means any one send*() call now produces several small messages
// instead of one big one, so logging only "the last thing sent" (the
// old LAST_COMPUTED_DICT/"Reload last sent data" button) stopped
// being useful -- it would just show whichever single chunk happened
// to go out last, not the full picture. Logging every chunk instead
// means a full refresh's whole 6-7-message batch is all inspectable
// afterwards, not just its tail end.
var RAW_MESSAGE_LOG_MAX = 10;
function recordRawMessage(batchIndex, batchTotal, dict) {
  try {
    var entries = [];
    var raw = localStorage.getItem('RAW_MESSAGE_LOG');
    if (raw) entries = JSON.parse(raw);
    if (!Array.isArray(entries)) entries = [];
    entries.push({ t: Date.now(), batchIndex: batchIndex, batchTotal: batchTotal, dict: dict });
    if (entries.length > RAW_MESSAGE_LOG_MAX) entries = entries.slice(entries.length - RAW_MESSAGE_LOG_MAX);
    localStorage.setItem('RAW_MESSAGE_LOG', JSON.stringify(entries));
  } catch (e) {
    // Not critical if this fails (storage full, etc.) -- just means the
    // settings page's Testing section won't have a fresh log this time.
  }
}

// Buckets a flat {KEY: value} dict (as every send*() function used to
// build a single one) into per-MESSAGE_TYPE chunks and queues them.
// A key with no entry in KEY_TYPE_MAP is a bug (a key was added
// without updating the map above) -- rather than silently dropping
// it, it's logged and placed in SETTINGS so it still reaches the
// watch instead of vanishing.
function enqueueFlatDict(flatDict) {
  var buckets = {};
  Object.keys(flatDict).forEach(function (k) {
    var type = KEY_TYPE_MAP[k];
    if (type === undefined) {
      console.log('eclipse-watch: no MSG_TYPE mapping for key ' + k + ', defaulting to SETTINGS -- update KEY_TYPE_MAP');
      type = MSG_TYPE.SETTINGS;
    }
    if (!buckets[type]) buckets[type] = {};
    buckets[type][k] = flatDict[k];
  });
  var batch = [];
  MSG_TYPE_SEND_ORDER.forEach(function (type) {
    if (buckets[type]) {
      buckets[type]['MESSAGE_TYPE'] = type;
      batch.push(buckets[type]);
    }
  });
  batch.forEach(function (dict, i) {
    s_sendQueue.push({ dict: dict, batchIndex: i + 1, batchTotal: batch.length });
  });
  pumpSendQueue();
}

function pumpSendQueue() {
  if (s_sendInFlight || s_sendQueue.length === 0) return;
  if (!s_phoneStartupSendDelayElapsed) return; // the setTimeout above re-pumps once the startup delay elapses
  s_sendInFlight = true;
  var entry = s_sendQueue.shift();
  var chunk = entry.dict;
  var msgType = chunk['MESSAGE_TYPE'];
  Pebble.sendAppMessage(chunk, function () {
    console.log('eclipse-watch: chunk sent (type ' + msgType + '), ' + s_sendQueue.length + ' queued');
    recordRawMessage(entry.batchIndex, entry.batchTotal, chunk);
    s_sendInFlight = false;
    pumpSendQueue();
  }, function (e) {
    // Drop rather than retry-forever: the next refresh/settings-save
    // cycle will enqueue a fresh chunk of the same type anyway, so a
    // stuck retry here would just delay everything behind it.
    console.log('eclipse-watch: chunk send failed (type ' + msgType + '), dropping: ' + JSON.stringify(e));
    s_sendInFlight = false;
    pumpSendQueue();
  });
}

// refreshTimer, lastBatterySaverPhase, lastHourlyRefreshHourKey,
// batterySaverHourlyTimer, and s_refreshGeneration all moved to
// refresh-manager.js -- see its own comment for why setBatterySaverPhase()
// exists (the 'appmessage' handler below still needs to report the
// watch's phase into that module).

// ---- tiny settings helpers -------------------------------------------
//
// Moved to settings/settings.js (settings subsystem extraction).
var settings = require('./settings/settings');
var getSetting = settings.getSetting;
var setSetting = settings.setSetting;

// parseDateTimeLocal()/getEffectiveNow() moved to refresh-manager.js
// (their only caller, refreshAndSend()).


// ---- settings codecs -----------------------------------------------------
//
// Moved to settings/settings-codecs.js (settings subsystem extraction).
// Each name below is a local alias for the same-named export there, so
// every call site elsewhere in this file is unchanged.
var settingsCodecs = require('./settings/settings-codecs');
var clampFontId = settingsCodecs.clampFontId;
var clockFontCode = settingsCodecs.clockFontCode;
var tempUnitCode = settingsCodecs.tempUnitCode;
var windSpeedUnitCode = settingsCodecs.windSpeedUnitCode;
var showSecondsCode = settingsCodecs.showSecondsCode;
var customColorByte = settingsCodecs.customColorByte;
var customBgByte = settingsCodecs.customBgByte;
var customTextByte = settingsCodecs.customTextByte;
var customAccentByte = settingsCodecs.customAccentByte;
var nightSchemeEnabledCode = settingsCodecs.nightSchemeEnabledCode;
var nightCustomBgByte = settingsCodecs.nightCustomBgByte;
var nightCustomTextByte = settingsCodecs.nightCustomTextByte;
var nightCustomAccentByte = settingsCodecs.nightCustomAccentByte;
var isAnalogModeNow = settingsCodecs.isAnalogModeNow;
var bottomStyleCode = settingsCodecs.bottomStyleCode;
var sunMoonSizeCode = settingsCodecs.sunMoonSizeCode;
var skyModeCode = settingsCodecs.skyModeCode;
var weatherIconStyleCode = settingsCodecs.weatherIconStyleCode;
var aqiUnitCode = settingsCodecs.aqiUnitCode;
var altitudeUnitCode = settingsCodecs.altitudeUnitCode;
var shakeLabelSecondsCode = settingsCodecs.shakeLabelSecondsCode;
var labelStyleCode = settingsCodecs.labelStyleCode;
var shadowTranslucentCode = settingsCodecs.shadowTranslucentCode;
var shadowAngleCode = settingsCodecs.shadowAngleCode;
var bigAnalogMarkerStyleCode = settingsCodecs.bigAnalogMarkerStyleCode;
var bitmapMarkerTransparentCode = settingsCodecs.bitmapMarkerTransparentCode;
var drawFeaturesBeneathHandsCode = settingsCodecs.drawFeaturesBeneathHandsCode;
var clampInt = settingsCodecs.clampInt;
var customMarkerStyleCode = settingsCodecs.customMarkerStyleCode;
var customMarkerBorderCode = settingsCodecs.customMarkerBorderCode;
var customHourStyleCode = settingsCodecs.customHourStyleCode;
var customHourThicknessCode = settingsCodecs.customHourThicknessCode;
var customHourInnerThicknessCode = settingsCodecs.customHourInnerThicknessCode;
var customHourInnerEccCode = settingsCodecs.customHourInnerEccCode;
var customHourOuterEccCode = settingsCodecs.customHourOuterEccCode;
var customHourInnerBorderCode = settingsCodecs.customHourInnerBorderCode;
var customHourOuterBorderCode = settingsCodecs.customHourOuterBorderCode;
var customHourTranslucentCode = settingsCodecs.customHourTranslucentCode;
var customHourColorCode = settingsCodecs.customHourColorCode;
var customSecStyleCode = settingsCodecs.customSecStyleCode;
var customSecThicknessCode = settingsCodecs.customSecThicknessCode;
var customSecInnerThicknessCode = settingsCodecs.customSecInnerThicknessCode;
var customSecInnerEccCode = settingsCodecs.customSecInnerEccCode;
var customSecOuterEccCode = settingsCodecs.customSecOuterEccCode;
var customSecInnerBorderCode = settingsCodecs.customSecInnerBorderCode;
var customSecOuterBorderCode = settingsCodecs.customSecOuterBorderCode;
var customSecTranslucentCode = settingsCodecs.customSecTranslucentCode;
var customSecColorCode = settingsCodecs.customSecColorCode;
var markerTextTargetCode = settingsCodecs.markerTextTargetCode;
var markerTextFontCode = settingsCodecs.markerTextFontCode;
var markerTextOffsetCode = settingsCodecs.markerTextOffsetCode;
var markerTextHourMaskCode = settingsCodecs.markerTextHourMaskCode;
var markerTextSecMaskCode = settingsCodecs.markerTextSecMaskCode;
var markerTextRomanCode = settingsCodecs.markerTextRomanCode;
var handStyleFieldCode = settingsCodecs.handStyleFieldCode;
var handColorFieldCode = settingsCodecs.handColorFieldCode;
var handMainColorFieldCode = settingsCodecs.handMainColorFieldCode;
var handOutlineEnabledCode = settingsCodecs.handOutlineEnabledCode;
var handTranslucentCode = settingsCodecs.handTranslucentCode;
var handShadowEnabledCode = settingsCodecs.handShadowEnabledCode;
var handShadowDistanceCode = settingsCodecs.handShadowDistanceCode;
var handMiddleOffsetCode = settingsCodecs.handMiddleOffsetCode;
var handSecondaryWidthCode = settingsCodecs.handSecondaryWidthCode;
var handHollowCode = settingsCodecs.handHollowCode;
var handHollowThicknessCode = settingsCodecs.handHollowThicknessCode;
var handHourStyleCode = settingsCodecs.handHourStyleCode;
var handHourWidthCode = settingsCodecs.handHourWidthCode;
var handHourLengthCode = settingsCodecs.handHourLengthCode;
var handHourBackOffsetCode = settingsCodecs.handHourBackOffsetCode;
var handHourMiddleOffsetCode = settingsCodecs.handHourMiddleOffsetCode;
var handHourSecondaryWidthCode = settingsCodecs.handHourSecondaryWidthCode;
var handHourColorCode = settingsCodecs.handHourColorCode;
var handHourOutlineEnabledCode = settingsCodecs.handHourOutlineEnabledCode;
var handHourOutlineColorCode = settingsCodecs.handHourOutlineColorCode;
var handHourTranslucentCode = settingsCodecs.handHourTranslucentCode;
var handHourShadowEnabledCode = settingsCodecs.handHourShadowEnabledCode;
var handHourShadowDistanceCode = settingsCodecs.handHourShadowDistanceCode;
var handHourHollowCode = settingsCodecs.handHourHollowCode;
var handHourHollowThicknessCode = settingsCodecs.handHourHollowThicknessCode;
var handMinStyleCode = settingsCodecs.handMinStyleCode;
var handMinWidthCode = settingsCodecs.handMinWidthCode;
var handMinLengthCode = settingsCodecs.handMinLengthCode;
var handMinBackOffsetCode = settingsCodecs.handMinBackOffsetCode;
var handMinMiddleOffsetCode = settingsCodecs.handMinMiddleOffsetCode;
var handMinSecondaryWidthCode = settingsCodecs.handMinSecondaryWidthCode;
var handMinColorCode = settingsCodecs.handMinColorCode;
var handMinOutlineEnabledCode = settingsCodecs.handMinOutlineEnabledCode;
var handMinOutlineColorCode = settingsCodecs.handMinOutlineColorCode;
var handMinTranslucentCode = settingsCodecs.handMinTranslucentCode;
var handMinShadowEnabledCode = settingsCodecs.handMinShadowEnabledCode;
var handMinShadowDistanceCode = settingsCodecs.handMinShadowDistanceCode;
var handMinHollowCode = settingsCodecs.handMinHollowCode;
var handMinHollowThicknessCode = settingsCodecs.handMinHollowThicknessCode;
var handSecStyleCode = settingsCodecs.handSecStyleCode;
var handSecWidthCode = settingsCodecs.handSecWidthCode;
var handSecLengthCode = settingsCodecs.handSecLengthCode;
var handSecBackOffsetCode = settingsCodecs.handSecBackOffsetCode;
var handSecMiddleOffsetCode = settingsCodecs.handSecMiddleOffsetCode;
var handSecSecondaryWidthCode = settingsCodecs.handSecSecondaryWidthCode;
var handSecColorCode = settingsCodecs.handSecColorCode;
var handSecOutlineEnabledCode = settingsCodecs.handSecOutlineEnabledCode;
var handSecOutlineColorCode = settingsCodecs.handSecOutlineColorCode;
var handSecTranslucentCode = settingsCodecs.handSecTranslucentCode;
var handSecShadowEnabledCode = settingsCodecs.handSecShadowEnabledCode;
var handSecShadowDistanceCode = settingsCodecs.handSecShadowDistanceCode;
var handSecHollowCode = settingsCodecs.handSecHollowCode;
var handSecHollowThicknessCode = settingsCodecs.handSecHollowThicknessCode;
var centerCircleRadiusCode = settingsCodecs.centerCircleRadiusCode;
var centerCircleColorCode = settingsCodecs.centerCircleColorCode;
var dualContextSetting = settingsCodecs.dualContextSetting;
var analogEdgeAvailability = settingsCodecs.analogEdgeAvailability;
var digitalSideActive = settingsCodecs.digitalSideActive;
var dualContextVisible = settingsCodecs.dualContextVisible;
var upperMiddleLine1ContentCode = settingsCodecs.upperMiddleLine1ContentCode;
var upperMiddleLine1ColorModeCode = settingsCodecs.upperMiddleLine1ColorModeCode;
var upperMiddleLine2ContentCode = settingsCodecs.upperMiddleLine2ContentCode;
var upperMiddleLine2ColorModeCode = settingsCodecs.upperMiddleLine2ColorModeCode;
var bottomMiddleLine1ContentCode = settingsCodecs.bottomMiddleLine1ContentCode;
var bottomMiddleLine1ColorModeCode = settingsCodecs.bottomMiddleLine1ColorModeCode;
var bottomMiddleLine2ContentCode = settingsCodecs.bottomMiddleLine2ContentCode;
var bottomMiddleLine2ColorModeCode = settingsCodecs.bottomMiddleLine2ColorModeCode;
var middleLeftLine1ContentCode = settingsCodecs.middleLeftLine1ContentCode;
var middleLeftLine1ColorModeCode = settingsCodecs.middleLeftLine1ColorModeCode;
var middleLeftLine2ContentCode = settingsCodecs.middleLeftLine2ContentCode;
var middleLeftLine2ColorModeCode = settingsCodecs.middleLeftLine2ColorModeCode;
var middleRightLine1ContentCode = settingsCodecs.middleRightLine1ContentCode;
var middleRightLine1ColorModeCode = settingsCodecs.middleRightLine1ColorModeCode;
var middleRightLine2ContentCode = settingsCodecs.middleRightLine2ContentCode;
var middleRightLine2ColorModeCode = settingsCodecs.middleRightLine2ColorModeCode;
var showSunTimeCode = settingsCodecs.showSunTimeCode;
var showIssCode = settingsCodecs.showIssCode;
var showMajorStarsCode = settingsCodecs.showMajorStarsCode;
var auroraEnabledCode = settingsCodecs.auroraEnabledCode;
var vibrateOnPhaseChangeCode = settingsCodecs.vibrateOnPhaseChangeCode;
var drawDebugCode = settingsCodecs.drawDebugCode;
var hourlyVibeModeCode = settingsCodecs.hourlyVibeModeCode;
var hourlyVibeIntervalMinCode = settingsCodecs.hourlyVibeIntervalMinCode;
var hourlyVibePatternCode = settingsCodecs.hourlyVibePatternCode;
var minutesSinceMidnight = settingsCodecs.minutesSinceMidnight;
var hourlyVibeStartMinCode = settingsCodecs.hourlyVibeStartMinCode;
var hourlyVibeEndMinCode = settingsCodecs.hourlyVibeEndMinCode;
var hourlyVibeDaysMaskCode = settingsCodecs.hourlyVibeDaysMaskCode;
var hourlyVibeOverrideQuietCode = settingsCodecs.hourlyVibeOverrideQuietCode;
var startupClockAnimModeCode = settingsCodecs.startupClockAnimModeCode;
var bgAnimModeCode = settingsCodecs.bgAnimModeCode;
var shakeAnimModeCode = settingsCodecs.shakeAnimModeCode;
var outlineEnabledCode = settingsCodecs.outlineEnabledCode;
var batterySaverEnabledCode = settingsCodecs.batterySaverEnabledCode;
var cornerFontCode = settingsCodecs.cornerFontCode;
var cornerContentBytes = settingsCodecs.cornerContentBytes;
var cornerColorModeBytes = settingsCodecs.cornerColorModeBytes;
var dailyStepGoalValue = settingsCodecs.dailyStepGoalValue;


// ---- C: consolidated settings fields -------------------------------------
// 5 packed byte arrays replacing 86 individual keys -- see
// apply_consolidated_fields() in pebble-eclipse-watch.c for the C side
// (memcpy targets, struct layouts, and why MARKER_TEXT/COLORS are an
// explicit unpack rather than a memcpy there). Byte order below MUST
// match that function's expectations exactly -- these build each
// group in the wire order the C-side struct itself declares fields in,
// which is NOT the same order the old individual per-field keys used
// (HandConfig in particular reorders hollow/hollow_thickness relative
// to the old key list).

// Two's-complement byte for a signed value in [-128, 127] -- Pebble's
// byte-array wire format is raw unsigned bytes 0-255; the C side
// memcpy's these directly into (or casts them to) int8_t fields, which
// assumes standard two's complement (true on every platform this ships to).
// ---- message encoding + sending ------------------------------------------
//
// Moved to comms/message-encoder.js (communication subsystem extraction).
// TYPE_CODE, toEpoch, u16ArrayToBytes/i32ArrayToBytes, PLANET_ORDER,
// skyFieldsDict, issFieldsDict, extraWeatherFieldsDict, the settings
// *Bytes() assemblers, and populateSettingsFields moved with it -- all
// were private implementation details of these 5 functions.
var messageEncoder = require('./comms/message-encoder');
var sendFlatDict = messageEncoder.sendFlatDict;
var sendInvalid = messageEncoder.sendInvalid;
var sendEclipseData = messageEncoder.sendEclipseData;
var sendNoEclipseToday = messageEncoder.sendNoEclipseToday;
var buildFullKeysetDict = messageEncoder.buildFullKeysetDict;

// ---- refresh manager -------------------------------------------------
//
// Moved to refresh-manager.js (refresh manager extraction). Location
// lookup, smart-refresh skip/resend, the ISS/AQI/aurora fetches, and
// the main refresh cycle + its two timers all moved with it.
var refreshManager = require('./refresh-manager');
var refreshAndSend = refreshManager.refreshAndSend;
var scheduleRefresh = refreshManager.scheduleRefresh;
var startBatterySaverHourlyCheck = refreshManager.startBatterySaverHourlyCheck;

// ---- overhead objects (shake/"Planet Seek" view: ISS + nearby flights) --
var overheadObjects = require('./overhead-objects');
var overheadObjectsBytes = messageEncoder.overheadObjectsBytes;

// ---- Pebble lifecycle --------------------------------------------------

Pebble.addEventListener('ready', function () {
  console.log('eclipse-watch: PKJS ready, capabilities OK, starting first refresh');
  refreshAndSend(false, false);
  scheduleRefresh();
  startBatterySaverHourlyCheck();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e && e.payload && e.payload.REQUEST_UPDATE) {
    // The watch sends this both on every app launch/relaunch and on
    // a deliberate select-button press -- we can't tell which, so
    // this respects the smart-refresh skip too rather than treating
    // every watchface start as a reason to hit the network. That skip
    // still means "don't refetch," not "send nothing" though -- the
    // one true resendOnSkip caller: see resendLastFullData()/
    // refreshAndSend()'s own comments for why a plain do-nothing skip
    // here used to leave the watch stuck on "No data yet, waiting for
    // phone" (most visibly right after a watch app update wipes its
    // persisted data) until the skip window itself expired -- and for
    // why every OTHER caller of refreshAndSend() deliberately leaves
    // resendOnSkip false instead of also passing true here.
    refreshAndSend(false, true);
  }
  // Battery saver: the watch's own current awake/sleep/deep-sleep
  // phase, resent whenever it changes (and retried by the watch on
  // the next tick if a send ever fails) -- see setBatterySaverPhase()'s
  // own comment in refresh-manager.js for what it gates. Checked with
  // !== undefined rather than a truthy check like REQUEST_UPDATE above,
  // since 0 (awake) is a legitimate, meaningful value here, not "absent".
  if (e && e.payload && e.payload.BATTERY_SAVER_PHASE !== undefined) {
    refreshManager.setBatterySaverPhase(e.payload.BATTERY_SAVER_PHASE);
  }
  // Fired on a shake when the watch's own cached overhead-objects list
  // (ISS + flights, for the Planet Seek view) is missing or older than
  // 3 minutes -- see comms_maybe_request_flights() on the watch side.
  // Fetches are on-demand only, never on the regular refresh timer, so a
  // shake that lands while one's still in flight just gets nothing new
  // (the watch's own loading flag only clears once a reply arrives).
  if (e && e.payload && e.payload.REQUEST_FLIGHTS) {
    refreshManager.getLocation(function (err, lat, lon) {
      if (err) {
        console.log('eclipse-watch: overhead objects: no location - ' + err.message);
        sendFlatDict({ 'OVERHEAD_OBJECTS_COMPUTED_AT': Math.floor(Date.now() / 1000) });
        return;
      }
      var opts = {
        showIss: getSetting('CONFIG_SHOW_ISS', 'false') === 'true',
        showFlights: getSetting('CONFIG_SHOW_FLIGHTS', 'false') === 'true',
        flightsRadiusKm: parseInt(getSetting('CONFIG_FLIGHTS_RANGE_KM', '50'), 10) || 50,
        flightsApiKey: getSetting('CONFIG_FLIGHTS_API_KEY', '')
      };
      overheadObjects.buildOverheadObjectList(lat, lon, opts, function (objects) {
        sendFlatDict({
          'OVERHEAD_OBJECTS': overheadObjectsBytes(objects),
          'OVERHEAD_OBJECT_COUNT': objects.length,
          // Sent whether or not anything came back -- this is what clears
          // the watch's "loading" flag either way (comms_decoder.c).
          'OVERHEAD_OBJECTS_COMPUTED_AT': Math.floor(Date.now() / 1000)
        });
      });
    });
  }
});

Pebble.addEventListener('showConfiguration', function () {
  var html = configPage.buildConfigHtml({
    autoLoc: getSetting('CONFIG_AUTO_LOC', 'true') !== 'false',
    lat: getSetting('CONFIG_LAT', ''),
    lon: getSetting('CONFIG_LON', ''),
    locationName: getSetting('CONFIG_LOCATION_NAME', ''),
    owmKey: getSetting('CONFIG_OWM_KEY', ''),
    updateMins: getSetting('CONFIG_UPDATE_MINS', '20'),
    clockFont: getSetting('CONFIG_CLOCK_FONT', '8'),
    tempUnit: getSetting('CONFIG_TEMP_UNIT', 'C'),
    windSpeedUnit: getSetting('CONFIG_WIND_SPEED_UNIT', 'kmh'),
    showSeconds: getSetting('CONFIG_SHOW_SECONDS', 'false') === 'true',
    customBg: getSetting('CONFIG_CUSTOM_BG', '255'),
    customText: getSetting('CONFIG_CUSTOM_TEXT', '192'),
    customAccent: getSetting('CONFIG_CUSTOM_ACCENT', '192'),
    nightEnabled: getSetting('CONFIG_NIGHT_ENABLED', 'false') === 'true',
    nightCustomBg: getSetting('CONFIG_NIGHT_CUSTOM_BG', '192'),
    nightCustomText: getSetting('CONFIG_NIGHT_CUSTOM_TEXT', '255'),
    nightCustomAccent: getSetting('CONFIG_NIGHT_CUSTOM_ACCENT', '255'),
    bottomStyle: getSetting('CONFIG_BOTTOM_STYLE', 'digital'),
    digitalSides: getSetting('CONFIG_DIGITAL_SIDES', 'none'),
    digitalSidesPreferred: getSetting('CONFIG_DIGITAL_SIDES_PREFERRED', getSetting('CONFIG_DIGITAL_SIDES', 'none')),
    sunMoonSize: getSetting('CONFIG_SUN_MOON_SIZE', '75'),
    skyMode: getSetting('CONFIG_SKY_MODE', '0'),
    weatherIconStyle: getSetting('CONFIG_WEATHER_ICON_STYLE', '1'),
    aqiUnit: getSetting('CONFIG_AQI_UNIT', '0'),
    altitudeUnit: getSetting('CONFIG_ALTITUDE_UNIT', '0'),
    shakeLabelSeconds: getSetting('CONFIG_SHAKE_LABEL_SECONDS', '3'),
    labelStyle: getSetting('CONFIG_LABEL_STYLE', '0'),
    shadowTranslucent: getSetting('CONFIG_SHADOW_TRANSLUCENT', 'true'),
    shadowAngle: getSetting('CONFIG_SHADOW_ANGLE', '120'),
    bigAnalogMarkerStyle: getSetting('CONFIG_BIG_ANALOG_MARKER_STYLE', '0'),
    bitmapMarkerTransparent: getSetting('CONFIG_BITMAP_MARKER_TRANSPARENT', 'false') === 'true',
    // Browser-side only -- the watch never needs this. By the time
    // corner_content[] reaches it, the settings page has already
    // either cleared those slots to 0 (override off) or left the
    // user's real picks in place (override on); features_layer.c just
    // draws whatever content id it's given either way. Persisted the
    // same way as every other setting (getSetting/setSetting) purely
    // so the checkbox itself doesn't reset every time the page reopens.
    bitmapCornerOverride: getSetting('CONFIG_BITMAP_CORNER_OVERRIDE', 'false') === 'true',
    drawFeaturesBeneathHands: getSetting('CONFIG_DRAW_FEATURES_BENEATH_HANDS', 'false') === 'true',
    customHourStyle: getSetting('CONFIG_CUSTOM_HOUR_STYLE', '0'),
    customHourThickness: getSetting('CONFIG_CUSTOM_HOUR_THICKNESS', '3'),
    customHourInnerThickness: getSetting('CONFIG_CUSTOM_HOUR_INNER_THICKNESS', '3'),
    customHourInnerEcc: getSetting('CONFIG_CUSTOM_HOUR_INNER_ECC', '0'),
    customHourOuterEcc: getSetting('CONFIG_CUSTOM_HOUR_OUTER_ECC', '0'),
    customHourInnerBorder: getSetting('CONFIG_CUSTOM_HOUR_INNER_BORDER', '20'),
    customHourOuterBorder: getSetting('CONFIG_CUSTOM_HOUR_OUTER_BORDER', '100'),
    customHourTranslucent: getSetting('CONFIG_CUSTOM_HOUR_TRANSLUCENT', 'false'),
    customHourColor: getSetting('CONFIG_CUSTOM_HOUR_COLOR', '0'),
    customSecStyle: getSetting('CONFIG_CUSTOM_SEC_STYLE', '0'),
    customSecThickness: getSetting('CONFIG_CUSTOM_SEC_THICKNESS', '1'),
    customSecInnerThickness: getSetting('CONFIG_CUSTOM_SEC_INNER_THICKNESS', '1'),
    customSecInnerEcc: getSetting('CONFIG_CUSTOM_SEC_INNER_ECC', '0'),
    customSecOuterEcc: getSetting('CONFIG_CUSTOM_SEC_OUTER_ECC', '0'),
    customSecInnerBorder: getSetting('CONFIG_CUSTOM_SEC_INNER_BORDER', '70'),
    customSecOuterBorder: getSetting('CONFIG_CUSTOM_SEC_OUTER_BORDER', '100'),
    customSecTranslucent: getSetting('CONFIG_CUSTOM_SEC_TRANSLUCENT', 'false'),
    customSecColor: getSetting('CONFIG_CUSTOM_SEC_COLOR', '0'),
    markerTextTarget: getSetting('CONFIG_MARKER_TEXT_TARGET', '0'),
    markerTextFont: getSetting('CONFIG_MARKER_TEXT_FONT', '0'),
    markerTextOffset: getSetting('CONFIG_MARKER_TEXT_OFFSET', '0'),
    markerTextHourMask: getSetting('CONFIG_MARKER_TEXT_HOUR_MASK', '4095'),
    markerTextSecMask: getSetting('CONFIG_MARKER_TEXT_SEC_MASK', '4095'),
    markerTextRoman: getSetting('CONFIG_MARKER_TEXT_ROMAN', 'false'),
    handHourStyle: getSetting('CONFIG_HAND_HOUR_STYLE', '1'),
    handHourWidth: getSetting('CONFIG_HAND_HOUR_WIDTH', '12'),
    handHourLength: getSetting('CONFIG_HAND_HOUR_LENGTH', '51'),
    handHourBackOffset: getSetting('CONFIG_HAND_HOUR_BACK_OFFSET', '0'),
    handHourMiddleOffset: getSetting('CONFIG_HAND_HOUR_MIDDLE_OFFSET', '0'),
    handHourSecondaryWidth: getSetting('CONFIG_HAND_HOUR_SECONDARY_WIDTH', '6'),
    handHourColor: getSetting('CONFIG_HAND_HOUR_COLOR', '0'),
    handHourOutlineEnabled: getSetting('CONFIG_HAND_HOUR_OUTLINE_ENABLED', 'false'),
    handHourOutlineColor: getSetting('CONFIG_HAND_HOUR_OUTLINE_COLOR', '0'),
    handHourTranslucent: getSetting('CONFIG_HAND_HOUR_TRANSLUCENT', 'false'),
    handHourShadowEnabled: getSetting('CONFIG_HAND_HOUR_SHADOW_ENABLED', 'false'),
    handHourShadowDistance: getSetting('CONFIG_HAND_HOUR_SHADOW_DISTANCE', '2'),
    handHourHollow: getSetting('CONFIG_HAND_HOUR_HOLLOW', 'false'),
    handHourHollowThickness: getSetting('CONFIG_HAND_HOUR_HOLLOW_THICKNESS', '1'),
    handMinStyle: getSetting('CONFIG_HAND_MIN_STYLE', '1'),
    handMinWidth: getSetting('CONFIG_HAND_MIN_WIDTH', '18'),
    handMinLength: getSetting('CONFIG_HAND_MIN_LENGTH', '78'),
    handMinBackOffset: getSetting('CONFIG_HAND_MIN_BACK_OFFSET', '0'),
    handMinMiddleOffset: getSetting('CONFIG_HAND_MIN_MIDDLE_OFFSET', '0'),
    handMinSecondaryWidth: getSetting('CONFIG_HAND_MIN_SECONDARY_WIDTH', '6'),
    handMinColor: getSetting('CONFIG_HAND_MIN_COLOR', '0'),
    handMinOutlineEnabled: getSetting('CONFIG_HAND_MIN_OUTLINE_ENABLED', 'false'),
    handMinOutlineColor: getSetting('CONFIG_HAND_MIN_OUTLINE_COLOR', '0'),
    handMinTranslucent: getSetting('CONFIG_HAND_MIN_TRANSLUCENT', 'false'),
    handMinShadowEnabled: getSetting('CONFIG_HAND_MIN_SHADOW_ENABLED', 'false'),
    handMinShadowDistance: getSetting('CONFIG_HAND_MIN_SHADOW_DISTANCE', '2'),
    handMinHollow: getSetting('CONFIG_HAND_MIN_HOLLOW', 'false'),
    handMinHollowThickness: getSetting('CONFIG_HAND_MIN_HOLLOW_THICKNESS', '1'),
    handSecStyle: getSetting('CONFIG_HAND_SEC_STYLE', '0'),
    handSecWidth: getSetting('CONFIG_HAND_SEC_WIDTH', '2'),
    handSecLength: getSetting('CONFIG_HAND_SEC_LENGTH', '85'),
    handSecBackOffset: getSetting('CONFIG_HAND_SEC_BACK_OFFSET', '0'),
    handSecMiddleOffset: getSetting('CONFIG_HAND_SEC_MIDDLE_OFFSET', '0'),
    handSecSecondaryWidth: getSetting('CONFIG_HAND_SEC_SECONDARY_WIDTH', '6'),
    handSecColor: getSetting('CONFIG_HAND_SEC_COLOR', '1'),
    handSecOutlineEnabled: getSetting('CONFIG_HAND_SEC_OUTLINE_ENABLED', 'false'),
    handSecOutlineColor: getSetting('CONFIG_HAND_SEC_OUTLINE_COLOR', '0'),
    handSecTranslucent: getSetting('CONFIG_HAND_SEC_TRANSLUCENT', 'false'),
    handSecShadowEnabled: getSetting('CONFIG_HAND_SEC_SHADOW_ENABLED', 'false'),
    handSecShadowDistance: getSetting('CONFIG_HAND_SEC_SHADOW_DISTANCE', '2'),
    handSecHollow: getSetting('CONFIG_HAND_SEC_HOLLOW', 'false'),
    handSecHollowThickness: getSetting('CONFIG_HAND_SEC_HOLLOW_THICKNESS', '1'),
    centerCircleRadius: getSetting('CONFIG_CENTER_CIRCLE_RADIUS', '3'),
    centerCircleColor: getSetting('CONFIG_CENTER_CIRCLE_COLOR', '0'),
    // Dual-context fields (see dualContextSetting()'s own comment):
    // exposes BOTH persisted halves, not just whichever one matches
    // the CURRENT layout -- config-page.js needs both up front so it
    // can swap between them live if the user switches layouts while
    // the page stays open, without a round trip back here.
    upperMiddleLine1ContentAnalog: getSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT_ANALOG', getSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT', '0')),
    upperMiddleLine1ContentDigital: getSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT_DIGITAL', getSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT', '0')),
    upperMiddleLine1ColorAnalog: getSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR_ANALOG', getSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR', '0')),
    upperMiddleLine1ColorDigital: getSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR_DIGITAL', getSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR', '0')),
    upperMiddleLine2ContentAnalog: getSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT_ANALOG', getSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT', '0')),
    upperMiddleLine2ContentDigital: getSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT_DIGITAL', getSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT', '0')),
    upperMiddleLine2ColorAnalog: getSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR_ANALOG', getSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR', '0')),
    upperMiddleLine2ColorDigital: getSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR_DIGITAL', getSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR', '0')),
    bottomMiddleLine1ContentAnalog: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_ANALOG', getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT', '105')),
    bottomMiddleLine1ContentDigital: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_DIGITAL', getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT', '105')),
    bottomMiddleLine1ColorAnalog: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_ANALOG', getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR', '0')),
    bottomMiddleLine1ColorDigital: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_DIGITAL', getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR', '0')),
    // Analog-only -- a single plain value, not a dual-context pair (see
    // bottomMiddleLine2ContentCode()'s own comment).
    bottomMiddleLine2Content: getSetting('CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT', '0'),
    bottomMiddleLine2Color: getSetting('CONFIG_BOTTOM_MIDDLE_LINE2_COLOR', '0'),
    middleLeftLine1ContentAnalog: getSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT_ANALOG', getSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT', '0')),
    middleLeftLine1ContentDigital: getSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT_DIGITAL', getSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT', '0')),
    middleLeftLine1ColorAnalog: getSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR_ANALOG', getSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR', '0')),
    middleLeftLine1ColorDigital: getSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR_DIGITAL', getSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR', '0')),
    middleLeftLine2ContentAnalog: getSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT_ANALOG', getSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT', '0')),
    middleLeftLine2ContentDigital: getSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT_DIGITAL', getSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT', '0')),
    middleLeftLine2ColorAnalog: getSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR_ANALOG', getSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR', '0')),
    middleLeftLine2ColorDigital: getSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR_DIGITAL', getSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR', '0')),
    middleRightLine1ContentAnalog: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_ANALOG', getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT', '0')),
    middleRightLine1ContentDigital: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_DIGITAL', getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT', '0')),
    middleRightLine1ColorAnalog: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR_ANALOG', getSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR', '0')),
    middleRightLine1ColorDigital: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR_DIGITAL', getSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR', '0')),
    middleRightLine2ContentAnalog: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_ANALOG', getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT', '0')),
    middleRightLine2ContentDigital: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_DIGITAL', getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT', '0')),
    middleRightLine2ColorAnalog: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR_ANALOG', getSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR', '0')),
    middleRightLine2ColorDigital: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR_DIGITAL', getSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR', '0')),
    cornerTL: getSetting('CONFIG_CORNER_TL', '0'),
    cornerTR: getSetting('CONFIG_CORNER_TR', '0'),
    cornerBL: getSetting('CONFIG_CORNER_BL', '0'),
    cornerBR: getSetting('CONFIG_CORNER_BR', '0'),
    cornerTLColor: getSetting('CONFIG_CORNER_TL_COLOR', '0'),
    cornerTRColor: getSetting('CONFIG_CORNER_TR_COLOR', '0'),
    cornerBLColor: getSetting('CONFIG_CORNER_BL_COLOR', '0'),
    cornerBRColor: getSetting('CONFIG_CORNER_BR_COLOR', '0'),
    stepGoal: getSetting('CONFIG_STEP_GOAL', '10000'),
    showSunTime: getSetting('CONFIG_SHOW_SUN_TIME', 'false') === 'true',
    showIss: getSetting('CONFIG_SHOW_ISS', 'false') === 'true',
    showFlights: getSetting('CONFIG_SHOW_FLIGHTS', 'false') === 'true',
    flightsRangeKm: getSetting('CONFIG_FLIGHTS_RANGE_KM', '50'),
    flightsApiKey: getSetting('CONFIG_FLIGHTS_API_KEY', ''),
    showMajorStars: getSetting('CONFIG_SHOW_MAJOR_STARS', 'true') === 'true',
    auroraEnabled: getSetting('CONFIG_AURORA_ENABLED', 'false') === 'true',
    vibrateOnPhaseChange: getSetting('CONFIG_VIBRATE_ON_PHASE_CHANGE', 'false') === 'true',
    startupClockAnimMode: getSetting('CONFIG_STARTUP_CLOCK_ANIM_MODE', '1'),
    bgAnimMode: getSetting('CONFIG_BG_ANIM_MODE', '0'),
    shakeAnimMode: getSetting('CONFIG_SHAKE_ANIM_MODE', '0'),
    outlineStyle: String(outlineEnabledCode()),
    batterySaverEnabled: getSetting('CONFIG_BATTERY_SAVER_ENABLED', 'false') === 'true',
    cornerFont: getSetting('CONFIG_CORNER_FONT', '1'),
    testMode: getSetting('CONFIG_TEST_MODE', 'false') === 'true',
    testDateTime: getSetting('CONFIG_TEST_DATETIME', ''),
    drawDebug: getSetting('CONFIG_DRAW_DEBUG', 'false') === 'true',
    hourlyVibeMode: getSetting('CONFIG_HOURLY_VIBE_MODE', '0'),
    hourlyVibeIntervalMin: getSetting('CONFIG_HOURLY_VIBE_INTERVAL_MIN', '30'),
    hourlyVibePattern: getSetting('CONFIG_HOURLY_VIBE_PATTERN', '0'),
    hourlyVibeStartTime: getSetting('CONFIG_HOURLY_VIBE_START_TIME', '00:00'),
    hourlyVibeEndTime: getSetting('CONFIG_HOURLY_VIBE_END_TIME', '00:00'),
    hourlyVibeDaysMask: getSetting('CONFIG_HOURLY_VIBE_DAYS_MASK', '127'),
    hourlyVibeOverrideQuiet: getSetting('CONFIG_HOURLY_VIBE_OVERRIDE_QUIET', 'true') === 'true',
    // Last RAW_MESSAGE_LOG_MAX individual chunks actually sent -- see
    // recordRawMessage() near the top of this file. Newest last (the
    // order they were recorded in); the settings page itself is what
    // shows them newest-first.
    rawMessageLog: (function () {
      try {
        var raw = localStorage.getItem('RAW_MESSAGE_LOG');
        var parsed = raw ? JSON.parse(raw) : [];
        return Array.isArray(parsed) ? parsed : [];
      } catch (e) {
        return [];
      }
    })(),
    debugOverrideEnabled: getSetting('CONFIG_DEBUG_OVERRIDE_ENABLED', 'false') === 'true',
    debugOverrideData: getSetting('CONFIG_DEBUG_OVERRIDE_DATA', ''),
    // Every key the watch could currently receive, pre-filled with
    // real current values -- see buildFullKeysetDict()'s own comment.
    // Computed fresh every time the settings page opens (cheap, no
    // network fetch of its own), not cached/persisted anywhere.
    fullKeysetJson: JSON.stringify(buildFullKeysetDict(), null, 2),
    serviceLogs: servicelog.snapshotAll(),
    presetSlot1Name: getSetting('CONFIG_PRESET_1_NAME', ''),
    presetSlot1Json: getSetting('CONFIG_PRESET_1_JSON', ''),
    presetSlot1Image: getSetting('CONFIG_PRESET_1_IMAGE', ''),
    presetSlot2Name: getSetting('CONFIG_PRESET_2_NAME', ''),
    presetSlot2Json: getSetting('CONFIG_PRESET_2_JSON', ''),
    presetSlot2Image: getSetting('CONFIG_PRESET_2_IMAGE', ''),
    presetSlot3Name: getSetting('CONFIG_PRESET_3_NAME', ''),
    presetSlot3Json: getSetting('CONFIG_PRESET_3_JSON', ''),
    presetSlot3Image: getSetting('CONFIG_PRESET_3_IMAGE', ''),
    presetSlot4Name: getSetting('CONFIG_PRESET_4_NAME', ''),
    presetSlot4Json: getSetting('CONFIG_PRESET_4_JSON', ''),
    presetSlot4Image: getSetting('CONFIG_PRESET_4_IMAGE', ''),
    presetSlot5Name: getSetting('CONFIG_PRESET_5_NAME', ''),
    presetSlot5Json: getSetting('CONFIG_PRESET_5_JSON', ''),
    presetSlot5Image: getSetting('CONFIG_PRESET_5_IMAGE', ''),
    presetSlot6Name: getSetting('CONFIG_PRESET_6_NAME', ''),
    presetSlot6Json: getSetting('CONFIG_PRESET_6_JSON', ''),
    presetSlot6Image: getSetting('CONFIG_PRESET_6_IMAGE', '')
  });
  // Classic no-server config page: the whole thing is a data: URI, no
  // hosting required. The page reads a `return_to` query param that
  // the runtime appends (falling back to pebblejs://close# per
  // Pebble's manual-setup guide) and navigates there with the
  // settings JSON, which 'webviewclosed' below picks up as
  // e.response.
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var raw = e.response;
  // Some platforms deliver e.response already decoded, others don't;
  // decodeURIComponent is a no-op on already-plain JSON so this is safe.
  try {
    raw = decodeURIComponent(raw);
  } catch (err) {
    // fall through and try to parse whatever we were given
  }

  var settings;
  try {
    settings = JSON.parse(raw);
  } catch (err) {
    console.log('eclipse-watch: failed to parse settings response: ' + err.message);
    return;
  }

  // Debug-only direct send (see config-page.js's "full keyset" window
  // and its own Send button): takes exactly the -- possibly hand-
  // edited -- full keyset already sitting in that textarea and sends
  // it chunked to the watch as-is, overriding whatever the normal
  // settings-save flow below would otherwise have computed and sent
  // instead. A completely separate action from an ordinary Save (that
  // button doesn't set this flag at all), so it deliberately returns
  // here rather than falling through into any of the normal
  // setSetting()/sendFlatDict()/refreshAndSend() calls below --
  // nothing about this send is persisted, and no other setting
  // changes.
  if (settings.CONFIG_SEND_FULL_KEYSET) {
    try {
      var keyset = JSON.parse(settings.CONFIG_FULL_KEYSET_DATA);
      if (keyset && typeof keyset === 'object') {
        enqueueFlatDict(keyset);
        console.log('eclipse-watch: full keyset debug send queued');
      } else {
        console.log('eclipse-watch: full keyset debug send skipped -- not a JSON object');
      }
    } catch (err) {
      console.log('eclipse-watch: full keyset debug send failed to parse: ' + err.message);
    }
    return;
  }

  setSetting('CONFIG_AUTO_LOC', settings.CONFIG_AUTO_LOC ? 'true' : 'false');
  setSetting('CONFIG_LAT', settings.CONFIG_LAT || '');
  setSetting('CONFIG_LON', settings.CONFIG_LON || '');
  // Cached reverse-geocoded (or searched-for) display name for the
  // manual lat/lon above -- purely for the settings page's own
  // "Location" section sub-header (see buildConfigHtml()'s own
  // comment), so it can show "Use Innsbruck, Austria" instead of raw
  // coordinates without re-resolving them over the network every
  // single time the page opens. Cleared browser-side whenever the
  // person edits lat/lon by hand until it's re-resolved.
  setSetting('CONFIG_LOCATION_NAME', settings.CONFIG_LOCATION_NAME || '');
  setSetting('CONFIG_OWM_KEY', settings.CONFIG_OWM_KEY || '');
  setSetting('CONFIG_UPDATE_MINS', settings.CONFIG_UPDATE_MINS || '20');
  setSetting('CONFIG_CLOCK_FONT', settings.CONFIG_CLOCK_FONT);
  setSetting('CONFIG_TEMP_UNIT', (settings.CONFIG_TEMP_UNIT === 'F' || settings.CONFIG_TEMP_UNIT === 'K') ? settings.CONFIG_TEMP_UNIT : 'C');
  setSetting('CONFIG_WIND_SPEED_UNIT', settings.CONFIG_WIND_SPEED_UNIT || 'kmh');
  setSetting('CONFIG_SHOW_SECONDS', settings.CONFIG_SHOW_SECONDS ? 'true' : 'false');
  setSetting('CONFIG_CUSTOM_BG', settings.CONFIG_CUSTOM_BG || '255');
  setSetting('CONFIG_CUSTOM_TEXT', settings.CONFIG_CUSTOM_TEXT || '192');
  setSetting('CONFIG_CUSTOM_ACCENT', settings.CONFIG_CUSTOM_ACCENT || '192');
  setSetting('CONFIG_NIGHT_ENABLED', settings.CONFIG_NIGHT_ENABLED ? 'true' : 'false');
  setSetting('CONFIG_NIGHT_CUSTOM_BG', settings.CONFIG_NIGHT_CUSTOM_BG || '192');
  setSetting('CONFIG_NIGHT_CUSTOM_TEXT', settings.CONFIG_NIGHT_CUSTOM_TEXT || '255');
  setSetting('CONFIG_NIGHT_CUSTOM_ACCENT', settings.CONFIG_NIGHT_CUSTOM_ACCENT || '255');
  setSetting('CONFIG_BOTTOM_STYLE', (settings.CONFIG_BOTTOM_STYLE === 'analog' || settings.CONFIG_BOTTOM_STYLE === 'biganalog') ? 'analog'
    : (settings.CONFIG_BOTTOM_STYLE === 'digitalTop' ? 'digitalTop'
    : (settings.CONFIG_BOTTOM_STYLE === 'bigDigital' ? 'bigDigital'
    : (settings.CONFIG_BOTTOM_STYLE === 'grid' ? 'grid' : 'digital'))));
  setSetting('CONFIG_DIGITAL_SIDES', settings.CONFIG_DIGITAL_SIDES || 'none');
  // The user's underlying preference, independent of whatever the
  // current font/layout collapsed CONFIG_DIGITAL_SIDES itself down to
  // -- see config-page.js's own digitalSidesVal/digitalSidesPreferred
  // split for the full reasoning. Falls back to CONFIG_DIGITAL_SIDES
  // itself only if the page somehow didn't send this (an old cached
  // config-page.js bundle, say), rather than silently defaulting to
  // "none" and losing whatever the effective value already showed.
  setSetting('CONFIG_DIGITAL_SIDES_PREFERRED', settings.CONFIG_DIGITAL_SIDES_PREFERRED || settings.CONFIG_DIGITAL_SIDES || 'none');
  setSetting('CONFIG_SUN_MOON_SIZE', settings.CONFIG_SUN_MOON_SIZE || '100');
  setSetting('CONFIG_SKY_MODE', settings.CONFIG_SKY_MODE || '0');
  setSetting('CONFIG_WEATHER_ICON_STYLE', settings.CONFIG_WEATHER_ICON_STYLE || '1');
  setSetting('CONFIG_AQI_UNIT', settings.CONFIG_AQI_UNIT || '0');
  setSetting('CONFIG_ALTITUDE_UNIT', settings.CONFIG_ALTITUDE_UNIT || '0');
  setSetting('CONFIG_SHAKE_LABEL_SECONDS', settings.CONFIG_SHAKE_LABEL_SECONDS || '3');
  setSetting('CONFIG_LABEL_STYLE', settings.CONFIG_LABEL_STYLE || '0');
  setSetting('CONFIG_SHADOW_TRANSLUCENT', settings.CONFIG_SHADOW_TRANSLUCENT || 'true');
  setSetting('CONFIG_SHADOW_ANGLE', settings.CONFIG_SHADOW_ANGLE || '120');
  setSetting('CONFIG_BIG_ANALOG_MARKER_STYLE', settings.CONFIG_BIG_ANALOG_MARKER_STYLE || '0');
  setSetting('CONFIG_BITMAP_MARKER_TRANSPARENT', settings.CONFIG_BITMAP_MARKER_TRANSPARENT ? 'true' : 'false');
  setSetting('CONFIG_BITMAP_CORNER_OVERRIDE', settings.CONFIG_BITMAP_CORNER_OVERRIDE ? 'true' : 'false');
  setSetting('CONFIG_DRAW_FEATURES_BENEATH_HANDS', settings.CONFIG_DRAW_FEATURES_BENEATH_HANDS ? 'true' : 'false');
  setSetting('CONFIG_CUSTOM_HOUR_STYLE', settings.CONFIG_CUSTOM_HOUR_STYLE || '0');
  setSetting('CONFIG_CUSTOM_HOUR_THICKNESS', settings.CONFIG_CUSTOM_HOUR_THICKNESS || '3');
  setSetting('CONFIG_CUSTOM_HOUR_INNER_THICKNESS', settings.CONFIG_CUSTOM_HOUR_INNER_THICKNESS || '3');
  setSetting('CONFIG_CUSTOM_HOUR_INNER_ECC', settings.CONFIG_CUSTOM_HOUR_INNER_ECC || '0');
  setSetting('CONFIG_CUSTOM_HOUR_OUTER_ECC', settings.CONFIG_CUSTOM_HOUR_OUTER_ECC || '0');
  setSetting('CONFIG_CUSTOM_HOUR_INNER_BORDER', settings.CONFIG_CUSTOM_HOUR_INNER_BORDER || '20');
  setSetting('CONFIG_CUSTOM_HOUR_OUTER_BORDER', settings.CONFIG_CUSTOM_HOUR_OUTER_BORDER || '100');
  setSetting('CONFIG_CUSTOM_HOUR_TRANSLUCENT', settings.CONFIG_CUSTOM_HOUR_TRANSLUCENT ? 'true' : 'false');
  setSetting('CONFIG_CUSTOM_HOUR_COLOR', settings.CONFIG_CUSTOM_HOUR_COLOR || '0');
  setSetting('CONFIG_CUSTOM_SEC_STYLE', settings.CONFIG_CUSTOM_SEC_STYLE || '0');
  setSetting('CONFIG_CUSTOM_SEC_THICKNESS', settings.CONFIG_CUSTOM_SEC_THICKNESS || '1');
  setSetting('CONFIG_CUSTOM_SEC_INNER_THICKNESS', settings.CONFIG_CUSTOM_SEC_INNER_THICKNESS || '1');
  setSetting('CONFIG_CUSTOM_SEC_INNER_ECC', settings.CONFIG_CUSTOM_SEC_INNER_ECC || '0');
  setSetting('CONFIG_CUSTOM_SEC_OUTER_ECC', settings.CONFIG_CUSTOM_SEC_OUTER_ECC || '0');
  setSetting('CONFIG_CUSTOM_SEC_INNER_BORDER', settings.CONFIG_CUSTOM_SEC_INNER_BORDER || '70');
  setSetting('CONFIG_CUSTOM_SEC_OUTER_BORDER', settings.CONFIG_CUSTOM_SEC_OUTER_BORDER || '100');
  setSetting('CONFIG_CUSTOM_SEC_TRANSLUCENT', settings.CONFIG_CUSTOM_SEC_TRANSLUCENT ? 'true' : 'false');
  setSetting('CONFIG_CUSTOM_SEC_COLOR', settings.CONFIG_CUSTOM_SEC_COLOR || '0');
  setSetting('CONFIG_MARKER_TEXT_TARGET', settings.CONFIG_MARKER_TEXT_TARGET || '0');
  setSetting('CONFIG_MARKER_TEXT_FONT', settings.CONFIG_MARKER_TEXT_FONT || '0');
  setSetting('CONFIG_MARKER_TEXT_OFFSET', settings.CONFIG_MARKER_TEXT_OFFSET || '0');
  setSetting('CONFIG_MARKER_TEXT_HOUR_MASK', settings.CONFIG_MARKER_TEXT_HOUR_MASK || '4095');
  setSetting('CONFIG_MARKER_TEXT_SEC_MASK', settings.CONFIG_MARKER_TEXT_SEC_MASK || '4095');
  setSetting('CONFIG_MARKER_TEXT_ROMAN', settings.CONFIG_MARKER_TEXT_ROMAN ? 'true' : 'false');
  setSetting('CONFIG_HAND_HOUR_STYLE', settings.CONFIG_HAND_HOUR_STYLE || '1');
  setSetting('CONFIG_HAND_HOUR_WIDTH', settings.CONFIG_HAND_HOUR_WIDTH || '12');
  setSetting('CONFIG_HAND_HOUR_LENGTH', settings.CONFIG_HAND_HOUR_LENGTH || '51');
  setSetting('CONFIG_HAND_HOUR_BACK_OFFSET', settings.CONFIG_HAND_HOUR_BACK_OFFSET || '0');
  setSetting('CONFIG_HAND_HOUR_MIDDLE_OFFSET', settings.CONFIG_HAND_HOUR_MIDDLE_OFFSET || '0');
  setSetting('CONFIG_HAND_HOUR_SECONDARY_WIDTH', settings.CONFIG_HAND_HOUR_SECONDARY_WIDTH || '6');
  setSetting('CONFIG_HAND_HOUR_COLOR', settings.CONFIG_HAND_HOUR_COLOR || '0');
  setSetting('CONFIG_HAND_HOUR_OUTLINE_ENABLED', settings.CONFIG_HAND_HOUR_OUTLINE_ENABLED || 'false');
  setSetting('CONFIG_HAND_HOUR_OUTLINE_COLOR', settings.CONFIG_HAND_HOUR_OUTLINE_COLOR || '0');
  setSetting('CONFIG_HAND_HOUR_TRANSLUCENT', settings.CONFIG_HAND_HOUR_TRANSLUCENT || 'false');
  setSetting('CONFIG_HAND_HOUR_SHADOW_ENABLED', settings.CONFIG_HAND_HOUR_SHADOW_ENABLED || 'false');
  setSetting('CONFIG_HAND_HOUR_SHADOW_DISTANCE', settings.CONFIG_HAND_HOUR_SHADOW_DISTANCE || '2');
  setSetting('CONFIG_HAND_HOUR_HOLLOW', settings.CONFIG_HAND_HOUR_HOLLOW || 'false');
  setSetting('CONFIG_HAND_HOUR_HOLLOW_THICKNESS', settings.CONFIG_HAND_HOUR_HOLLOW_THICKNESS || '1');
  setSetting('CONFIG_HAND_MIN_STYLE', settings.CONFIG_HAND_MIN_STYLE || '1');
  setSetting('CONFIG_HAND_MIN_WIDTH', settings.CONFIG_HAND_MIN_WIDTH || '18');
  setSetting('CONFIG_HAND_MIN_LENGTH', settings.CONFIG_HAND_MIN_LENGTH || '78');
  setSetting('CONFIG_HAND_MIN_BACK_OFFSET', settings.CONFIG_HAND_MIN_BACK_OFFSET || '0');
  setSetting('CONFIG_HAND_MIN_MIDDLE_OFFSET', settings.CONFIG_HAND_MIN_MIDDLE_OFFSET || '0');
  setSetting('CONFIG_HAND_MIN_SECONDARY_WIDTH', settings.CONFIG_HAND_MIN_SECONDARY_WIDTH || '6');
  setSetting('CONFIG_HAND_MIN_COLOR', settings.CONFIG_HAND_MIN_COLOR || '0');
  setSetting('CONFIG_HAND_MIN_OUTLINE_ENABLED', settings.CONFIG_HAND_MIN_OUTLINE_ENABLED || 'false');
  setSetting('CONFIG_HAND_MIN_OUTLINE_COLOR', settings.CONFIG_HAND_MIN_OUTLINE_COLOR || '0');
  setSetting('CONFIG_HAND_MIN_TRANSLUCENT', settings.CONFIG_HAND_MIN_TRANSLUCENT || 'false');
  setSetting('CONFIG_HAND_MIN_SHADOW_ENABLED', settings.CONFIG_HAND_MIN_SHADOW_ENABLED || 'false');
  setSetting('CONFIG_HAND_MIN_SHADOW_DISTANCE', settings.CONFIG_HAND_MIN_SHADOW_DISTANCE || '2');
  setSetting('CONFIG_HAND_MIN_HOLLOW', settings.CONFIG_HAND_MIN_HOLLOW || 'false');
  setSetting('CONFIG_HAND_MIN_HOLLOW_THICKNESS', settings.CONFIG_HAND_MIN_HOLLOW_THICKNESS || '1');
  setSetting('CONFIG_HAND_SEC_STYLE', settings.CONFIG_HAND_SEC_STYLE || '0');
  setSetting('CONFIG_HAND_SEC_WIDTH', settings.CONFIG_HAND_SEC_WIDTH || '2');
  setSetting('CONFIG_HAND_SEC_LENGTH', settings.CONFIG_HAND_SEC_LENGTH || '85');
  setSetting('CONFIG_HAND_SEC_BACK_OFFSET', settings.CONFIG_HAND_SEC_BACK_OFFSET || '0');
  setSetting('CONFIG_HAND_SEC_MIDDLE_OFFSET', settings.CONFIG_HAND_SEC_MIDDLE_OFFSET || '0');
  setSetting('CONFIG_HAND_SEC_SECONDARY_WIDTH', settings.CONFIG_HAND_SEC_SECONDARY_WIDTH || '6');
  setSetting('CONFIG_HAND_SEC_COLOR', settings.CONFIG_HAND_SEC_COLOR || '1');
  setSetting('CONFIG_HAND_SEC_OUTLINE_ENABLED', settings.CONFIG_HAND_SEC_OUTLINE_ENABLED || 'false');
  setSetting('CONFIG_HAND_SEC_OUTLINE_COLOR', settings.CONFIG_HAND_SEC_OUTLINE_COLOR || '0');
  setSetting('CONFIG_HAND_SEC_TRANSLUCENT', settings.CONFIG_HAND_SEC_TRANSLUCENT || 'false');
  setSetting('CONFIG_HAND_SEC_SHADOW_ENABLED', settings.CONFIG_HAND_SEC_SHADOW_ENABLED || 'false');
  setSetting('CONFIG_HAND_SEC_SHADOW_DISTANCE', settings.CONFIG_HAND_SEC_SHADOW_DISTANCE || '2');
  setSetting('CONFIG_HAND_SEC_HOLLOW', settings.CONFIG_HAND_SEC_HOLLOW || 'false');
  setSetting('CONFIG_HAND_SEC_HOLLOW_THICKNESS', settings.CONFIG_HAND_SEC_HOLLOW_THICKNESS || '1');
  setSetting('CONFIG_CENTER_CIRCLE_RADIUS', settings.CONFIG_CENTER_CIRCLE_RADIUS || '3');
  setSetting('CONFIG_CENTER_CIRCLE_COLOR', settings.CONFIG_CENTER_CIRCLE_COLOR || '0');
  // Dual-context fields -- see dualContextSetting()'s own comment.
  // config-page.js's save() now sends both halves explicitly (it has
  // to: only one of the two is ever live in the shared DOM element at
  // save time, the other comes from its own in-page shadow copy), so
  // this just writes each straight through, one setting per half.
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT_ANALOG', settings.CONFIG_UPPER_MIDDLE_LINE1_CONTENT_ANALOG || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT_DIGITAL', settings.CONFIG_UPPER_MIDDLE_LINE1_CONTENT_DIGITAL || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR_ANALOG', settings.CONFIG_UPPER_MIDDLE_LINE1_COLOR_ANALOG || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR_DIGITAL', settings.CONFIG_UPPER_MIDDLE_LINE1_COLOR_DIGITAL || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT_ANALOG', settings.CONFIG_UPPER_MIDDLE_LINE2_CONTENT_ANALOG || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT_DIGITAL', settings.CONFIG_UPPER_MIDDLE_LINE2_CONTENT_DIGITAL || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR_ANALOG', settings.CONFIG_UPPER_MIDDLE_LINE2_COLOR_ANALOG || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR_DIGITAL', settings.CONFIG_UPPER_MIDDLE_LINE2_COLOR_DIGITAL || '0');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_ANALOG', settings.CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_ANALOG || '105');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_DIGITAL', settings.CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_DIGITAL || '105');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_ANALOG', settings.CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_ANALOG || '0');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_DIGITAL', settings.CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_DIGITAL || '0');
  // Analog-only -- a single plain setting, not a dual-context pair.
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT', settings.CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT || '0');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE2_COLOR', settings.CONFIG_BOTTOM_MIDDLE_LINE2_COLOR || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT_ANALOG', settings.CONFIG_MIDDLE_LEFT_LINE1_CONTENT_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT_DIGITAL', settings.CONFIG_MIDDLE_LEFT_LINE1_CONTENT_DIGITAL || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR_ANALOG', settings.CONFIG_MIDDLE_LEFT_LINE1_COLOR_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR_DIGITAL', settings.CONFIG_MIDDLE_LEFT_LINE1_COLOR_DIGITAL || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT_ANALOG', settings.CONFIG_MIDDLE_LEFT_LINE2_CONTENT_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT_DIGITAL', settings.CONFIG_MIDDLE_LEFT_LINE2_CONTENT_DIGITAL || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR_ANALOG', settings.CONFIG_MIDDLE_LEFT_LINE2_COLOR_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR_DIGITAL', settings.CONFIG_MIDDLE_LEFT_LINE2_COLOR_DIGITAL || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_ANALOG', settings.CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_DIGITAL', settings.CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_DIGITAL || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR_ANALOG', settings.CONFIG_MIDDLE_RIGHT_LINE1_COLOR_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR_DIGITAL', settings.CONFIG_MIDDLE_RIGHT_LINE1_COLOR_DIGITAL || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_ANALOG', settings.CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_DIGITAL', settings.CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_DIGITAL || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR_ANALOG', settings.CONFIG_MIDDLE_RIGHT_LINE2_COLOR_ANALOG || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR_DIGITAL', settings.CONFIG_MIDDLE_RIGHT_LINE2_COLOR_DIGITAL || '0');
  setSetting('CONFIG_CORNER_TL', settings.CONFIG_CORNER_TL || '0');
  setSetting('CONFIG_CORNER_TR', settings.CONFIG_CORNER_TR || '0');
  setSetting('CONFIG_CORNER_BL', settings.CONFIG_CORNER_BL || '0');
  setSetting('CONFIG_CORNER_BR', settings.CONFIG_CORNER_BR || '0');
  setSetting('CONFIG_CORNER_TL_COLOR', settings.CONFIG_CORNER_TL_COLOR || '0');
  setSetting('CONFIG_CORNER_TR_COLOR', settings.CONFIG_CORNER_TR_COLOR || '0');
  setSetting('CONFIG_CORNER_BL_COLOR', settings.CONFIG_CORNER_BL_COLOR || '0');
  setSetting('CONFIG_CORNER_BR_COLOR', settings.CONFIG_CORNER_BR_COLOR || '0');
  setSetting('CONFIG_STEP_GOAL', settings.CONFIG_STEP_GOAL || '10000');
  setSetting('CONFIG_SHOW_SUN_TIME', settings.CONFIG_SHOW_SUN_TIME ? 'true' : 'false');
  setSetting('CONFIG_SHOW_ISS', settings.CONFIG_SHOW_ISS ? 'true' : 'false');
  setSetting('CONFIG_SHOW_FLIGHTS', settings.CONFIG_SHOW_FLIGHTS ? 'true' : 'false');
  setSetting('CONFIG_FLIGHTS_RANGE_KM', settings.CONFIG_FLIGHTS_RANGE_KM || '50');
  setSetting('CONFIG_FLIGHTS_API_KEY', settings.CONFIG_FLIGHTS_API_KEY || '');
  setSetting('CONFIG_SHOW_MAJOR_STARS', settings.CONFIG_SHOW_MAJOR_STARS === false ? 'false' : 'true');
  setSetting('CONFIG_AURORA_ENABLED', settings.CONFIG_AURORA_ENABLED ? 'true' : 'false');
  setSetting('CONFIG_VIBRATE_ON_PHASE_CHANGE', settings.CONFIG_VIBRATE_ON_PHASE_CHANGE ? 'true' : 'false');
  setSetting('CONFIG_STARTUP_CLOCK_ANIM_MODE', settings.CONFIG_STARTUP_CLOCK_ANIM_MODE || '1');
  setSetting('CONFIG_BG_ANIM_MODE', settings.CONFIG_BG_ANIM_MODE || '0');
  setSetting('CONFIG_SHAKE_ANIM_MODE', settings.CONFIG_SHAKE_ANIM_MODE || '0');
  setSetting('CONFIG_OUTLINE_ENABLED', settings.CONFIG_OUTLINE_ENABLED || '1');
  setSetting('CONFIG_BATTERY_SAVER_ENABLED', settings.CONFIG_BATTERY_SAVER_ENABLED ? 'true' : 'false');
  setSetting('CONFIG_CORNER_FONT', settings.CONFIG_CORNER_FONT || '1');
  setSetting('CONFIG_TEST_MODE', settings.CONFIG_TEST_MODE ? 'true' : 'false');
  setSetting('CONFIG_TEST_DATETIME', settings.CONFIG_TEST_DATETIME || '');
  setSetting('CONFIG_DEBUG_OVERRIDE_ENABLED', settings.CONFIG_DEBUG_OVERRIDE_ENABLED ? 'true' : 'false');
  setSetting('CONFIG_DEBUG_OVERRIDE_DATA', settings.CONFIG_DEBUG_OVERRIDE_DATA || '');
  setSetting('CONFIG_PRESET_1_NAME', settings.CONFIG_PRESET_1_NAME || '');
  setSetting('CONFIG_PRESET_1_JSON', settings.CONFIG_PRESET_1_JSON || '');
  setSetting('CONFIG_PRESET_1_IMAGE', settings.CONFIG_PRESET_1_IMAGE || '');
  setSetting('CONFIG_PRESET_2_NAME', settings.CONFIG_PRESET_2_NAME || '');
  setSetting('CONFIG_PRESET_2_JSON', settings.CONFIG_PRESET_2_JSON || '');
  setSetting('CONFIG_PRESET_2_IMAGE', settings.CONFIG_PRESET_2_IMAGE || '');
  setSetting('CONFIG_PRESET_3_NAME', settings.CONFIG_PRESET_3_NAME || '');
  setSetting('CONFIG_PRESET_3_JSON', settings.CONFIG_PRESET_3_JSON || '');
  setSetting('CONFIG_PRESET_3_IMAGE', settings.CONFIG_PRESET_3_IMAGE || '');
  setSetting('CONFIG_PRESET_4_NAME', settings.CONFIG_PRESET_4_NAME || '');
  setSetting('CONFIG_PRESET_4_JSON', settings.CONFIG_PRESET_4_JSON || '');
  setSetting('CONFIG_PRESET_4_IMAGE', settings.CONFIG_PRESET_4_IMAGE || '');
  setSetting('CONFIG_PRESET_5_NAME', settings.CONFIG_PRESET_5_NAME || '');
  setSetting('CONFIG_PRESET_5_JSON', settings.CONFIG_PRESET_5_JSON || '');
  setSetting('CONFIG_PRESET_5_IMAGE', settings.CONFIG_PRESET_5_IMAGE || '');
  setSetting('CONFIG_PRESET_6_NAME', settings.CONFIG_PRESET_6_NAME || '');
  setSetting('CONFIG_PRESET_6_JSON', settings.CONFIG_PRESET_6_JSON || '');
  setSetting('CONFIG_PRESET_6_IMAGE', settings.CONFIG_PRESET_6_IMAGE || '');
  setSetting('CONFIG_DRAW_DEBUG', settings.CONFIG_DRAW_DEBUG ? 'true' : 'false');
  setSetting('CONFIG_HOURLY_VIBE_MODE', settings.CONFIG_HOURLY_VIBE_MODE || '0');
  setSetting('CONFIG_HOURLY_VIBE_INTERVAL_MIN', settings.CONFIG_HOURLY_VIBE_INTERVAL_MIN || '30');
  setSetting('CONFIG_HOURLY_VIBE_PATTERN', settings.CONFIG_HOURLY_VIBE_PATTERN || '0');
  setSetting('CONFIG_HOURLY_VIBE_START_TIME', settings.CONFIG_HOURLY_VIBE_START_TIME || '00:00');
  setSetting('CONFIG_HOURLY_VIBE_END_TIME', settings.CONFIG_HOURLY_VIBE_END_TIME || '00:00');
  setSetting('CONFIG_HOURLY_VIBE_DAYS_MASK', settings.CONFIG_HOURLY_VIBE_DAYS_MASK || '127');
  setSetting('CONFIG_HOURLY_VIBE_OVERRIDE_QUIET', settings.CONFIG_HOURLY_VIBE_OVERRIDE_QUIET === false ? 'false' : 'true');

  // The clock font / weather-readout toggle / colors are purely
  // cosmetic and phone-local -- send them immediately rather than
  // waiting for a full refresh cycle to complete, so a settings
  // change feels instant regardless of whether one happens at all.
  sendFlatDict({});

  scheduleRefresh();
  // Only an explicit "Force refresh now" press bypasses the smart-
  // refresh skip -- an ordinary settings save no longer forces its
  // own network refetch. It used to, and that refetch could hit a
  // transient failure right as the user was just tweaking a color,
  // whose result (sendEclipseData/sendNoEclipseToday, called
  // regardless of success) would still get sent and applied --
  // silently wiping the watch's last known-good weather. A plain
  // save still gets a refresh if one is actually due (moved far
  // enough, or the interval elapsed) via this same normal check.
  //
  // resendOnSkip is deliberately false here (not just omitted) -- a
  // skip means nothing needs refetching, and the cosmetic-only
  // sendFlatDict({}) two lines up already just pushed this save's own
  // fresh settings/features chunks. Resending the cached FULL dict on
  // top of that would mean resending whatever full data was cached
  // from BEFORE this save (LAST_FULL_COMPUTED_DICT only updates on a
  // genuine full refresh, not a cosmetic-only push) -- overwriting the
  // save that just happened with stale settings a few seconds later.
  // That was a real bug: resendOnSkip used to be unconditional on
  // every skip, which is exactly what made it fire here too.
  refreshAndSend(!!settings.CONFIG_FORCE_REFRESH, false);
});
