// ---- client-side save + Pebble/browser compatibility layer --------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 -- config-runtime slice). Same non-Node-module shape as
// config-preview.js: this exports the exact client-side JS source text
// that gets spliced into the settings page's embedded <script>, since
// that script runs in the phone's webview, a separate JS runtime from
// PKJS.
//
// Two things live here together because they're both about getting a
// finished, in-memory settings object safely off this page and into
// PKJS's hands, regardless of which Pebble app/OS/emulator is hosting
// the webview:
//
//   save(forceRefresh) -- reads every field on the page (re-deriving a
//   few values -- Show Seconds, which edge slots are actually
//   available -- at save time rather than trusting the DOM already
//   reflects the latest style change, since several UI events can
//   affect them) into one settings object, then hands it off via
//   getQueryParam()'s `return_to` protocol.
//
//   getQueryParam(name, defaultValue) -- the actual compatibility
//   shim: different Pebble apps/OS versions/the emulator all append
//   their own `return_to` query param telling this page where to
//   navigate with the finished settings (falling back to the legacy
//   `pebblejs://close#` prefix when none is present) -- see this
//   file's own top-of-module comment in config-page.js for the full
//   rationale.

module.exports =
'function save(forceRefresh) {' +
'  var mins = parseInt(document.getElementById("updateMins").value, 10);' +
'  if (isNaN(mins) || mins < 5) mins = 20;' +
'  var bottomStyleVal = document.getElementById("bottomStyleValue").value;' +
// Belt-and-suspenders re-check, independent of whatever the (possibly
// stale, possibly never-recomputed-since-load) Show Seconds checkbox
// itself currently holds -- onBottomStyleChange()/onFontChange()
// already gray it out and uncheck it live whenever the selected clock
// font can't support seconds, but this is the actual value that goes
// to the watch, so it\'s re-derived here from the current clock font
// selection directly rather than trusted to already be correct.
'  var clockFontSel = document.getElementById("clockFont");' +
'  var secondsOverriddenOff = bottomStyleVal !== "analog" && !secondsAvailableForDigital(parseInt(clockFontSel.value, 10), document.getElementById("digitalSides").value);' +
'  var showSecondsVal = !secondsOverriddenOff && document.getElementById("showSeconds").checked;' +
// Same "re-derive at save time rather than trust the DOM already
// reflects it" belt-and-suspenders principle as showSecondsVal above,
// now for which of the 8 edge-line content fields actually apply --
// onMarkerStyleChange()/onBottomStyleChange() already clear these live
// as the user changes styles, but this is the actual data that goes to
// the watch, so it\'s re-checked here against the CURRENT marker/bottom
// style regardless of whether an earlier UI event already handled it.
// features_layer.c no longer has its own copy of "which marker/
// bottom_style supports which edge slots" at all -- it just draws
// whatever content it\'s given, trusting a content of 0 to mean "off"
// -- so this is the one and only place that decision gets made.
'  var avail = computeSlotAvailability();' +
'  var isAnalogNow = bottomStyleVal === "analog";' +
// Every dual-context field (see DUAL_CONTEXT_FIELD_LIST\'s own
// comment) sends BOTH its Analog and Digital halves explicitly, one
// CONFIG_<FIELD>_CONTENT/COLOR_ANALOG/_DIGITAL setting each -- the
// context matching whatever\'s live right now (isAnalogNow) reads
// straight from its shared DOM element; the other one has no live
// element of its own to read (it\'s not the context currently shown),
// so it comes from DUAL_CONTEXT_SHADOW instead -- kept up to date by
// swapDualContextFields() every time the user actually switches
// layouts, and seeded from DUAL_CONTEXT_SEED at page load, so it\'s
// never actually missing by the time a save happens. Replaces the old
// edgeVal()/avail-gated zeroing this used to do for these 7 fields --
// that was the whole bug: a slot being unavailable in whichever
// context happened to be active AT SAVE TIME (the other layout, or a
// font/side-count change) used to wipe it instead of just not showing
// it right now.
'  function dualCtxVal(base, field, ctx) {' +
'    var id = base + field;' +
'    if ((ctx === "analog") === isAnalogNow) {' +
'      var el = document.getElementById(id);' +
'      return el ? el.value : "0";' +
'    }' +
'    var shadowVal = DUAL_CONTEXT_SHADOW[ctx][id];' +
'    return shadowVal !== undefined ? shadowVal : "0";' +
'  }' +
'  var settings = {' +
'    CONFIG_AUTO_LOC: document.getElementById("autoLoc").checked,' +
'    CONFIG_LAT: document.getElementById("lat").value,' +
'    CONFIG_LON: document.getElementById("lon").value,' +
'    CONFIG_LOCATION_NAME: document.getElementById("locationName").value,' +
'    CONFIG_OWM_KEY: document.getElementById("owmKey").value,' +
'    CONFIG_UPDATE_MINS: mins,' +
'    CONFIG_CLOCK_FONT: document.getElementById("clockFont").value,' +
'    CONFIG_LAST_BIG_DIGITAL_FONT_ID: lastBigDigitalFontId === null ? "" : String(lastBigDigitalFontId),' +
'    CONFIG_LAST_DIGITAL_FONT_ID: lastDigitalFontId === null ? "" : String(lastDigitalFontId),' +
'    CONFIG_TEMP_UNIT: document.getElementById("tempUnit").value,' +
'    CONFIG_WIND_SPEED_UNIT: document.getElementById("windSpeedUnit").value,' +
'    CONFIG_AQI_UNIT: document.getElementById("aqiUnit").value,' +
'    CONFIG_ALTITUDE_UNIT: document.getElementById("altitudeUnit").value,' +
'    CONFIG_SKY_MODE: document.getElementById("skyMode").value,' +
'    CONFIG_SHOW_MAJOR_STARS: document.getElementById("showMajorStars").checked,' +
'    CONFIG_WEATHER_ICON_STYLE: document.getElementById("weatherIconStyle").value,' +
'    CONFIG_SHOW_SECONDS: showSecondsVal,' +
'    CONFIG_CUSTOM_BG: document.getElementById("customBgValue").value,' +
'    CONFIG_CUSTOM_TEXT: document.getElementById("customTextValue").value,' +
'    CONFIG_CUSTOM_ACCENT: document.getElementById("customAccentValue").value,' +
'    CONFIG_NIGHT_ENABLED: document.getElementById("nightEnabled").checked,' +
'    CONFIG_NIGHT_CUSTOM_BG: document.getElementById("nightCustomBgValue").value,' +
'    CONFIG_NIGHT_CUSTOM_TEXT: document.getElementById("nightCustomTextValue").value,' +
'    CONFIG_NIGHT_CUSTOM_ACCENT: document.getElementById("nightCustomAccentValue").value,' +
'    CONFIG_BOTTOM_STYLE: bottomStyleVal || "digital",' +
'    CONFIG_SHADOW_TRANSLUCENT: document.getElementById("shadowTranslucent").value,' +
'    CONFIG_SHADOW_ANGLE: document.getElementById("shadowAngle").value,' +
'    CONFIG_BIG_ANALOG_MARKER_STYLE: document.getElementById("bigAnalogMarkerStyle").value,' +
'    CONFIG_BITMAP_MARKER_TRANSPARENT: document.getElementById("bitmapMarkerTransparent").checked,' +
'    CONFIG_BITMAP_CORNER_OVERRIDE: document.getElementById("bitmapCornerOverride").checked,' +
'    CONFIG_DRAW_FEATURES_BENEATH_HANDS: document.getElementById("drawFeaturesBeneathHands").checked,' +
'    CONFIG_UPPER_MIDDLE_LINE1_CONTENT_ANALOG: dualCtxVal("upperMiddleLine1", "Content", "analog"),' +
'    CONFIG_UPPER_MIDDLE_LINE1_CONTENT_DIGITAL: dualCtxVal("upperMiddleLine1", "Content", "digital"),' +
'    CONFIG_UPPER_MIDDLE_LINE1_COLOR_ANALOG: dualCtxVal("upperMiddleLine1", "Color", "analog"),' +
'    CONFIG_UPPER_MIDDLE_LINE1_COLOR_DIGITAL: dualCtxVal("upperMiddleLine1", "Color", "digital"),' +
'    CONFIG_UPPER_MIDDLE_LINE2_CONTENT_ANALOG: dualCtxVal("upperMiddleLine2", "Content", "analog"),' +
'    CONFIG_UPPER_MIDDLE_LINE2_CONTENT_DIGITAL: dualCtxVal("upperMiddleLine2", "Content", "digital"),' +
'    CONFIG_UPPER_MIDDLE_LINE2_COLOR_ANALOG: dualCtxVal("upperMiddleLine2", "Color", "analog"),' +
'    CONFIG_UPPER_MIDDLE_LINE2_COLOR_DIGITAL: dualCtxVal("upperMiddleLine2", "Color", "digital"),' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_ANALOG: dualCtxVal("bottomMiddleLine1", "Content", "analog"),' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_DIGITAL: dualCtxVal("bottomMiddleLine1", "Content", "digital"),' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_ANALOG: dualCtxVal("bottomMiddleLine1", "Color", "analog"),' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_DIGITAL: dualCtxVal("bottomMiddleLine1", "Color", "digital"),' +
// Analog-only -- a single plain setting, not a dual-context pair (see
// index.js\'s own bottomMiddleLine2ContentCode() comment). Still zeroed
// when avail.bottom says THIS marker style has no room for it while
// Analog is active (unchanged from before) -- just no longer zeroed
// purely for being in Digital mode, which was never a legitimate
// "unavailable" in the first place, only a different, irrelevant mode.
'    CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT: (isAnalogNow && !avail.bottom) ? "0" : document.getElementById("bottomMiddleLine2Content").value,' +
'    CONFIG_BOTTOM_MIDDLE_LINE2_COLOR: document.getElementById("bottomMiddleLine2Color").value,' +
'    CONFIG_MIDDLE_LEFT_LINE1_CONTENT_ANALOG: dualCtxVal("middleLeftLine1", "Content", "analog"),' +
'    CONFIG_MIDDLE_LEFT_LINE1_CONTENT_DIGITAL: dualCtxVal("middleLeftLine1", "Content", "digital"),' +
'    CONFIG_MIDDLE_LEFT_LINE1_COLOR_ANALOG: dualCtxVal("middleLeftLine1", "Color", "analog"),' +
'    CONFIG_MIDDLE_LEFT_LINE1_COLOR_DIGITAL: dualCtxVal("middleLeftLine1", "Color", "digital"),' +
'    CONFIG_MIDDLE_LEFT_LINE2_CONTENT_ANALOG: dualCtxVal("middleLeftLine2", "Content", "analog"),' +
'    CONFIG_MIDDLE_LEFT_LINE2_CONTENT_DIGITAL: dualCtxVal("middleLeftLine2", "Content", "digital"),' +
'    CONFIG_MIDDLE_LEFT_LINE2_COLOR_ANALOG: dualCtxVal("middleLeftLine2", "Color", "analog"),' +
'    CONFIG_MIDDLE_LEFT_LINE2_COLOR_DIGITAL: dualCtxVal("middleLeftLine2", "Color", "digital"),' +
'    CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_ANALOG: dualCtxVal("middleRightLine1", "Content", "analog"),' +
'    CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_DIGITAL: dualCtxVal("middleRightLine1", "Content", "digital"),' +
'    CONFIG_MIDDLE_RIGHT_LINE1_COLOR_ANALOG: dualCtxVal("middleRightLine1", "Color", "analog"),' +
'    CONFIG_MIDDLE_RIGHT_LINE1_COLOR_DIGITAL: dualCtxVal("middleRightLine1", "Color", "digital"),' +
'    CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_ANALOG: dualCtxVal("middleRightLine2", "Content", "analog"),' +
'    CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_DIGITAL: dualCtxVal("middleRightLine2", "Content", "digital"),' +
'    CONFIG_MIDDLE_RIGHT_LINE2_COLOR_ANALOG: dualCtxVal("middleRightLine2", "Color", "analog"),' +
'    CONFIG_MIDDLE_RIGHT_LINE2_COLOR_DIGITAL: dualCtxVal("middleRightLine2", "Color", "digital"),' +
'    CONFIG_DIGITAL_SIDES: document.getElementById("digitalSides").value,' +
'    CONFIG_DIGITAL_SIDES_PREFERRED: document.getElementById("digitalSidesPreferred").value,' +
'    CONFIG_SHOW_ISS: document.getElementById("showIss").checked,' +
'    CONFIG_SHOW_FLIGHTS: document.getElementById("showFlights").checked,' +
'    CONFIG_FLIGHTS_RANGE_KM: document.getElementById("flightsRangeKm").value,' +
'    CONFIG_AURORA_ENABLED: document.getElementById("auroraEnabled").checked,' +
'    CONFIG_VIBRATE_ON_PHASE_CHANGE: document.getElementById("vibrateOnPhaseChange").checked,' +
'    CONFIG_STARTUP_CLOCK_ANIM_MODE: document.getElementById("startupClockAnimMode").value,' +
'    CONFIG_BG_ANIM_MODE: document.getElementById("bgAnimMode").value,' +
'    CONFIG_SHAKE_ANIM_MODE: document.getElementById("shakeAnimMode").value,' +
'    CONFIG_OUTLINE_ENABLED: document.getElementById("outlineStyle").value,' +
'    CONFIG_BATTERY_SAVER_ENABLED: document.getElementById("batterySaverEnabled").checked,' +
'    CONFIG_CORNER_FONT: document.getElementById("cornerFont").value,' +
'    CONFIG_CORNER_TL: avail.cornersGrayed ? "0" : document.getElementById("cornerTL").value,' +
'    CONFIG_CORNER_TR: avail.cornersGrayed ? "0" : document.getElementById("cornerTR").value,' +
'    CONFIG_CORNER_BL: avail.cornersGrayed ? "0" : document.getElementById("cornerBL").value,' +
'    CONFIG_CORNER_BR: avail.cornersGrayed ? "0" : document.getElementById("cornerBR").value,' +
'    CONFIG_CORNER_TL_COLOR: document.getElementById("cornerTLColor").value,' +
'    CONFIG_CORNER_TR_COLOR: document.getElementById("cornerTRColor").value,' +
'    CONFIG_CORNER_BL_COLOR: document.getElementById("cornerBLColor").value,' +
'    CONFIG_CORNER_BR_COLOR: document.getElementById("cornerBRColor").value,' +
'    CONFIG_STEP_GOAL: document.getElementById("stepGoal").value,' +
'    CONFIG_SUN_MOON_SIZE: document.getElementById("sunMoonSize").value,' +
'    CONFIG_SHAKE_LABEL_SECONDS: document.getElementById("shakeLabelSeconds").value,' +
'    CONFIG_LABEL_STYLE: document.getElementById("labelStyle").value,' +
'    CONFIG_TEST_MODE: document.getElementById("testMode").checked,' +
'    CONFIG_TEST_DATETIME: document.getElementById("testDateTime").value,' +
'    CONFIG_DEBUG_OVERRIDE_ENABLED: document.getElementById("debugOverrideEnabled").checked,' +
'    CONFIG_CUSTOM_HOUR_STYLE: document.getElementById("customHourStyle").value,' +
'    CONFIG_CUSTOM_HOUR_THICKNESS: document.getElementById("customHourThickness").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_THICKNESS: document.getElementById("customHourInnerThickness").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_ECC: document.getElementById("customHourInnerEcc").value,' +
'    CONFIG_CUSTOM_HOUR_OUTER_ECC: document.getElementById("customHourOuterEcc").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_BORDER: document.getElementById("customHourInnerBorder").value,' +
'    CONFIG_CUSTOM_HOUR_OUTER_BORDER: document.getElementById("customHourOuterBorder").value,' +
'    CONFIG_CUSTOM_HOUR_TRANSLUCENT: document.getElementById("customHourTranslucent").value === "true",' +
'    CONFIG_CUSTOM_HOUR_COLOR: document.getElementById("customHourColor").value,' +
'    CONFIG_CUSTOM_SEC_STYLE: document.getElementById("customSecStyle").value,' +
'    CONFIG_CUSTOM_SEC_THICKNESS: document.getElementById("customSecThickness").value,' +
'    CONFIG_CUSTOM_SEC_INNER_THICKNESS: document.getElementById("customSecInnerThickness").value,' +
'    CONFIG_CUSTOM_SEC_INNER_ECC: document.getElementById("customSecInnerEcc").value,' +
'    CONFIG_CUSTOM_SEC_OUTER_ECC: document.getElementById("customSecOuterEcc").value,' +
'    CONFIG_CUSTOM_SEC_INNER_BORDER: document.getElementById("customSecInnerBorder").value,' +
'    CONFIG_CUSTOM_SEC_OUTER_BORDER: document.getElementById("customSecOuterBorder").value,' +
'    CONFIG_CUSTOM_SEC_TRANSLUCENT: document.getElementById("customSecTranslucent").value === "true",' +
'    CONFIG_CUSTOM_SEC_COLOR: document.getElementById("customSecColor").value,' +
'    CONFIG_MARKER_TEXT_TARGET: document.getElementById("markerTextTarget").value,' +
'    CONFIG_MARKER_TEXT_FONT: document.getElementById("markerTextFont").value,' +
'    CONFIG_MARKER_TEXT_OFFSET: document.getElementById("markerTextOffset").value,' +
'    CONFIG_MARKER_TEXT_HOUR_MASK: document.getElementById("markerTextHourMask").value,' +
'    CONFIG_MARKER_TEXT_SEC_MASK: document.getElementById("markerTextSecMask").value,' +
'    CONFIG_MARKER_TEXT_ROMAN: document.getElementById("markerTextRoman").checked,' +
'    CONFIG_HAND_HOUR_STYLE: document.getElementById("handHourStyle").value,' +
'    CONFIG_HAND_HOUR_WIDTH: document.getElementById("handHourWidth").value,' +
'    CONFIG_HAND_HOUR_LENGTH: document.getElementById("handHourLength").value,' +
'    CONFIG_HAND_HOUR_BACK_OFFSET: document.getElementById("handHourBackOffset").value,' +
'    CONFIG_HAND_HOUR_MIDDLE_OFFSET: document.getElementById("handHourMiddleOffset").value,' +
'    CONFIG_HAND_HOUR_SECONDARY_WIDTH: document.getElementById("handHourSecondaryWidth").value,' +
'    CONFIG_HAND_HOUR_COLOR: document.getElementById("handHourColor").value,' +
'    CONFIG_HAND_HOUR_OUTLINE_ENABLED: document.getElementById("handHourOutlineEnabled").value,' +
'    CONFIG_HAND_HOUR_OUTLINE_COLOR: document.getElementById("handHourOutlineColor").value,' +
'    CONFIG_HAND_HOUR_TRANSLUCENT: document.getElementById("handHourTranslucent").value === "true",' +
'    CONFIG_HAND_HOUR_SHADOW_ENABLED: document.getElementById("handHourShadowEnabled").value === "true",' +
'    CONFIG_HAND_HOUR_SHADOW_DISTANCE: document.getElementById("handHourShadowDistance").value,' +
'    CONFIG_HAND_HOUR_HOLLOW: document.getElementById("handHourHollow").value === "true",' +
'    CONFIG_HAND_HOUR_HOLLOW_THICKNESS: document.getElementById("handHourHollowThickness").value,' +
'    CONFIG_HAND_MIN_STYLE: document.getElementById("handMinStyle").value,' +
'    CONFIG_HAND_MIN_WIDTH: document.getElementById("handMinWidth").value,' +
'    CONFIG_HAND_MIN_LENGTH: document.getElementById("handMinLength").value,' +
'    CONFIG_HAND_MIN_BACK_OFFSET: document.getElementById("handMinBackOffset").value,' +
'    CONFIG_HAND_MIN_MIDDLE_OFFSET: document.getElementById("handMinMiddleOffset").value,' +
'    CONFIG_HAND_MIN_SECONDARY_WIDTH: document.getElementById("handMinSecondaryWidth").value,' +
'    CONFIG_HAND_MIN_COLOR: document.getElementById("handMinColor").value,' +
'    CONFIG_HAND_MIN_OUTLINE_ENABLED: document.getElementById("handMinOutlineEnabled").value,' +
'    CONFIG_HAND_MIN_OUTLINE_COLOR: document.getElementById("handMinOutlineColor").value,' +
'    CONFIG_HAND_MIN_TRANSLUCENT: document.getElementById("handMinTranslucent").value === "true",' +
'    CONFIG_HAND_MIN_SHADOW_ENABLED: document.getElementById("handMinShadowEnabled").value === "true",' +
'    CONFIG_HAND_MIN_SHADOW_DISTANCE: document.getElementById("handMinShadowDistance").value,' +
'    CONFIG_HAND_MIN_HOLLOW: document.getElementById("handMinHollow").value === "true",' +
'    CONFIG_HAND_MIN_HOLLOW_THICKNESS: document.getElementById("handMinHollowThickness").value,' +
'    CONFIG_HAND_SEC_STYLE: document.getElementById("handSecStyle").value,' +
'    CONFIG_HAND_SEC_WIDTH: document.getElementById("handSecWidth").value,' +
'    CONFIG_HAND_SEC_LENGTH: document.getElementById("handSecLength").value,' +
'    CONFIG_HAND_SEC_BACK_OFFSET: document.getElementById("handSecBackOffset").value,' +
'    CONFIG_HAND_SEC_MIDDLE_OFFSET: document.getElementById("handSecMiddleOffset").value,' +
'    CONFIG_HAND_SEC_SECONDARY_WIDTH: document.getElementById("handSecSecondaryWidth").value,' +
'    CONFIG_HAND_SEC_COLOR: document.getElementById("handSecColor").value,' +
'    CONFIG_HAND_SEC_OUTLINE_ENABLED: document.getElementById("handSecOutlineEnabled").value,' +
'    CONFIG_HAND_SEC_OUTLINE_COLOR: document.getElementById("handSecOutlineColor").value,' +
'    CONFIG_HAND_SEC_TRANSLUCENT: document.getElementById("handSecTranslucent").value === "true",' +
'    CONFIG_HAND_SEC_SHADOW_ENABLED: document.getElementById("handSecShadowEnabled").value === "true",' +
'    CONFIG_HAND_SEC_SHADOW_DISTANCE: document.getElementById("handSecShadowDistance").value,' +
'    CONFIG_HAND_SEC_HOLLOW: document.getElementById("handSecHollow").value === "true",' +
'    CONFIG_HAND_SEC_HOLLOW_THICKNESS: document.getElementById("handSecHollowThickness").value,' +
'    CONFIG_CENTER_CIRCLE_RADIUS: document.getElementById("centerCircleRadius").value,' +
'    CONFIG_CENTER_CIRCLE_COLOR: document.getElementById("centerCircleColor").value,' +
'    CONFIG_DEBUG_OVERRIDE_DATA: document.getElementById("debugData").value,' +
'    CONFIG_PRESET_1_NAME: document.getElementById("presetSlot1Name").value,' +
'    CONFIG_PRESET_1_JSON: document.getElementById("presetSlot1Json").value,' +
'    CONFIG_PRESET_1_IMAGE: document.getElementById("presetSlot1Image").value,' +
'    CONFIG_PRESET_2_NAME: document.getElementById("presetSlot2Name").value,' +
'    CONFIG_PRESET_2_JSON: document.getElementById("presetSlot2Json").value,' +
'    CONFIG_PRESET_2_IMAGE: document.getElementById("presetSlot2Image").value,' +
'    CONFIG_PRESET_3_NAME: document.getElementById("presetSlot3Name").value,' +
'    CONFIG_PRESET_3_JSON: document.getElementById("presetSlot3Json").value,' +
'    CONFIG_PRESET_3_IMAGE: document.getElementById("presetSlot3Image").value,' +
'    CONFIG_PRESET_4_NAME: document.getElementById("presetSlot4Name").value,' +
'    CONFIG_PRESET_4_JSON: document.getElementById("presetSlot4Json").value,' +
'    CONFIG_PRESET_4_IMAGE: document.getElementById("presetSlot4Image").value,' +
'    CONFIG_PRESET_5_NAME: document.getElementById("presetSlot5Name").value,' +
'    CONFIG_PRESET_5_JSON: document.getElementById("presetSlot5Json").value,' +
'    CONFIG_PRESET_5_IMAGE: document.getElementById("presetSlot5Image").value,' +
'    CONFIG_PRESET_6_NAME: document.getElementById("presetSlot6Name").value,' +
'    CONFIG_PRESET_6_JSON: document.getElementById("presetSlot6Json").value,' +
'    CONFIG_PRESET_6_IMAGE: document.getElementById("presetSlot6Image").value,' +
'    CONFIG_PRESET_1_SCHEDULE: document.getElementById("presetSlot1Schedule").value,' +
'    CONFIG_PRESET_2_SCHEDULE: document.getElementById("presetSlot2Schedule").value,' +
'    CONFIG_PRESET_3_SCHEDULE: document.getElementById("presetSlot3Schedule").value,' +
'    CONFIG_PRESET_4_SCHEDULE: document.getElementById("presetSlot4Schedule").value,' +
'    CONFIG_PRESET_5_SCHEDULE: document.getElementById("presetSlot5Schedule").value,' +
'    CONFIG_PRESET_6_SCHEDULE: document.getElementById("presetSlot6Schedule").value,' +
'    CONFIG_DRAW_DEBUG: document.getElementById("drawDebug").checked,' +
'    CONFIG_HOURLY_VIBE_MODE: document.getElementById("hourlyVibeMode").value,' +
'    CONFIG_HOURLY_VIBE_INTERVAL_MIN: document.getElementById("hourlyVibeIntervalMin").value,' +
'    CONFIG_HOURLY_VIBE_PATTERN: document.getElementById("hourlyVibePattern").value,' +
'    CONFIG_HOURLY_VIBE_START_TIME: document.getElementById("hourlyVibeStartTime").value,' +
'    CONFIG_HOURLY_VIBE_END_TIME: document.getElementById("hourlyVibeEndTime").value,' +
'    CONFIG_HOURLY_VIBE_DAYS_MASK: document.getElementById("hourlyVibeDaysMask").value,' +
'    CONFIG_HOURLY_VIBE_OVERRIDE_QUIET: (function () { var el = document.getElementById("hourlyVibeOverrideQuiet"); return el ? el.checked : true; })()' +
'  };' +
// Transient, one-shot -- read once by index.js's webviewclosed
// handler to decide whether this save should force an immediate
// network refetch ("Force refresh now") or just apply cosmetic
// settings and let the normal refresh cadence pick up anything that
// actually needs new data -- never itself persisted via setSetting.
'  settings.CONFIG_FORCE_REFRESH = !!forceRefresh;' +
'  var returnTo = getQueryParam("return_to", "pebblejs://close#");' +
'  document.location = returnTo + encodeURIComponent(JSON.stringify(settings));' +
'}' +
'function getQueryParam(name, defaultValue) {' +
'  var query = location.search.substring(1);' +
'  var vars = query.split("&");' +
'  for (var i = 0; i < vars.length; i++) {' +
'    var pair = vars[i].split("=");' +
'    if (pair[0] === name) return decodeURIComponent(pair[1] || "");' +
'  }' +
'  return defaultValue;' +
'}' +
'';
