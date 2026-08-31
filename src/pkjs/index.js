var astro = require('./astro');
var weather = require('./weather');
var configPage = require('./config-page');
var geocode = require('./geocode');
var iss = require('./iss');

var TYPE_CODE = { none: 0, partial: 1, total: 2, annular: 3 };

var MAX_FEATURES = 83; // highest corner/edge content id -- see CORNER_CONTENT_OPTIONS in config-page.js

var refreshTimer = null;
// Guards against a slow, older refresh's response arriving AFTER a
// newer one and overwriting it with stale data -- e.g. the periodic
// background timer firing (using the phone's real GPS location) right
// as the user saves a manual-coordinates override in settings: without
// this, if that older in-flight request's chain of network calls
// happens to finish later than the new forced one, it would clobber
// the correct new location/weather with the old location's data. Each
// refreshAndSend() call claims the next generation number; only the
// call that's still the current generation when its data is finally
// ready is allowed to actually send it.
var s_refreshGeneration = 0;

// ---- tiny settings helpers, backed directly by localStorage -------------

function getSetting(key, fallback) {
  var v = localStorage.getItem(key);
  return (v === null || v === undefined || v === '') ? fallback : v;
}

function setSetting(key, value) {
  if (value === null || value === undefined) return;
  localStorage.setItem(key, value);
}

// ---- time / dict helpers -------------------------------------------------

function toEpoch(d) {
  if (d && d instanceof Date) return Math.round(d.getTime() / 1000);
  return 0;
}

// Parses an HTML5 datetime-local value ("YYYY-MM-DDTHH:mm", always in
// this exact format per spec) into a local Date, by hand rather than
// via Date.parse/new Date(string) -- PKJS's JS engine's string-parsing
// support is not guaranteed to match a desktop browser's.
function parseDateTimeLocal(str) {
  if (!str) return null;
  var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(str);
  if (!m) return null;
  return new Date(
    parseInt(m[1], 10), parseInt(m[2], 10) - 1, parseInt(m[3], 10),
    parseInt(m[4], 10), parseInt(m[5], 10), 0, 0
  );
}

// "Now" for the purposes of the eclipse calculation -- normally the
// real current time, but overridable from settings so you can preview
// the watchface against a known eclipse date without waiting for one.
// This only affects what PKJS calculates and sends; it doesn't touch
// the watch's own clock, so set that to match by hand if you want the
// on-screen countdown to line up too.
function getEffectiveNow() {
  var testMode = getSetting('CONFIG_TEST_MODE', 'false') === 'true';
  if (testMode) {
    var d = parseDateTimeLocal(getSetting('CONFIG_TEST_DATETIME', ''));
    if (d) {
      console.log('eclipse-watch: TEST MODE active, using ' + d.toString());
      return d;
    }
    console.log('eclipse-watch: test mode on but no valid test date set, using real time');
  }
  return new Date();
}

function u16ArrayToBytes(arr) {
  var bytes = [];
  for (var i = 0; i < arr.length; i++) {
    var v = arr[i] & 0xFFFF;
    bytes.push(v & 0xFF);
    bytes.push((v >> 8) & 0xFF);
  }
  return bytes;
}

function i32ArrayToBytes(arr) {
  var bytes = [];
  for (var i = 0; i < arr.length; i++) {
    var v = arr[i] | 0;
    bytes.push(v & 0xFF);
    bytes.push((v >> 8) & 0xFF);
    bytes.push((v >> 16) & 0xFF);
    bytes.push((v >> 24) & 0xFF);
  }
  return bytes;
}

// Must match PlanetId's order in eclipse_data.h exactly -- both this
// array's order and astro.js's <name>AltDecideg field naming feed the
// single packed PLANET_ALT_SAMPLES/RISE/SET message keys.
var PLANET_ORDER = ['mercury', 'venus', 'mars', 'jupiter', 'saturn'];

// Fields shared by both the "eclipse today" and "no eclipse today"
// payloads: the full-day sun/moon/planet-altitude + cloud-cover grid
// that drives the background sky gradient, every body's rise/set
// animation, and the dithered cloud puffs. Sent every refresh,
// eclipse or not, since the sky itself isn't eclipse-specific.
function skyFieldsDict(sky, cloudGrid, moonPhase, riseSet, meteorShower, cloudAltitudePct, sunRiseTomorrow, stars) {
  var cloudBytes = (cloudGrid || sky.sunAltDecideg.map(function () { return 0; }))
    .map(function (v) { return Math.max(0, Math.min(100, Math.round(v))); });

  var planetAltCombined = [];
  var planetRiseArr = [];
  var planetSetArr = [];
  PLANET_ORDER.forEach(function (name) {
    planetAltCombined = planetAltCombined.concat(sky[name + 'AltDecideg']);
    planetRiseArr.push(toEpoch(riseSet[name].rise));
    planetSetArr.push(toEpoch(riseSet[name].set));
  });

  // Decidegrees, same convention as SUN_ALT_SAMPLES/etc -- alt can be
  // negative (below horizon; space-view mode on the watch decides
  // whether to still draw it), az is always 0-360 so its Bayer-
  // decidegree range (0-3600) fits uint16 with room to spare.
  var starAlt = (stars || []).map(function (s) { return Math.round(s.alt * 10); });
  var starAz = (stars || []).map(function (s) { return Math.round(s.az * 10); });

  return {
    'SKY_SAMPLE_START': toEpoch(sky.sampleStart),
    'STAR_ALT_SAMPLES': u16ArrayToBytes(starAlt),
    'STAR_AZ_SAMPLES': u16ArrayToBytes(starAz),
    'SKY_SAMPLE_INTERVAL': sky.intervalS,
    'SKY_SAMPLE_COUNT': sky.sunAltDecideg.length,
    'SUN_ALT_SAMPLES': u16ArrayToBytes(sky.sunAltDecideg),
    'MOON_ALT_SAMPLES': u16ArrayToBytes(sky.moonAltDecideg),
    'PLANET_ALT_SAMPLES': u16ArrayToBytes(planetAltCombined),
    'PLANET_RISE': i32ArrayToBytes(planetRiseArr),
    'PLANET_SET': i32ArrayToBytes(planetSetArr),
    'SATURN_RING_OPEN_PCT': sky.saturnRingOpenPct,
    'SKY_SCALE_MAX_ALT': sky.scaleMaxAltDecideg,
    'CLOUD_SAMPLES': cloudBytes,
    'CLOUD_ALTITUDE_PCT': (typeof cloudAltitudePct === 'number') ? cloudAltitudePct : 50,
    'MOON_PHASE_PCT': moonPhase.illuminatedPct,
    'MOON_WAXING': moonPhase.waxing ? 1 : 0,
    'SUN_RISE': toEpoch(riseSet.sun.rise),
    'SUN_SET': toEpoch(riseSet.sun.set),
    'SUN_RISE_TOMORROW': toEpoch(sunRiseTomorrow),
    'MOON_RISE': toEpoch(riseSet.moon.rise),
    'MOON_SET': toEpoch(riseSet.moon.set),
    'METEOR_INTENSITY': meteorShower ? meteorShower.intensity : 0,
    'METEOR_SHOWER_NAME': meteorShower ? meteorShower.name : ''
  };
}

// Must match apply_clock_font()'s code numbers in
// pebble-eclipse-watch.c exactly.
const CLOCK_STYLE_IDS = {
  leco: 0,
  clockforge: 1,
  sfpixelate: 2,
  radioland: 3,
  minisystem: 4,
  minecrafter: 5,
  kitchenpolice: 6,
  dsdigib: 7,
  distgrg: 8,
  dimitri: 9,
  digitaldream: 10,
  blackout: 11,
  audiowide: 12,
  formation: 13,
  komikahb: 14,
  miso: 15,
  pricedown: 16,
  roboto: 17,
  bithamlight: 18,
  bithambold: 19,
  bebas: 20
};

function mapToFontCode(font) {
  if (CLOCK_STYLE_IDS[font] === undefined) {
    return 0;
  }
  return CLOCK_STYLE_IDS[font];
}

function clockFontCode() {
  return mapToFontCode(getSetting('CONFIG_CLOCK_FONT', 'leco'));
}

function tempUnitCode() {
  var v = getSetting('CONFIG_TEMP_UNIT', 'C');
  if (v === 'F') return 1;
  if (v === 'K') return 2;
  return 0;
}

function windSpeedUnitCode() {
  var v = getSetting('CONFIG_WIND_SPEED_UNIT', 'kmh');
  if (v === 'mph') return 1;
  if (v === 'ms') return 2;
  if (v === 'kn') return 3;
  return 0;
}

function showSecondsCode() {
  var font = getSetting('CONFIG_CLOCK_FONT', 'leco');
  var wanted = getSetting('CONFIG_SHOW_SECONDS', 'false') === 'true';
  return (wanted) ? 1 : 0;
}

// Custom colors are stored as raw packed GColor argb bytes (0-255) --
// the settings page's 64-color picker computes these directly in the
// browser (0xC0 | (r2<<4) | (g2<<2) | b2, matching Pebble's own 2-bit-
// per-channel packing), so this side just passes them through with a
// sane fallback if something's missing or out of range.
function customColorByte(key, fallbackByte) {
  var v = parseInt(getSetting(key, String(fallbackByte)), 10);
  if (isNaN(v) || v < 0 || v > 255) return fallbackByte;
  return v;
}
function customBgByte() { return customColorByte('CONFIG_CUSTOM_BG', 0xFF); }
function customTextByte() { return customColorByte('CONFIG_CUSTOM_TEXT', 0xC0); }
function customAccentByte() { return customColorByte('CONFIG_CUSTOM_ACCENT', 0xC0); }

function nightSchemeEnabledCode() {
  return getSetting('CONFIG_NIGHT_ENABLED', 'false') === 'true' ? 1 : 0;
}
function nightCustomBgByte() { return customColorByte('CONFIG_NIGHT_CUSTOM_BG', 0xC0); }
function nightCustomTextByte() { return customColorByte('CONFIG_NIGHT_CUSTOM_TEXT', 0xFF); }
function nightCustomAccentByte() { return customColorByte('CONFIG_NIGHT_CUSTOM_ACCENT', 0xFF); }

function bottomStyleCode() {
  var v = getSetting('CONFIG_BOTTOM_STYLE', 'digital');
  if (v === 'biganalog') return 2;
  if (v === 'analog') return 1;
  return 0;
}
function analogStyleCode() {
  var id = parseInt(getSetting('CONFIG_ANALOG_STYLE', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}

function sunMoonSizeCode() {
  var v = getSetting('CONFIG_SUN_MOON_SIZE', '75');
  var pct = parseInt(v, 10);
  if ([25, 50, 75, 100].indexOf(pct) === -1) pct = 75;
  return pct;
}

function cloudRenderStyleCode() {
  var id = parseInt(getSetting('CONFIG_CLOUD_RENDER_STYLE', '1'), 10);
  if (isNaN(id) || id < 0 || id > 1) id = 1;
  return id;
}
// 0=Weather sky (default), 1=Clear sky, 2=Space view -- see
// background_layer.c's canvas_update_proc for what each mode changes.
function skyModeCode() {
  var id = parseInt(getSetting('CONFIG_SKY_MODE', '0'), 10);
  if (isNaN(id) || id < 0 || id > 2) id = 0;
  return id;
}
// 0=simple, 1=hollow, 2=filled -- only hollow is actually implemented
// on-watch right now (see draw_weather_icon_hollow() in
// pebble-eclipse-watch.c); simple/filled are drawing-side stubs.
function weatherIconStyleCode() {
  var id = parseInt(getSetting('CONFIG_WEATHER_ICON_STYLE', '1'), 10);
  if (isNaN(id) || id < 0 || id > 2) id = 1;
  return id;
}
// 0=US AQI (EPA scale, 0-500+), 1=European AQI (0-100+) -- which of the
// two values already fetched (see fetchAirQualityIfEnabled) the "Air
// quality" corner content displays.
function aqiUnitCode() {
  var id = parseInt(getSetting('CONFIG_AQI_UNIT', '0'), 10);
  if (isNaN(id) || id < 0 || id > 1) id = 0;
  return id;
}
// 0=meters, 1=feet -- used by the "Altitude" corner content.
function altitudeUnitCode() {
  var id = parseInt(getSetting('CONFIG_ALTITUDE_UNIT', '0'), 10);
  if (isNaN(id) || id < 0 || id > 1) id = 0;
  return id;
}

function shakeLabelSecondsCode() {
  var secs = parseInt(getSetting('CONFIG_SHAKE_LABEL_SECONDS', '3'), 10);
  if (isNaN(secs) || secs < 1) secs = 1;
  if (secs > 10) secs = 10;
  return secs;
}
function bottomInfoBarModeCode() {
  var v = parseInt(getSetting('CONFIG_BOTTOM_INFO_BAR_MODE', '1'), 10);
  return [0, 1, 2].indexOf(v) === -1 ? 1 : v;
}

function bigAnalogHandStyleCode() {
  var id = parseInt(getSetting('CONFIG_BIG_ANALOG_HAND_STYLE', '0'), 10);
  if (isNaN(id) || id < 0 || id > 4) id = 0;
  return id;
}
function bigAnalogHandsTransparentCode() { return getSetting('CONFIG_BIG_ANALOG_TRANSPARENT', 'false') === 'true' ? 1 : 0; }
// One shared on/off toggle applied to all 3 procedural hand presets at
// once (same pattern as bigAnalogHandsTransparentCode() above) -- angle/
// distance aren't user-adjustable for presets, the watch hardcodes
// 120deg/2px whenever this is on.
function bigAnalogHandsShadowCode() { return getSetting('CONFIG_BIG_ANALOG_HANDS_SHADOW', 'false') === 'true' ? 1 : 0; }
// A single global style choice -- solid black shadows, or dithered
// translucent ones -- applied to every hand's shadow regardless of
// whether it's a procedural preset or the custom hand system. Defaults
// to translucent.
function shadowTranslucentCode() { return getSetting('CONFIG_SHADOW_TRANSLUCENT', 'true') === 'true' ? 1 : 0; }

function bigAnalogMarkerStyleCode() {
  var id = parseInt(getSetting('CONFIG_BIG_ANALOG_MARKER_STYLE', '0'), 10);
  if (isNaN(id) || id < 0 || id > 9) id = 0;
  return id;
}
// Bitmap marker styles' own transparency -- deliberately separate from
// bigAnalogHandsTransparentCode() above (that one used to double as
// this too, which meant toggling hand transparency also silently
// dimmed the markers whether or not that's what was wanted).
function bitmapMarkerTransparentCode() { return getSetting('CONFIG_BITMAP_MARKER_TRANSPARENT', 'false') === 'true' ? 1 : 0; }

// Whether the corners/edges feature overlay draws underneath the big-
// analog hands instead of on top of them -- only meaningful (and only
// shown on the settings page) when bottomStyle is 'biganalog'.
function drawFeaturesBeneathHandsCode() { return getSetting('CONFIG_DRAW_FEATURES_BENEATH_HANDS', 'false') === 'true' ? 1 : 0; }

// Custom hour/second marker system (bigAnalogMarkerStyleCode() === 8) --
// see MarkerRingConfig/MarkerTextConfig in marker_layer.h for what each
// field means and its valid range. Border values are 0-100% "reach"
// (marker_reach_px() in marker_layer.c does the actual px mapping,
// on-watch, from real screen dimensions) -- a mark is drawn directly
// between its inner and outer border points, no separate length field.
var MARKER_BORDER_MIN = 0, MARKER_BORDER_MAX = 100;
function clampInt(v, lo, hi, fallback) {
  var n = parseInt(v, 10);
  if (isNaN(n)) return fallback;
  if (n < lo) return lo;
  if (n > hi) return hi;
  return n;
}
function customMarkerStyleCode(key, fallback) { return clampInt(getSetting(key, fallback), 0, 2, fallback); }
function customMarkerBorderCode(key, fallback) { return clampInt(getSetting(key, fallback), MARKER_BORDER_MIN, MARKER_BORDER_MAX, fallback); }

function customHourStyleCode() { return customMarkerStyleCode('CONFIG_CUSTOM_HOUR_STYLE', 0); }
function customHourThicknessCode() { return clampInt(getSetting('CONFIG_CUSTOM_HOUR_THICKNESS', '3'), 1, 20, 3); }
function customHourInnerEccCode() { return clampInt(getSetting('CONFIG_CUSTOM_HOUR_INNER_ECC', '0'), 0, 100, 0); }
function customHourOuterEccCode() { return clampInt(getSetting('CONFIG_CUSTOM_HOUR_OUTER_ECC', '0'), 0, 100, 0); }
function customHourInnerBorderCode() { return customMarkerBorderCode('CONFIG_CUSTOM_HOUR_INNER_BORDER', 20); }
function customHourOuterBorderCode() {
  var outer = customMarkerBorderCode('CONFIG_CUSTOM_HOUR_OUTER_BORDER', 100);
  var inner = customHourInnerBorderCode();
  return outer < inner ? inner : outer; // outer never below inner -- see MarkerRingConfig
}
function customHourTranslucentCode() { return getSetting('CONFIG_CUSTOM_HOUR_TRANSLUCENT', 'false') === 'true' ? 1 : 0; }
function customSecStyleCode() { return customMarkerStyleCode('CONFIG_CUSTOM_SEC_STYLE', 0); }
function customSecThicknessCode() { return clampInt(getSetting('CONFIG_CUSTOM_SEC_THICKNESS', '1'), 1, 10, 1); }
function customSecInnerEccCode() { return clampInt(getSetting('CONFIG_CUSTOM_SEC_INNER_ECC', '0'), 0, 100, 0); }
function customSecOuterEccCode() { return clampInt(getSetting('CONFIG_CUSTOM_SEC_OUTER_ECC', '0'), 0, 100, 0); }
function customSecInnerBorderCode() { return customMarkerBorderCode('CONFIG_CUSTOM_SEC_INNER_BORDER', 70); }
function customSecOuterBorderCode() {
  var outer = customMarkerBorderCode('CONFIG_CUSTOM_SEC_OUTER_BORDER', 100);
  var inner = customSecInnerBorderCode();
  return outer < inner ? inner : outer;
}
function customSecTranslucentCode() { return getSetting('CONFIG_CUSTOM_SEC_TRANSLUCENT', 'false') === 'true' ? 1 : 0; }
function markerTextTargetCode() { return clampInt(getSetting('CONFIG_MARKER_TEXT_TARGET', '0'), 0, 2, 0); }
function markerTextFontCode() { return clampInt(getSetting('CONFIG_MARKER_TEXT_FONT', '0'), 0, 6, 0); }
function markerTextOffsetCode() { return clampInt(getSetting('CONFIG_MARKER_TEXT_OFFSET', '0'), -50, 50, 0); }
function markerTextHourMaskCode() { return clampInt(getSetting('CONFIG_MARKER_TEXT_HOUR_MASK', '4095'), 0, 4095, 4095); }
function markerTextSecMaskCode() { return clampInt(getSetting('CONFIG_MARKER_TEXT_SEC_MASK', '4095'), 0, 4095, 4095); }
function markerTextRomanCode() { return getSetting('CONFIG_MARKER_TEXT_ROMAN', 'false') === 'true' ? 1 : 0; }

// Custom hour/minute/second hand system (bigAnalogHandStyleCode() === 4) --
// see HandConfig in hand_layer.h for what each field means.
function handStyleFieldCode(key, fallback) { return clampInt(getSetting(key, fallback), 0, 2, fallback); }
function handColorFieldCode(key, fallback) { return clampInt(getSetting(key, fallback), 0, 2, fallback); }
// A hand's own color additionally allows 3 = "none" (skip the fill) --
// outline color and the center circle color don't get that option.
function handMainColorFieldCode(key, fallback) { return clampInt(getSetting(key, fallback), 0, 3, fallback); }
function handOutlineEnabledCode(key) { return getSetting(key, 'false') === 'true' ? 1 : 0; }
function handTranslucentCode(key) { return getSetting(key, 'false') === 'true' ? 1 : 0; }
function handShadowEnabledCode(key) { return getSetting(key, 'false') === 'true' ? 1 : 0; }
// 0-359 degrees, same "0 = 12 o'clock, clockwise" convention as every
// other angle sent to the watch.
function handShadowAngleCode(key) { return clampInt(getSetting(key, '120'), 0, 359, 120); }
function handShadowDistanceCode(key) { return clampInt(getSetting(key, '2'), 1, 5, 2); }

function handHourStyleCode() { return handStyleFieldCode('CONFIG_HAND_HOUR_STYLE', 1); }
function handHourWidthCode() { return clampInt(getSetting('CONFIG_HAND_HOUR_WIDTH', '12'), 1, 40, 12); }
function handHourLengthCode() { return clampInt(getSetting('CONFIG_HAND_HOUR_LENGTH', '51'), 10, 100, 51); }
function handHourBackOffsetCode() { return clampInt(getSetting('CONFIG_HAND_HOUR_BACK_OFFSET', '0'), -40, 40, 0); }
function handHourColorCode() { return handMainColorFieldCode('CONFIG_HAND_HOUR_COLOR', 0); }
function handHourOutlineEnabledCode() { return handOutlineEnabledCode('CONFIG_HAND_HOUR_OUTLINE_ENABLED'); }
function handHourOutlineColorCode() { return handColorFieldCode('CONFIG_HAND_HOUR_OUTLINE_COLOR', 0); }
function handHourTranslucentCode() { return handTranslucentCode('CONFIG_HAND_HOUR_TRANSLUCENT'); }
function handHourShadowEnabledCode() { return handShadowEnabledCode('CONFIG_HAND_HOUR_SHADOW_ENABLED'); }
function handHourShadowAngleCode() { return handShadowAngleCode('CONFIG_HAND_HOUR_SHADOW_ANGLE'); }
function handHourShadowDistanceCode() { return handShadowDistanceCode('CONFIG_HAND_HOUR_SHADOW_DISTANCE'); }

function handMinStyleCode() { return handStyleFieldCode('CONFIG_HAND_MIN_STYLE', 1); }
function handMinWidthCode() { return clampInt(getSetting('CONFIG_HAND_MIN_WIDTH', '18'), 1, 40, 18); }
function handMinLengthCode() { return clampInt(getSetting('CONFIG_HAND_MIN_LENGTH', '78'), 10, 100, 78); }
function handMinBackOffsetCode() { return clampInt(getSetting('CONFIG_HAND_MIN_BACK_OFFSET', '0'), -40, 40, 0); }
function handMinColorCode() { return handMainColorFieldCode('CONFIG_HAND_MIN_COLOR', 0); }
function handMinOutlineEnabledCode() { return handOutlineEnabledCode('CONFIG_HAND_MIN_OUTLINE_ENABLED'); }
function handMinOutlineColorCode() { return handColorFieldCode('CONFIG_HAND_MIN_OUTLINE_COLOR', 0); }
function handMinTranslucentCode() { return handTranslucentCode('CONFIG_HAND_MIN_TRANSLUCENT'); }
function handMinShadowEnabledCode() { return handShadowEnabledCode('CONFIG_HAND_MIN_SHADOW_ENABLED'); }
function handMinShadowAngleCode() { return handShadowAngleCode('CONFIG_HAND_MIN_SHADOW_ANGLE'); }
function handMinShadowDistanceCode() { return handShadowDistanceCode('CONFIG_HAND_MIN_SHADOW_DISTANCE'); }

function handSecStyleCode() { return handStyleFieldCode('CONFIG_HAND_SEC_STYLE', 0); }
function handSecWidthCode() { return clampInt(getSetting('CONFIG_HAND_SEC_WIDTH', '2'), 1, 40, 2); }
function handSecLengthCode() { return clampInt(getSetting('CONFIG_HAND_SEC_LENGTH', '85'), 10, 100, 85); }
function handSecBackOffsetCode() { return clampInt(getSetting('CONFIG_HAND_SEC_BACK_OFFSET', '0'), -40, 40, 0); }
function handSecColorCode() { return handMainColorFieldCode('CONFIG_HAND_SEC_COLOR', 1); }
function handSecOutlineEnabledCode() { return handOutlineEnabledCode('CONFIG_HAND_SEC_OUTLINE_ENABLED'); }
function handSecOutlineColorCode() { return handColorFieldCode('CONFIG_HAND_SEC_OUTLINE_COLOR', 0); }
function handSecTranslucentCode() { return handTranslucentCode('CONFIG_HAND_SEC_TRANSLUCENT'); }
function handSecShadowEnabledCode() { return handShadowEnabledCode('CONFIG_HAND_SEC_SHADOW_ENABLED'); }
function handSecShadowAngleCode() { return handShadowAngleCode('CONFIG_HAND_SEC_SHADOW_ANGLE'); }
function handSecShadowDistanceCode() { return handShadowDistanceCode('CONFIG_HAND_SEC_SHADOW_DISTANCE'); }

function centerCircleRadiusCode() { return clampInt(getSetting('CONFIG_CENTER_CIRCLE_RADIUS', '3'), 0, 30, 3); }
function centerCircleColorCode() { return handColorFieldCode('CONFIG_CENTER_CIRCLE_COLOR', 0); }

function upperMiddleLine1ContentCode() {
  var id = parseInt(getSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT', '0'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function upperMiddleLine1ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}
function upperMiddleLine2ContentCode() {
  var id = parseInt(getSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT', '0'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function upperMiddleLine2ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}
// Bottom-middle line 1 (the upper of its own pair) defaults to short
// date (content 12) -- matches what used to be the separate "show
// date behind the hands" toggle's default-on behavior, now folded
// into the regular content system instead of being its own setting.
function bottomMiddleLine1ContentCode() {
  var id = parseInt(getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT', '12'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function bottomMiddleLine1ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}
function bottomMiddleLine2ContentCode() {
  var id = parseInt(getSetting('CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT', '0'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function bottomMiddleLine2ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_BOTTOM_MIDDLE_LINE2_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}
function middleLeftLine1ContentCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT', '0'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function middleLeftLine1ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}
function middleLeftLine2ContentCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT', '0'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function middleLeftLine2ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}
function middleRightLine1ContentCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT', '0'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function middleRightLine1ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}
function middleRightLine2ContentCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT', '0'), 10);
  if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
  return id;
}
function middleRightLine2ColorModeCode() {
  var id = parseInt(getSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR', '0'), 10);
  if (isNaN(id) || id < 0 || id > 3) id = 0;
  return id;
}

function showSunTimeCode() { return getSetting('CONFIG_SHOW_SUN_TIME', 'false') === 'true' ? 1 : 0; }
function showIssCode() { return getSetting('CONFIG_SHOW_ISS', 'false') === 'true' ? 1 : 0; }
function vibrateOnPhaseChangeCode() { return getSetting('CONFIG_VIBRATE_ON_PHASE_CHANGE', 'false') === 'true' ? 1 : 0; }
function outlineEnabledCode() { return getSetting('CONFIG_OUTLINE_ENABLED', 'true') === 'true' ? 1 : 0; }
function cornerFontSizeCode() {
  var v = parseInt(getSetting('CONFIG_CORNER_FONT_SIZE', '1'), 10);
  return [0, 1, 2, 3].indexOf(v) === -1 ? 1 : v;
}
function cornerCustomFontCode() {
  var v = parseInt(getSetting('CONFIG_CORNER_CUSTOM_FONT', '0'), 10);
  return [0, 1, 2, 3, 4, 5].indexOf(v) === -1 ? 0 : v;
}

// Corners: 4 slots (0=top-left, 1=top-right, 2=bottom-left,
// 3=bottom-right), each an independent content-type + color-mode
// pair. Sent as two packed byte arrays rather than 8 separate keys.
function cornerContentBytes() {
  var keys = ['CONFIG_CORNER_TL', 'CONFIG_CORNER_TR', 'CONFIG_CORNER_BL', 'CONFIG_CORNER_BR'];
  return keys.map(function (k) {
    var id = parseInt(getSetting(k, '0'), 10);
    if (isNaN(id) || id < 0 || id > MAX_FEATURES) id = 0;
    return id;
  });
}
function cornerColorModeBytes() {
  var keys = ['CONFIG_CORNER_TL_COLOR', 'CONFIG_CORNER_TR_COLOR', 'CONFIG_CORNER_BL_COLOR', 'CONFIG_CORNER_BR_COLOR'];
  return keys.map(function (k) {
    var id = parseInt(getSetting(k, '0'), 10);
    if (isNaN(id) || id < 0 || id > 3) id = 0;
    return id;
  });
}
function dailyStepGoalValue() {
  var v = parseInt(getSetting('CONFIG_STEP_GOAL', '10000'), 10);
  if (isNaN(v) || v < 1000) v = 10000;
  if (v > 60000) v = 60000;
  return v;
}

function sendDict(dict) {
  // Always carried, on every message -- these are purely cosmetic,
  // phone-local preferences, not eclipse data, so there's no reason
  // to gate them behind DATA_VALID or wait for a full refresh cycle.
  dict['CLOCK_FONT'] = clockFontCode();
  dict['TEMP_UNIT'] = tempUnitCode();
  dict['WIND_SPEED_UNIT'] = windSpeedUnitCode();
  dict['SHOW_SECONDS'] = showSecondsCode();
  dict['CUSTOM_BG'] = customBgByte();
  dict['CUSTOM_TEXT'] = customTextByte();
  dict['CUSTOM_ACCENT'] = customAccentByte();
  dict['NIGHT_SCHEME_ENABLED'] = nightSchemeEnabledCode();
  dict['NIGHT_CUSTOM_BG'] = nightCustomBgByte();
  dict['NIGHT_CUSTOM_TEXT'] = nightCustomTextByte();
  dict['NIGHT_CUSTOM_ACCENT'] = nightCustomAccentByte();
  dict['BOTTOM_STYLE'] = bottomStyleCode();
  dict['ANALOG_STYLE'] = analogStyleCode();
  dict['SUN_MOON_SIZE_PCT'] = sunMoonSizeCode();
  dict['CLOUD_RENDER_STYLE'] = cloudRenderStyleCode();
  dict['SKY_MODE'] = skyModeCode();
  dict['WEATHER_ICON_STYLE'] = weatherIconStyleCode();
  dict['AQI_UNIT'] = aqiUnitCode();
  dict['ALTITUDE_UNIT'] = altitudeUnitCode();
  dict['SHAKE_LABEL_SECONDS'] = shakeLabelSecondsCode();
  dict['BOTTOM_INFO_BAR_MODE'] = bottomInfoBarModeCode();
  dict['BIG_ANALOG_HAND_STYLE'] = bigAnalogHandStyleCode();
  dict['BIG_ANALOG_HANDS_TRANSPARENT'] = bigAnalogHandsTransparentCode();
  dict['BIG_ANALOG_HANDS_SHADOW'] = bigAnalogHandsShadowCode();
  dict['SHADOW_TRANSLUCENT'] = shadowTranslucentCode();
  dict['BIG_ANALOG_MARKER_STYLE'] = bigAnalogMarkerStyleCode();
  dict['BITMAP_MARKER_TRANSPARENT'] = bitmapMarkerTransparentCode();
  dict['DRAW_FEATURES_BENEATH_HANDS'] = drawFeaturesBeneathHandsCode();
  dict['CUSTOM_HOUR_STYLE'] = customHourStyleCode();
  dict['CUSTOM_HOUR_THICKNESS'] = customHourThicknessCode();
  dict['CUSTOM_HOUR_INNER_ECC'] = customHourInnerEccCode();
  dict['CUSTOM_HOUR_OUTER_ECC'] = customHourOuterEccCode();
  dict['CUSTOM_HOUR_INNER_BORDER'] = customHourInnerBorderCode();
  dict['CUSTOM_HOUR_OUTER_BORDER'] = customHourOuterBorderCode();
  dict['CUSTOM_HOUR_TRANSLUCENT'] = customHourTranslucentCode();
  dict['CUSTOM_SEC_STYLE'] = customSecStyleCode();
  dict['CUSTOM_SEC_THICKNESS'] = customSecThicknessCode();
  dict['CUSTOM_SEC_INNER_ECC'] = customSecInnerEccCode();
  dict['CUSTOM_SEC_OUTER_ECC'] = customSecOuterEccCode();
  dict['CUSTOM_SEC_INNER_BORDER'] = customSecInnerBorderCode();
  dict['CUSTOM_SEC_OUTER_BORDER'] = customSecOuterBorderCode();
  dict['CUSTOM_SEC_TRANSLUCENT'] = customSecTranslucentCode();
  dict['MARKER_TEXT_TARGET'] = markerTextTargetCode();
  dict['MARKER_TEXT_FONT'] = markerTextFontCode();
  dict['MARKER_TEXT_OFFSET'] = markerTextOffsetCode();
  dict['MARKER_TEXT_HOUR_MASK'] = markerTextHourMaskCode();
  dict['MARKER_TEXT_SEC_MASK'] = markerTextSecMaskCode();
  dict['MARKER_TEXT_ROMAN'] = markerTextRomanCode();
  dict['HAND_HOUR_STYLE'] = handHourStyleCode();
  dict['HAND_HOUR_WIDTH'] = handHourWidthCode();
  dict['HAND_HOUR_LENGTH'] = handHourLengthCode();
  dict['HAND_HOUR_BACK_OFFSET'] = handHourBackOffsetCode();
  dict['HAND_HOUR_COLOR'] = handHourColorCode();
  dict['HAND_HOUR_OUTLINE_ENABLED'] = handHourOutlineEnabledCode();
  dict['HAND_HOUR_OUTLINE_COLOR'] = handHourOutlineColorCode();
  dict['HAND_HOUR_TRANSLUCENT'] = handHourTranslucentCode();
  dict['HAND_HOUR_SHADOW_ENABLED'] = handHourShadowEnabledCode();
  dict['HAND_HOUR_SHADOW_ANGLE'] = handHourShadowAngleCode();
  dict['HAND_HOUR_SHADOW_DISTANCE'] = handHourShadowDistanceCode();
  dict['HAND_MIN_STYLE'] = handMinStyleCode();
  dict['HAND_MIN_WIDTH'] = handMinWidthCode();
  dict['HAND_MIN_LENGTH'] = handMinLengthCode();
  dict['HAND_MIN_BACK_OFFSET'] = handMinBackOffsetCode();
  dict['HAND_MIN_COLOR'] = handMinColorCode();
  dict['HAND_MIN_OUTLINE_ENABLED'] = handMinOutlineEnabledCode();
  dict['HAND_MIN_OUTLINE_COLOR'] = handMinOutlineColorCode();
  dict['HAND_MIN_TRANSLUCENT'] = handMinTranslucentCode();
  dict['HAND_MIN_SHADOW_ENABLED'] = handMinShadowEnabledCode();
  dict['HAND_MIN_SHADOW_ANGLE'] = handMinShadowAngleCode();
  dict['HAND_MIN_SHADOW_DISTANCE'] = handMinShadowDistanceCode();
  dict['HAND_SEC_STYLE'] = handSecStyleCode();
  dict['HAND_SEC_WIDTH'] = handSecWidthCode();
  dict['HAND_SEC_LENGTH'] = handSecLengthCode();
  dict['HAND_SEC_BACK_OFFSET'] = handSecBackOffsetCode();
  dict['HAND_SEC_COLOR'] = handSecColorCode();
  dict['HAND_SEC_OUTLINE_ENABLED'] = handSecOutlineEnabledCode();
  dict['HAND_SEC_OUTLINE_COLOR'] = handSecOutlineColorCode();
  dict['HAND_SEC_TRANSLUCENT'] = handSecTranslucentCode();
  dict['HAND_SEC_SHADOW_ENABLED'] = handSecShadowEnabledCode();
  dict['HAND_SEC_SHADOW_ANGLE'] = handSecShadowAngleCode();
  dict['HAND_SEC_SHADOW_DISTANCE'] = handSecShadowDistanceCode();
  dict['CENTER_CIRCLE_RADIUS'] = centerCircleRadiusCode();
  dict['CENTER_CIRCLE_COLOR'] = centerCircleColorCode();
  dict['UPPER_MIDDLE_LINE1_CONTENT'] = upperMiddleLine1ContentCode();
  dict['UPPER_MIDDLE_LINE1_COLOR_MODE'] = upperMiddleLine1ColorModeCode();
  dict['UPPER_MIDDLE_LINE2_CONTENT'] = upperMiddleLine2ContentCode();
  dict['UPPER_MIDDLE_LINE2_COLOR_MODE'] = upperMiddleLine2ColorModeCode();
  dict['BOTTOM_MIDDLE_LINE1_CONTENT'] = bottomMiddleLine1ContentCode();
  dict['BOTTOM_MIDDLE_LINE1_COLOR_MODE'] = bottomMiddleLine1ColorModeCode();
  dict['BOTTOM_MIDDLE_LINE2_CONTENT'] = bottomMiddleLine2ContentCode();
  dict['BOTTOM_MIDDLE_LINE2_COLOR_MODE'] = bottomMiddleLine2ColorModeCode();
  dict['MIDDLE_LEFT_LINE1_CONTENT'] = middleLeftLine1ContentCode();
  dict['MIDDLE_LEFT_LINE1_COLOR_MODE'] = middleLeftLine1ColorModeCode();
  dict['MIDDLE_LEFT_LINE2_CONTENT'] = middleLeftLine2ContentCode();
  dict['MIDDLE_LEFT_LINE2_COLOR_MODE'] = middleLeftLine2ColorModeCode();
  dict['MIDDLE_RIGHT_LINE1_CONTENT'] = middleRightLine1ContentCode();
  dict['MIDDLE_RIGHT_LINE1_COLOR_MODE'] = middleRightLine1ColorModeCode();
  dict['MIDDLE_RIGHT_LINE2_CONTENT'] = middleRightLine2ContentCode();
  dict['MIDDLE_RIGHT_LINE2_COLOR_MODE'] = middleRightLine2ColorModeCode();
  dict['SHOW_SUN_TIME'] = showSunTimeCode();
  dict['SHOW_ISS'] = showIssCode();
  dict['VIBRATE_ON_PHASE_CHANGE'] = vibrateOnPhaseChangeCode();
  dict['OUTLINE_ENABLED'] = outlineEnabledCode();
  dict['CORNER_FONT_SIZE'] = cornerFontSizeCode();
  dict['CORNER_CUSTOM_FONT'] = cornerCustomFontCode();
  dict['CORNER_CONTENT'] = cornerContentBytes();
  dict['CORNER_COLOR_MODE'] = cornerColorModeBytes();
  dict['DAILY_STEP_GOAL'] = dailyStepGoalValue();

  try {
    localStorage.setItem('LAST_COMPUTED_DICT', JSON.stringify(dict));
  } catch (e) {
    // Not critical if this fails (storage full, etc.) -- just means the
    // settings page's debug view won't have a fresh snapshot this time.
  }

  var toSend = dict;
  
  var debugData = '{'+
'  "DATA_VALID": 1,'+
'  "ECLIPSE_TYPE": 0,'+
'  "C1_TIME": 0,'+
'  "C2_TIME": 0,'+
'  "MAX_TIME": 0,'+
'  "C3_TIME": 0,'+
'  "C4_TIME": 0,'+
'  "SUNSET_TIME": 0,'+
'  "MAGNITUDE": 0,'+
'  "POS_ANGLE": 0,'+
'  "SAMPLE_START": 0,'+
'  "SAMPLE_INTERVAL": 0,'+
'  "SAMPLE_COUNT": 0,'+
'  "SEP_SAMPLES": ['+
'    0,'+
'    0'+
'  ],'+
'  "MAG_SAMPLES": ['+
'    0,'+
'    0'+
'  ],'+
'  "RADIUS_RATIO_PCT": 0,'+
'  "CLOUD_COVER": 13,'+
'  "VIS_SCORE": 87,'+
'  "WEATHER_SOURCES": 1,'+
'  "WEATHER_CONDITION": 0,'+
'  "WEATHER_TEMP_C": 15,'+
'  "WEATHER_TEMP_HIGH_C": 27,'+
'  "WEATHER_TEMP_LOW_C": 14,'+
'  "UV_INDEX_X10": 61,'+
'  "RAIN_CHANCE_PCT": 18,'+
'  "HUMIDITY_PCT": 78,'+
'  "WIND_SPEED_KMH": 5,'+
'  "LOCATION_NAME": "Innsbruck, Tyrol",'+
'  "SKY_SAMPLE_START": 1787522400,'+
'  "SKY_SAMPLE_INTERVAL": 3600,'+
'  "SKY_SAMPLE_COUNT": 25,'+
'  "SUN_ALT_SAMPLES": ['+
'    228,'+
'    254,'+
'    205,'+
'    254,'+
'    211,'+
'    254,'+
'    247,'+
'    254,'+
'    51,'+
'    255,'+
'    128,'+
'    255,'+
'    219,'+
'    255,'+
'    60,'+
'    0,'+
'    161,'+
'    0,'+
'    6,'+
'    1,'+
'    103,'+
'    1,'+
'    189,'+
'    1,'+
'    254,'+
'    1,'+
'    31,'+
'    2,'+
'    21,'+
'    2,'+
'    228,'+
'    1,'+
'    152,'+
'    1,'+
'    60,'+
'    1,'+
'    217,'+
'    0,'+
'    115,'+
'    0,'+
'    15,'+
'    0,'+
'    176,'+
'    255,'+
'    90,'+
'    255,'+
'    19,'+
'    255,'+
'    225,'+
'    254'+
'  ],'+
'  "MOON_ALT_SAMPLES": ['+
'    111,'+
'    0,'+
'    59,'+
'    0,'+
'    246,'+
'    255,'+
'    167,'+
'    255,'+
'    79,'+
'    255,'+
'    241,'+
'    254,'+
'    145,'+
'    254,'+
'    49,'+
'    254,'+
'    213,'+
'    253,'+
'    136,'+
'    253,'+
'    90,'+
'    253,'+
'    98,'+
'    253,'+
'    156,'+
'    253,'+
'    240,'+
'    253,'+
'    78,'+
'    254,'+
'    176,'+
'    254,'+
'    18,'+
'    255,'+
'    112,'+
'    255,'+
'    200,'+
'    255,'+
'    24,'+
'    0,'+
'    91,'+
'    0,'+
'    142,'+
'    0,'+
'    173,'+
'    0,'+
'    181,'+
'    0,'+
'    166,'+
'    0'+
'  ],'+
'  "PLANET_ALT_SAMPLES": ['+
'    250,'+
'    254,'+
'    234,'+
'    254,'+
'    246,'+
'    254,'+
'    29,'+
'    255,'+
'    91,'+
'    255,'+
'    169,'+
'    255,'+
'    3,'+
'    0,'+
'    101,'+
'    0,'+
'    201,'+
'    0,'+
'    46,'+
'    1,'+
'    142,'+
'    1,'+
'    227,'+
'    1,'+
'    34,'+
'    2,'+
'    61,'+
'    2,'+
'    43,'+
'    2,'+
'    242,'+
'    1,'+
'    160,'+
'    1,'+
'    65,'+
'    1,'+
'    221,'+
'    0,'+
'    119,'+
'    0,'+
'    20,'+
'    0,'+
'    184,'+
'    255,'+
'    102,'+
'    255,'+
'    35,'+
'    255,'+
'    246,'+
'    254,'+
'    2,'+
'    255,'+
'    165,'+
'    254,'+
'    84,'+
'    254,'+
'    27,'+
'    254,'+
'    5,'+
'    254,'+
'    24,'+
'    254,'+
'    78,'+
'    254,'+
'    157,'+
'    254,'+
'    250,'+
'    254,'+
'    94,'+
'    255,'+
'    195,'+
'    255,'+
'    39,'+
'    0,'+
'    133,'+
'    0,'+
'    216,'+
'    0,'+
'    28,'+
'    1,'+
'    73,'+
'    1,'+
'    89,'+
'    1,'+
'    74,'+
'    1,'+
'    31,'+
'    1,'+
'    220,'+
'    0,'+
'    137,'+
'    0,'+
'    43,'+
'    0,'+
'    199,'+
'    255,'+
'    98,'+
'    255,'+
'    253,'+
'    254,'+
'    138,'+
'    255,'+
'    202,'+
'    255,'+
'    25,'+
'    0,'+
'    114,'+
'    0,'+
'    211,'+
'    0,'+
'    55,'+
'    1,'+
'    157,'+
'    1,'+
'    255,'+
'    1,'+
'    86,'+
'    2,'+
'    145,'+
'    2,'+
'    155,'+
'    2,'+
'    110,'+
'    2,'+
'    29,'+
'    2,'+
'    190,'+
'    1,'+
'    89,'+
'    1,'+
'    244,'+
'    0,'+
'    145,'+
'    0,'+
'    53,'+
'    0,'+
'    227,'+
'    255,'+
'    157,'+
'    255,'+
'    106,'+
'    255,'+
'    77,'+
'    255,'+
'    73,'+
'    255,'+
'    95,'+
'    255,'+
'    139,'+
'    255,'+
'    13,'+
'    255,'+
'    26,'+
'    255,'+
'    65,'+
'    255,'+
'    126,'+
'    255,'+
'    203,'+
'    255,'+
'    36,'+
'    0,'+
'    133,'+
'    0,'+
'    234,'+
'    0,'+
'    80,'+
'    1,'+
'    178,'+
'    1,'+
'    9,'+
'    2,'+
'    74,'+
'    2,'+
'    100,'+
'    2,'+
'    76,'+
'    2,'+
'    12,'+
'    2,'+
'    182,'+
'    1,'+
'    84,'+
'    1,'+
'    238,'+
'    0,'+
'    137,'+
'    0,'+
'    40,'+
'    0,'+
'    206,'+
'    255,'+
'    128,'+
'    255,'+
'    67,'+
'    255,'+
'    27,'+
'    255,'+
'    13,'+
'    255,'+
'    228,'+
'    0,'+
'    63,'+
'    1,'+
'    138,'+
'    1,'+
'    190,'+
'    1,'+
'    209,'+
'    1,'+
'    190,'+
'    1,'+
'    138,'+
'    1,'+
'    62,'+
'    1,'+
'    227,'+
'    0,'+
'    129,'+
'    0,'+
'    27,'+
'    0,'+
'    182,'+
'    255,'+
'    86,'+
'    255,'+
'    255,'+
'    254,'+
'    184,'+
'    254,'+
'    137,'+
'    254,'+
'    122,'+
'    254,'+
'    140,'+
'    254,'+
'    189,'+
'    254,'+
'    5,'+
'    255,'+
'    93,'+
'    255,'+
'    190,'+
'    255,'+
'    35,'+
'    0,'+
'    137,'+
'    0,'+
'    235,'+
'    0'+
'  ],'+
'  "PLANET_RISE": ['+
'    79,'+
'    194,'+
'    139,'+
'    106,'+
'    135,'+
'    3,'+
'    140,'+
'    106,'+
'    140,'+
'    134,'+
'    139,'+
'    106,'+
'    63,'+
'    175,'+
'    139,'+
'    106,'+
'    235,'+
'    158,'+
'    140,'+
'    106'+
'  ],'+
'  "PLANET_SET": ['+
'    148,'+
'    136,'+
'    140,'+
'    106,'+
'    214,'+
'    153,'+
'    140,'+
'    106,'+
'    5,'+
'    100,'+
'    140,'+
'    106,'+
'    137,'+
'    125,'+
'    140,'+
'    106,'+
'    0,'+
'    0,'+
'    0,'+
'    0'+
'  ],'+
'  "STAR_ALT_SAMPLES": ['+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0'+
'  ],'+
'  "STAR_AZ_SAMPLES": ['+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0,'+
'    0'+
'  ],'+
'  "SATURN_RING_OPEN_PCT": 32,'+
'  "SKY_SCALE_MAX_ALT": 543,'+
'  "CLOUD_SAMPLES": ['+
'    32,'+
'    13,'+
'    5,'+
'    6,'+
'    0,'+
'    17,'+
'    29,'+
'    0,'+
'    15,'+
'    92,'+
'    83,'+
'    18,'+
'    100,'+
'    100,'+
'    100,'+
'    25,'+
'    95,'+
'    13,'+
'    35,'+
'    51,'+
'    73,'+
'    100,'+
'    100,'+
'    100,'+
'    100'+
'  ],'+
'  "CLOUD_ALTITUDE_PCT": 25,'+
'  "MOON_PHASE_PCT": 17,'+
'  "MOON_WAXING": 1,'+
'  "SUN_RISE": 1787545670,'+
'  "SUN_SET": 1787594692,'+
'  "SUN_RISE_TOMORROW": 1787632070,'+
'  "MOON_RISE": 1787589996,'+
'  "MOON_SET": 0,'+
'  "METEOR_INTENSITY": 0,'+
'  "METEOR_SHOWER_NAME": "",'+
'  "ISS_ALT": 0,'+
'  "ISS_AZ": 0,'+
'  "ISS_COMPUTED_AT": 0,'+
'  "ISS_NEXT_PASS": 0,'+
'  "CLOCK_FONT": 3,'+
'  "TEMP_UNIT": 0,'+
'  "WIND_SPEED_UNIT": 0,'+
'  "SHOW_SECONDS": 1,'+
'  "CUSTOM_BG": 255,'+
'  "CUSTOM_TEXT": 192,'+
'  "CUSTOM_ACCENT": 194,'+
'  "NIGHT_SCHEME_ENABLED": 1,'+
'  "NIGHT_CUSTOM_BG": 192,'+
'  "NIGHT_CUSTOM_TEXT": 255,'+
'  "NIGHT_CUSTOM_ACCENT": 240,'+
'  "BOTTOM_STYLE": 2,'+
'  "ANALOG_STYLE": 3,'+
'  "SUN_MOON_SIZE_PCT": 50,'+
'  "CLOUD_RENDER_STYLE": 0,'+
'  "SKY_MODE": 0,'+
'  "WEATHER_ICON_STYLE": 1,'+
'  "AQI_UNIT": 0,'+
'  "ALTITUDE_UNIT": 0,'+
'  "ALTITUDE_M": 380,'+
'  "WIND_DIR_DEG": 225,'+
'  "DEW_POINT_C": 12,'+
'  "PRESSURE_HPA": 1013,'+
'  "PRESSURE_TREND": 0,'+
'  "AQI_US": 42,'+
'  "AQI_EU": 18,'+
'  "SHAKE_LABEL_SECONDS": 8,'+
'  "BOTTOM_INFO_BAR_MODE": 0,'+
'  "BIG_ANALOG_HAND_STYLE": 0,'+
'  "BIG_ANALOG_HANDS_TRANSPARENT": 1,'+
'  "BIG_ANALOG_HANDS_SHADOW": 0,'+
'  "SHADOW_TRANSLUCENT": 1,'+
'  "BIG_ANALOG_MARKER_STYLE": 8,'+
'  "BITMAP_MARKER_TRANSPARENT": 0,'+
'  "DRAW_FEATURES_BENEATH_HANDS": 0,'+
'  "CUSTOM_HOUR_STYLE": 0,'+
'  "CUSTOM_HOUR_THICKNESS": 1,'+
'  "CUSTOM_HOUR_INNER_ECC": 0,'+
'  "CUSTOM_HOUR_OUTER_ECC": 100,'+
'  "CUSTOM_HOUR_INNER_BORDER": 0,'+
'  "CUSTOM_HOUR_OUTER_BORDER": 25,'+
'  "CUSTOM_HOUR_TRANSLUCENT": 0,'+
'  "CUSTOM_SEC_STYLE": 0,'+
'  "CUSTOM_SEC_THICKNESS": 1,'+
'  "CUSTOM_SEC_INNER_ECC": 0,'+
'  "CUSTOM_SEC_OUTER_ECC": 0,'+
'  "CUSTOM_SEC_INNER_BORDER": 21,'+
'  "CUSTOM_SEC_OUTER_BORDER": 45,'+
'  "CUSTOM_SEC_TRANSLUCENT": 0,'+
'  "MARKER_TEXT_TARGET": 1,'+
'  "MARKER_TEXT_FONT": 4,'+
'  "MARKER_TEXT_OFFSET": -10,'+
'  "MARKER_TEXT_HOUR_MASK": 4095,'+
'  "MARKER_TEXT_SEC_MASK": 4095,'+
'  "HAND_HOUR_STYLE": 1,'+
'  "HAND_HOUR_WIDTH": 12,'+
'  "HAND_HOUR_LENGTH": 51,'+
'  "HAND_HOUR_BACK_OFFSET": 0,'+
'  "HAND_HOUR_COLOR": 0,'+
'  "HAND_HOUR_OUTLINE_ENABLED": 1,'+
'  "HAND_HOUR_OUTLINE_COLOR": 1,'+
'  "HAND_HOUR_TRANSLUCENT": 0,'+
'  "HAND_HOUR_SHADOW_ENABLED": 0,'+
'  "HAND_HOUR_SHADOW_ANGLE": 120,'+
'  "HAND_HOUR_SHADOW_DISTANCE": 2,'+
'  "HAND_MIN_STYLE": 1,'+
'  "HAND_MIN_WIDTH": 18,'+
'  "HAND_MIN_LENGTH": 78,'+
'  "HAND_MIN_BACK_OFFSET": 0,'+
'  "HAND_MIN_COLOR": 0,'+
'  "HAND_MIN_OUTLINE_ENABLED": 1,'+
'  "HAND_MIN_OUTLINE_COLOR": 1,'+
'  "HAND_MIN_TRANSLUCENT": 0,'+
'  "HAND_MIN_SHADOW_ENABLED": 0,'+
'  "HAND_MIN_SHADOW_ANGLE": 120,'+
'  "HAND_MIN_SHADOW_DISTANCE": 2,'+
'  "HAND_SEC_STYLE": 0,'+
'  "HAND_SEC_WIDTH": 2,'+
'  "HAND_SEC_LENGTH": 85,'+
'  "HAND_SEC_BACK_OFFSET": 0,'+
'  "HAND_SEC_COLOR": 1,'+
'  "HAND_SEC_OUTLINE_ENABLED": 0,'+
'  "HAND_SEC_OUTLINE_COLOR": 0,'+
'  "HAND_SEC_TRANSLUCENT": 0,'+
'  "HAND_SEC_SHADOW_ENABLED": 0,'+
'  "HAND_SEC_SHADOW_ANGLE": 120,'+
'  "HAND_SEC_SHADOW_DISTANCE": 2,'+
'  "CENTER_CIRCLE_RADIUS": 0,'+
'  "CENTER_CIRCLE_COLOR": 0,'+
'  "UPPER_MIDDLE_LINE1_CONTENT": 18,'+
'  "UPPER_MIDDLE_LINE1_COLOR_MODE": 3,'+
'  "UPPER_MIDDLE_LINE2_CONTENT": 19,'+
'  "UPPER_MIDDLE_LINE2_COLOR_MODE": 1,'+
'  "BOTTOM_MIDDLE_LINE1_CONTENT": 12,'+
'  "BOTTOM_MIDDLE_LINE1_COLOR_MODE": 3,'+
'  "BOTTOM_MIDDLE_LINE2_CONTENT": 4,'+
'  "BOTTOM_MIDDLE_LINE2_COLOR_MODE": 3,'+
'  "MIDDLE_LEFT_LINE1_CONTENT": 6,'+
'  "MIDDLE_LEFT_LINE1_COLOR_MODE": 3,'+
'  "MIDDLE_LEFT_LINE2_CONTENT": 0,'+
'  "MIDDLE_LEFT_LINE2_COLOR_MODE": 0,'+
'  "MIDDLE_RIGHT_LINE1_CONTENT": 8,'+
'  "MIDDLE_RIGHT_LINE1_COLOR_MODE": 3,'+
'  "MIDDLE_RIGHT_LINE2_CONTENT": 0,'+
'  "MIDDLE_RIGHT_LINE2_COLOR_MODE": 0,'+
'  "SHOW_SUN_TIME": 1,'+
'  "SHOW_ISS": 1,'+
'  "VIBRATE_ON_PHASE_CHANGE": 0,'+
'  "OUTLINE_ENABLED": 1,'+
'  "CORNER_FONT_SIZE": 1,'+
'  "CORNER_CUSTOM_FONT": 5,'+
'  "CORNER_CONTENT": ['+
'    10,'+
'    12,'+
'    2,'+
'    4'+
'  ],'+
'  "CORNER_COLOR_MODE": ['+
'    3,'+
'    3,'+
'    3,'+
'    0'+
'  ],'+
'  "DAILY_STEP_GOAL": 10000'+
'}';
  
//  try {
//    var parsed = JSON.parse(debugData);
//    if (parsed && typeof parsed === 'object') {
//      toSend = parsed;
//      console.log('eclipse-watch: MANUAL DEBUG OVERRIDE active, sending static data from index.js instead of computed data');
//    }
//  } catch (e) {
//    console.log('eclipse-watch: MANUAL DEBUG OVERRIDE enabled but stored data is not valid JSON, sending normal computed data instead: ' + e);
//  }
  
  if (getSetting('CONFIG_DEBUG_OVERRIDE_ENABLED', 'false') === 'true') {
    try {
      var parsed = JSON.parse(getSetting('CONFIG_DEBUG_OVERRIDE_DATA', ''));
      if (parsed && typeof parsed === 'object') {
        toSend = parsed;
        console.log('eclipse-watch: DEBUG OVERRIDE active, sending user-edited data instead of computed data');
      }
    } catch (e) {
      console.log('eclipse-watch: DEBUG OVERRIDE enabled but stored data is not valid JSON, sending normal computed data instead: ' + e);
    }
  }

  Pebble.sendAppMessage(toSend, function () {
    console.log('eclipse-watch: data sent to watch');
  }, function (e) {
    console.log('eclipse-watch: send failed, will retry next cycle: ' + JSON.stringify(e));
  });
}

function sendInvalid(errorCode) {
  sendDict({ 'DATA_VALID': 0, 'ERROR_CODE': errorCode || 0 });
}

function issFieldsDict(issPos) {
  return {
    'ISS_ALT': issPos ? Math.round(issPos.alt) : 0,
    'ISS_AZ': issPos ? Math.round(issPos.az) : 0,
    'ISS_COMPUTED_AT': issPos ? Math.floor(Date.now() / 1000) : 0,
    'ISS_NEXT_PASS': (issPos && issPos.nextPass) ? toEpoch(issPos.nextPass) : 0
  };
}

// Bundles the newer weather-extra fields (pressure/wind direction/dew
// point/air quality) into one object param on the two send functions
// below, rather than growing their already-long positional parameter
// lists by another 6 -- extra = { windDirDeg, dewPointC, pressureHpa,
// pressureTrend, aqiUs, aqiEu }, any of which may be null/undefined.
function extraWeatherFieldsDict(extra) {
  extra = extra || {};
  return {
    'WIND_DIR_DEG': (typeof extra.windDirDeg === 'number') ? Math.round(extra.windDirDeg) : 0,
    'DEW_POINT_C': (typeof extra.dewPointC === 'number') ? Math.round(extra.dewPointC) : 0,
    'PRESSURE_HPA': (typeof extra.pressureHpa === 'number') ? Math.round(extra.pressureHpa) : 0,
    'PRESSURE_TREND': extra.pressureTrend || 0,
    'AQI_US': (typeof extra.aqiUs === 'number') ? extra.aqiUs : 0,
    'AQI_EU': (typeof extra.aqiEu === 'number') ? extra.aqiEu : 0,
    // -32000 = sentinel for "no altitude available" (many phones don't
    // report GPS altitude, and manual-coordinates mode never has it --
    // see getLocation()). A real altitude can legitimately be negative
    // (Death Valley, the Dead Sea shore) or exactly 0 (sea level), so
    // those can't double as the "missing" signal the way they might
    // elsewhere -- this needs its own out-of-range sentinel instead.
    'ALTITUDE_M': (typeof extra.altitudeMeters === 'number') ? Math.round(extra.altitudeMeters) : -32000
  };
}

function sendNoEclipseToday(sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, weatherCondition, weatherTempC, meteorShower, cloudAltitudePct, tempHighC, tempLowC, issPos, uvIndexMax, rainChancePct, humidityPct, windSpeedKmh, currentCloudPct, sunRiseTomorrow, extraWeather) {
  var displayCloudPct = (typeof currentCloudPct === 'number') ? currentCloudPct : (headlineCloud || 0);
  var dict = {
    'DATA_VALID': 1,
    'ECLIPSE_TYPE': TYPE_CODE.none,
    'C1_TIME': 0, 'C2_TIME': 0, 'MAX_TIME': 0, 'C3_TIME': 0, 'C4_TIME': 0, 'SUNSET_TIME': 0,
    'MAGNITUDE': 0,
    'POS_ANGLE': 0,
    'SAMPLE_START': 0, 'SAMPLE_INTERVAL': 0, 'SAMPLE_COUNT': 0,
    'SEP_SAMPLES': [0, 0],
    'MAG_SAMPLES': [0, 0],
    'RADIUS_RATIO_PCT': 0,
    'CLOUD_COVER': displayCloudPct,
    'VIS_SCORE': 100 - displayCloudPct,
    'WEATHER_SOURCES': headlineSources || 0,
    'WEATHER_CONDITION': weatherCondition || 0,
    'WEATHER_TEMP_C': (typeof weatherTempC === 'number') ? Math.round(weatherTempC) : 0,
    'WEATHER_TEMP_HIGH_C': (typeof tempHighC === 'number') ? Math.round(tempHighC) : 0,
    'WEATHER_TEMP_LOW_C': (typeof tempLowC === 'number') ? Math.round(tempLowC) : 0,
    'UV_INDEX_X10': (typeof uvIndexMax === 'number') ? Math.round(Math.max(0, Math.min(25.5, uvIndexMax)) * 10) : 0,
    'RAIN_CHANCE_PCT': (typeof rainChancePct === 'number') ? Math.round(rainChancePct) : 0,
    'HUMIDITY_PCT': (typeof humidityPct === 'number') ? Math.round(humidityPct) : 0,
    'WIND_SPEED_KMH': (typeof windSpeedKmh === 'number') ? Math.round(windSpeedKmh) : 0,
    'LOCATION_NAME': locationName || ''
  };
  var sky_ = skyFieldsDict(sky, cloudGrid, moonPhase, riseSet, meteorShower, cloudAltitudePct, sunRiseTomorrow, stars);
  Object.keys(sky_).forEach(function (k) { dict[k] = sky_[k]; });
  var extraW_ = extraWeatherFieldsDict(extraWeather);
  Object.keys(extraW_).forEach(function (k) { dict[k] = extraW_[k]; });
  var iss_ = issFieldsDict(issPos);
  Object.keys(iss_).forEach(function (k) { dict[k] = iss_[k]; });
  sendDict(dict);
}

function sendEclipseData(result, sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, weatherCondition, weatherTempC, meteorShower, cloudAltitudePct, tempHighC, tempLowC, issPos, uvIndexMax, rainChancePct, humidityPct, windSpeedKmh, currentCloudPct, sunRiseTomorrow, extraWeather) {
  var displayCloudPct = (typeof currentCloudPct === 'number') ? currentCloudPct : (headlineCloud || 0);
  var dict = {
    'DATA_VALID': 1,
    'C1_TIME': toEpoch(result.c1),
    'C2_TIME': toEpoch(result.c2),
    'MAX_TIME': toEpoch(result.max),
    'C3_TIME': toEpoch(result.c3),
    'C4_TIME': toEpoch(result.c4),
    'SUNSET_TIME': toEpoch(result.sunset),
    'MAGNITUDE': result.magnitudePct,
    'ECLIPSE_TYPE': TYPE_CODE[result.type] || 0,
    'POS_ANGLE': result.posAngleDeg,
    'SAMPLE_START': toEpoch(result.sampleStart),
    'SAMPLE_INTERVAL': result.sampleIntervalS,
    'SAMPLE_COUNT': result.sepSamplesCentideg.length,
    'SEP_SAMPLES': u16ArrayToBytes(result.sepSamplesCentideg),
    'MAG_SAMPLES': result.magPctSamples,
    'RADIUS_RATIO_PCT': result.radiusRatioPct,
    'CLOUD_COVER': displayCloudPct,
    'VIS_SCORE': 100 - displayCloudPct,
    'WEATHER_SOURCES': headlineSources || 0,
    'WEATHER_CONDITION': weatherCondition || 0,
    'WEATHER_TEMP_C': (typeof weatherTempC === 'number') ? Math.round(weatherTempC) : 0,
    'WEATHER_TEMP_HIGH_C': (typeof tempHighC === 'number') ? Math.round(tempHighC) : 0,
    'WEATHER_TEMP_LOW_C': (typeof tempLowC === 'number') ? Math.round(tempLowC) : 0,
    'UV_INDEX_X10': (typeof uvIndexMax === 'number') ? Math.round(Math.max(0, Math.min(25.5, uvIndexMax)) * 10) : 0,
    'RAIN_CHANCE_PCT': (typeof rainChancePct === 'number') ? Math.round(rainChancePct) : 0,
    'HUMIDITY_PCT': (typeof humidityPct === 'number') ? Math.round(humidityPct) : 0,
    'WIND_SPEED_KMH': (typeof windSpeedKmh === 'number') ? Math.round(windSpeedKmh) : 0,
    'LOCATION_NAME': locationName || ''
  };
  var sky_ = skyFieldsDict(sky, cloudGrid, moonPhase, riseSet, meteorShower, cloudAltitudePct, sunRiseTomorrow, stars);
  Object.keys(sky_).forEach(function (k) { dict[k] = sky_[k]; });
  var extraW_ = extraWeatherFieldsDict(extraWeather);
  Object.keys(extraW_).forEach(function (k) { dict[k] = extraW_[k]; });
  var iss_ = issFieldsDict(issPos);
  Object.keys(iss_).forEach(function (k) { dict[k] = iss_[k]; });
  sendDict(dict);
}

// ---- location naming (cached reverse geocode) -----------------------------

// Only re-geocode when the location has moved more than ~5km (0.05
// degrees is a rough approximation, fine at the scale this matters
// for), so a stationary watch doesn't hit Nominatim on every refresh
// -- keeps us well within their usage policy and avoids pointless
// network calls for a name that isn't going to have changed.
var GEOCODE_MOVE_THRESHOLD_DEG = 0.05;

function getLocationName(lat, lon, cb) {
  var cachedLat = parseFloat(getSetting('CACHE_GEOCODE_LAT', ''));
  var cachedLon = parseFloat(getSetting('CACHE_GEOCODE_LON', ''));
  var cachedName = getSetting('CACHE_GEOCODE_NAME', '');

  var moved = isNaN(cachedLat) || isNaN(cachedLon) ||
    Math.abs(cachedLat - lat) > GEOCODE_MOVE_THRESHOLD_DEG ||
    Math.abs(cachedLon - lon) > GEOCODE_MOVE_THRESHOLD_DEG;

  if (!moved && cachedName) {
    cb(cachedName);
    return;
  }

  geocode.reverseGeocode(lat, lon, function (err, name) {
    if (err) {
      console.log('eclipse-watch: reverse geocode failed - ' + err.message);
      cb(cachedName || ''); // fall back to a stale name rather than nothing
      return;
    }
    setSetting('CACHE_GEOCODE_LAT', String(lat));
    setSetting('CACHE_GEOCODE_LON', String(lon));
    setSetting('CACHE_GEOCODE_NAME', name);
    cb(name);
  });
}

function getLocation(cb) {
  var autoLoc = getSetting('CONFIG_AUTO_LOC', 'true') !== 'false';

  if (!autoLoc) {
    var lat = parseFloat(getSetting('CONFIG_LAT', ''));
    var lon = parseFloat(getSetting('CONFIG_LON', ''));
    if (!isNaN(lat) && !isNaN(lon)) {
      cb(null, lat, lon, null); // no altitude in manual-coordinates mode
      return;
    }
    console.log('eclipse-watch: manual location selected but not set yet, falling back to GPS');
  }

  if (!navigator.geolocation) {
    cb(new Error('no geolocation available'));
    return;
  }

  navigator.geolocation.getCurrentPosition(
    function (pos) {
      // altitude is meters above the WGS84 ellipsoid when the device
      // can supply it (GPS fix with altitude support) -- null on many
      // phones/emulators, in which case we just skip the horizon-dip
      // correction rather than guessing.
      cb(null, pos.coords.latitude, pos.coords.longitude, pos.coords.altitude);
    },
    function (err) {
      var reason = 'unknown (' + err.code + ')';
      if (err.code === 1) reason = 'permission denied -- check the location permission for this app on your phone';
      else if (err.code === 2) reason = 'position unavailable -- no GPS/network fix';
      else if (err.code === 3) reason = 'timed out waiting for a GPS fix';
      cb(new Error('geolocation failed: ' + reason));
    },
    { timeout: 15000, maximumAge: 600000, enableHighAccuracy: false }
  );
}

// Standard horizon-dip approximation: dip (arcminutes) ~= 1.76 *
// sqrt(height in meters). A higher vantage point genuinely sees a
// lower horizon, so the Sun/Moon should appear to rise a little
// earlier and set a little later -- modeled here simply by adding
// this to their computed altitude before mapping to a screen
// position, which is equivalent for our purposes.
function horizonDipDeg(altitudeMeters) {
  if (!altitudeMeters || altitudeMeters <= 0) return 0;
  return (1.76 * Math.sqrt(altitudeMeters)) / 60;
}

// ---- smart refresh (skip re-fetching if nothing's likely changed) --------

function haversineKm(lat1, lon1, lat2, lon2) {
  var R = 6371;
  var dLat = (lat2 - lat1) * Math.PI / 180;
  var dLon = (lon2 - lon1) * Math.PI / 180;
  var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
          Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
          Math.sin(dLon / 2) * Math.sin(dLon / 2);
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

function markRefreshDone(lat, lon) {
  setSetting('CACHE_LAST_FETCH_TS', String(Date.now()));
  setSetting('CACHE_LAST_FETCH_LAT', String(lat));
  setSetting('CACHE_LAST_FETCH_LON', String(lon));
}

// True if we fetched recently enough (within the user's refresh
// interval) AND haven't moved more than ~10km since -- in which case
// there's nothing meaningful to gain from hitting the network again.
// A location change bypasses this regardless of how recently we
// fetched, since that's exactly the case where stale data would
// actually be wrong rather than just slightly dated.
function shouldSkipRefresh(lat, lon) {
  var lastTs = parseInt(getSetting('CACHE_LAST_FETCH_TS', '0'), 10);
  var lastLat = parseFloat(getSetting('CACHE_LAST_FETCH_LAT', ''));
  var lastLon = parseFloat(getSetting('CACHE_LAST_FETCH_LON', ''));
  if (!lastTs || isNaN(lastLat) || isNaN(lastLon)) return false;

  var mins = parseInt(getSetting('CONFIG_UPDATE_MINS', '20'), 10);
  if (isNaN(mins) || mins < 5) mins = 20;
  if (Date.now() - lastTs >= mins * 60000) return false;

  return haversineKm(lat, lon, lastLat, lastLon) < 10;
}

// Only fetches ISS elements when the user has explicitly opted in
// (per the brief -- this needs a live external data source, unlike
// the planets, so it's opt-in rather than always-on). Fails open: a
// fetch error just means no ISS this cycle, not a hard refresh
// failure -- everything else in the payload is still good. Fires
// either when the sky-view "Show ISS" toggle is on, or when the
// separate "Next ISS pass" corner content (id 83, text-only, no sky
// dot) is picked anywhere -- same "only fetch if actually shown
// somewhere" reasoning as fetchAirQualityIfEnabled below, since both
// draw from the exact same Celestrak fetch + astro.js computation.
function fetchIssIfEnabled(lat, lon, cb) {
  var nextPassInUse = AQI_SLOT_CONTENT_KEYS.some(function (key) { return getSetting(key, '0') === '83'; });
  if (getSetting('CONFIG_SHOW_ISS', 'false') !== 'true' && !nextPassInUse) {
    return cb(null);
  }
  iss.getIssPosition(lat, lon, function (err, pos) {
    if (err) {
      console.log('eclipse-watch: ISS fetch failed - ' + err.message);
      return cb(null);
    }
    cb(pos);
  });
}

// Same "only fetch if actually shown somewhere" gate as fetchIssIfEnabled
// above -- there's no dedicated CONFIG_SHOW_AQI checkbox, so this checks
// the 12 corner/edge slots directly for content id 36 ("Air quality").
var AQI_SLOT_CONTENT_KEYS = ['CONFIG_CORNER_TL', 'CONFIG_CORNER_TR',
  'CONFIG_CORNER_BL', 'CONFIG_CORNER_BR',
  'CONFIG_UPPER_MIDDLE_LINE1_CONTENT', 'CONFIG_UPPER_MIDDLE_LINE2_CONTENT',
  'CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT', 'CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT',
  'CONFIG_MIDDLE_LEFT_LINE1_CONTENT', 'CONFIG_MIDDLE_LEFT_LINE2_CONTENT',
  'CONFIG_MIDDLE_RIGHT_LINE1_CONTENT', 'CONFIG_MIDDLE_RIGHT_LINE2_CONTENT'];
function fetchAirQualityIfEnabled(lat, lon, cb) {
  var inUse = AQI_SLOT_CONTENT_KEYS.some(function (key) { return getSetting(key, '0') === '36'; });
  if (!inUse) return cb({ aqiUs: null, aqiEu: null });
  weather.fetchAirQuality(lat, lon, function (err, aqi) {
    if (err) {
      console.log('eclipse-watch: air quality fetch failed - ' + err.message);
      return cb({ aqiUs: null, aqiEu: null });
    }
    cb(aqi);
  });
}

// ---- main refresh cycle --------------------------------------------------

function refreshAndSend(force) {
  console.log('eclipse-watch: refresh starting' + (force ? ' (forced)' : ''));
  s_refreshGeneration++;
  var myGeneration = s_refreshGeneration;
  // True once a newer refreshAndSend() call has started since this one
  // did -- checked before every point below that would send data to
  // the watch, so a slow older request can never clobber a newer one's
  // result (see the s_refreshGeneration comment above).
  function isStale() { return myGeneration !== s_refreshGeneration; }

  getLocation(function (err, lat, lon, altitudeMeters) {
    if (isStale()) { console.log('eclipse-watch: refresh superseded, discarding (location step)'); return; }
    if (err) {
      console.log('eclipse-watch: LOCATION FAILED - ' + err.message);
      sendInvalid(1);
      return;
    }
    console.log('eclipse-watch: location = ' + lat + ', ' + lon +
      (altitudeMeters ? (', altitude ' + Math.round(altitudeMeters) + 'm') : ''));

    if (!force && shouldSkipRefresh(lat, lon)) {
      console.log('eclipse-watch: skipping refresh - fetched recently and location unchanged (<10km)');
      return;
    }

    var now = getEffectiveNow();
    var dayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0, 0);
    var dip = horizonDipDeg(altitudeMeters);

    if (getSetting('CONFIG_TEST_MODE', 'false') === 'true') {
      console.log('eclipse-watch: *** TEST MODE IS ON *** using simulated time ' + now.toString() +
        ' instead of the real current time -- if the sky looks stuck/wrong, turn this off in settings ' +
        '(and remember it only overrides what the PHONE calculates; the watch\'s own clock is separate).');
    }

    var result, sky, stars, moonPhase, riseSet, meteorShower;
    try {
      result = astro.findEclipse(dayStart, lat, lon);
      sky = astro.computeDaySkySamples(dayStart, lat, lon, dip);
      stars = astro.computeVisibleStars(now, lat, lon);
      moonPhase = astro.computeMoonPhase(now);
      riseSet = {
        sun: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.sunAlt; }),
        moon: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.moonAlt; }),
        mercury: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.mercuryAlt; }),
        venus: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.venusAlt; }),
        mars: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.marsAlt; }),
        jupiter: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.jupiterAlt; }),
        saturn: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.saturnAlt; })
      };
      // Tomorrow's sunrise too -- once today's sunset has passed, the
      // watch has nothing left to count down to otherwise (today's
      // sun_rise is already in the past), and used to just show "--:--".
      var nextDayStart = new Date(dayStart.getTime() + 86400000);
      var sunRiseTomorrow = astro.findRiseSet(nextDayStart, lat, lon, function (g) { return g.sunAlt; }).rise;
      meteorShower = astro.activeMeteorShower(now);
    } catch (e) {
      console.log('eclipse-watch: CALC FAILED - ' + e.message);
      if (!isStale()) sendInvalid(2);
      return;
    }
    console.log('eclipse-watch: ' + (result.hasEclipse ? ('eclipse found, type=' + result.type) : 'no eclipse today at this location') +
      '; moon ' + moonPhase.illuminatedPct + '% lit, ' + (moonPhase.waxing ? 'waxing' : 'waning') +
      (meteorShower ? ('; ' + meteorShower.name + ' intensity ' + meteorShower.intensity) : ''));

    // The headline "Clouds X% Vis Y%" stat covers the eclipse window
    // if there is one, otherwise just the next few hours -- separate
    // from (and optionally blended across more sources than) the
    // full-day background grid below.
    var headlineFrom = result.hasEclipse ? result.c1 : now;
    var headlineTo = result.hasEclipse ? result.c4 : new Date(now.getTime() + 3 * 3600000);
    var owmKey = getSetting('CONFIG_OWM_KEY', '');

    getLocationName(lat, lon, function (locationName) {
      weather.getDailyCloudGrid(lat, lon, sky.times, now, function (gridErr, cloudGrid, extras) {
        if (gridErr) console.log('eclipse-watch: sky cloud grid fetch failed - ' + gridErr.message);

        // Open-Meteo's own sunrise/sunset accounts for standard
        // atmospheric refraction properly and is more accurate than
        // our reduced-term ephemeris -- use it to refine the
        // eclipse's sunset field when we have it and it's actually
        // today's (not tomorrow's, if the API's day boundary landed
        // differently than ours).
        if (result.hasEclipse && extras.sunset &&
            extras.sunset.getFullYear() === dayStart.getFullYear() &&
            extras.sunset.getMonth() === dayStart.getMonth() &&
            extras.sunset.getDate() === dayStart.getDate()) {
          result.sunset = extras.sunset;
        }

        weather.getEclipseWeather(lat, lon, owmKey || null, headlineFrom, headlineTo, function (w) {
          var headlineCloud = (w.cloudCoverPct === 255) ? 0 : w.cloudCoverPct;
          var headlineSources = w.sourceCount;

          fetchIssIfEnabled(lat, lon, function (issPos) {
            fetchAirQualityIfEnabled(lat, lon, function (aqi) {
              if (isStale()) { console.log('eclipse-watch: refresh superseded, discarding (final step)'); return; }
              var extraWeather = {
                windDirDeg: extras.windDirDeg, dewPointC: extras.dewPointC,
                pressureHpa: extras.pressureHpa, pressureTrend: extras.pressureTrend,
                aqiUs: aqi.aqiUs, aqiEu: aqi.aqiEu, altitudeMeters: altitudeMeters
              };
              if (result.hasEclipse) {
                sendEclipseData(result, sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, extras.condition, extras.tempC, meteorShower, extras.cloudAltitudePct, extras.tempHighC, extras.tempLowC, issPos, extras.uvIndexMax, extras.rainChancePct, extras.humidityPct, extras.windSpeedKmh, extras.currentCloudPct, sunRiseTomorrow, extraWeather);
              } else {
                sendNoEclipseToday(sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, extras.condition, extras.tempC, meteorShower, extras.cloudAltitudePct, extras.tempHighC, extras.tempLowC, issPos, extras.uvIndexMax, extras.rainChancePct, extras.humidityPct, extras.windSpeedKmh, extras.currentCloudPct, sunRiseTomorrow, extraWeather);
              }
              markRefreshDone(lat, lon);
            });
          });
        });
      });
    });
  });
}

function scheduleRefresh() {
  if (refreshTimer) clearInterval(refreshTimer);
  var mins = parseInt(getSetting('CONFIG_UPDATE_MINS', '20'), 10);
  if (isNaN(mins) || mins < 5) mins = 20;
  refreshTimer = setInterval(refreshAndSend, mins * 60000);
}

// ---- Pebble lifecycle --------------------------------------------------

Pebble.addEventListener('ready', function () {
  console.log('eclipse-watch: PKJS ready, capabilities OK, starting first refresh');
  refreshAndSend(false);
  scheduleRefresh();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e && e.payload && e.payload.REQUEST_UPDATE) {
    // The watch sends this both on every app launch/relaunch and on
    // a deliberate select-button press -- we can't tell which, so
    // this respects the smart-refresh skip too rather than treating
    // every watchface start as a reason to hit the network.
    refreshAndSend(false);
  }
});

Pebble.addEventListener('showConfiguration', function () {
  var html = configPage.buildConfigHtml({
    autoLoc: getSetting('CONFIG_AUTO_LOC', 'true') !== 'false',
    lat: getSetting('CONFIG_LAT', ''),
    lon: getSetting('CONFIG_LON', ''),
    owmKey: getSetting('CONFIG_OWM_KEY', ''),
    updateMins: getSetting('CONFIG_UPDATE_MINS', '20'),
    clockFont: getSetting('CONFIG_CLOCK_FONT', 'leco'),
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
    analogStyle: getSetting('CONFIG_ANALOG_STYLE', '0'),
    sunMoonSize: getSetting('CONFIG_SUN_MOON_SIZE', '75'),
    cloudRenderStyle: getSetting('CONFIG_CLOUD_RENDER_STYLE', '1'),
    skyMode: getSetting('CONFIG_SKY_MODE', '0'),
    weatherIconStyle: getSetting('CONFIG_WEATHER_ICON_STYLE', '1'),
    aqiUnit: getSetting('CONFIG_AQI_UNIT', '0'),
    altitudeUnit: getSetting('CONFIG_ALTITUDE_UNIT', '0'),
    shakeLabelSeconds: getSetting('CONFIG_SHAKE_LABEL_SECONDS', '3'),
    bottomInfoBarMode: getSetting('CONFIG_BOTTOM_INFO_BAR_MODE', '1'),
    bigAnalogHandStyle: getSetting('CONFIG_BIG_ANALOG_HAND_STYLE', '0'),
    bigAnalogTransparent: getSetting('CONFIG_BIG_ANALOG_TRANSPARENT', 'false') === 'true',
    bigAnalogHandsShadow: getSetting('CONFIG_BIG_ANALOG_HANDS_SHADOW', 'false') === 'true',
    shadowTranslucent: getSetting('CONFIG_SHADOW_TRANSLUCENT', 'true'),
    bigAnalogMarkerStyle: getSetting('CONFIG_BIG_ANALOG_MARKER_STYLE', '0'),
    bitmapMarkerTransparent: getSetting('CONFIG_BITMAP_MARKER_TRANSPARENT', 'false') === 'true',
    drawFeaturesBeneathHands: getSetting('CONFIG_DRAW_FEATURES_BENEATH_HANDS', 'false') === 'true',
    customHourStyle: getSetting('CONFIG_CUSTOM_HOUR_STYLE', '0'),
    customHourThickness: getSetting('CONFIG_CUSTOM_HOUR_THICKNESS', '3'),
    customHourInnerEcc: getSetting('CONFIG_CUSTOM_HOUR_INNER_ECC', '0'),
    customHourOuterEcc: getSetting('CONFIG_CUSTOM_HOUR_OUTER_ECC', '0'),
    customHourInnerBorder: getSetting('CONFIG_CUSTOM_HOUR_INNER_BORDER', '20'),
    customHourOuterBorder: getSetting('CONFIG_CUSTOM_HOUR_OUTER_BORDER', '100'),
    customHourTranslucent: getSetting('CONFIG_CUSTOM_HOUR_TRANSLUCENT', 'false'),
    customSecStyle: getSetting('CONFIG_CUSTOM_SEC_STYLE', '0'),
    customSecThickness: getSetting('CONFIG_CUSTOM_SEC_THICKNESS', '1'),
    customSecInnerEcc: getSetting('CONFIG_CUSTOM_SEC_INNER_ECC', '0'),
    customSecOuterEcc: getSetting('CONFIG_CUSTOM_SEC_OUTER_ECC', '0'),
    customSecInnerBorder: getSetting('CONFIG_CUSTOM_SEC_INNER_BORDER', '70'),
    customSecOuterBorder: getSetting('CONFIG_CUSTOM_SEC_OUTER_BORDER', '100'),
    customSecTranslucent: getSetting('CONFIG_CUSTOM_SEC_TRANSLUCENT', 'false'),
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
    handHourColor: getSetting('CONFIG_HAND_HOUR_COLOR', '0'),
    handHourOutlineEnabled: getSetting('CONFIG_HAND_HOUR_OUTLINE_ENABLED', 'false'),
    handHourOutlineColor: getSetting('CONFIG_HAND_HOUR_OUTLINE_COLOR', '0'),
    handHourTranslucent: getSetting('CONFIG_HAND_HOUR_TRANSLUCENT', 'false'),
    handHourShadowEnabled: getSetting('CONFIG_HAND_HOUR_SHADOW_ENABLED', 'false'),
    handHourShadowAngle: getSetting('CONFIG_HAND_HOUR_SHADOW_ANGLE', '120'),
    handHourShadowDistance: getSetting('CONFIG_HAND_HOUR_SHADOW_DISTANCE', '2'),
    handMinStyle: getSetting('CONFIG_HAND_MIN_STYLE', '1'),
    handMinWidth: getSetting('CONFIG_HAND_MIN_WIDTH', '18'),
    handMinLength: getSetting('CONFIG_HAND_MIN_LENGTH', '78'),
    handMinBackOffset: getSetting('CONFIG_HAND_MIN_BACK_OFFSET', '0'),
    handMinColor: getSetting('CONFIG_HAND_MIN_COLOR', '0'),
    handMinOutlineEnabled: getSetting('CONFIG_HAND_MIN_OUTLINE_ENABLED', 'false'),
    handMinOutlineColor: getSetting('CONFIG_HAND_MIN_OUTLINE_COLOR', '0'),
    handMinTranslucent: getSetting('CONFIG_HAND_MIN_TRANSLUCENT', 'false'),
    handMinShadowEnabled: getSetting('CONFIG_HAND_MIN_SHADOW_ENABLED', 'false'),
    handMinShadowAngle: getSetting('CONFIG_HAND_MIN_SHADOW_ANGLE', '120'),
    handMinShadowDistance: getSetting('CONFIG_HAND_MIN_SHADOW_DISTANCE', '2'),
    handSecStyle: getSetting('CONFIG_HAND_SEC_STYLE', '0'),
    handSecWidth: getSetting('CONFIG_HAND_SEC_WIDTH', '2'),
    handSecLength: getSetting('CONFIG_HAND_SEC_LENGTH', '85'),
    handSecBackOffset: getSetting('CONFIG_HAND_SEC_BACK_OFFSET', '0'),
    handSecColor: getSetting('CONFIG_HAND_SEC_COLOR', '1'),
    handSecOutlineEnabled: getSetting('CONFIG_HAND_SEC_OUTLINE_ENABLED', 'false'),
    handSecOutlineColor: getSetting('CONFIG_HAND_SEC_OUTLINE_COLOR', '0'),
    handSecTranslucent: getSetting('CONFIG_HAND_SEC_TRANSLUCENT', 'false'),
    handSecShadowEnabled: getSetting('CONFIG_HAND_SEC_SHADOW_ENABLED', 'false'),
    handSecShadowAngle: getSetting('CONFIG_HAND_SEC_SHADOW_ANGLE', '120'),
    handSecShadowDistance: getSetting('CONFIG_HAND_SEC_SHADOW_DISTANCE', '2'),
    centerCircleRadius: getSetting('CONFIG_CENTER_CIRCLE_RADIUS', '3'),
    centerCircleColor: getSetting('CONFIG_CENTER_CIRCLE_COLOR', '0'),
    upperMiddleLine1Content: getSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT', '0'),
    upperMiddleLine1Color: getSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR', '0'),
    upperMiddleLine2Content: getSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT', '0'),
    upperMiddleLine2Color: getSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR', '0'),
    bottomMiddleLine1Content: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT', '12'),
    bottomMiddleLine1Color: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR', '0'),
    bottomMiddleLine2Content: getSetting('CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT', '0'),
    bottomMiddleLine2Color: getSetting('CONFIG_BOTTOM_MIDDLE_LINE2_COLOR', '0'),
    middleLeftLine1Content: getSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT', '0'),
    middleLeftLine1Color: getSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR', '0'),
    middleLeftLine2Content: getSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT', '0'),
    middleLeftLine2Color: getSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR', '0'),
    middleRightLine1Content: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT', '0'),
    middleRightLine1Color: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR', '0'),
    middleRightLine2Content: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT', '0'),
    middleRightLine2Color: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR', '0'),
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
    vibrateOnPhaseChange: getSetting('CONFIG_VIBRATE_ON_PHASE_CHANGE', 'false') === 'true',
    outlineEnabled: getSetting('CONFIG_OUTLINE_ENABLED', 'true') === 'true',
    cornerFontSize: getSetting('CONFIG_CORNER_FONT_SIZE', '1'),
    cornerCustomFont: getSetting('CONFIG_CORNER_CUSTOM_FONT', '0'),
    testMode: getSetting('CONFIG_TEST_MODE', 'false') === 'true',
    testDateTime: getSetting('CONFIG_TEST_DATETIME', ''),
    lastSentData: (function () {
      try {
        var raw = localStorage.getItem('LAST_COMPUTED_DICT');
        return raw ? JSON.stringify(JSON.parse(raw), null, 2) : '';
      } catch (e) {
        return '';
      }
    })(),
    debugOverrideEnabled: getSetting('CONFIG_DEBUG_OVERRIDE_ENABLED', 'false') === 'true',
    debugOverrideData: getSetting('CONFIG_DEBUG_OVERRIDE_DATA', '')
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

  setSetting('CONFIG_AUTO_LOC', settings.CONFIG_AUTO_LOC ? 'true' : 'false');
  setSetting('CONFIG_LAT', settings.CONFIG_LAT || '');
  setSetting('CONFIG_LON', settings.CONFIG_LON || '');
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
  setSetting('CONFIG_BOTTOM_STYLE', (settings.CONFIG_BOTTOM_STYLE === 'analog' || settings.CONFIG_BOTTOM_STYLE === 'biganalog') ? settings.CONFIG_BOTTOM_STYLE : 'digital');
  setSetting('CONFIG_ANALOG_STYLE', settings.CONFIG_ANALOG_STYLE || '0');
  setSetting('CONFIG_SUN_MOON_SIZE', settings.CONFIG_SUN_MOON_SIZE || '100');
  setSetting('CONFIG_CLOUD_RENDER_STYLE', settings.CONFIG_CLOUD_RENDER_STYLE || '1');
  setSetting('CONFIG_SKY_MODE', settings.CONFIG_SKY_MODE || '0');
  setSetting('CONFIG_WEATHER_ICON_STYLE', settings.CONFIG_WEATHER_ICON_STYLE || '1');
  setSetting('CONFIG_AQI_UNIT', settings.CONFIG_AQI_UNIT || '0');
  setSetting('CONFIG_ALTITUDE_UNIT', settings.CONFIG_ALTITUDE_UNIT || '0');
  setSetting('CONFIG_SHAKE_LABEL_SECONDS', settings.CONFIG_SHAKE_LABEL_SECONDS || '3');
  setSetting('CONFIG_BOTTOM_INFO_BAR_MODE', settings.CONFIG_BOTTOM_INFO_BAR_MODE || '1');
  setSetting('CONFIG_BIG_ANALOG_HAND_STYLE', settings.CONFIG_BIG_ANALOG_HAND_STYLE || '0');
  setSetting('CONFIG_BIG_ANALOG_TRANSPARENT', settings.CONFIG_BIG_ANALOG_TRANSPARENT ? 'true' : 'false');
  setSetting('CONFIG_BIG_ANALOG_HANDS_SHADOW', settings.CONFIG_BIG_ANALOG_HANDS_SHADOW ? 'true' : 'false');
  setSetting('CONFIG_SHADOW_TRANSLUCENT', settings.CONFIG_SHADOW_TRANSLUCENT || 'true');
  setSetting('CONFIG_BIG_ANALOG_MARKER_STYLE', settings.CONFIG_BIG_ANALOG_MARKER_STYLE || '0');
  setSetting('CONFIG_BITMAP_MARKER_TRANSPARENT', settings.CONFIG_BITMAP_MARKER_TRANSPARENT ? 'true' : 'false');
  setSetting('CONFIG_DRAW_FEATURES_BENEATH_HANDS', settings.CONFIG_DRAW_FEATURES_BENEATH_HANDS ? 'true' : 'false');
  setSetting('CONFIG_CUSTOM_HOUR_STYLE', settings.CONFIG_CUSTOM_HOUR_STYLE || '0');
  setSetting('CONFIG_CUSTOM_HOUR_THICKNESS', settings.CONFIG_CUSTOM_HOUR_THICKNESS || '3');
  setSetting('CONFIG_CUSTOM_HOUR_INNER_ECC', settings.CONFIG_CUSTOM_HOUR_INNER_ECC || '0');
  setSetting('CONFIG_CUSTOM_HOUR_OUTER_ECC', settings.CONFIG_CUSTOM_HOUR_OUTER_ECC || '0');
  setSetting('CONFIG_CUSTOM_HOUR_INNER_BORDER', settings.CONFIG_CUSTOM_HOUR_INNER_BORDER || '20');
  setSetting('CONFIG_CUSTOM_HOUR_OUTER_BORDER', settings.CONFIG_CUSTOM_HOUR_OUTER_BORDER || '100');
  setSetting('CONFIG_CUSTOM_HOUR_TRANSLUCENT', settings.CONFIG_CUSTOM_HOUR_TRANSLUCENT ? 'true' : 'false');
  setSetting('CONFIG_CUSTOM_SEC_STYLE', settings.CONFIG_CUSTOM_SEC_STYLE || '0');
  setSetting('CONFIG_CUSTOM_SEC_THICKNESS', settings.CONFIG_CUSTOM_SEC_THICKNESS || '1');
  setSetting('CONFIG_CUSTOM_SEC_INNER_ECC', settings.CONFIG_CUSTOM_SEC_INNER_ECC || '0');
  setSetting('CONFIG_CUSTOM_SEC_OUTER_ECC', settings.CONFIG_CUSTOM_SEC_OUTER_ECC || '0');
  setSetting('CONFIG_CUSTOM_SEC_INNER_BORDER', settings.CONFIG_CUSTOM_SEC_INNER_BORDER || '70');
  setSetting('CONFIG_CUSTOM_SEC_OUTER_BORDER', settings.CONFIG_CUSTOM_SEC_OUTER_BORDER || '100');
  setSetting('CONFIG_CUSTOM_SEC_TRANSLUCENT', settings.CONFIG_CUSTOM_SEC_TRANSLUCENT ? 'true' : 'false');
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
  setSetting('CONFIG_HAND_HOUR_COLOR', settings.CONFIG_HAND_HOUR_COLOR || '0');
  setSetting('CONFIG_HAND_HOUR_OUTLINE_ENABLED', settings.CONFIG_HAND_HOUR_OUTLINE_ENABLED || 'false');
  setSetting('CONFIG_HAND_HOUR_OUTLINE_COLOR', settings.CONFIG_HAND_HOUR_OUTLINE_COLOR || '0');
  setSetting('CONFIG_HAND_HOUR_TRANSLUCENT', settings.CONFIG_HAND_HOUR_TRANSLUCENT || 'false');
  setSetting('CONFIG_HAND_HOUR_SHADOW_ENABLED', settings.CONFIG_HAND_HOUR_SHADOW_ENABLED || 'false');
  setSetting('CONFIG_HAND_HOUR_SHADOW_ANGLE', settings.CONFIG_HAND_HOUR_SHADOW_ANGLE || '120');
  setSetting('CONFIG_HAND_HOUR_SHADOW_DISTANCE', settings.CONFIG_HAND_HOUR_SHADOW_DISTANCE || '2');
  setSetting('CONFIG_HAND_MIN_STYLE', settings.CONFIG_HAND_MIN_STYLE || '1');
  setSetting('CONFIG_HAND_MIN_WIDTH', settings.CONFIG_HAND_MIN_WIDTH || '18');
  setSetting('CONFIG_HAND_MIN_LENGTH', settings.CONFIG_HAND_MIN_LENGTH || '78');
  setSetting('CONFIG_HAND_MIN_BACK_OFFSET', settings.CONFIG_HAND_MIN_BACK_OFFSET || '0');
  setSetting('CONFIG_HAND_MIN_COLOR', settings.CONFIG_HAND_MIN_COLOR || '0');
  setSetting('CONFIG_HAND_MIN_OUTLINE_ENABLED', settings.CONFIG_HAND_MIN_OUTLINE_ENABLED || 'false');
  setSetting('CONFIG_HAND_MIN_OUTLINE_COLOR', settings.CONFIG_HAND_MIN_OUTLINE_COLOR || '0');
  setSetting('CONFIG_HAND_MIN_TRANSLUCENT', settings.CONFIG_HAND_MIN_TRANSLUCENT || 'false');
  setSetting('CONFIG_HAND_MIN_SHADOW_ENABLED', settings.CONFIG_HAND_MIN_SHADOW_ENABLED || 'false');
  setSetting('CONFIG_HAND_MIN_SHADOW_ANGLE', settings.CONFIG_HAND_MIN_SHADOW_ANGLE || '120');
  setSetting('CONFIG_HAND_MIN_SHADOW_DISTANCE', settings.CONFIG_HAND_MIN_SHADOW_DISTANCE || '2');
  setSetting('CONFIG_HAND_SEC_STYLE', settings.CONFIG_HAND_SEC_STYLE || '0');
  setSetting('CONFIG_HAND_SEC_WIDTH', settings.CONFIG_HAND_SEC_WIDTH || '2');
  setSetting('CONFIG_HAND_SEC_LENGTH', settings.CONFIG_HAND_SEC_LENGTH || '85');
  setSetting('CONFIG_HAND_SEC_BACK_OFFSET', settings.CONFIG_HAND_SEC_BACK_OFFSET || '0');
  setSetting('CONFIG_HAND_SEC_COLOR', settings.CONFIG_HAND_SEC_COLOR || '1');
  setSetting('CONFIG_HAND_SEC_OUTLINE_ENABLED', settings.CONFIG_HAND_SEC_OUTLINE_ENABLED || 'false');
  setSetting('CONFIG_HAND_SEC_OUTLINE_COLOR', settings.CONFIG_HAND_SEC_OUTLINE_COLOR || '0');
  setSetting('CONFIG_HAND_SEC_TRANSLUCENT', settings.CONFIG_HAND_SEC_TRANSLUCENT || 'false');
  setSetting('CONFIG_HAND_SEC_SHADOW_ENABLED', settings.CONFIG_HAND_SEC_SHADOW_ENABLED || 'false');
  setSetting('CONFIG_HAND_SEC_SHADOW_ANGLE', settings.CONFIG_HAND_SEC_SHADOW_ANGLE || '120');
  setSetting('CONFIG_HAND_SEC_SHADOW_DISTANCE', settings.CONFIG_HAND_SEC_SHADOW_DISTANCE || '2');
  setSetting('CONFIG_CENTER_CIRCLE_RADIUS', settings.CONFIG_CENTER_CIRCLE_RADIUS || '3');
  setSetting('CONFIG_CENTER_CIRCLE_COLOR', settings.CONFIG_CENTER_CIRCLE_COLOR || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_CONTENT', settings.CONFIG_UPPER_MIDDLE_LINE1_CONTENT || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_COLOR', settings.CONFIG_UPPER_MIDDLE_LINE1_COLOR || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_CONTENT', settings.CONFIG_UPPER_MIDDLE_LINE2_CONTENT || '0');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_COLOR', settings.CONFIG_UPPER_MIDDLE_LINE2_COLOR || '0');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT', settings.CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT || '12');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_COLOR', settings.CONFIG_BOTTOM_MIDDLE_LINE1_COLOR || '0');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT', settings.CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT || '0');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE2_COLOR', settings.CONFIG_BOTTOM_MIDDLE_LINE2_COLOR || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_CONTENT', settings.CONFIG_MIDDLE_LEFT_LINE1_CONTENT || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_COLOR', settings.CONFIG_MIDDLE_LEFT_LINE1_COLOR || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_CONTENT', settings.CONFIG_MIDDLE_LEFT_LINE2_CONTENT || '0');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_COLOR', settings.CONFIG_MIDDLE_LEFT_LINE2_COLOR || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_CONTENT', settings.CONFIG_MIDDLE_RIGHT_LINE1_CONTENT || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_COLOR', settings.CONFIG_MIDDLE_RIGHT_LINE1_COLOR || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_CONTENT', settings.CONFIG_MIDDLE_RIGHT_LINE2_CONTENT || '0');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_COLOR', settings.CONFIG_MIDDLE_RIGHT_LINE2_COLOR || '0');
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
  setSetting('CONFIG_VIBRATE_ON_PHASE_CHANGE', settings.CONFIG_VIBRATE_ON_PHASE_CHANGE ? 'true' : 'false');
  setSetting('CONFIG_OUTLINE_ENABLED', settings.CONFIG_OUTLINE_ENABLED ? 'true' : 'false');
  setSetting('CONFIG_CORNER_FONT_SIZE', settings.CONFIG_CORNER_FONT_SIZE || '1');
  setSetting('CONFIG_CORNER_CUSTOM_FONT', settings.CONFIG_CORNER_CUSTOM_FONT || '0');
  setSetting('CONFIG_TEST_MODE', settings.CONFIG_TEST_MODE ? 'true' : 'false');
  setSetting('CONFIG_TEST_DATETIME', settings.CONFIG_TEST_DATETIME || '');
  setSetting('CONFIG_DEBUG_OVERRIDE_ENABLED', settings.CONFIG_DEBUG_OVERRIDE_ENABLED ? 'true' : 'false');
  setSetting('CONFIG_DEBUG_OVERRIDE_DATA', settings.CONFIG_DEBUG_OVERRIDE_DATA || '');

  // The clock font / weather-readout toggle / colors are purely
  // cosmetic and phone-local -- send them immediately rather than
  // waiting for the full refresh cycle (location + astronomy + two
  // weather calls) to complete, so a settings change feels instant.
  // Actual weather temperature still needs that full cycle to arrive.
  sendDict({});

  scheduleRefresh();
  // Any settings save -- including a press of the "Force refresh now"
  // button, which is just this same save flow with nothing else
  // changed -- always bypasses the smart-refresh skip, since the
  // user explicitly asked for this one.
  refreshAndSend(true);
});
