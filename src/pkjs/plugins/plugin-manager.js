// ---- Pebble plugins (new in recent Pebble apps) --------------------------------
//
// Plugins are data sources provided by the phone app (weather, battery,
// calendar...). PKJS reads them through Pebble.enumeratePlugins() /
// Pebble.subscribeToSource(). Both are missing on older apps (and the app
// keeps them switched off until "Use experimental plugins" is enabled), so
// every call here is feature-detected and wrapped: status() says why plugins
// are unavailable, and nothing else in the project depends on them.
//
// Data sources are offered to the user per PROPERTY ("Weather / location /
// temperature"), as a single text primitive each. The watch has
// PLUGIN_SLOT_COUNT text slots (EclipseData.plugin_text); a source the user picks
// is assigned a slot (the settings page does the assigning and saves the
// list as CONFIG_PLUGIN_SLOTS, a JSON array of source keys), the matching
// feature compiles to PRIM_PLUGIN_TEXT with aux = slot, and this module keeps
// one subscription per assigned slot and pushes the latest text.
//
// Source key: "<plugin uuid>|<category>|<item>|<property>[#<instance>]" (see parseKey()).

var settings = require('../settings/settings');
var getSetting = settings.getSetting;
var setSetting = settings.setSetting;

var SLOT_COUNT = 12;
var TEXT_LEN = 16; // bytes per slot on the watch, including the terminating NUL
var SLOTS_KEY = 'CONFIG_PLUGIN_SLOTS';
var TEXT_SHAPES = ['shortText', 'numericValue', 'timestamp', 'boolean', 'longText'];

var slotKeys = [];     // slot index -> source key ('' = unused)
var slotTexts = [];    // slot index -> latest text
var subscriptions = []; // slot index -> { key, handle }
var pushTimer = null;
var pushFn = null;     // set by init(): function (bytes) { send to watch }

for (var i = 0; i < SLOT_COUNT; i++) { slotKeys.push(''); slotTexts.push(''); subscriptions.push(null); }

function hasApi() {
  try {
    return typeof Pebble !== 'undefined' && typeof Pebble.enumeratePlugins === 'function' &&
      typeof Pebble.subscribeToSource === 'function';
  } catch (e) { return false; }
}

function humanize(s) {
  return String(s || '').replace(/[_-]+/g, ' ').replace(/^\s*[a-z]/, function (c) { return c.toUpperCase(); });
}

// Returns { status: 'ok' | 'unsupported' | 'disabled', message, sources: [...] }.
//   unsupported: this Pebble app has no plugin API at all (old app)
//   disabled:    the API exists but enumerating failed (plugins switched off / not ready)
// Each source: { key, plugin, pluginName, category, item, property, shapes, label, group }.
function readCatalogue() {
  if (!hasApi()) {
    return { status: 'unsupported', sources: [],
      message: 'Plugins are not enabled: this version of the Pebble app does not support plugins. Update the app to use them.' };
  }
  var plugins;
  try {
    plugins = Pebble.enumeratePlugins();
  } catch (e) {
    return { status: 'disabled', sources: [],
      message: 'Plugins are not enabled in the Pebble app (' + ((e && e.message) || 'unavailable') + '). Turn on plugins in the app\'s settings.' };
  }
  var sources = [], weatherProviders = [];
  (plugins || []).forEach(function (plugin) {
    (plugin.sources || []).forEach(function (source) {
      var props = source.properties || {};
      // A weather provider serves weather/location with at least a temperature.
      if (source.category === 'weather' && (source.items || []).indexOf('location') !== -1 && props.temperature &&
          !weatherProviders.some(function (w) { return w.uuid === plugin.uuid; })) {
        weatherProviders.push({ uuid: plugin.uuid, name: plugin.name || 'Weather plugin' });
      }
      (source.items || []).forEach(function (item) {
        Object.keys(props).forEach(function (prop) {
          var shapes = props[prop] || [];
          var usable = shapes.filter(function (s) { return TEXT_SHAPES.indexOf(s) !== -1; });
          if (!usable.length) return; // series, images and icons cannot be shown as text
          sources.push({
            key: [plugin.uuid, source.category, item, prop].join('|'),
            plugin: plugin.uuid, pluginName: plugin.name || 'Plugin',
            category: source.category, item: item, property: prop, shapes: usable,
            group: (plugin.name || 'Plugin') + ' - ' + humanize(source.category) + ' / ' + humanize(item),
            label: humanize(prop)
          });
        });
      });
    });
  });
  return { status: 'ok', sources: sources, weatherProviders: weatherProviders, message: sources.length ? '' : 'No plugin provides text data right now.' };
}

function status() { return readCatalogue().status; }

// ---- value -> short text --------------------------------------------------------

function pad2(n) { return (n < 10 ? '0' : '') + n; }

function textFromPayloads(payloads) {
  if (!payloads) return '';
  if (payloads.shortText && typeof payloads.shortText.text === 'string') return payloads.shortText.text;
  var nv = payloads.numericValue;
  if (nv && typeof nv.value === 'number') {
    var v = Math.round(nv.value * 10) / 10;
    return String(v) + (nv.unit || '');
  }
  var ts = payloads.timestamp;
  if (ts && typeof ts.value === 'number') {
    var d = new Date(ts.value);
    return pad2(d.getHours()) + ':' + pad2(d.getMinutes());
  }
  var b = payloads.boolean;
  if (b && typeof b.value === 'boolean') return b.value ? 'On' : 'Off';
  if (payloads.longText && typeof payloads.longText.text === 'string') return payloads.longText.text;
  return '';
}

// UTF-8 encode, cut at a character boundary so it fits maxBytes.
function utf8Fit(str, maxBytes) {
  var out = [];
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i);
    if (c >= 0xD800 && c <= 0xDBFF) { i++; c = 63; } // surrogate pair: not drawable, use '?'
    var enc;
    if (c < 0x80) enc = [c];
    else if (c < 0x800) enc = [0xC0 | (c >> 6), 0x80 | (c & 63)];
    else enc = [0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63)];
    if (out.length + enc.length > maxBytes) break;
    out = out.concat(enc);
  }
  return out;
}

function textsBytes() {
  var bytes = [];
  for (var s = 0; s < SLOT_COUNT; s++) {
    var enc = utf8Fit(String(slotTexts[s] || '').replace(/\s+/g, ' ').trim(), TEXT_LEN - 1);
    while (enc.length < TEXT_LEN) enc.push(0);
    bytes = bytes.concat(enc);
  }
  return bytes;
}

function schedulePush() {
  if (!pushFn) return;
  if (pushTimer) clearTimeout(pushTimer);
  pushTimer = setTimeout(function () {
    pushTimer = null;
    try { pushFn(textsBytes()); } catch (e) { console.log('eclipse-watch: plugin push failed - ' + e.message); }
  }, 600);
}

function setText(slot, text) {
  if (slotTexts[slot] === text) return;
  slotTexts[slot] = text;
  schedulePush();
}

// ---- subscriptions ----------------------------------------------------------------

// "<plugin uuid>|<category>|<item>|<property>", optionally "...<property>#<n>" to read
// instance n of the source instead of the first (the user's preferred complications), and
// an empty uuid to let the Pebble app pick the plugin (the user's preferred sources).
function parseKey(key) {
  var p = String(key).split('|');
  if (p.length < 4) return null;
  var property = p.slice(3).join('|');
  var index = 0;
  var m = /^(.*)#(\d+)$/.exec(property);
  if (m) { property = m[1]; index = parseInt(m[2], 10); }
  return { plugin: p[0], category: p[1], item: p[2], property: property, index: index };
}

function unsubscribe(slot) {
  var sub = subscriptions[slot];
  subscriptions[slot] = null;
  if (sub && sub.handle && typeof sub.handle.unsubscribe === 'function') {
    try { sub.handle.unsubscribe(); } catch (e) { /* already gone */ }
  }
}

function subscribe(slot) {
  var parsed = parseKey(slotKeys[slot]);
  if (!parsed || !hasApi()) return;
  var handle = null;
  try {
    handle = Pebble.subscribeToSource({
      category: parsed.category,
      item: parsed.item,
      plugin: parsed.plugin || undefined,
      properties: [parsed.property],
      onData: function (envelope) {
        var inst = envelope && envelope.instances && envelope.instances[parsed.index];
        var text = inst && inst.properties ? textFromPayloads(inst.properties[parsed.property]) : '';
        setText(slot, text || '--');
      },
      onError: function (err) {
        console.log('eclipse-watch: plugin ' + slotKeys[slot] + ' error ' + (err && err.code));
        setText(slot, (err && err.code === 'PERMISSION_DENIED') ? 'No access' : 'N/A');
      }
    });
  } catch (e) {
    console.log('eclipse-watch: plugin subscribe failed - ' + e.message);
    setText(slot, 'N/A');
    return;
  }
  subscriptions[slot] = { key: slotKeys[slot], handle: handle };
}

function syncSubscriptions() {
  for (var s = 0; s < SLOT_COUNT; s++) {
    // "text|<string>" = a fixed custom-text element: no plugin involved.
    if (slotKeys[s].indexOf('text|') === 0) {
      unsubscribe(s);
      var fixed = slotKeys[s].slice(5);
      if (slotTexts[s] !== fixed) { slotTexts[s] = fixed; schedulePush(); }
      continue;
    }
    var sub = subscriptions[s];
    if (sub && sub.key === slotKeys[s]) continue;
    unsubscribe(s);
    if (slotKeys[s]) { slotTexts[s] = ''; subscribe(s); }
    else if (slotTexts[s]) { slotTexts[s] = ''; schedulePush(); }
  }
}

// ---- slot assignment (from the settings page) ---------------------------------------

function normalizeSlots(raw) {
  var arr = raw;
  if (typeof raw === 'string') { try { arr = JSON.parse(raw); } catch (e) { arr = []; } }
  if (!Array.isArray(arr)) arr = [];
  var out = [];
  for (var i = 0; i < SLOT_COUNT; i++) out.push(typeof arr[i] === 'string' ? arr[i] : '');
  return out;
}

function getSlots() { return normalizeSlots(getSetting(SLOTS_KEY, '[]')); }

// Stores the page's slot list and (re)subscribes. Safe to call without plugins.
function setSlots(raw) {
  slotKeys = normalizeSlots(raw);
  setSetting(SLOTS_KEY, JSON.stringify(slotKeys));
  syncSubscriptions();
}

// pushFnArg(bytes) sends the plugin text blob to the watch.
function init(pushFnArg) {
  pushFn = pushFnArg;
  slotKeys = getSlots();
  syncSubscriptions();
}

module.exports = {
  SLOT_COUNT: SLOT_COUNT,
  TEXT_LEN: TEXT_LEN,
  hasApi: hasApi,
  status: status,
  readCatalogue: readCatalogue,
  getSlots: getSlots,
  setSlots: setSlots,
  textsBytes: textsBytes,
  init: init,
  textFromPayloads: textFromPayloads
};
