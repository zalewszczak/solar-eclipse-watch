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
var scheduledStyle = require('../scheduled-style');
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
var hourlyVibeModeCode = settingsCodecs.hourlyVibeModeCode;
var hourlyVibeIntervalMinCode = settingsCodecs.hourlyVibeIntervalMinCode;
var hourlyVibePatternCode = settingsCodecs.hourlyVibePatternCode;
var minutesSinceMidnight = settingsCodecs.minutesSinceMidnight;
var hourlyVibeStartMinCode = settingsCodecs.hourlyVibeStartMinCode;
var hourlyVibeEndMinCode = settingsCodecs.hourlyVibeEndMinCode;
var hourlyVibeDaysMaskCode = settingsCodecs.hourlyVibeDaysMaskCode;
var hourlyVibeOverrideQuietCode = settingsCodecs.hourlyVibeOverrideQuietCode;
var bgAnimModeCode = settingsCodecs.bgAnimModeCode;
var shakeAnimModeCode = settingsCodecs.shakeAnimModeCode;
var outlineEnabledCode = settingsCodecs.outlineEnabledCode;
var batterySaverEnabledCode = settingsCodecs.batterySaverEnabledCode;
var cornerFontCode = settingsCodecs.cornerFontCode;
var cornerContentBytes = settingsCodecs.cornerContentBytes; // local compiler input only, never sent
var cornerColorModeBytes = settingsCodecs.cornerColorModeBytes;
var dailyStepGoalValue = settingsCodecs.dailyStepGoalValue;
var weatherNormalize = require('../weather/weather-normalize');
var formatTempDisplay = weatherNormalize.formatTempDisplay;
var apparentTempC = weatherNormalize.apparentTempC;
var tempGradientColorByte = weatherNormalize.tempGradientColorByte;
var formatUvDisplay = weatherNormalize.formatUvDisplay;
var uvGradientColorByte = weatherNormalize.uvGradientColorByte;
var formatPercentDisplay = weatherNormalize.formatPercentDisplay;
var percentGradientColorByte = weatherNormalize.percentGradientColorByte;
var overcastGrayColorByte = weatherNormalize.overcastGrayColorByte;
var formatWindSpeedDisplay = weatherNormalize.formatWindSpeedDisplay;
var windSpeedGradientColorByte = weatherNormalize.windSpeedGradientColorByte;
var formatPressureDisplay = weatherNormalize.formatPressureDisplay;
var pressureGradientColorByte = weatherNormalize.pressureGradientColorByte;
var formatAqiDisplay = weatherNormalize.formatAqiDisplay;
var aqiGradientColorByte = weatherNormalize.aqiGradientColorByte;
var formatAltitudeDisplay = weatherNormalize.formatAltitudeDisplay;
var altitudeGradientColorByte = weatherNormalize.altitudeGradientColorByte;
var weatherIconCategory = weatherNormalize.weatherIconCategory;
var weatherConditionColorByte = weatherNormalize.weatherConditionColorByte;
var featureDisplay = require('../astronomy/feature-display');
var primitiveDecompose = require('../config/primitive-decompose');
var primitiveIdSequenceForContent = primitiveDecompose.primitiveIdSequenceForContent;
var primitiveAuxSequenceForContent = primitiveDecompose.primitiveAuxSequenceForContent;
var normalizePrimitiveSequence = primitiveDecompose.normalizePrimitiveSequence;
var normalizePrimitiveAux = primitiveDecompose.normalizePrimitiveAux;
var parseCustomPrimsSetting = primitiveDecompose.parseCustomPrimsSetting;
var pluginManager = require('../plugins/plugin-manager');
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
    'METEOR_SHOWER_NAME': meteorShower ? meteorShower.name : '',
    // Feature-facing display text/colours (primitive features read these strings).
    'MOON_PHASE_DISPLAY': featureDisplay.formatMoonPhaseDisplay(moonPhase.illuminatedPct, moonPhase.waxing),
    'SATURN_RINGS_DISPLAY': featureDisplay.formatSaturnRingsDisplay(sky.saturnRingOpenPct),
    'METEOR_DISPLAY': featureDisplay.formatMeteorDisplay(meteorShower ? meteorShower.intensity : 0, meteorShower ? meteorShower.name : ''),
    'METEOR_COLOR': featureDisplay.meteorGradientColorByte(meteorShower ? meteorShower.intensity : 0)
  };
}

function toU8(v) { return ((v % 256) + 256) % 256; }

// OVERHEAD_OBJECTS: 8 bytes per object -- 2 bytes packing az (9 bits,
// 0-359) and alt (7 bits, 0-90) into one uint16 (low byte first), plus a
// fixed 6-byte label (5 chars + NUL, truncated already by
// overhead-objects.js's own MAX_LABEL_CHARS). See eclipse_data.h's
// OverheadObject for the matching C-side layout.
function overheadObjectsBytes(objects) {
  var bytes = [];
  objects.forEach(function (obj) {
    var az = Math.round(obj.az) % 360;
    if (az < 0) az += 360;
    var alt = Math.max(0, Math.min(90, Math.round(obj.alt)));
    var packed = (az & 0x1FF) | ((alt & 0x7F) << 9);
    bytes.push(packed & 0xFF, (packed >> 8) & 0xFF);

    var label = String(obj.label || '').slice(0, 5);
    for (var i = 0; i < 5; i++) {
      bytes.push(i < label.length ? label.charCodeAt(i) : 0);
    }
    bytes.push(0); // NUL terminator (6th label byte)
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

// EDGE_LINE_COLOR_MODE: eight color-mode bytes in slot order (SLOT_UPPER_L1 ..
// SLOT_RIGHT_L2). Feature identity (content ids) is phone-local: only compiled
// primitive sequences cross the wire. Each code is read through the active
// analog-or-digital context (dualContextSetting), exactly like before.
function edgeLinesBytes() {
  return [
    upperMiddleLine1ColorModeCode(), upperMiddleLine2ColorModeCode(),
    bottomMiddleLine1ColorModeCode(), bottomMiddleLine2ColorModeCode(),
    middleLeftLine1ColorModeCode(), middleLeftLine2ColorModeCode(),
    middleRightLine1ColorModeCode(), middleRightLine2ColorModeCode()
  ];
}
function edgeCustomPrims() {
  return settingsCodecs.edgeCustomPrimsStrings().map(parseCustomPrimsSetting);
}
// Selected feature IDs are local compiler inputs only; they are never sent.
function edgeFeatureCodesRaw() {
  return [
    upperMiddleLine1ContentCode(), upperMiddleLine2ContentCode(),
    bottomMiddleLine1ContentCode(), bottomMiddleLine2ContentCode(),
    middleLeftLine1ContentCode(), middleLeftLine2ContentCode(),
    middleRightLine1ContentCode(), middleRightLine2ContentCode()
  ];
}
var CORNER_CUSTOM_PRIMS_KEYS = ['CONFIG_CORNER_TL_CUSTOM_PRIMS', 'CONFIG_CORNER_TR_CUSTOM_PRIMS',
  'CONFIG_CORNER_BL_CUSTOM_PRIMS', 'CONFIG_CORNER_BR_CUSTOM_PRIMS'];
function cornerCustomPrims() {
  return CORNER_CUSTOM_PRIMS_KEYS.map(function (k) { return parseCustomPrimsSetting(getSetting(k, '')); });
}
// Bit n set = slot n is a user-built sequence: corners TL,TR,BL,BR are bits 0-3,
// edge/middle slots (SLOT_UPPER_L1..SLOT_RIGHT_L2) are bits 4-11. The watch lays
// those out with no automatic gaps between elements.
function primitiveTightMask() {
  var mask = 0;
  cornerCustomPrims().forEach(function (c, i) { if (c.ids.length) mask |= (1 << i); });
  edgeCustomPrims().forEach(function (c, i) { if (c.ids.length) mask |= (1 << (4 + i)); });
  return mask;
}
// A slot with a custom sequence sends that sequence (and its own aux bytes); every
// other slot derives its sequence from its content id via primitive-decompose.js.
function slotSequences(contentIds, customs) {
  return contentIds.map(function (contentId, i) {
    var custom = customs[i];
    return custom.ids.length
      ? { ids: normalizePrimitiveSequence(custom.ids), aux: normalizePrimitiveAux(custom.aux) }
      : { ids: primitiveIdSequenceForContent(contentId), aux: primitiveAuxSequenceForContent(contentId) };
  });
}
function concatField(seqs, field) {
  return seqs.reduce(function (acc, q) { return acc.concat(q[field]); }, []);
}
function edgeSequences() { return slotSequences(edgeFeatureCodesRaw(), edgeCustomPrims()); }
function cornerSequences() { return slotSequences(cornerContentBytes(), cornerCustomPrims()); }

// Every primitive id currently in use by an active slot (for refresh-manager's
// "only fetch optional sources a slot actually shows" gating).
function activePrimitiveIds() {
  var seen = {};
  edgeSequences().concat(cornerSequences()).forEach(function (q) {
    q.ids.forEach(function (id) { if (id) seen[id] = true; });
  });
  return seen;
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
  dict['COLORS'] = colorsBytes();
  dict['WEATHER_ICON_STYLE'] = weatherIconStyleCode();
  dict['AQI_UNIT'] = aqiUnitCode();
  dict['ALTITUDE_UNIT'] = altitudeUnitCode();
  dict['SHADOW_ANGLE'] = shadowAngleCode();
  dict['MARKER_RINGS'] = markerRingsBytes();
  dict['HANDS'] = handsBytes();
  dict['EDGE_LINE_COLOR_MODE'] = edgeLinesBytes(); // primitive-only slot color modes
  var edgeSeqs = edgeSequences();
  dict['EDGE_LINE_PRIMITIVE_IDS'] = concatField(edgeSeqs, 'ids'); // 8 slots x 10
  dict['EDGE_LINE_PRIMITIVE_AUX'] = concatField(edgeSeqs, 'aux');
  var cornerSeqs = cornerSequences();
  dict['CORNER_PRIMITIVE_IDS'] = concatField(cornerSeqs, 'ids'); // 4 slots x 10, TL,TR,BL,BR
  dict['CORNER_PRIMITIVE_AUX'] = concatField(cornerSeqs, 'aux');
  dict['PLUGIN_TEXTS'] = pluginManager.textsBytes();
  // SETTINGS_U8_BLOB: 36 single-byte settings copied verbatim into the watch's settings
  // block (struct EclipseData's anonymous block). ORDER = struct member order; keep in
  // sync with eclipse_data.h and the SETTINGS_BLOCK_LEN assert in comms_decoder.c.
  var tightMask = primitiveTightMask();
  dict['SETTINGS_U8_BLOB'] = [
    tempUnitCode() & 255,
    windSpeedUnitCode() & 255,
    shakeLabelSecondsCode() & 255,
    vibrateOnPhaseChangeCode() & 255,
    outlineEnabledCode() & 255,
    batterySaverEnabledCode() & 255,
    cornerFontCode() & 255,
    centerCircleRadiusCode() & 255,
    centerCircleColorCode() & 255,
    bitmapMarkerTransparentCode() & 255,
    customHourInnerThicknessCode() & 255,
    customSecInnerThicknessCode() & 255,
    hourlyVibeModeCode() & 255,
    hourlyVibeIntervalMinCode() & 255,
    hourlyVibePatternCode() & 255,
    hourlyVibeDaysMaskCode() & 255,
    hourlyVibeOverrideQuietCode() & 255,
    showFlightsCode() & 255,
    bgAnimModeCode() & 255,
    shakeAnimModeCode() & 255,
    clockFontCode() & 255,
    showSecondsCode() & 255,
    bottomStyleCode() & 255,
    sunMoonSizeCode() & 255,
    skyModeCode() & 255,
    labelStyleCode() & 255,
    shadowTranslucentCode() & 255,
    drawFeaturesBeneathHandsCode() & 255,
    bigAnalogMarkerStyleCode() & 255,
    showSunTimeCode() & 255,
    showIssCode() & 255,
    showMajorStarsCode() & 255,
    auroraEnabledCode() & 255,
    nightSchemeEnabledCode() & 255,
    tightMask & 255, (tightMask >> 8) & 255
  ];
  // Legacy CORNER_CONTENT/EDGE_LINES are never emitted: feature ids stay on the phone.
  dict['CORNER_COLOR_MODE'] = cornerColorModeBytes();
  dict['DAILY_STEP_GOAL'] = dailyStepGoalValue();
  dict['HOURLY_VIBE_START_MIN'] = hourlyVibeStartMinCode();
  dict['HOURLY_VIBE_END_MIN'] = hourlyVibeEndMinCode();
  // Scheduled My styles: when the watch should next ask PKJS to serve one
  // (0 = none). Always sent so the watch's copy can never go stale.
  dict['NEXT_STYLE_CHECK'] = scheduledStyle.nextCheckSeconds(Date.now());
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
// Packs one display string per forecast slot into 6-byte NUL-padded records (15 slots, the
// watch's forecast_temp_display layout) so they travel as one blob instead of 15 keys.
function forecastDisplayBytes(temps, unitCode) {
  var out = [];
  for (var i = 0; i < 15; i++) {
    var str = (i < temps.length) ? String(formatTempDisplay(temps[i], unitCode)) : '';
    for (var j = 0; j < 6; j++) out.push(j < 5 && j < str.length ? (str.charCodeAt(j) & 255) : 0);
  }
  return out;
}

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
  if (typeof extra.altitudeMeters === 'number') {
    // Not sent for the -32000/"unavailable" case: the watch keeps its own N/A handling.
    dict['ALTITUDE_DISPLAY'] = formatAltitudeDisplay(extra.altitudeMeters, altitudeUnitCode());
    dict['ALTITUDE_COLOR'] = altitudeGradientColorByte(extra.altitudeMeters);
  }
  if (typeof extra.windDirDeg === 'number') dict['WIND_DIR_DEG'] = Math.round(extra.windDirDeg);
  if (typeof extra.dewPointC === 'number') {
    dict['DEW_POINT_C'] = Math.round(extra.dewPointC);
    dict['DEW_POINT_DISPLAY'] = formatTempDisplay(extra.dewPointC, tempUnitCode()); // no colour: never a gradient
  }
  if (typeof extra.pressureHpa === 'number') {
    dict['PRESSURE_HPA'] = Math.round(extra.pressureHpa);
    dict['PRESSURE_DISPLAY'] = formatPressureDisplay(extra.pressureHpa);
    dict['PRESSURE_COLOR'] = pressureGradientColorByte(extra.pressureHpa);
  }
  if (typeof extra.aqiUs === 'number') dict['AQI_US'] = extra.aqiUs;
  if (typeof extra.aqiEu === 'number') dict['AQI_EU'] = extra.aqiEu;
  if (typeof extra.aqiUs === 'number' || typeof extra.aqiEu === 'number') {
    var aqiValue = aqiUnitCode() === 1 ? extra.aqiEu : extra.aqiUs;
    if (typeof aqiValue === 'number') {
      dict['AQI_DISPLAY'] = formatAqiDisplay(aqiValue);
      dict['AQI_COLOR'] = aqiGradientColorByte(aqiValue, aqiUnitCode());
    }
  }
  // x10 -- same convention as every other _x10/_decideg field. Kp is
  // fractional (thirds: .00/.33/.67), hence the scaling rather than
  // sending it as a plain 0-9 integer.
  if (typeof extra.auroraKpX10 === 'number') {
    dict['AURORA_KP_X10'] = extra.auroraKpX10;
    dict['AURORA_KP_DISPLAY'] = featureDisplay.formatAuroraKpDisplay(extra.auroraKpX10);
    dict['AURORA_KP_COLOR'] = featureDisplay.auroraGradientColorByte(extra.auroraKpX10);
  }
  if (typeof extra.auroraVisibilityPct === 'number') dict['AURORA_VISIBILITY_PCT'] = extra.auroraVisibilityPct;
  // Always sent, same reasoning as ISS_ERROR_CODE above.
  dict['AURORA_ERROR_CODE'] = extra.auroraErrorCode || 0;
  // "Weather in N hours/days" -- sent as byte arrays, one entry per forecast slot: 1h
  // through 6h ahead, 1 through 3 days ahead, then 7h-12h ahead (indices 0-5, 6-8, 9-14;
  // content id - 87 either way, same index eclipse_data.h's own
  // forecast_temp_c/forecast_condition use). Only sent when the
  // daily-forecast fetch that produced them actually succeeded, same
  // reasoning as every other field here -- a transient failure
  // shouldn't wipe out the watch's last known-good forecast.
  // forecast_temp_c can't go through as a plain signed byte (AppMessage
  // byte-array tuples are unsigned), so each value is offset by +50
  // celsius first; 255 is the "not available" sentinel for a single
  // entry within an otherwise-successful fetch (see forecastTempC's own
  // per-index null check in weather.js).
  if (extra.forecastTempC) {
    // FORECAST blob (4 x 15 bytes, decoded by comms_decoder.c): temp C with a +50 offset
    // (AppMessage byte arrays are unsigned; 255 = this entry unavailable), packed temp
    // colour, condition, icon category (same function as WEATHER_ICON_CATEGORY, neutral 50%
    // cloud guess). An unavailable slot's display text is '' ("nothing to show").
    var n = extra.forecastTempC.length;
    var cond = extra.forecastCondition || [];
    var fTemp = [], fColor = [], fCond = [], fIcon = [];
    for (var fi = 0; fi < n; fi++) {
      var fc = extra.forecastTempC[fi];
      var cc = (typeof cond[fi] === 'number') ? cond[fi] : 0;
      fTemp.push((typeof fc === 'number') ? Math.max(0, Math.min(255, Math.round(fc) + 50)) : 255);
      fColor.push(tempGradientColorByte(fc));
      fCond.push(cc);
      fIcon.push(weatherIconCategory(cc, 50));
    }
    dict['FORECAST'] = fTemp.concat(fColor, fCond, fIcon);
    dict['FORECAST_TEMP_DISPLAYS'] = forecastDisplayBytes(extra.forecastTempC, tempUnitCode());
  }
  return dict;
}

// Feature-facing weather fields shared by both full-data sends: the display strings and
// gradient colours the primitive features show (formatted here, in the user's units), the
// location name, the PKJS-chosen weather icon category/colour and the colour blobs whose
// order must match WEATHER_CLOUD_BLOB_OFFSETS / WEATHER_MAIN_BLOB_OFFSETS in comms_decoder.c.
// The eclipse-window CLOUD_COVER/VIS_SCORE keys stay as the sends set them.
function addFeatureWeatherFields(dict, o) {
  dict['LOCATION_NAME'] = o.locationName || '';
  if (o.haveCloudData) {
    var cloudColor = overcastGrayColorByte(o.displayCloudPct);
    dict['WEATHER_CLOUD_U8_BLOB'] = [cloudColor, cloudColor]; // [cloud_cover_color, vis_score_color]
    dict['CLOUD_COVER_DISPLAY'] = formatPercentDisplay(o.displayCloudPct);
    dict['VIS_SCORE_DISPLAY'] = formatPercentDisplay(100 - o.displayCloudPct);
  }
  if (o.weatherOk) {
    var unit = tempUnitCode();
    // [current_temp, temp_high, temp_low, feels_like, uv_daily, uv_current, rain_chance, humidity, wind_speed]
    dict['WEATHER_MAIN_U8_BLOB'] = [
      tempGradientColorByte(o.tempC), tempGradientColorByte(o.tempHighC), tempGradientColorByte(o.tempLowC),
      tempGradientColorByte(apparentTempC(o.tempC, o.windSpeedKmh, o.humidityPct)),
      uvGradientColorByte(o.uvIndexMax), uvGradientColorByte(o.uvIndexCurrent),
      percentGradientColorByte(o.rainChancePct), percentGradientColorByte(o.humidityPct),
      windSpeedGradientColorByte(o.windSpeedKmh)
    ];
    if (o.haveCloudData) {
      dict['WEATHER_ICON_CATEGORY'] = weatherIconCategory(o.condition || 0, o.displayCloudPct);
      dict['WEATHER_ICON_COLOR'] = weatherConditionColorByte(o.condition || 0, o.displayCloudPct, o.rainChancePct);
    }
    dict['CURRENT_TEMP_DISPLAY'] = formatTempDisplay(o.tempC, unit);
    dict['TEMP_HIGH_DISPLAY'] = formatTempDisplay(o.tempHighC, unit);
    dict['TEMP_LOW_DISPLAY'] = formatTempDisplay(o.tempLowC, unit);
    dict['FEELS_LIKE_DISPLAY'] = formatTempDisplay(apparentTempC(o.tempC, o.windSpeedKmh, o.humidityPct), unit);
    dict['UV_DAILY_DISPLAY'] = formatUvDisplay(o.uvIndexMax);
    dict['UV_CURRENT_DISPLAY'] = formatUvDisplay(o.uvIndexCurrent);
    dict['RAIN_CHANCE_DISPLAY'] = formatPercentDisplay(o.rainChancePct);
    dict['HUMIDITY_DISPLAY'] = formatPercentDisplay(o.humidityPct);
    dict['WIND_SPEED_DISPLAY'] = formatWindSpeedDisplay(o.windSpeedKmh, windSpeedUnitCode());
  }
}

// ---- unit changes without a weather refresh -----------------------------------------
//
// The *_DISPLAY strings that depend on a unit setting (temperatures, wind speed, altitude,
// air quality scale) are formatted here when weather is sent, so changing a unit would show
// on the watch only after the next weather push. The raw inputs of the last push are
// remembered (LAST_UNIT_INPUTS) so the strings can be re-formatted the moment a unit changes.
var UNIT_INPUTS_KEY = 'LAST_UNIT_INPUTS';
function rememberUnitInputs(o) {
  try {
    var e = o.extraWeather || {};
    localStorage.setItem(UNIT_INPUTS_KEY, JSON.stringify({
      ok: !!o.weatherOk, tempC: o.tempC, highC: o.tempHighC, lowC: o.tempLowC, humidity: o.humidityPct, windKmh: o.windSpeedKmh,
      dewC: e.dewPointC, altM: e.altitudeMeters, forecastTempC: e.forecastTempC, aqiUs: e.aqiUs, aqiEu: e.aqiEu
    }));
  } catch (err) { /* storage unavailable: units then apply with the next weather push */ }
}
function unitDisplayDict() {
  var raw = null;
  try { raw = JSON.parse(localStorage.getItem(UNIT_INPUTS_KEY)); } catch (err) { return null; }
  if (!raw || typeof raw !== 'object') return null;
  var dict = {}, tempUnit = tempUnitCode();
  if (raw.ok) {
    dict['CURRENT_TEMP_DISPLAY'] = formatTempDisplay(raw.tempC, tempUnit);
    dict['TEMP_HIGH_DISPLAY'] = formatTempDisplay(raw.highC, tempUnit);
    dict['TEMP_LOW_DISPLAY'] = formatTempDisplay(raw.lowC, tempUnit);
    dict['FEELS_LIKE_DISPLAY'] = formatTempDisplay(apparentTempC(raw.tempC, raw.windKmh, raw.humidity), tempUnit);
    dict['WIND_SPEED_DISPLAY'] = formatWindSpeedDisplay(raw.windKmh, windSpeedUnitCode());
  }
  if (typeof raw.dewC === 'number') dict['DEW_POINT_DISPLAY'] = formatTempDisplay(raw.dewC, tempUnit);
  if (typeof raw.altM === 'number') dict['ALTITUDE_DISPLAY'] = formatAltitudeDisplay(raw.altM, altitudeUnitCode());
  if (Array.isArray(raw.forecastTempC)) {
    dict['FORECAST_TEMP_DISPLAYS'] = forecastDisplayBytes(raw.forecastTempC, tempUnit);
  }
  if (typeof raw.aqiUs === 'number' || typeof raw.aqiEu === 'number') {
    var aqiUnit = aqiUnitCode();
    var aqiValue = aqiUnit === 1 ? raw.aqiEu : raw.aqiUs;
    if (typeof aqiValue === 'number') {
      dict['AQI_DISPLAY'] = formatAqiDisplay(aqiValue);
      dict['AQI_COLOR'] = aqiGradientColorByte(aqiValue, aqiUnit);
    }
  }
  return Object.keys(dict).length ? dict : null;
}
// Sends the re-formatted strings and updates the cached full dict, so a later resend of it
// does not bring the old units back. Returns false when nothing was sent yet.
function resendUnitDisplays() {
  var dict = unitDisplayDict();
  if (!dict) return false;
  try {
    var cached = JSON.parse(localStorage.getItem('LAST_FULL_COMPUTED_DICT'));
    if (cached && typeof cached === 'object') {
      Object.keys(dict).forEach(function (k) { cached[k] = dict[k]; });
      localStorage.setItem('LAST_FULL_COMPUTED_DICT', JSON.stringify(cached));
    }
  } catch (err) { /* nothing cached yet */ }
  enqueueFlatDict(dict);
  return true;
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
    'RADIUS_RATIO_PCT': 0
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
  addFeatureWeatherFields(dict, {
    locationName: locationName, haveCloudData: haveCloudData, displayCloudPct: displayCloudPct, weatherOk: weatherOk,
    condition: weatherCondition, tempC: weatherTempC, tempHighC: tempHighC, tempLowC: tempLowC,
    uvIndexMax: uvIndexMax, uvIndexCurrent: uvIndexCurrent, rainChancePct: rainChancePct,
    humidityPct: humidityPct, windSpeedKmh: windSpeedKmh, extraWeather: extraWeather
  });
  rememberUnitInputs({ weatherOk: weatherOk, tempC: weatherTempC, tempHighC: tempHighC, tempLowC: tempLowC,
    humidityPct: humidityPct, windSpeedKmh: windSpeedKmh, extraWeather: extraWeather });
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
    'RADIUS_RATIO_PCT': result.radiusRatioPct
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
  addFeatureWeatherFields(dict, {
    locationName: locationName, haveCloudData: haveCloudData, displayCloudPct: displayCloudPct, weatherOk: weatherOk,
    condition: weatherCondition, tempC: weatherTempC, tempHighC: tempHighC, tempLowC: tempLowC,
    uvIndexMax: uvIndexMax, uvIndexCurrent: uvIndexCurrent, rainChancePct: rainChancePct,
    humidityPct: humidityPct, windSpeedKmh: windSpeedKmh, extraWeather: extraWeather
  });
  rememberUnitInputs({ weatherOk: weatherOk, tempC: weatherTempC, tempHighC: tempHighC, tempLowC: tempLowC,
    humidityPct: humidityPct, windSpeedKmh: windSpeedKmh, extraWeather: extraWeather });
  var sky_ = skyFieldsDict(sky, cloudGrid, moonPhase, riseSet, meteorShower, cloudAltitudePct, sunRiseTomorrow, stars);
  Object.keys(sky_).forEach(function (k) { dict[k] = sky_[k]; });
  var extraW_ = extraWeatherFieldsDict(extraWeather);
  Object.keys(extraW_).forEach(function (k) { dict[k] = extraW_[k]; });
  var iss_ = issFieldsDict(issPos, issErrorCode);
  Object.keys(iss_).forEach(function (k) { dict[k] = iss_[k]; });
  sendFlatDict(dict);
}

// Settings-page "Fake weather" debug panel: the raw slider values (wire key names, as the
// page builds them) go out unchanged, plus the display strings/colours the primitive
// features read, derived from them. Enqueued as-is; nothing is persisted.
function sendFakeWeather(v) {
  var dict = {};
  Object.keys(v).forEach(function (k) { dict[k] = v[k]; });
  dict['DATA_VALID'] = 1;
  var cloud = typeof v.CLOUD_COVER === 'number' && !isNaN(v.CLOUD_COVER) ? v.CLOUD_COVER : 0;
  var windSpeedKmh = v.WIND_SPEED_KMH, humidityPct = v.HUMIDITY_PCT;
  var extra = extraWeatherFieldsDict({
    windDirDeg: v.WIND_DIR_DEG, dewPointC: v.DEW_POINT_C, pressureHpa: v.PRESSURE_HPA,
    pressureTrend: v.PRESSURE_TREND, aqiUs: v.AQI_US, aqiEu: v.AQI_EU
  });
  delete extra['AURORA_ERROR_CODE'];
  delete extra['ALTITUDE_M'];
  var feature = {};
  addFeatureWeatherFields(feature, {
    locationName: '', haveCloudData: true, displayCloudPct: cloud, weatherOk: true,
    condition: v.WEATHER_CONDITION, tempC: v.WEATHER_TEMP_C, tempHighC: v.WEATHER_TEMP_HIGH_C, tempLowC: v.WEATHER_TEMP_LOW_C,
    uvIndexMax: v.UV_INDEX_X10 / 10, uvIndexCurrent: v.UV_INDEX_CURRENT_X10 / 10, rainChancePct: v.RAIN_CHANCE_PCT,
    humidityPct: humidityPct, windSpeedKmh: windSpeedKmh
  });
  delete feature['LOCATION_NAME'];
  [extra, feature].forEach(function (part) { Object.keys(part).forEach(function (k) { dict[k] = part[k]; }); });
  enqueueFlatDict(dict);
}

module.exports = {
  sendFlatDict: sendFlatDict,
  sendInvalid: sendInvalid,
  sendEclipseData: sendEclipseData,
  sendNoEclipseToday: sendNoEclipseToday,
  sendFakeWeather: sendFakeWeather,
  resendUnitDisplays: resendUnitDisplays,
  buildFullKeysetDict: buildFullKeysetDict,
  overheadObjectsBytes: overheadObjectsBytes,
  activePrimitiveIds: activePrimitiveIds
};
