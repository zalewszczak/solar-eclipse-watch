// ---- AppMessage message encoder ------------------------------------------
//
// Moved to comms/message-encoder.js (communication subsystem extraction).
// Behavior is unchanged; only the module boundary is new.
//
// Owns: turning settings (via settings-codecs.js), astronomy/sky data,
// and weather data into AppMessage-ready dicts, and the top-level
// send*()/buildFullKeysetDict() functions index.js calls with that data.
// Public API: sendFlatDict, sendInvalid, sendEclipseData,
// sendNoEclipseToday, buildFullKeysetDict -- everything else here
// (populateSettingsFields, skyFieldsDict, issFieldsDict,
// extraWeatherFieldsDict, the *Bytes() assemblers, TYPE_CODE,
// PLANET_ORDER, toEpoch, u16ArrayToBytes/i32ArrayToBytes) is a private
// implementation detail of building those 5 dicts.

var settings = require('../settings/settings');
var getSetting = settings.getSetting;
var settingsCodecs = require('../settings/settings-codecs');
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
var showFlightsCode = settingsCodecs.showFlightsCode;
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
var messageQueue = require('./message-queue');
var enqueueFlatDict = messageQueue.enqueueFlatDict;

var TYPE_CODE = { none: 0, partial: 1, total: 2, annular: 3 };

function toEpoch(d) {
  if (d && d instanceof Date) return Math.round(d.getTime() / 1000);
  return 0;
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
  var planetAzCombined = [];
  var planetRiseArr = [];
  var planetSetArr = [];
  PLANET_ORDER.forEach(function (name) {
    planetAltCombined = planetAltCombined.concat(sky[name + 'AltDecideg']);
    planetAzCombined = planetAzCombined.concat(sky[name + 'AzDecideg']);
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
    // Az samples below are for "Planet seek" (see shake_anim_mode's
    // own eclipse_data.h comment) -- real compass-relative bearings,
    // not just how high up something is. Unlike the alt samples,
    // these are never adjusted for the observer's elevation dip, since
    // that only ever affects the apparent horizon (a vertical effect).
    'SUN_AZ_SAMPLES': u16ArrayToBytes(sky.sunAzDecideg),
    'MOON_ALT_SAMPLES': u16ArrayToBytes(sky.moonAltDecideg),
    'MOON_AZ_SAMPLES': u16ArrayToBytes(sky.moonAzDecideg),
    'PLANET_ALT_SAMPLES': u16ArrayToBytes(planetAltCombined),
    'PLANET_AZ_SAMPLES': u16ArrayToBytes(planetAzCombined),
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

function toU8(v) { return ((v % 256) + 256) % 256; }

// OVERHEAD_OBJECTS: 4 bytes per object (az_deg low, az_deg high, alt_deg,
// is_iss) -- see overhead-objects.js for the {az, alt, isIss} list shape
// and MAX_OVERHEAD_OBJECTS in eclipse_data.h for the cap this is already
// capped to before it ever gets here.
function overheadObjectsBytes(objects) {
  var bytes = [];
  objects.forEach(function (obj) {
    var az = Math.round(obj.az) % 360;
    if (az < 0) az += 360;
    bytes.push(az & 0xFF, (az >> 8) & 0xFF, toU8(Math.round(obj.alt)), obj.isIss ? 1 : 0);
  });
  return bytes;
}

// One hand's 14-byte HandConfig blob, in HandConfig's own field order
// (see hand_layer.h): style, width, length, back_offset, middle_offset,
// secondary_width, color, outline_enabled, outline_color, translucent,
// hollow, hollow_thickness, shadow_enabled, shadow_distance_px.
function handBytes(style, width, length, backOffset, middleOffset, secondaryWidth,
                    color, outlineEnabled, outlineColor, translucent,
                    hollow, hollowThickness, shadowEnabled, shadowDistance) {
  return [
    style, width, length, toU8(backOffset), toU8(middleOffset), secondaryWidth,
    color, outlineEnabled, outlineColor, translucent,
    hollow, hollowThickness, shadowEnabled, shadowDistance
  ];
}

// HANDS: 42 bytes, hour/minute/second HandConfig back to back.
function handsBytes() {
  return [].concat(
    handBytes(
      handHourStyleCode(), handHourWidthCode(), handHourLengthCode(), handHourBackOffsetCode(),
      handHourMiddleOffsetCode(), handHourSecondaryWidthCode(), handHourColorCode(),
      handHourOutlineEnabledCode(), handHourOutlineColorCode(), handHourTranslucentCode(),
      handHourHollowCode(), handHourHollowThicknessCode(), handHourShadowEnabledCode(), handHourShadowDistanceCode()
    ),
    handBytes(
      handMinStyleCode(), handMinWidthCode(), handMinLengthCode(), handMinBackOffsetCode(),
      handMinMiddleOffsetCode(), handMinSecondaryWidthCode(), handMinColorCode(),
      handMinOutlineEnabledCode(), handMinOutlineColorCode(), handMinTranslucentCode(),
      handMinHollowCode(), handMinHollowThicknessCode(), handMinShadowEnabledCode(), handMinShadowDistanceCode()
    ),
    handBytes(
      handSecStyleCode(), handSecWidthCode(), handSecLengthCode(), handSecBackOffsetCode(),
      handSecMiddleOffsetCode(), handSecSecondaryWidthCode(), handSecColorCode(),
      handSecOutlineEnabledCode(), handSecOutlineColorCode(), handSecTranslucentCode(),
      handSecHollowCode(), handSecHollowThicknessCode(), handSecShadowEnabledCode(), handSecShadowDistanceCode()
    )
  );
}

// One ring's 8-byte MarkerRingConfig blob, in MarkerRingConfig's own
// field order (see eclipse_data.h): style, thickness, inner_eccentricity,
// outer_eccentricity, inner_border_pct, outer_border_pct, translucent, color.
function markerRingBytes(style, thickness, innerEcc, outerEcc, innerBorder, outerBorder, translucent, color) {
  return [style, thickness, innerEcc, outerEcc, innerBorder, outerBorder, translucent, color];
}

// MARKER_RINGS: 16 bytes, hour ring then second ring.
function markerRingsBytes() {
  return [].concat(
    markerRingBytes(
      customHourStyleCode(), customHourThicknessCode(), customHourInnerEccCode(), customHourOuterEccCode(),
      customHourInnerBorderCode(), customHourOuterBorderCode(), customHourTranslucentCode(), customHourColorCode()
    ),
    markerRingBytes(
      customSecStyleCode(), customSecThicknessCode(), customSecInnerEccCode(), customSecOuterEccCode(),
      customSecInnerBorderCode(), customSecOuterBorderCode(), customSecTranslucentCode(), customSecColorCode()
    )
  );
}

// EDGE_LINES: 16 bytes, in the exact order eclipse_data.h declares the
// 16 upper/bottom/middle_left/middle_right line1/line2 content+color_mode
// fields (a truly contiguous run in the struct, hence a plain memcpy target).
function edgeLinesBytes() {
  return [
    upperMiddleLine1ContentCode(), upperMiddleLine1ColorModeCode(),
    upperMiddleLine2ContentCode(), upperMiddleLine2ColorModeCode(),
    bottomMiddleLine1ContentCode(), bottomMiddleLine1ColorModeCode(),
    bottomMiddleLine2ContentCode(), bottomMiddleLine2ColorModeCode(),
    middleLeftLine1ContentCode(), middleLeftLine1ColorModeCode(),
    middleLeftLine2ContentCode(), middleLeftLine2ColorModeCode(),
    middleRightLine1ContentCode(), middleRightLine1ColorModeCode(),
    middleRightLine2ContentCode(), middleRightLine2ColorModeCode()
  ];
}

// MARKER_TEXT: 8 bytes -- target, font_choice, offset_px (signed byte),
// hour_mask (u16 little-endian), second_mask (u16 little-endian),
// roman_numerals. Not a memcpy target on the C side (MarkerTextConfig
// has padding around its two u16 masks), but PKJS still needs to send
// exactly these 8 bytes in exactly this order for that unpack to work.
function markerTextBytes() {
  var hourMask = markerTextHourMaskCode();
  var secMask = markerTextSecMaskCode();
  return [
    markerTextTargetCode(),
    markerTextFontCode(),
    toU8(markerTextOffsetCode()),
    hourMask & 0xFF, (hourMask >> 8) & 0xFF,
    secMask & 0xFF, (secMask >> 8) & 0xFF,
    markerTextRomanCode()
  ];
}

// COLORS: 6 bytes -- custom_bg, custom_text, custom_accent,
// night_custom_bg, night_custom_text, night_custom_accent.
// Deliberately NOT night_scheme_enabled too (still its own separate
// key -- see apply_consolidated_fields()'s own comment in
// pebble-eclipse-watch.c for why).
function colorsBytes() {
  return [
    customBgByte(), customTextByte(), customAccentByte(),
    nightCustomBgByte(), nightCustomTextByte(), nightCustomAccentByte()
  ];
}

// Populates every settings-derived AppMessage field (all ~148 of
// them) into `dict`, reading current values fresh from localStorage
// each time -- no send-related side effects of its own (no cache
// write, no debug-override check, no enqueueFlatDict()), so callers
// that just want a correctly-encoded settings snapshot (the debug
// page's own "full keyset" window -- see buildFullKeysetDict()) can
// get one without it also quietly sending anything.
function populateSettingsFields(dict) {
  // Always carried, on every message -- these are purely cosmetic,
  // phone-local preferences, not eclipse data, so there's no reason
  // to gate them behind DATA_VALID or wait for a full refresh cycle.
  dict['CLOCK_FONT'] = clockFontCode();
  dict['TEMP_UNIT'] = tempUnitCode();
  dict['WIND_SPEED_UNIT'] = windSpeedUnitCode();
  dict['SHOW_SECONDS'] = showSecondsCode();
  dict['COLORS'] = colorsBytes();
  dict['NIGHT_SCHEME_ENABLED'] = nightSchemeEnabledCode();
  dict['BOTTOM_STYLE'] = bottomStyleCode();
  dict['SUN_MOON_SIZE_PCT'] = sunMoonSizeCode();
  dict['SKY_MODE'] = skyModeCode();
  dict['SHOW_MAJOR_STARS'] = showMajorStarsCode();
  dict['WEATHER_ICON_STYLE'] = weatherIconStyleCode();
  dict['AQI_UNIT'] = aqiUnitCode();
  dict['ALTITUDE_UNIT'] = altitudeUnitCode();
  dict['SHAKE_LABEL_SECONDS'] = shakeLabelSecondsCode();
  dict['LABEL_STYLE'] = labelStyleCode();
  dict['SHADOW_TRANSLUCENT'] = shadowTranslucentCode();
  dict['SHADOW_ANGLE'] = shadowAngleCode();
  dict['BIG_ANALOG_MARKER_STYLE'] = bigAnalogMarkerStyleCode();
  dict['BITMAP_MARKER_TRANSPARENT'] = bitmapMarkerTransparentCode();
  dict['DRAW_FEATURES_BENEATH_HANDS'] = drawFeaturesBeneathHandsCode();
  dict['MARKER_RINGS'] = markerRingsBytes();
  dict['CUSTOM_HOUR_INNER_THICKNESS'] = customHourInnerThicknessCode();
  dict['CUSTOM_SEC_INNER_THICKNESS'] = customSecInnerThicknessCode();
  dict['MARKER_TEXT'] = markerTextBytes();
  dict['HANDS'] = handsBytes();
  dict['CENTER_CIRCLE_RADIUS'] = centerCircleRadiusCode();
  dict['CENTER_CIRCLE_COLOR'] = centerCircleColorCode();
  dict['EDGE_LINES'] = edgeLinesBytes();
  dict['SHOW_SUN_TIME'] = showSunTimeCode();
  dict['SHOW_ISS'] = showIssCode();
  dict['SHOW_FLIGHTS'] = showFlightsCode();
  dict['AURORA_ENABLED'] = auroraEnabledCode();
  dict['VIBRATE_ON_PHASE_CHANGE'] = vibrateOnPhaseChangeCode();
  dict['STARTUP_CLOCK_ANIM_MODE'] = startupClockAnimModeCode();
  dict['BG_ANIM_MODE'] = bgAnimModeCode();
  dict['SHAKE_ANIM_MODE'] = shakeAnimModeCode();
  dict['OUTLINE_ENABLED'] = outlineEnabledCode();
  dict['BATTERY_SAVER_ENABLED'] = batterySaverEnabledCode();
  dict['CORNER_FONT'] = cornerFontCode();
  dict['CORNER_CONTENT'] = cornerContentBytes();
  dict['CORNER_COLOR_MODE'] = cornerColorModeBytes();
  dict['DAILY_STEP_GOAL'] = dailyStepGoalValue();
  dict['HOURLY_VIBE_MODE'] = hourlyVibeModeCode();
  dict['HOURLY_VIBE_INTERVAL_MIN'] = hourlyVibeIntervalMinCode();
  dict['HOURLY_VIBE_PATTERN'] = hourlyVibePatternCode();
  dict['HOURLY_VIBE_START_MIN'] = hourlyVibeStartMinCode();
  dict['HOURLY_VIBE_END_MIN'] = hourlyVibeEndMinCode();
  dict['HOURLY_VIBE_DAYS_MASK'] = hourlyVibeDaysMaskCode();
  dict['HOURLY_VIBE_OVERRIDE_QUIET'] = hourlyVibeOverrideQuietCode();
  dict['DRAW_DEBUG'] = drawDebugCode();
}

// For the settings page's own debug "full keyset" window (see
// showConfiguration in index.js and fullKeysetData in config-page.js) --
// every key the watch could currently receive, ready to hand-edit and
// send as-is. Starts from the last genuinely computed full send (the
// same LAST_FULL_COMPUTED_DICT resendLastFullData() in index.js already
// uses) so the eclipse/weather/astronomy fields are real values
// rather than blank/zeroed placeholders, then overlays fresh settings
// on top (same reasoning as resendLastFullData()'s own fix: those
// need to reflect whatever's actually configured right now, not
// whatever happened to be true whenever that snapshot was last
// cached). No network fetch of its own -- opening the settings page
// should be instant, not wait on a fresh weather/astronomy call just
// to populate a debug textarea.
function buildFullKeysetDict() {
  var dict = {};
  try {
    var raw = localStorage.getItem('LAST_FULL_COMPUTED_DICT');
    if (raw) {
      var parsed = JSON.parse(raw);
      if (parsed && typeof parsed === 'object') dict = parsed;
    }
  } catch (e) {
    // Corrupt/missing cache -- fall back to settings-only, below.
  }
  populateSettingsFields(dict);
  return dict;
}

// Builds the full flat dict (eclipse/weather/sky data passed in, plus
// the cosmetic settings/features fields merged in below) exactly as
// before, then hands it to enqueueFlatDict() (comms/message-queue.js) to
// be split into typed chunks and sent one at a time -- see
// message-schema.js's KEY_TYPE_MAP. Kept the name/shape of the old
// single-big-message sendDict() so every call site (mostly in index.js,
// plus the debug-override/localStorage-snapshot logic already living
// here) stayed untouched.
//
// The actual field-by-field population (dict['CLOCK_FONT'] = ...,
// all ~148 of them) lives in populateSettingsFields() below, split
// out so the settings page's own debug "full keyset" window (see
// buildFullKeysetDict()) can get a fresh, correctly-encoded settings
// snapshot without going through this function's OWN side effects
// (the LAST_FULL_COMPUTED_DICT cache write, the debug-override check,
// and -- the one that actually matters here -- enqueueFlatDict()
// itself, which would mean just opening the settings page quietly
// sent something to the watch).
function sendFlatDict(dict) {
  populateSettingsFields(dict);

  try {
    // Separate from the raw-message log recordRawMessage() builds
    // once each chunk actually sends (comms/message-queue.js) -- this
    // one is ONLY overwritten when `dict` is a genuine full-data send
    // (from sendEclipseData()/sendNoEclipseToday(), which both always
    // set C1_TIME -- 0 for "no eclipse today", a real epoch otherwise --
    // vs. the cosmetic-only sendFlatDict({}) push settings-only saves
    // trigger, or sendInvalid()'s {DATA_VALID:0,...}, neither of
    // which ever sets it). refreshAndSend()'s skip-path resend (refresh-manager.js)
    // needs something that's reliably the real, complete thing, not
    // just whatever the most recent send of any kind happened to be.
    if (Object.prototype.hasOwnProperty.call(dict, 'C1_TIME')) {
      localStorage.setItem('LAST_FULL_COMPUTED_DICT', JSON.stringify(dict));
    }
  } catch (e) {
    // Not critical if this fails (storage full, etc.) -- just means
    // refreshAndSend()'s (refresh-manager.js) skip-path resend won't have a fresh snapshot
    // to fall back on this time (see resendLastFullData()).
  }

  var toSend = dict;
  
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

  // Splits toSend into per-subject chunks (STATUS/ECLIPSE/WEATHER/
  // ASTRONOMY/SKY_EFFECTS/FEATURES/SETTINGS) and queues them -- see
  // enqueueFlatDict() in comms/message-queue.js.
  enqueueFlatDict(toSend);
}


function sendInvalid(errorCode) {
  sendFlatDict({ 'DATA_VALID': 0, 'ERROR_CODE': errorCode || 0 });
}

function issFieldsDict(issPos, issErrorCode) {
  return {
    'ISS_ALT': issPos ? Math.round(issPos.alt) : 0,
    'ISS_AZ': issPos ? Math.round(issPos.az) : 0,
    'ISS_COMPUTED_AT': issPos ? Math.floor(Date.now() / 1000) : 0,
    'ISS_NEXT_PASS': (issPos && issPos.nextPass) ? toEpoch(issPos.nextPass) : 0,
    // Always sent (0 = this cycle's fetch was fine, or ISS wasn't in
    // use at all) -- unlike the fields above, this is the signal the
    // watch actually needs every cycle to know whether to trust them,
    // so it can't be omitted the way a stale-data-preserving field can.
    'ISS_ERROR_CODE': issErrorCode || 0
  };
}

// Bundles the newer weather-extra fields (pressure/wind direction/dew
// point/air quality) into one object param on the two send functions
// below, rather than growing their already-long positional parameter
// lists by another 6 -- extra = { windDirDeg, dewPointC, pressureHpa,
// pressureTrend, aqiUs, aqiEu }, any of which may be null/undefined.
//
// Fields sourced from a live network fetch (everything except
// ALTITUDE_M, which comes from the phone's own GPS) are OMITTED
// entirely -- not sent as a zeroed default -- when that value is
// null/not a number, i.e. when the fetch failed or genuinely returned
// nothing. dict_find() on the watch only touches a field when its key
// is actually present, so an omitted key means "leave whatever's
// already showing alone" -- a transient network hiccup no longer
// wipes out the last known-good reading the way sending an
// unconditional 0 used to.
function extraWeatherFieldsDict(extra) {
  extra = extra || {};
  var dict = {
    // -32000 = sentinel for "no altitude available" (many phones don't
    // report GPS altitude, and manual-coordinates mode never has it --
    // see getLocation()). A real altitude can legitimately be negative
    // (Death Valley, the Dead Sea shore) or exactly 0 (sea level), so
    // those can't double as the "missing" signal the way they might
    // elsewhere -- this needs its own out-of-range sentinel instead.
    // Always sent (never omitted): unlike the fields below, "no GPS
    // altitude" is a stable fact about this phone/location, not a
    // transient fetch failure to protect a previous reading from.
    'ALTITUDE_M': (typeof extra.altitudeMeters === 'number') ? Math.round(extra.altitudeMeters) : -32000
  };
  if (typeof extra.windDirDeg === 'number') dict['WIND_DIR_DEG'] = Math.round(extra.windDirDeg);
  if (typeof extra.dewPointC === 'number') dict['DEW_POINT_C'] = Math.round(extra.dewPointC);
  if (typeof extra.pressureHpa === 'number') {
    dict['PRESSURE_HPA'] = Math.round(extra.pressureHpa);
    dict['PRESSURE_TREND'] = extra.pressureTrend || 0; // trend only means anything alongside a real pressure reading
  }
  if (typeof extra.aqiUs === 'number') dict['AQI_US'] = extra.aqiUs;
  if (typeof extra.aqiEu === 'number') dict['AQI_EU'] = extra.aqiEu;
  // x10 -- same convention as every other _x10/_decideg field. Kp is
  // fractional (thirds: .00/.33/.67), hence the scaling rather than
  // sending it as a plain 0-9 integer.
  if (typeof extra.auroraKpX10 === 'number') dict['AURORA_KP_X10'] = extra.auroraKpX10;
  if (typeof extra.auroraVisibilityPct === 'number') dict['AURORA_VISIBILITY_PCT'] = extra.auroraVisibilityPct;
  // Always sent, same reasoning as ISS_ERROR_CODE above.
  dict['AURORA_ERROR_CODE'] = extra.auroraErrorCode || 0;
  // "Weather in N hours" (features_layer.c ids 87-92) -- sent as byte
  // arrays, one entry per forecast hour (1h through 6h ahead). Only
  // sent when the daily-forecast fetch that produced them actually
  // succeeded, same reasoning as every other field here -- a transient
  // failure shouldn't wipe out the watch's last known-good forecast.
  // forecast_temp_c can't go through as a plain signed byte (AppMessage
  // byte-array tuples are unsigned), so each value is offset by +50
  // celsius first; 255 is the "not available" sentinel for a single
  // hour within an otherwise-successful fetch (see forecastTempC's own
  // per-index null check in weather.js).
  if (extra.forecastTempC) {
    dict['FORECAST_TEMP_C'] = extra.forecastTempC.map(function (c) {
      return (typeof c === 'number') ? Math.max(0, Math.min(255, Math.round(c) + 50)) : 255;
    });
  }
  if (extra.forecastCondition) {
    dict['FORECAST_CONDITION'] = extra.forecastCondition.map(function (c) {
      return (typeof c === 'number') ? c : 0;
    });
  }
  // "Weather in N days" (features_layer.c ids 93-95) -- same shape and
  // same +50/255 offset convention as FORECAST_TEMP_C above, just 3
  // entries (1-3 days ahead) instead of 6.
  if (extra.dailyForecastTempC) {
    dict['FORECAST_DAILY_TEMP_C'] = extra.dailyForecastTempC.map(function (c) {
      return (typeof c === 'number') ? Math.max(0, Math.min(255, Math.round(c) + 50)) : 255;
    });
  }
  if (extra.dailyForecastCondition) {
    dict['FORECAST_DAILY_CONDITION'] = extra.dailyForecastCondition.map(function (c) {
      return (typeof c === 'number') ? c : 0;
    });
  }
  return dict;
}

// weatherOk: false when the daily-forecast fetch (weather.js's
// getDailyCloudGrid, which weatherCondition/weatherTempC/etc all come
// from) failed -- in which case every field sourced from it is
// OMITTED from the dict below rather than sent as a zeroed default,
// so a transient network failure can't wipe the watch's last known-
// good weather reading. This was the root cause of settings-page
// saves occasionally "zeroing out" weather: a save used to force an
// immediate refetch, and if that particular refetch hit a transient
// failure, the resulting all-zeros dict got sent and applied anyway.
// weatherErrorCode/issErrorCode: unlike the data fields, these ARE
// always sent (0 = this cycle's fetch was fine) -- see
// servicelog.js's classifyError() for what a nonzero value means (an
// HTTP status, or one of its own small ERR_* codes for a failure that
// never got an HTTP response at all). The watch uses
// WEATHER_ERROR_CODE specifically to decide when a run of failures is
// long enough to actually show "ERR ###" instead of quietly keeping
// last known-good data on screen -- see weather_should_show_error()
// in pebble-eclipse-watch.c.
function sendNoEclipseToday(sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, weatherCondition, weatherTempC, meteorShower, cloudAltitudePct, tempHighC, tempLowC, issPos, uvIndexMax, uvIndexCurrent, rainChancePct, humidityPct, windSpeedKmh, currentCloudPct, sunRiseTomorrow, extraWeather, stars, weatherOk, weatherErrorCode, issErrorCode) {
  // CLOUD_COVER/VIS_SCORE prefer currentCloudPct (same-source as the
  // sky canvas's own CLOUD_SAMPLES grid) but fall back to the
  // separate eclipse-window headline average if that's all that
  // succeeded -- haveCloudData is true as long as EITHER source came
  // through, only omitting these fields if both failed.
  var haveCloudData = (typeof currentCloudPct === 'number') || headlineSources > 0;
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
    'LOCATION_NAME': locationName || '',
    'WEATHER_ERROR_CODE': weatherErrorCode || 0
  };
  if (haveCloudData) {
    dict['CLOUD_COVER'] = displayCloudPct;
    dict['VIS_SCORE'] = 100 - displayCloudPct;
    dict['WEATHER_SOURCES'] = headlineSources || 0;
  }
  if (weatherOk) {
    dict['WEATHER_CONDITION'] = weatherCondition || 0;
    dict['WEATHER_TEMP_C'] = (typeof weatherTempC === 'number') ? Math.round(weatherTempC) : 0;
    dict['WEATHER_TEMP_HIGH_C'] = (typeof tempHighC === 'number') ? Math.round(tempHighC) : 0;
    dict['WEATHER_TEMP_LOW_C'] = (typeof tempLowC === 'number') ? Math.round(tempLowC) : 0;
    dict['UV_INDEX_X10'] = (typeof uvIndexMax === 'number') ? Math.round(Math.max(0, Math.min(25.5, uvIndexMax)) * 10) : 0;
    dict['UV_INDEX_CURRENT_X10'] = (typeof uvIndexCurrent === 'number') ? Math.round(Math.max(0, Math.min(25.5, uvIndexCurrent)) * 10) : 0;
    dict['RAIN_CHANCE_PCT'] = (typeof rainChancePct === 'number') ? Math.round(rainChancePct) : 0;
    dict['HUMIDITY_PCT'] = (typeof humidityPct === 'number') ? Math.round(humidityPct) : 0;
    dict['WIND_SPEED_KMH'] = (typeof windSpeedKmh === 'number') ? Math.round(windSpeedKmh) : 0;
    dict['WEATHER_LAST_UPDATE'] = Math.floor(Date.now() / 1000);
  }
  var sky_ = skyFieldsDict(sky, cloudGrid, moonPhase, riseSet, meteorShower, cloudAltitudePct, sunRiseTomorrow, stars);
  Object.keys(sky_).forEach(function (k) { dict[k] = sky_[k]; });
  var extraW_ = extraWeatherFieldsDict(extraWeather);
  Object.keys(extraW_).forEach(function (k) { dict[k] = extraW_[k]; });
  var iss_ = issFieldsDict(issPos, issErrorCode);
  Object.keys(iss_).forEach(function (k) { dict[k] = iss_[k]; });
  sendFlatDict(dict);
}

// weatherOk/weatherErrorCode/issErrorCode: see sendNoEclipseToday's
// own comment above -- same reasoning, same treatment.
function sendEclipseData(result, sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, weatherCondition, weatherTempC, meteorShower, cloudAltitudePct, tempHighC, tempLowC, issPos, uvIndexMax, uvIndexCurrent, rainChancePct, humidityPct, windSpeedKmh, currentCloudPct, sunRiseTomorrow, extraWeather, stars, weatherOk, weatherErrorCode, issErrorCode) {
  var haveCloudData = (typeof currentCloudPct === 'number') || headlineSources > 0;
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
    'LOCATION_NAME': locationName || '',
    'WEATHER_ERROR_CODE': weatherErrorCode || 0
  };
  if (haveCloudData) {
    dict['CLOUD_COVER'] = displayCloudPct;
    dict['VIS_SCORE'] = 100 - displayCloudPct;
    dict['WEATHER_SOURCES'] = headlineSources || 0;
  }
  if (weatherOk) {
    dict['WEATHER_CONDITION'] = weatherCondition || 0;
    dict['WEATHER_TEMP_C'] = (typeof weatherTempC === 'number') ? Math.round(weatherTempC) : 0;
    dict['WEATHER_TEMP_HIGH_C'] = (typeof tempHighC === 'number') ? Math.round(tempHighC) : 0;
    dict['WEATHER_TEMP_LOW_C'] = (typeof tempLowC === 'number') ? Math.round(tempLowC) : 0;
    dict['UV_INDEX_X10'] = (typeof uvIndexMax === 'number') ? Math.round(Math.max(0, Math.min(25.5, uvIndexMax)) * 10) : 0;
    dict['UV_INDEX_CURRENT_X10'] = (typeof uvIndexCurrent === 'number') ? Math.round(Math.max(0, Math.min(25.5, uvIndexCurrent)) * 10) : 0;
    dict['RAIN_CHANCE_PCT'] = (typeof rainChancePct === 'number') ? Math.round(rainChancePct) : 0;
    dict['HUMIDITY_PCT'] = (typeof humidityPct === 'number') ? Math.round(humidityPct) : 0;
    dict['WIND_SPEED_KMH'] = (typeof windSpeedKmh === 'number') ? Math.round(windSpeedKmh) : 0;
    dict['WEATHER_LAST_UPDATE'] = Math.floor(Date.now() / 1000);
  }
  var sky_ = skyFieldsDict(sky, cloudGrid, moonPhase, riseSet, meteorShower, cloudAltitudePct, sunRiseTomorrow, stars);
  Object.keys(sky_).forEach(function (k) { dict[k] = sky_[k]; });
  var extraW_ = extraWeatherFieldsDict(extraWeather);
  Object.keys(extraW_).forEach(function (k) { dict[k] = extraW_[k]; });
  var iss_ = issFieldsDict(issPos, issErrorCode);
  Object.keys(iss_).forEach(function (k) { dict[k] = iss_[k]; });
  sendFlatDict(dict);
}

module.exports = {
  sendFlatDict: sendFlatDict,
  sendInvalid: sendInvalid,
  sendEclipseData: sendEclipseData,
  sendNoEclipseToday: sendNoEclipseToday,
  buildFullKeysetDict: buildFullKeysetDict,
  overheadObjectsBytes: overheadObjectsBytes
};
