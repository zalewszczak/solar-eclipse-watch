var configPage = require('./config-page');
var servicelog = require('./servicelog');

// Settings keys are migrated to the current schema version before anything
// reads them (self-executes on require).
require('./settings/settings-migration');

// AppMessage chunking, message types and the key-to-chunk map live in
// comms/message-schema.js; queueing/sending in comms/message-queue.js; field
// encoding in comms/message-encoder.js.
var settings = require('./settings/settings');
var getSetting = settings.getSetting;
var setSetting = settings.setSetting;
var settingsCodecs = require('./settings/settings-codecs');
var outlineEnabledCode = settingsCodecs.outlineEnabledCode;
var messageEncoder = require('./comms/message-encoder');
var sendFlatDict = messageEncoder.sendFlatDict;
var buildFullKeysetDict = messageEncoder.buildFullKeysetDict;
var overheadObjectsBytes = messageEncoder.overheadObjectsBytes;
var messageQueue = require('./comms/message-queue');
var enqueueFlatDict = messageQueue.enqueueFlatDict;
var refreshManager = require('./refresh-manager');
var refreshAndSend = refreshManager.refreshAndSend;
var scheduleRefresh = refreshManager.scheduleRefresh;
var startBatterySaverHourlyCheck = refreshManager.startBatterySaverHourlyCheck;
var overheadObjects = require('./overhead-objects');
var pluginManager = require('./plugins/plugin-manager');
var preferences = require('./plugins/preferences');
var firstRun = require('./settings/first-run');
var configBridge = require('./plugins/config-bridge');

// ---- scheduled My styles ------------------------------------------------
//
// See scheduled-style.js for the whole design. Short version: the watch
// asks (REQUEST_SCHEDULED_STYLE) once NEXT_STYLE_CHECK has passed; this
// applies whatever is due and pushes the result.
var scheduledStyle = require('./scheduled-style');

// Applies any due scheduled style and, if one was applied, pushes the new
// settings (plus the fresh NEXT_STYLE_CHECK they carry) to the watch.
function serveScheduledStyle() {
  var r = scheduledStyle.applyDue(Date.now());
  if (r.applied) {
    console.log('eclipse-watch: scheduled style applied from My style slot ' + r.slot);
    // The style may carry its own plugin slot assignments: re-subscribe to them first.
    pluginManager.setSlots(pluginManager.getSlots());
    sendFlatDict({});
    // Same follow-up a manual settings save does: only refetches if the
    // regular refresh interval has actually elapsed.
    refreshAndSend(false, false);
  }
  return r.applied;
}

// ---- first start ---------------------------------------------------------
//
// On the very first start only the Pebble app's user preferences (units and
// preferred complications) are applied; Eclipz keeps its own defaults (see
// settings/first-run.js).
firstRun.seedIfFresh();

function applyFirstRunPreferences() {
  if (!firstRun.pending()) return;
  preferences.capture(5000, function (prefs) {
    if (!firstRun.applyPreferences(prefs, pluginManager)) return;
    sendFlatDict({});
    messageEncoder.resendUnitDisplays();
    refreshAndSend(false, false);
  });
}

// ---- Pebble lifecycle --------------------------------------------------

Pebble.addEventListener('ready', function () {
  console.log('eclipse-watch: PKJS ready, capabilities OK, starting first refresh');
  // Plugin sources: subscribe to whatever the user assigned and push values as they change.
  pluginManager.init(function (bytes) { enqueueFlatDict({ PLUGIN_TEXTS: bytes }); });
  applyFirstRunPreferences();
  // Catch up on anything that came due while PKJS was not running (phone
  // off, watch on another app). Done first so the refresh below already
  // sends the applied style.
  serveScheduledStyle();
  refreshAndSend(false, false);
  scheduleRefresh();
  startBatterySaverHourlyCheck();
});

Pebble.addEventListener('appmessage', function (e) {
  // The watch's own clock says a scheduled style is due (or overdue -- it
  // repeats once a minute until it receives a new NEXT_STYLE_CHECK). If
  // nothing is actually due here (clock skew, already served), reply with
  // just the fresh next time so the watch stops or re-aims its retries.
  if (e && e.payload && e.payload.REQUEST_SCHEDULED_STYLE) {
    if (!serveScheduledStyle()) {
      messageQueue.enqueueFlatDict({ 'NEXT_STYLE_CHECK': scheduledStyle.nextCheckSeconds(Date.now()) });
    }
  }
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
        // Keep the cache stale so the next shake can retry.
        sendFlatDict({ 'OVERHEAD_OBJECTS_COMPUTED_AT': 0 });
        return;
      }
      var opts = {
        showIss: getSetting('CONFIG_SHOW_ISS', 'false') === 'true',
        showFlights: getSetting('CONFIG_SHOW_FLIGHTS', 'false') === 'true',
        flightsRadiusKm: parseInt(getSetting('CONFIG_FLIGHTS_RANGE_KM', '50'), 10) || 50
      };
      overheadObjects.buildOverheadObjectList(lat, lon, opts, function (objects, hadError) {
        sendFlatDict({
          'OVERHEAD_OBJECTS': overheadObjectsBytes(objects),
          'OVERHEAD_OBJECT_COUNT': objects.length,
          // Sent whether or not anything came back -- this is what clears
          // the watch's "loading" flag either way (comms_decoder.c).
          // A failed source must not make the watch think its cache is fresh.
          // Presence of this field still clears the loading flag; zero keeps
          // the list immediately eligible for a retry on the next shake.
          'OVERHEAD_OBJECTS_COMPUTED_AT': hadError ? 0 : Math.floor(Date.now() / 1000)
        });
      });
    });
  }
});

// Everything the settings page needs to show its current state.
function buildCurrent() {
  return {
    autoLoc: getSetting('CONFIG_AUTO_LOC', 'true') !== 'false',
    lat: getSetting('CONFIG_LAT', ''),
    lon: getSetting('CONFIG_LON', ''),
    locationName: getSetting('CONFIG_LOCATION_NAME', ''),
    owmKey: getSetting('CONFIG_OWM_KEY', ''),
    updateMins: getSetting('CONFIG_UPDATE_MINS', '20'),
    clockFont: getSetting('CONFIG_CLOCK_FONT', '8'),
    // Settings-page-only remembered font choices. These are not sent to
    // the watch; they let the browser-side picker restore each font family
    // after the settings page itself is reloaded.
    lastBigDigitalFontId: getSetting('CONFIG_LAST_BIG_DIGITAL_FONT_ID', ''),
    lastDigitalFontId: getSetting('CONFIG_LAST_DIGITAL_FONT_ID', ''),
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
    bigAnalogMarkerStyle: getSetting('CONFIG_BIG_ANALOG_MARKER_STYLE', '10'),
    bitmapMarkerTransparent: getSetting('CONFIG_BITMAP_MARKER_TRANSPARENT', 'false') === 'true',
    // Browser-side only -- the watch never needs feature ids. The page compiles the chosen
    // features to primitive sequences before sending; persisted so the checkbox survives reopening.
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
    showMajorStars: getSetting('CONFIG_SHOW_MAJOR_STARS', 'true') === 'true',
    auroraEnabled: getSetting('CONFIG_AURORA_ENABLED', 'false') === 'true',
    vibrateOnPhaseChange: getSetting('CONFIG_VIBRATE_ON_PHASE_CHANGE', 'false') === 'true',
    bgAnimMode: getSetting('CONFIG_BG_ANIM_MODE', '0'),
    shakeAnimMode: getSetting('CONFIG_SHAKE_ANIM_MODE', '0'),
    outlineStyle: String(outlineEnabledCode()),
    batterySaverEnabled: getSetting('CONFIG_BATTERY_SAVER_ENABLED', 'false') === 'true',
    cornerFont: getSetting('CONFIG_CORNER_FONT', '1'),
    testMode: getSetting('CONFIG_TEST_MODE', 'false') === 'true',
    testDateTime: getSetting('CONFIG_TEST_DATETIME', ''),
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
    presetSlot6Image: getSetting('CONFIG_PRESET_6_IMAGE', ''),
    presetSlot1Schedule: getSetting('CONFIG_PRESET_1_SCHEDULE', ''),
    presetSlot2Schedule: getSetting('CONFIG_PRESET_2_SCHEDULE', ''),
    presetSlot3Schedule: getSetting('CONFIG_PRESET_3_SCHEDULE', ''),
    presetSlot4Schedule: getSetting('CONFIG_PRESET_4_SCHEDULE', ''),
    presetSlot5Schedule: getSetting('CONFIG_PRESET_5_SCHEDULE', ''),
    presetSlot6Schedule: getSetting('CONFIG_PRESET_6_SCHEDULE', ''),
    scheduleKnownIds: scheduledStyle.knownIds(),
    // Per-feature "show only when shaken" flags ('0'/'1') and user-built primitive sequences
    // (edge slots per layout; corners and bottomMiddleLine2 single). The page compiles chosen
    // features to primitive sequences itself; the phone persists them here.
    upperMiddleLine1CustomPrimsAnalog: getSetting('CONFIG_UPPER_MIDDLE_LINE1_CUSTOM_PRIMS_ANALOG', ''),
    upperMiddleLine1CustomPrimsDigital: getSetting('CONFIG_UPPER_MIDDLE_LINE1_CUSTOM_PRIMS_DIGITAL', ''),
    upperMiddleLine2CustomPrimsAnalog: getSetting('CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_ANALOG', ''),
    upperMiddleLine2CustomPrimsDigital: getSetting('CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_DIGITAL', ''),
    bottomMiddleLine1CustomPrimsAnalog: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_ANALOG', ''),
    bottomMiddleLine1CustomPrimsDigital: getSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_DIGITAL', ''),
    bottomMiddleLine2CustomPrims: getSetting('CONFIG_BOTTOM_MIDDLE_LINE2_CUSTOM_PRIMS', ''),
    middleLeftLine1CustomPrimsAnalog: getSetting('CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_ANALOG', ''),
    middleLeftLine1CustomPrimsDigital: getSetting('CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_DIGITAL', ''),
    middleLeftLine2CustomPrimsAnalog: getSetting('CONFIG_MIDDLE_LEFT_LINE2_CUSTOM_PRIMS_ANALOG', ''),
    middleLeftLine2CustomPrimsDigital: getSetting('CONFIG_MIDDLE_LEFT_LINE2_CUSTOM_PRIMS_DIGITAL', ''),
    middleRightLine1CustomPrimsAnalog: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_ANALOG', ''),
    middleRightLine1CustomPrimsDigital: getSetting('CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_DIGITAL', ''),
    middleRightLine2CustomPrimsAnalog: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CUSTOM_PRIMS_ANALOG', ''),
    middleRightLine2CustomPrimsDigital: getSetting('CONFIG_MIDDLE_RIGHT_LINE2_CUSTOM_PRIMS_DIGITAL', ''),
    cornerTLCustomPrims: getSetting('CONFIG_CORNER_TL_CUSTOM_PRIMS', ''),
    cornerTRCustomPrims: getSetting('CONFIG_CORNER_TR_CUSTOM_PRIMS', ''),
    cornerBLCustomPrims: getSetting('CONFIG_CORNER_BL_CUSTOM_PRIMS', ''),
    cornerBRCustomPrims: getSetting('CONFIG_CORNER_BR_CUSTOM_PRIMS', ''),
    // Pebble plugins: what the phone app offers (or why it does not), the sources assigned to
    // the watch's plugin slots, the weather source, and the Pebble app's user preferences
    // (units, complications) as last read, for the page's "Load user preferences" buttons.
    pluginCatalogue: pluginManager.readCatalogue(),
    preferences: preferences.snapshot(),
    pluginSlots: pluginManager.getSlots(),
    weatherSource: getSetting('CONFIG_WEATHER_SOURCE', 'plugin')
  };
}

// Legacy flow (older Pebble apps, or when no bundled page is configured).
Pebble.addEventListener('showConfiguration', function () {
  var html = configPage.buildConfigHtml(buildCurrent());
  // Classic no-server config page: the whole thing is a data: URI, no hosting required.
  // The page reads a `return_to` query param that the runtime appends (falling back to
  // pebblejs://close# per Pebble's manual-setup guide) and navigates there with the
  // settings JSON, which 'webviewclosed' below picks up as e.response.
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

  var parsed;
  try {
    parsed = JSON.parse(raw);
  } catch (err) {
    console.log('eclipse-watch: failed to parse settings response: ' + err.message);
    return;
  }
  applySettingsObject(parsed);
});

function unitSettingsSignature() {
  return [getSetting('CONFIG_TEMP_UNIT', 'C'), getSetting('CONFIG_WIND_SPEED_UNIT', 'kmh'),
    getSetting('CONFIG_ALTITUDE_UNIT', '0'), getSetting('CONFIG_AQI_UNIT', '0')].join('|');
}

// Applies a settings object from the page -- either handed over when the legacy
// page closes (above) or sent live by the bundled page (see plugins/config-bridge.js).
function applySettingsObject(settings) {

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

  // Debug-only fake weather send (see config-page.js's "Fake weather"
  // panel and its own sendFakeWeatherToWatch()/buildFakeWeatherDict()):
  // same shape as the CONFIG_SEND_FULL_KEYSET branch just above, just
  // for the smaller WEATHER-chunk-only dict that panel builds instead
  // of the full keyset -- enqueue it as-is and return before any of
  // the normal setSetting()/save-flow code below runs, so nothing
  // about a spoofed weather push is persisted and no other setting
  // changes.
  if (settings.CONFIG_SEND_FAKE_WEATHER) {
    try {
      var fakeWeather = JSON.parse(settings.CONFIG_FAKE_WEATHER_DATA);
      if (fakeWeather && typeof fakeWeather === 'object') {
        messageEncoder.sendFakeWeather(fakeWeather);
        console.log('eclipse-watch: fake weather debug send queued');
      } else {
        console.log('eclipse-watch: fake weather debug send skipped -- not a JSON object');
      }
    } catch (err) {
      console.log('eclipse-watch: fake weather debug send failed to parse: ' + err.message);
    }
    return;
  }

  // A real save: from here on the person's settings win over first-start defaults.
  firstRun.markDone();
  var unitsBefore = unitSettingsSignature();

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
  // Browser-side-only font context memory. Do not add these to the
  // AppMessage schema: they are purely for restoring the settings-page
  // picker after a page reload.
  if (settings.CONFIG_LAST_BIG_DIGITAL_FONT_ID !== undefined) {
    setSetting('CONFIG_LAST_BIG_DIGITAL_FONT_ID', settings.CONFIG_LAST_BIG_DIGITAL_FONT_ID || '');
  }
  if (settings.CONFIG_LAST_DIGITAL_FONT_ID !== undefined) {
    setSetting('CONFIG_LAST_DIGITAL_FONT_ID', settings.CONFIG_LAST_DIGITAL_FONT_ID || '');
  }
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
  setSetting('CONFIG_BIG_ANALOG_MARKER_STYLE', settings.CONFIG_BIG_ANALOG_MARKER_STYLE || '10');
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
  setSetting('CONFIG_SHOW_MAJOR_STARS', settings.CONFIG_SHOW_MAJOR_STARS === false ? 'false' : 'true');
  setSetting('CONFIG_AURORA_ENABLED', settings.CONFIG_AURORA_ENABLED ? 'true' : 'false');
  setSetting('CONFIG_VIBRATE_ON_PHASE_CHANGE', settings.CONFIG_VIBRATE_ON_PHASE_CHANGE ? 'true' : 'false');
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
  // Scheduled My styles. A changed schedule re-arms itself from now; an
  // unchanged one is left alone (see persistSlotSchedule()).
  (function () {
    var nowMs = Date.now();
    for (var n = 1; n <= scheduledStyle.SLOT_COUNT; n++) {
      scheduledStyle.persistSlotSchedule(n, settings['CONFIG_PRESET_' + n + '_SCHEDULE'], nowMs);
    }
  })();
  setSetting('CONFIG_HOURLY_VIBE_MODE', settings.CONFIG_HOURLY_VIBE_MODE || '0');
  setSetting('CONFIG_HOURLY_VIBE_INTERVAL_MIN', settings.CONFIG_HOURLY_VIBE_INTERVAL_MIN || '30');
  setSetting('CONFIG_HOURLY_VIBE_PATTERN', settings.CONFIG_HOURLY_VIBE_PATTERN || '0');
  setSetting('CONFIG_HOURLY_VIBE_START_TIME', settings.CONFIG_HOURLY_VIBE_START_TIME || '00:00');
  setSetting('CONFIG_HOURLY_VIBE_END_TIME', settings.CONFIG_HOURLY_VIBE_END_TIME || '00:00');
  setSetting('CONFIG_HOURLY_VIBE_DAYS_MASK', settings.CONFIG_HOURLY_VIBE_DAYS_MASK || '127');
  setSetting('CONFIG_HOURLY_VIBE_OVERRIDE_QUIET', settings.CONFIG_HOURLY_VIBE_OVERRIDE_QUIET === false ? 'false' : 'true');
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_CUSTOM_PRIMS_ANALOG', settings.CONFIG_UPPER_MIDDLE_LINE1_CUSTOM_PRIMS_ANALOG || '');
  setSetting('CONFIG_UPPER_MIDDLE_LINE1_CUSTOM_PRIMS_DIGITAL', settings.CONFIG_UPPER_MIDDLE_LINE1_CUSTOM_PRIMS_DIGITAL || '');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_ANALOG', settings.CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_ANALOG || '');
  setSetting('CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_DIGITAL', settings.CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_DIGITAL || '');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_ANALOG', settings.CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_ANALOG || '');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_DIGITAL', settings.CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_DIGITAL || '');
  setSetting('CONFIG_BOTTOM_MIDDLE_LINE2_CUSTOM_PRIMS', settings.CONFIG_BOTTOM_MIDDLE_LINE2_CUSTOM_PRIMS || '');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_ANALOG', settings.CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_ANALOG || '');
  setSetting('CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_DIGITAL', settings.CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_DIGITAL || '');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_CUSTOM_PRIMS_ANALOG', settings.CONFIG_MIDDLE_LEFT_LINE2_CUSTOM_PRIMS_ANALOG || '');
  setSetting('CONFIG_MIDDLE_LEFT_LINE2_CUSTOM_PRIMS_DIGITAL', settings.CONFIG_MIDDLE_LEFT_LINE2_CUSTOM_PRIMS_DIGITAL || '');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_ANALOG', settings.CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_ANALOG || '');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_DIGITAL', settings.CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_DIGITAL || '');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_CUSTOM_PRIMS_ANALOG', settings.CONFIG_MIDDLE_RIGHT_LINE2_CUSTOM_PRIMS_ANALOG || '');
  setSetting('CONFIG_MIDDLE_RIGHT_LINE2_CUSTOM_PRIMS_DIGITAL', settings.CONFIG_MIDDLE_RIGHT_LINE2_CUSTOM_PRIMS_DIGITAL || '');
  setSetting('CONFIG_CORNER_TL_CUSTOM_PRIMS', settings.CONFIG_CORNER_TL_CUSTOM_PRIMS || '');
  setSetting('CONFIG_CORNER_TR_CUSTOM_PRIMS', settings.CONFIG_CORNER_TR_CUSTOM_PRIMS || '');
  setSetting('CONFIG_CORNER_BL_CUSTOM_PRIMS', settings.CONFIG_CORNER_BL_CUSTOM_PRIMS || '');
  setSetting('CONFIG_CORNER_BR_CUSTOM_PRIMS', settings.CONFIG_CORNER_BR_CUSTOM_PRIMS || '');
  if (settings.CONFIG_WEATHER_SOURCE) setSetting('CONFIG_WEATHER_SOURCE', settings.CONFIG_WEATHER_SOURCE === 'openmeteo' ? 'openmeteo' : (/^plugin:[^\s]+$/.test(settings.CONFIG_WEATHER_SOURCE) ? settings.CONFIG_WEATHER_SOURCE : 'plugin'));
  // Plugin data slots: store the page's list and (re)subscribe to those sources.
  if (settings.CONFIG_PLUGIN_SLOTS !== undefined) pluginManager.setSlots(settings.CONFIG_PLUGIN_SLOTS);

  // The clock font / weather-readout toggle / colors are purely
  // cosmetic and phone-local -- send them immediately rather than
  // waiting for a full refresh cycle to complete, so a settings
  // change feels instant regardless of whether one happens at all.
  sendFlatDict({});
  // The unit-dependent texts (temperatures, wind, altitude, AQI) are formatted here, not on
  // the watch: re-send them now instead of waiting for the next weather update.
  if (unitSettingsSignature() !== unitsBefore) messageEncoder.resendUnitDisplays();

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
}

// Bundled settings page (newer Pebble apps): serves the page and takes live changes.
configBridge.install(function () {
  var current = buildCurrent();
  current.liveMode = true;
  return configPage.buildConfigHtml(current);
}, applySettingsObject, pluginManager, preferences);
