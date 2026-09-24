// ---- Scheduled "My styles" -------------------------------------------------
//
// Each of the six My Style slots can carry a schedule ("apply this style at
// 08:00 on these weekdays"). The settings page stores it in
// CONFIG_PRESET_<n>_SCHEDULE as JSON: {"enabled":true,"days":127,"time":"08:00"}
// (days: bit 0 = Sunday ... bit 6 = Saturday, same as the hourly vibration).
//
// PKJS cannot be trusted to wake itself up: it only exists while the watchface
// is running, and timers in a backgrounded phone app are best-effort at
// most. The watch, however, is always running the watchface and always knows
// what time it is. So the split is:
//
//   phone  -> watch : NEXT_STYLE_CHECK, the epoch second of the next schedule
//                     occurrence that has not been served yet (0 = none).
//                     Added to every settings push (see populateSettingsFields).
//   watch  -> phone : REQUEST_SCHEDULED_STYLE, sent from the watch's tick
//                     handler once that time has passed. That message is what
//                     starts/wakes PKJS. If the phone is out of reach the watch
//                     just asks again a minute later until it gets through.
//   phone  -> watch : the style (a normal settings push) plus a new
//                     NEXT_STYLE_CHECK, which is what ends the retries.
//
// Bookkeeping lives in localStorage so it survives PKJS restarts:
//   SCHEDULE_ARMED_AT_<n>     when the current schedule was saved. Occurrences
//                             before this never fire, so enabling a schedule at
//                             09:30 for 08:00 does not apply the style at once.
//   SCHEDULE_LAST_APPLIED_<n> occurrence time last served for this slot.
// An occurrence is "unserved" if it is later than both. Missed ones (phone off,
// watch away from the watchface) are caught up when the watch next asks, or
// when PKJS starts: the most recent missed occurrence across all slots wins,
// and older missed ones are marked served without being applied.

var settings = require('./settings/settings');
var getSetting = settings.getSetting;
var setSetting = settings.setSetting;

var SLOT_COUNT = 6;
var DAY_MS = 24 * 60 * 60 * 1000;
// The watch and phone clocks can disagree by a few seconds; treat an
// occurrence slightly in the phone's future as due.
var DUE_TOLERANCE_MS = 90 * 1000;

// ---- preset JSON -> stored settings ------------------------------------------
//
// A saved style is the settings page's own DOM snapshot (collectStyleCornersJson():
// element id -> value). Applying one from PKJS means doing what the page's
// save() + the webviewclosed handler would do for those elements. The table
// below is the element id -> CONFIG_* key mirror of save()'s payload
// ('s' = stored as the string, 'b' = checkbox / "true" string -> 'true'|'false').
// scripts/check-scheduled-style-map.js verifies it against config-runtime.js,
// and the settings page warns in the schedule popup if a style contains an
// element id that is neither listed here nor in UI_ONLY_IDS.

var FIELD_MAP = {
  showSeconds: ['CONFIG_SHOW_SECONDS', 'b'],
  clockFont: ['CONFIG_CLOCK_FONT', 's'],
  handHourStyle: ['CONFIG_HAND_HOUR_STYLE', 's'],
  handHourWidth: ['CONFIG_HAND_HOUR_WIDTH', 's'],
  handHourLength: ['CONFIG_HAND_HOUR_LENGTH', 's'],
  handHourBackOffset: ['CONFIG_HAND_HOUR_BACK_OFFSET', 's'],
  handHourMiddleOffset: ['CONFIG_HAND_HOUR_MIDDLE_OFFSET', 's'],
  handHourSecondaryWidth: ['CONFIG_HAND_HOUR_SECONDARY_WIDTH', 's'],
  handHourColor: ['CONFIG_HAND_HOUR_COLOR', 's'],
  handHourOutlineEnabled: ['CONFIG_HAND_HOUR_OUTLINE_ENABLED', 's'],
  handHourOutlineColor: ['CONFIG_HAND_HOUR_OUTLINE_COLOR', 's'],
  handHourTranslucent: ['CONFIG_HAND_HOUR_TRANSLUCENT', 'b'],
  handHourShadowEnabled: ['CONFIG_HAND_HOUR_SHADOW_ENABLED', 'b'],
  handHourShadowDistance: ['CONFIG_HAND_HOUR_SHADOW_DISTANCE', 's'],
  handHourHollow: ['CONFIG_HAND_HOUR_HOLLOW', 'b'],
  handHourHollowThickness: ['CONFIG_HAND_HOUR_HOLLOW_THICKNESS', 's'],
  handMinStyle: ['CONFIG_HAND_MIN_STYLE', 's'],
  handMinWidth: ['CONFIG_HAND_MIN_WIDTH', 's'],
  handMinLength: ['CONFIG_HAND_MIN_LENGTH', 's'],
  handMinBackOffset: ['CONFIG_HAND_MIN_BACK_OFFSET', 's'],
  handMinMiddleOffset: ['CONFIG_HAND_MIN_MIDDLE_OFFSET', 's'],
  handMinSecondaryWidth: ['CONFIG_HAND_MIN_SECONDARY_WIDTH', 's'],
  handMinColor: ['CONFIG_HAND_MIN_COLOR', 's'],
  handMinOutlineEnabled: ['CONFIG_HAND_MIN_OUTLINE_ENABLED', 's'],
  handMinOutlineColor: ['CONFIG_HAND_MIN_OUTLINE_COLOR', 's'],
  handMinTranslucent: ['CONFIG_HAND_MIN_TRANSLUCENT', 'b'],
  handMinShadowEnabled: ['CONFIG_HAND_MIN_SHADOW_ENABLED', 'b'],
  handMinShadowDistance: ['CONFIG_HAND_MIN_SHADOW_DISTANCE', 's'],
  handMinHollow: ['CONFIG_HAND_MIN_HOLLOW', 'b'],
  handMinHollowThickness: ['CONFIG_HAND_MIN_HOLLOW_THICKNESS', 's'],
  handSecStyle: ['CONFIG_HAND_SEC_STYLE', 's'],
  handSecWidth: ['CONFIG_HAND_SEC_WIDTH', 's'],
  handSecLength: ['CONFIG_HAND_SEC_LENGTH', 's'],
  handSecBackOffset: ['CONFIG_HAND_SEC_BACK_OFFSET', 's'],
  handSecMiddleOffset: ['CONFIG_HAND_SEC_MIDDLE_OFFSET', 's'],
  handSecSecondaryWidth: ['CONFIG_HAND_SEC_SECONDARY_WIDTH', 's'],
  handSecColor: ['CONFIG_HAND_SEC_COLOR', 's'],
  handSecOutlineEnabled: ['CONFIG_HAND_SEC_OUTLINE_ENABLED', 's'],
  handSecOutlineColor: ['CONFIG_HAND_SEC_OUTLINE_COLOR', 's'],
  handSecTranslucent: ['CONFIG_HAND_SEC_TRANSLUCENT', 'b'],
  handSecShadowEnabled: ['CONFIG_HAND_SEC_SHADOW_ENABLED', 'b'],
  handSecShadowDistance: ['CONFIG_HAND_SEC_SHADOW_DISTANCE', 's'],
  handSecHollow: ['CONFIG_HAND_SEC_HOLLOW', 'b'],
  handSecHollowThickness: ['CONFIG_HAND_SEC_HOLLOW_THICKNESS', 's'],
  bigAnalogMarkerStyle: ['CONFIG_BIG_ANALOG_MARKER_STYLE', 's'],
  bitmapMarkerTransparent: ['CONFIG_BITMAP_MARKER_TRANSPARENT', 'b'],
  customHourStyle: ['CONFIG_CUSTOM_HOUR_STYLE', 's'],
  customHourThickness: ['CONFIG_CUSTOM_HOUR_THICKNESS', 's'],
  customHourInnerThickness: ['CONFIG_CUSTOM_HOUR_INNER_THICKNESS', 's'],
  customHourInnerEcc: ['CONFIG_CUSTOM_HOUR_INNER_ECC', 's'],
  customHourOuterEcc: ['CONFIG_CUSTOM_HOUR_OUTER_ECC', 's'],
  customHourInnerBorder: ['CONFIG_CUSTOM_HOUR_INNER_BORDER', 's'],
  customHourOuterBorder: ['CONFIG_CUSTOM_HOUR_OUTER_BORDER', 's'],
  customHourTranslucent: ['CONFIG_CUSTOM_HOUR_TRANSLUCENT', 'b'],
  customHourColor: ['CONFIG_CUSTOM_HOUR_COLOR', 's'],
  customSecStyle: ['CONFIG_CUSTOM_SEC_STYLE', 's'],
  customSecThickness: ['CONFIG_CUSTOM_SEC_THICKNESS', 's'],
  customSecInnerThickness: ['CONFIG_CUSTOM_SEC_INNER_THICKNESS', 's'],
  customSecInnerEcc: ['CONFIG_CUSTOM_SEC_INNER_ECC', 's'],
  customSecOuterEcc: ['CONFIG_CUSTOM_SEC_OUTER_ECC', 's'],
  customSecInnerBorder: ['CONFIG_CUSTOM_SEC_INNER_BORDER', 's'],
  customSecOuterBorder: ['CONFIG_CUSTOM_SEC_OUTER_BORDER', 's'],
  customSecTranslucent: ['CONFIG_CUSTOM_SEC_TRANSLUCENT', 'b'],
  customSecColor: ['CONFIG_CUSTOM_SEC_COLOR', 's'],
  markerTextHourMask: ['CONFIG_MARKER_TEXT_HOUR_MASK', 's'],
  markerTextSecMask: ['CONFIG_MARKER_TEXT_SEC_MASK', 's'],
  skyMode: ['CONFIG_SKY_MODE', 's'],
  labelStyle: ['CONFIG_LABEL_STYLE', 's'],
  outlineStyle: ['CONFIG_OUTLINE_ENABLED', 's'],
  customBgValue: ['CONFIG_CUSTOM_BG', 's'],
  customTextValue: ['CONFIG_CUSTOM_TEXT', 's'],
  customAccentValue: ['CONFIG_CUSTOM_ACCENT', 's'],
  nightEnabled: ['CONFIG_NIGHT_ENABLED', 'b'],
  nightCustomBgValue: ['CONFIG_NIGHT_CUSTOM_BG', 's'],
  nightCustomTextValue: ['CONFIG_NIGHT_CUSTOM_TEXT', 's'],
  nightCustomAccentValue: ['CONFIG_NIGHT_CUSTOM_ACCENT', 's'],
  bitmapCornerOverride: ['CONFIG_BITMAP_CORNER_OVERRIDE', 'b'],
  digitalSides: ['CONFIG_DIGITAL_SIDES', 's'],
  digitalSidesPreferred: ['CONFIG_DIGITAL_SIDES_PREFERRED', 's'],
  cornerFont: ['CONFIG_CORNER_FONT', 's'],
  weatherIconStyle: ['CONFIG_WEATHER_ICON_STYLE', 's'],
  cornerTL: ['CONFIG_CORNER_TL', 's'],
  cornerTLColor: ['CONFIG_CORNER_TL_COLOR', 's'],
  cornerTR: ['CONFIG_CORNER_TR', 's'],
  cornerTRColor: ['CONFIG_CORNER_TR_COLOR', 's'],
  cornerBL: ['CONFIG_CORNER_BL', 's'],
  cornerBLColor: ['CONFIG_CORNER_BL_COLOR', 's'],
  cornerBR: ['CONFIG_CORNER_BR', 's'],
  cornerBRColor: ['CONFIG_CORNER_BR_COLOR', 's'],
  bottomMiddleLine2Content: ['CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT', 's'],
  bottomMiddleLine2Color: ['CONFIG_BOTTOM_MIDDLE_LINE2_COLOR', 's'],
  stepGoal: ['CONFIG_STEP_GOAL', 's'],
  drawFeaturesBeneathHands: ['CONFIG_DRAW_FEATURES_BENEATH_HANDS', 'b'],
  startupClockAnimMode: ['CONFIG_STARTUP_CLOCK_ANIM_MODE', 's'],
  bgAnimMode: ['CONFIG_BG_ANIM_MODE', 's'],
  shakeAnimMode: ['CONFIG_SHAKE_ANIM_MODE', 's'],
  shakeLabelSeconds: ['CONFIG_SHAKE_LABEL_SECONDS', 's']
};

var DUAL_MAP = {
  upperMiddleLine1Content: ['CONFIG_UPPER_MIDDLE_LINE1_CONTENT_ANALOG', 'CONFIG_UPPER_MIDDLE_LINE1_CONTENT_DIGITAL'],
  upperMiddleLine1Color: ['CONFIG_UPPER_MIDDLE_LINE1_COLOR_ANALOG', 'CONFIG_UPPER_MIDDLE_LINE1_COLOR_DIGITAL'],
  upperMiddleLine2Content: ['CONFIG_UPPER_MIDDLE_LINE2_CONTENT_ANALOG', 'CONFIG_UPPER_MIDDLE_LINE2_CONTENT_DIGITAL'],
  upperMiddleLine2Color: ['CONFIG_UPPER_MIDDLE_LINE2_COLOR_ANALOG', 'CONFIG_UPPER_MIDDLE_LINE2_COLOR_DIGITAL'],
  bottomMiddleLine1Content: ['CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_ANALOG', 'CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_DIGITAL'],
  bottomMiddleLine1Color: ['CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_ANALOG', 'CONFIG_BOTTOM_MIDDLE_LINE1_COLOR_DIGITAL'],
  middleLeftLine1Content: ['CONFIG_MIDDLE_LEFT_LINE1_CONTENT_ANALOG', 'CONFIG_MIDDLE_LEFT_LINE1_CONTENT_DIGITAL'],
  middleLeftLine1Color: ['CONFIG_MIDDLE_LEFT_LINE1_COLOR_ANALOG', 'CONFIG_MIDDLE_LEFT_LINE1_COLOR_DIGITAL'],
  middleLeftLine2Content: ['CONFIG_MIDDLE_LEFT_LINE2_CONTENT_ANALOG', 'CONFIG_MIDDLE_LEFT_LINE2_CONTENT_DIGITAL'],
  middleLeftLine2Color: ['CONFIG_MIDDLE_LEFT_LINE2_COLOR_ANALOG', 'CONFIG_MIDDLE_LEFT_LINE2_COLOR_DIGITAL'],
  middleRightLine1Content: ['CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_ANALOG', 'CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_DIGITAL'],
  middleRightLine1Color: ['CONFIG_MIDDLE_RIGHT_LINE1_COLOR_ANALOG', 'CONFIG_MIDDLE_RIGHT_LINE1_COLOR_DIGITAL'],
  middleRightLine2Content: ['CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_ANALOG', 'CONFIG_MIDDLE_RIGHT_LINE2_CONTENT_DIGITAL'],
  middleRightLine2Color: ['CONFIG_MIDDLE_RIGHT_LINE2_COLOR_ANALOG', 'CONFIG_MIDDLE_RIGHT_LINE2_COLOR_DIGITAL']
};

// Elements in the preset scope that are UI state only, or that are handled
// explicitly in applyPreset() below instead of through the tables.
var UI_ONLY_IDS = ['showAllColorPresets', 'bigDigitalTransparent'];
var SPECIAL_IDS = ['bottomStyleValue'];

function knownIds() {
  var ids = [];
  var k;
  for (k in FIELD_MAP) if (FIELD_MAP.hasOwnProperty(k)) ids.push(k);
  for (k in DUAL_MAP) if (DUAL_MAP.hasOwnProperty(k)) ids.push(k);
  return ids.concat(UI_ONLY_IDS, SPECIAL_IDS);
}

function normalizeBottomStyle(v) {
  if (v === 'analog' || v === 'biganalog') return 'analog';
  if (v === 'digitalTop') return 'digitalTop';
  if (v === 'bigDigital') return 'bigDigital';
  if (v === 'grid') return 'grid';
  return 'digital';
}

function toStored(kind, v) {
  if (kind === 'b') return (v === true || v === 'true') ? 'true' : 'false';
  return String(v);
}

// Writes a preset's values into the stored settings. Only keys the preset
// actually contains are touched. Returns the number of keys written.
function applyPreset(preset) {
  var written = 0;
  var id;
  var bottom = normalizeBottomStyle(
    preset.bottomStyleValue !== undefined ? preset.bottomStyleValue : getSetting('CONFIG_BOTTOM_STYLE', 'digital'));

  if (preset.bottomStyleValue !== undefined) {
    setSetting('CONFIG_BOTTOM_STYLE', bottom);
    written++;
  }
  // The page keeps a mirror checkbox for the big digital layout; the real
  // field is bitmapMarkerTransparent. Only fall back to the mirror if the
  // real one is missing from an older / hand-edited style.
  if (preset.bitmapMarkerTransparent === undefined && preset.bigDigitalTransparent !== undefined) {
    setSetting('CONFIG_BITMAP_MARKER_TRANSPARENT', toStored('b', preset.bigDigitalTransparent));
    written++;
  }

  for (id in preset) {
    if (!preset.hasOwnProperty(id)) continue;
    var v = preset[id];
    if (v === null || v === undefined || v === '') continue;
    var f = FIELD_MAP[id];
    if (f) {
      setSetting(f[0], toStored(f[1], v));
      written++;
      continue;
    }
    var d = DUAL_MAP[id];
    if (d) {
      // The page keeps one live element per dual-context field, holding the
      // half that belongs to the current layout; the other half stays as it
      // was. Same here: write only the half for the style's own layout.
      setSetting(bottom === 'analog' ? d[0] : d[1], String(v));
      written++;
    }
  }
  return written;
}

// ---- schedule model -----------------------------------------------------------

function parseSchedule(raw) {
  if (!raw) return null;
  var o;
  try { o = JSON.parse(raw); } catch (e) { return null; }
  if (!o || o.enabled !== true) return null;
  var days = parseInt(o.days, 10);
  if (isNaN(days)) days = 127;
  days = days & 127;
  if (days === 0) return null;
  var m = /^(\d{1,2}):(\d{2})$/.exec(String(o.time || ''));
  if (!m) return null;
  var hh = parseInt(m[1], 10), mm = parseInt(m[2], 10);
  if (hh > 23 || mm > 59) return null;
  return { days: days, hour: hh, minute: mm };
}

function readPreset(n) {
  var raw = getSetting('CONFIG_PRESET_' + n + '_JSON', '');
  if (!raw) return null;
  try {
    var p = JSON.parse(raw);
    return (p && typeof p === 'object') ? p : null;
  } catch (e) { return null; }
}

// A slot only schedules if it is enabled AND still holds a saved style.
function activeSchedule(n) {
  var s = parseSchedule(getSetting('CONFIG_PRESET_' + n + '_SCHEDULE', ''));
  if (!s) return null;
  return readPreset(n) ? s : null;
}

function numSetting(key) {
  var v = parseInt(getSetting(key, '0'), 10);
  return isNaN(v) ? 0 : v;
}

// Earliest occurrence strictly after afterMs (phone-local time, so DST and
// travel are handled by the Date constructor).
function occurrenceAfter(sched, afterMs) {
  var start = new Date(afterMs);
  for (var i = 0; i <= 8; i++) {
    var c = new Date(start.getFullYear(), start.getMonth(), start.getDate() + i, sched.hour, sched.minute, 0, 0);
    var t = c.getTime();
    if (t > afterMs && (sched.days & (1 << c.getDay()))) return t;
  }
  return null;
}

// Latest occurrence in (afterMs, limitMs], or 0 if there is none.
function latestOccurrenceUpTo(sched, afterMs, limitMs) {
  var t = Math.max(afterMs, limitMs - 8 * DAY_MS); // any 8-day window holds one
  var last = 0;
  for (var i = 0; i < 16; i++) {
    var o = occurrenceAfter(sched, t);
    if (o === null || o > limitMs) break;
    last = o;
    t = o;
  }
  return last;
}

// Occurrences before the schedule was saved, or at/before the last one served,
// do not count. A schedule seen for the first time (e.g. local storage was
// cleared) is armed now rather than fired retroactively.
function baseMs(n, nowMs) {
  var armed = numSetting('SCHEDULE_ARMED_AT_' + n);
  if (!armed) {
    armed = nowMs;
    setSetting('SCHEDULE_ARMED_AT_' + n, String(armed));
  }
  return Math.max(armed, numSetting('SCHEDULE_LAST_APPLIED_' + n));
}

// Epoch seconds for the watch's NEXT_STYLE_CHECK: the earliest unserved
// occurrence across all slots. May be in the past (something is pending); the
// watch then asks right away. 0 when no slot has an active schedule.
function nextCheckSeconds(nowMs) {
  var best = null;
  for (var n = 1; n <= SLOT_COUNT; n++) {
    var s = activeSchedule(n);
    if (!s) continue;
    var o = occurrenceAfter(s, baseMs(n, nowMs));
    if (o !== null && (best === null || o < best)) best = o;
  }
  return best === null ? 0 : Math.floor(best / 1000);
}

// Serves whatever is due: applies the most recent due style and marks every
// due occurrence as served. Returns {applied: bool, slot: n}. The caller is
// responsible for pushing the new settings to the watch.
function applyDue(nowMs) {
  var limit = nowMs + DUE_TOLERANCE_MS;
  var due = [];
  for (var n = 1; n <= SLOT_COUNT; n++) {
    var s = activeSchedule(n);
    if (!s) continue;
    var o = latestOccurrenceUpTo(s, baseMs(n, nowMs), limit);
    if (o) due.push({ slot: n, at: o });
  }
  if (!due.length) return { applied: false, slot: 0 };

  var winner = due[0];
  for (var i = 1; i < due.length; i++) {
    // Most recent wins; two styles at the exact same minute: higher slot.
    if (due[i].at >= winner.at) winner = due[i];
  }
  for (var j = 0; j < due.length; j++) {
    setSetting('SCHEDULE_LAST_APPLIED_' + due[j].slot, String(due[j].at));
  }
  applyPreset(readPreset(winner.slot));
  return { applied: true, slot: winner.slot };
}

// Called with the raw schedule string the settings page just saved. A changed
// schedule is re-armed from now: nothing earlier fires, and the served marker
// is reset. An unchanged one is left alone so saving other settings does not
// disturb a schedule that is already in progress.
function persistSlotSchedule(n, raw, nowMs) {
  raw = raw || '';
  var key = 'CONFIG_PRESET_' + n + '_SCHEDULE';
  var prev = getSetting(key, '');
  setSetting(key, raw);
  if (raw !== prev) {
    setSetting('SCHEDULE_ARMED_AT_' + n, String(nowMs));
    setSetting('SCHEDULE_LAST_APPLIED_' + n, '0');
  }
}

module.exports = {
  SLOT_COUNT: SLOT_COUNT,
  FIELD_MAP: FIELD_MAP,
  DUAL_MAP: DUAL_MAP,
  UI_ONLY_IDS: UI_ONLY_IDS,
  knownIds: knownIds,
  applyPreset: applyPreset,
  parseSchedule: parseSchedule,
  occurrenceAfter: occurrenceAfter,
  nextCheckSeconds: nextCheckSeconds,
  applyDue: applyDue,
  persistSlotSchedule: persistSlotSchedule
};
