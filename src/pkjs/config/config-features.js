// ---- corner/edge content picker options ---------------------------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 fifth slice).

var presetsLookups = require('../presets-lookups');
var CORNER_CATEGORIES = presetsLookups.CORNER_CATEGORIES;
var esc = require('./config-template').esc;

// Flattened, id-ascending view of CORNER_CATEGORIES (see
// presets-lookups.js's own header comment for why that's the single
// source of truth now) -- used to build the flat <option> lists the
// hidden per-slot <select>s need. Category grouping/curated item order
// only matters to the categorized picker itself (see
// categoryItemOptionsHtml() in config-page.js), not to these hidden
// stores.

var CORNER_CONTENT_OPTIONS = [];
CORNER_CATEGORIES.forEach(function (cat) {
  cat.items.forEach(function (item) {
    CORNER_CONTENT_OPTIONS.push({ id: item.id, label: item.label });
  });
});
CORNER_CONTENT_OPTIONS.sort(function (a, b) { return a.id - b.id; });
// auroraEnabled omits id 84 entirely (not just hides it) when auroras
// are turned off in the Astronomy section -- see onAuroraEnabledChange()
// for the live version of this same filtering, run client-side when
// the checkbox itself is toggled without a page reload.
function cornerContentOptionsHtml(selected, auroraEnabled) {
  return CORNER_CONTENT_OPTIONS.filter(function (o) {
    return o.id !== 84 || auroraEnabled;
  }).map(function (o) {
    return '<option value="' + o.id + '"' + (String(selected) === String(o.id) ? ' selected' : '') + '>' + esc(o.label) + '</option>';
  }).join('');
}

module.exports = {
  CORNER_CONTENT_OPTIONS: CORNER_CONTENT_OPTIONS,
  cornerContentOptionsHtml: cornerContentOptionsHtml
};
