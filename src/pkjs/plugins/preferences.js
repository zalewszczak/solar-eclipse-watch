// ---- Pebble app user preferences (preferences/units, preferences/complications) ----
//
// The built-in Preferences plugin tells apps how the user wants things shown:
//   preferences/units          one instance, every property a shortText
//                              (temperature, distance, wind_speed, pressure, precipitation, clock, locale)
//   preferences/complications  the user's favourite readings (Settings -> Plugins -> Complications),
//                              one instance per filled slot, in order: category / item / property
//                              (shortText) and index (numericValue = which instance of that source)
//
// Both are read once per capture (subscribe, take the first update, unsubscribe) so
// nothing stays subscribed. The latest result is cached in memory and in localStorage
// (PREFERENCES_CACHE -- deliberately not a CONFIG_ key) so the settings page can offer it
// even when the plugin API is slow or unavailable.
//
// Used by settings/first-run.js (first start) and the settings page's
// "Load user preferences" / "Load units" buttons.

var CACHE_KEY = 'PREFERENCES_CACHE';
var UNIT_PROPS = ['temperature', 'distance', 'wind_speed', 'pressure', 'precipitation', 'clock', 'locale'];
var MAX_COMPLICATIONS = 5;

var cache = loadCache();

function hasApi() {
  try { return typeof Pebble !== 'undefined' && typeof Pebble.subscribeToSource === 'function'; } catch (e) { return false; }
}

function loadCache() {
  try {
    var raw = localStorage.getItem(CACHE_KEY);
    var p = raw ? JSON.parse(raw) : null;
    if (p && typeof p === 'object') {
      return { units: (p.units && typeof p.units === 'object') ? p.units : null, complications: Array.isArray(p.complications) ? p.complications : null };
    }
  } catch (e) { /* unreadable cache: start empty */ }
  return { units: null, complications: null };
}

function saveCache() {
  try { localStorage.setItem(CACHE_KEY, JSON.stringify(cache)); } catch (e) { /* storage unavailable */ }
}

function shortTextOf(props, name) {
  var p = props && props[name];
  return (p && p.shortText && typeof p.shortText.text === 'string') ? p.shortText.text : '';
}

function parseUnits(envelope) {
  var inst = envelope && envelope.instances && envelope.instances[0];
  if (!inst || !inst.properties) return null;
  var out = {}, any = false;
  UNIT_PROPS.forEach(function (name) {
    var t = shortTextOf(inst.properties, name);
    if (t) { out[name] = t; any = true; }
  });
  return any ? out : null;
}

function parseComplications(envelope) {
  var list = (envelope && envelope.instances) || [];
  var entries = [];
  list.forEach(function (inst, pos) {
    var props = inst && inst.properties;
    if (!props) return;
    var category = shortTextOf(props, 'category'), item = shortTextOf(props, 'item'), property = shortTextOf(props, 'property');
    if (!category || !item || !property) return;
    var idx = props.index && props.index.numericValue && typeof props.index.numericValue.value === 'number' ? props.index.numericValue.value : 0;
    var order = parseInt(inst.instanceId, 10); // the slot number; empty slots are left out
    entries.push({ category: category, item: item, property: property, index: Math.max(0, Math.floor(idx)), order: isNaN(order) ? pos : order });
  });
  entries.sort(function (a, b) { return a.order - b.order; });
  return entries.slice(0, MAX_COMPLICATIONS).map(function (e) {
    return { category: e.category, item: e.item, property: e.property, index: e.index };
  });
}

// Reads one source once: done(value | null).
function readOnce(request, parse, done) {
  var handle = null, finished = false;
  function finish(value) {
    if (finished) return;
    finished = true;
    if (handle && typeof handle.unsubscribe === 'function') { try { handle.unsubscribe(); } catch (e) { /* already gone */ } }
    done(value);
  }
  try {
    handle = Pebble.subscribeToSource({
      category: request.category,
      item: request.item,
      onData: function (envelope) { var v = null; try { v = parse(envelope); } catch (e) { /* malformed update */ } finish(v); },
      onError: function (err) { console.log('eclipse-watch: preferences ' + request.item + ' error ' + (err && err.code)); finish(null); }
    });
  } catch (e) {
    console.log('eclipse-watch: preferences ' + request.item + ' subscribe failed - ' + ((e && e.message) || e));
    finish(null);
    return;
  }
  if (finished && handle && typeof handle.unsubscribe === 'function') { try { handle.unsubscribe(); } catch (e2) { /* already gone */ } }
}

function snapshot() {
  return { available: hasApi(), units: cache.units, complications: cache.complications };
}

// Refreshes the cache from the Pebble app and calls cb(snapshot) once (also when plugins
// are unavailable or nothing arrives within timeoutMs, with whatever the cache held).
function capture(timeoutMs, cb) {
  if (!hasApi()) { cb(snapshot()); return; }
  var pending = 2, called = false, timer = null;
  function complete() {
    if (called) return;
    called = true;
    if (timer) clearTimeout(timer);
    saveCache();
    cb(snapshot());
  }
  function oneDone() { if (--pending <= 0) complete(); }
  timer = setTimeout(complete, timeoutMs || 4000);
  readOnce({ category: 'preferences', item: 'units' }, parseUnits, function (u) { if (u) cache.units = u; oneDone(); });
  readOnce({ category: 'preferences', item: 'complications' }, parseComplications, function (c) { if (c) cache.complications = c; oneDone(); });
}

// preferences/units -> the watchface's own unit settings (CONFIG_ keys, stored values).
// Only what the watchface has a setting for: temperature, wind speed and altitude
// (distance km -> metres, mi -> feet). Pressure, precipitation and clock have no setting here.
function unitSettings(units) {
  var out = {};
  if (!units) return out;
  if (units.temperature === '°C') out.CONFIG_TEMP_UNIT = 'C';
  else if (units.temperature === '°F') out.CONFIG_TEMP_UNIT = 'F';
  var wind = { 'km/h': 'kmh', 'mph': 'mph', 'm/s': 'ms', 'knots': 'kn' }[units.wind_speed];
  if (wind) out.CONFIG_WIND_SPEED_UNIT = wind;
  if (units.distance === 'km') out.CONFIG_ALTITUDE_UNIT = '0';
  else if (units.distance === 'mi') out.CONFIG_ALTITUDE_UNIT = '1';
  return out;
}

// A complication as a plugin-slot source key: "|category|item|property#instanceIndex".
// The empty plugin part means "whichever plugin the user prefers for it" (see
// plugin-manager.js, which passes no plugin id when subscribing).
function complicationKey(c) {
  return '|' + c.category + '|' + c.item + '|' + c.property + '#' + (c.index || 0);
}

module.exports = {
  MAX_COMPLICATIONS: MAX_COMPLICATIONS,
  hasApi: hasApi,
  capture: capture,
  snapshot: snapshot,
  unitSettings: unitSettings,
  complicationKey: complicationKey,
  parseUnits: parseUnits,
  parseComplications: parseComplications
};
