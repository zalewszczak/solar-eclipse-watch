// ---- first start of the watchface ---------------------------------------------------
//
// On the very first start (no settings were ever saved on this phone) the Pebble app's user
// preferences are laid over Eclipz's own defaults once they have been read: preferred units
// (preferences/units) and the preferred complications (preferences/complications) in up to
// four feature slots. Nothing else is seeded -- Eclipz keeps its own look and defaults.
//
// State (FIRST_RUN_STATE): absent = not decided yet, 'seeded' = preferences still to be
// applied, 'done'. Anyone who already has saved settings, or saves any before the
// preferences arrive, is marked 'done' and never touched.

var settings = require('./settings');
var setSetting = settings.setSetting;
var preferences = require('../plugins/preferences');

var STATE_KEY = 'FIRST_RUN_STATE';

// Keys every settings save writes: any of them present means settings exist already.
var SAVED_PROBE_KEYS = ['CONFIG_CUSTOM_BG', 'CONFIG_BIG_ANALOG_MARKER_STYLE', 'CONFIG_UPDATE_MINS', 'CONFIG_TEMP_UNIT'];

// The four slots the preferred complications go into, in order: upper-middle line 2,
// bottom-middle line 1, then the first line on the left and on the right.
var COMPLICATION_SLOTS = [
  { slot: 'UPPER_MIDDLE_LINE2', contents: ['CONFIG_UPPER_MIDDLE_LINE2_CONTENT_ANALOG', 'CONFIG_UPPER_MIDDLE_LINE2_CONTENT_DIGITAL'],
    prims: ['CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_ANALOG', 'CONFIG_UPPER_MIDDLE_LINE2_CUSTOM_PRIMS_DIGITAL'] },
  { slot: 'BOTTOM_MIDDLE_LINE1', contents: ['CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_ANALOG', 'CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT_DIGITAL'],
    prims: ['CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_ANALOG', 'CONFIG_BOTTOM_MIDDLE_LINE1_CUSTOM_PRIMS_DIGITAL'] },
  { slot: 'MIDDLE_LEFT_LINE1', contents: ['CONFIG_MIDDLE_LEFT_LINE1_CONTENT_ANALOG', 'CONFIG_MIDDLE_LEFT_LINE1_CONTENT_DIGITAL'],
    prims: ['CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_ANALOG', 'CONFIG_MIDDLE_LEFT_LINE1_CUSTOM_PRIMS_DIGITAL'] },
  { slot: 'MIDDLE_RIGHT_LINE1', contents: ['CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_ANALOG', 'CONFIG_MIDDLE_RIGHT_LINE1_CONTENT_DIGITAL'],
    prims: ['CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_ANALOG', 'CONFIG_MIDDLE_RIGHT_LINE1_CUSTOM_PRIMS_DIGITAL'] }
];
var PLUGIN_CONTENT_FIRST = 200; // content ids 200-211 = plugin text slots 0-11

function getState() {
  try { return localStorage.getItem(STATE_KEY); } catch (e) { return 'done'; }
}
function setState(v) {
  try { localStorage.setItem(STATE_KEY, v); } catch (e) { /* storage unavailable */ }
}

function settingsAlreadySaved() {
  try {
    return SAVED_PROBE_KEYS.some(function (k) { var v = localStorage.getItem(k); return v !== null && v !== undefined; });
  } catch (e) { return true; } // cannot tell: leave everything alone
}

// Call once at startup, before anything reads settings. Returns true when this is a
// fresh install whose preferences are still to be applied.
function seedIfFresh() {
  var state = getState();
  if (state === 'done') return false;
  if (state === 'seeded') return true;
  if (settingsAlreadySaved()) { setState('done'); return false; }
  setState('seeded');
  console.log('eclipse-watch: first start - user preferences will be applied');
  return true;
}

// A settings save counts as "the person has taken over": never apply preferences after it.
function markDone() {
  if (getState() !== 'done') setState('done');
}

function pending() { return getState() === 'seeded'; }

// Applies a preferences snapshot (see preferences.capture) once. pluginManager stores the
// plugin slot list. Returns true when settings changed and should be sent to the watch.
function applyPreferences(prefs, pluginManager) {
  if (!pending()) return false;
  setState('done');
  var changed = false;

  var units = preferences.unitSettings(prefs && prefs.units);
  Object.keys(units).forEach(function (k) { setSetting(k, units[k]); changed = true; });

  var comps = (prefs && prefs.complications) || [];
  if (comps.length) {
    var slots = pluginManager.getSlots();
    var used = 0;
    comps.slice(0, COMPLICATION_SLOTS.length).forEach(function (c, i) {
      var key = preferences.complicationKey(c);
      var s = slots.indexOf(key);
      if (s < 0) { s = slots.indexOf(''); if (s < 0) return; slots[s] = key; }
      var target = COMPLICATION_SLOTS[i];
      target.contents.forEach(function (k) { setSetting(k, String(PLUGIN_CONTENT_FIRST + s)); });
      target.prims.forEach(function (k) { setSetting(k, ''); });
      used++;
    });
    if (used) { pluginManager.setSlots(slots); changed = true; }
  }
  console.log('eclipse-watch: first start - user preferences applied (' + Object.keys(units).length + ' units, ' + comps.length + ' complications)');
  return changed;
}

module.exports = {
  COMPLICATION_SLOTS: COMPLICATION_SLOTS,
  seedIfFresh: seedIfFresh,
  markDone: markDone,
  pending: pending,
  applyPreferences: applyPreferences
};
