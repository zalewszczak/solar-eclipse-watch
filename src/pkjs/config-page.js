/**
 * config-page.js -- builds the settings webview HTML for the classic
 * Pebble "configurable" capability (no Clay, no build-time
 * dependency). PKJS opens this via a data: URI in index.js.
 *
 * The page hands settings back by navigating to a "return URL" --
 * but that URL is *not* a fixed constant. The runtime that opened the
 * page (the real phone app, `pebble emu-app-config`, CloudPebble's
 * emulator, etc.) appends its own `return_to` query parameter to the
 * page URL, and the page is expected to use that value if present,
 * falling back to the legacy `pebblejs://close#` prefix only when
 * it's absent. This is the standard pattern from Pebble's own "App
 * Configuration (manual setup)" guide -- skipping it is why Save can
 * silently do nothing under some runtimes (there's no handler
 * registered for a URL scheme we made up ourselves).
 *
 * Font/colour "previews" here are necessarily approximations -- the
 * watch's actual system fonts (Leco, Roboto subset, Bitham) and
 * custom resource fonts aren't available as web fonts in a phone
 * browser, so each option gets a CSS style chosen to be visually
 * evocative of the real thing rather than pixel-identical to it.
 * Good enough to tell them apart before committing to one. The color
 * scheme preview, however, is exact -- both this page and the watch
 * derive colors the same way (2-bit-per-channel packed bytes), so
 * what you see here is exactly what you'll get.
 */

function esc(str) {
  return String(str == null ? '' : str)
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

// Base64 data: URIs for each bitmap marker style's preview image,
// generated at build time from the same resource PNGs used on the
// watch itself (resources/images/<name>_background.png -- see
// scripts/generate-marker-previews.js -- this file itself has no way
// to load an external image at runtime, since the whole settings
// page ends up as one self-contained data: URI with no server behind
// it). Missing entries (a style with no PNG provided yet) are handled
// gracefully wherever this is used below, not treated as an error.
var MARKER_PREVIEW_IMAGES = require('./marker-preview-images');

// "Example styles" grid (first section on the settings page) -- each
// numbered slot pairs a screenshot (resources/example-styles/<n>.png,
// embedded at build time -- see generate-example-style-previews.js)
// with a hand-authored preset (src/pkjs/example-style-presets.js) in
// exactly the same shape the "Style Presets" section's own export/
// import uses. This single constant controls how many slots exist;
// add a 10th by bumping it, adding resources/example-styles/10.png,
// re-running the generator script, and adding a "10" entry to
// example-style-presets.js -- nothing else here needs to change.
var EXAMPLE_STYLE_COUNT = 9;
var EXAMPLE_STYLE_IMAGES = require('./example-style-images');
var EXAMPLE_STYLE_PRESETS = require('./example-style-presets');

// Must match CLOCK_STYLE_IDS in index.js / apply_clock_font()'s code
// numbers in pebble-eclipse-watch.c. `preview` is inline CSS applied
// to the "88:88" sample so each option looks distinct even without
// the real (custom, resource-loaded) font available in this webview.
var CLOCK_FONTS = [
  { id: 'leco', label: 'Leco (default)', preview: 'font-family: Arial, sans-serif; font-weight: 700;' },
  { id: 'clockforge', label: 'ClockForge', preview: "font-family: Impact, sans-serif; font-weight: 700; letter-spacing: 1px;" },
  { id: 'sfpixelate', label: 'SF Pixelate', preview: "font-family: 'Courier New', monospace; letter-spacing: 2px;" },
  { id: 'radioland', label: 'Radioland', preview: "font-family: 'Courier New', monospace; font-weight: 700;" },
  { id: 'minisystem', label: 'Mini System', preview: "font-family: 'Courier New', monospace;" },
  { id: 'minecrafter', label: 'Minecrafter', preview: "font-family: 'Courier New', monospace; letter-spacing: 3px;" },
  { id: 'kitchenpolice', label: 'Kitchen Police', preview: "font-family: Impact, 'Arial Narrow', sans-serif;" },
  { id: 'dsdigib', label: 'DS Digital', preview: "font-family: 'Courier New', monospace; font-weight: 700; letter-spacing: 2px;" },
  { id: 'distgrg', label: 'Distant Galaxy', preview: "font-family: 'Arial Narrow', sans-serif; letter-spacing: 3px; font-weight: 700;" },
  { id: 'dimitri', label: 'Dimitri', preview: "font-family: Georgia, serif; font-weight: 700;" },
  { id: 'digitaldream', label: 'Digital Dream', preview: "font-family: 'Courier New', monospace; letter-spacing: 4px; font-weight: 700;" },
  { id: 'blackout', label: 'Blackout', preview: "font-family: Impact, sans-serif; font-weight: 900;" },
  { id: 'audiowide', label: 'Audiowide', preview: "font-family: 'Arial Black', sans-serif; letter-spacing: 1px;" },
  { id: 'formation', label: 'Formation', preview: "font-family: Verdana, sans-serif; font-weight: 700;" },
  { id: 'komikahb', label: 'Komika', preview: "font-family: 'Comic Sans MS', cursive; font-weight: 700;" },
  { id: 'miso', label: 'Miso', preview: "font-family: 'Century Gothic', sans-serif; font-weight: 600;" },
  { id: 'pricedown', label: 'Pricedown', preview: "font-family: Impact, 'Arial Narrow', sans-serif; font-style: italic; letter-spacing: 1px;" },
  { id: 'roboto', label: 'Roboto', preview: "font-family: 'Roboto', Arial, sans-serif; font-weight: 700;" },
  { id: 'bithamlight', label: 'Bitham Light', preview: "font-family: 'Futura', 'Century Gothic', sans-serif; font-weight: 300; letter-spacing: 1px;" },
  { id: 'bithambold', label: 'Bitham Bold', preview: "font-family: 'Futura', 'Century Gothic', sans-serif; font-weight: 700; letter-spacing: 1px;" },
  { id: 'bebas', label: 'Bebas', preview: "font-family: 'Bebas', 'Century Gothic', sans-serif; font-weight: 700; letter-spacing: 1px;" }
];

// Fonts too wide to fit a seconds readout next to them by default in
// digital mode (analog mode's seconds hand has no such issue) -- the
// seconds checkbox is grayed out accordingly (but not silently
// ignored: index.js's showSecondsCode() double-checks this same rule
// server-side regardless of what the checkbox holds).
var FONTS_WITHOUT_SECONDS = { digitaldream: true, minecrafter: true};

// Must match get_color_scheme() in pebble-eclipse-watch.c exactly --
// same order, same id, same colors.
var COLOR_SCHEMES = [
  { id: 0, label: 'Black on White', bg: '#ffffff', text: '#000000', accent: '#000000' },
  { id: 1, label: 'White on Black', bg: '#000000', text: '#ffffff', accent: '#ffffff' },
  { id: 2, label: 'Red on Black', bg: '#000000', text: '#ff0000', accent: '#ff0000' },
  { id: 3, label: 'White on Dark Blue', bg: '#00003c', text: '#ffffff', accent: '#ffffff' },
  { id: 4, label: 'Yellow on Dark Blue', bg: '#00003c', text: '#ffff00', accent: '#ffff00' },
  { id: 5, label: 'White on Black, Red accent', bg: '#000000', text: '#ffffff', accent: '#ff0000' },
  { id: 6, label: 'Black on White, Dark Red accent', bg: '#ffffff', text: '#000000', accent: '#8b0000' },
  { id: 7, label: 'Black on White, Dark Blue accent', bg: '#ffffff', text: '#000000', accent: '#00008b' },
  { id: 8, label: 'Red on Black, White accent', bg: '#000000', text: '#ff0000', accent: '#ffffff' },
  { id: 9, label: 'Red on White, Orange accent', bg: '#ffffff', text: '#ff0000', accent: '#ff8c00' },
  { id: 11, label: 'Brown on Green, Orange accent', bg: '#228b22', text: '#8b4513', accent: '#ff8c00' }
];

// Must match corner_content's switch in pebble-eclipse-watch.c exactly.
var CORNER_CONTENT_OPTIONS = [
  { id: 0, label: 'None' },
  { id: 1, label: 'Heart rate' },
  { id: 2, label: 'Steps today' },
  { id: 3, label: 'Step goal %' },
  { id: 4, label: 'High / low temperature' },
  { id: 5, label: 'Current conditions' },
  { id: 6, label: 'UV index' },
  { id: 7, label: 'Rain chance today' },
  { id: 8, label: 'Humidity' },
  { id: 9, label: 'Wind' },
  { id: 10, label: 'Battery' },
  { id: 11, label: 'Moon phase' },
  { id: 12, label: 'Short date' },
  { id: 13, label: 'Location' },
  { id: 14, label: 'Visibility' },
  { id: 15, label: 'Cloud cover' },
  { id: 16, label: 'Sunrise / sunset' },
  { id: 17, label: 'Pebble logo /w battery bar' },
  { id: 18, label: 'Time' },
  { id: 19, label: 'Week number' },
  { id: 20, label: 'Bluetooth connection' },
  { id: 21, label: 'Date: Month Day (SEP 11)' },
  { id: 22, label: 'Date: Day of month (11)' },
  { id: 23, label: 'Date: Weekday, short (MON)' },
  { id: 24, label: 'Date: Weekday, long (Monday)' },
  { id: 25, label: 'Date: Month, short (SEP)' },
  { id: 26, label: 'Date: Month, long (September)' },
  { id: 27, label: 'Date: Day/Month (11/9)' },
  { id: 28, label: 'Date: Month/Day (9/11)' },
  { id: 29, label: 'Date: Full (24/9/2026)' },
  { id: 30, label: 'Date: Full, imperial (9/24/26)' },
  { id: 31, label: 'Weather icon' },
  { id: 32, label: 'Temp + weather icon' },
  { id: 34, label: 'Pressure' },
  { id: 35, label: 'Wind direction' },
  { id: 36, label: 'Air quality' },
  { id: 37, label: 'Dew point' },
  { id: 38, label: 'Altitude' },
  { id: 39, label: 'Sleep duration' },
  { id: 40, label: 'Restful sleep duration' },
  { id: 41, label: 'Sleep quality %' },
  { id: 42, label: 'Bed time' },
  { id: 43, label: 'Wake time' },
  // Each city is its own content id (see the id=44-62 case block in
  // draw_corner_item() in pebble-eclipse-watch.c) rather than one
  // "Timezone" id plus a separate shared setting -- that's what makes
  // this genuinely independent per slot: pick "London" in one slot and
  // "Tokyo" in another and both show at once, each on its own actual
  // clock, rather than every "Timezone" slot being forced to share
  // whichever single city a global setting pointed at. The city's
  // abbreviation (LON, TOK, ...) only appears on the watch, per the
  // brief -- these labels show the GMT offset (standard time, not
  // DST-adjusted) and the city's full name instead, matching the
  // format the case block's own comment describes. Order/offsets must
  // match TIMEZONES[] in pebble-eclipse-watch.c exactly.
  { id: 44, label: 'GMT+0 London' },
  { id: 45, label: 'GMT+1 Paris / Berlin / Madrid' },
  { id: 46, label: 'GMT+2 Cairo' },
  { id: 47, label: 'GMT+3 Moscow' },
  { id: 48, label: 'GMT+4 Dubai' },
  { id: 49, label: 'GMT+5:30 Delhi / Mumbai' },
  { id: 50, label: 'GMT+6 Dhaka' },
  { id: 51, label: 'GMT+7 Bangkok / Jakarta' },
  { id: 52, label: 'GMT+8 Beijing / Shanghai / Singapore' },
  { id: 53, label: 'GMT+9 Tokyo' },
  { id: 54, label: 'GMT+10 Sydney' },
  { id: 55, label: 'GMT+12 Auckland' },
  { id: 56, label: 'GMT-5 New York' },
  { id: 57, label: 'GMT-6 Chicago' },
  { id: 58, label: 'GMT-7 Denver' },
  { id: 59, label: 'GMT-8 Los Angeles' },
  { id: 60, label: 'GMT-9 Anchorage' },
  { id: 61, label: 'GMT-10 Honolulu' },
  { id: 62, label: 'GMT-3 Sao Paulo' },
  { id: 63, label: 'Time: full (H:M:S)' },
  { id: 64, label: 'Time: hour, 24h leading zero (07)' },
  { id: 65, label: 'Time: hour, 24h (7)' },
  { id: 66, label: 'Time: hour, 12h (7)' },
  { id: 67, label: 'Time: minute (5)' },
  { id: 68, label: 'Time: minute, leading zero (05)' },
  { id: 69, label: 'Time: second (8)' },
  { id: 70, label: 'Time: second, leading zero (08)' },
  { id: 71, label: 'Time: seconds, tens digit' },
  { id: 72, label: 'Time: seconds, ones digit' },
  { id: 73, label: 'Current temp' },
  { id: 74, label: 'High temp' },
  { id: 75, label: 'Low temp' },
  { id: 76, label: 'Weather icon + all temps' },
  { id: 77, label: 'Feels like temp' },
  { id: 78, label: 'Bluetooth status (icon only)' },
  { id: 79, label: 'Planets visible now' },
  { id: 80, label: 'Meteor shower' },
  { id: 81, label: 'Saturn ring angle' },
  { id: 82, label: 'Next planet rise' },
  { id: 83, label: 'Next ISS pass' },
  { id: 84, label: 'Aurora Kp index' }
];
// Must match draw_corner_item()'s color_mode switch exactly.
var CORNER_COLOR_MODE_LABELS = ['MONO', 'ACC', 'SEMI', 'COLOR'];
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

// Same encoding as corner_custom_font/corner_font_size combined -- see
// marker_text_font_resource_id() in marker_layer.c.
// Must match get_marker_text_font()/marker_text_font_resource_id() in
// background_layer.c exactly -- same id, same font. This used to be
// badly out of sync with that switch (wrong labels on the wrong ids,
// two ids -- 3 and 8 -- that didn't exist on the watch at all and
// silently fell back to the small system font instead), which is why
// picking most of the custom options here never actually looked like
// what the label said. ROMAN_INCOMPATIBLE_FONTS below flags entries
// whose glyphs don't include the extra characters roman numerals
// need.
var MARKER_TEXT_FONTS = [
  { id: 0, label: 'System - small' },
  { id: 1, label: 'System - medium' },
  { id: 2, label: 'System - large' },
  { id: 3, label: 'Digital' },
  { id: 4, label: 'Minecraft' },
  { id: 5, label: 'Pixelate' },
  { id: 6, label: 'Miso' },
  { id: 7, label: 'Leco' },
  { id: 8, label: 'Leco L' },
  { id: 9, label: 'Leco XL' },
  { id: 10, label: 'Droid Serif' },
  { id: 11, label: 'Roboto Condensed' },
  { id: 12, label: 'Bitham bold' },
  { id: 13, label: 'Bitham M' },
  { id: 14, label: 'Bebas' }
];
// Fonts known not to render Roman numerals correctly (missing/wrong
// glyphs for some of the letters int_to_roman() needs) -- the Roman
// numerals checkbox gets disabled (and, if it was checked, force-
// unchecked) whenever one of these is selected for marker text. Only
// verified for these three so far; add more here as they're checked
// -- see int_to_roman() in background_layer.c for what it actually
// needs (I, V, X, L, C, D, M).
var ROMAN_INCOMPATIBLE_FONTS = { 8: true, 9: true, 13: true }; // Leco L, Leco XL, Bitham M
function markerTextFontOptionsHtml(selected) {
  var sel = selected || '0';
  return MARKER_TEXT_FONTS.map(function (f) {
    return '<option value="' + f.id + '"' + (String(sel) === String(f.id) ? ' selected' : '') + '>' + esc(f.label) + '</option>';
  }).join('');
}

// A 12-button grid for picking which hour numerals (kind='hour', labels
// 12,1..11) or which every-5-second slots (kind='sec', labels 0,5..55)
// should get a text marker -- bit i of the mask corresponds to button i,
// same order marker_layer_draw_text() iterates on-watch.
function markBtnGridHtml(kind, maskStr) {
  var mask = parseInt(maskStr, 10);
  if (isNaN(mask)) mask = 0;
  var html = '<div class="mark-btn-grid">';
  for (var i = 0; i < 12; i++) {
    var label = kind === 'hour' ? (i === 0 ? 12 : i) : (i * 5);
    var active = (mask & (1 << i)) !== 0;
    html += '<button type="button" class="mark-btn' + (active ? ' active' : '') +
      '" id="markBtn-' + kind + '-' + i + '" onclick="toggleMarkBtn(\'' + kind + '\',' + i + ')">' + label + '</button>';
  }
  return html + '</div>';
}

// The 16 fields that actually get sent to the watch for the custom
// marker system -- kept as hidden inputs (same pattern as the corner
// slots' hidden color inputs) since they're edited inside the two
// popups, not directly on the page. Defaults approximate the "Big"
// procedural preset so the ring is visible rather than invisible
// (thickness 0) the first time someone picks "Custom".
// Border sliders are 0-100% "reach" values -- see marker_reach_px() in
// marker_layer.c for the exact mapping. On a 200x228 screen that's a
// px range of [100,114]: 0% is the largest circle guaranteed to stay
// fully on-screen (min(w,h)/2), 100% is the screen-fitted rectangle's
// own far edge (max(w,h)/2). Deliberately narrow -- the range only
// widens on a screen with a more extreme aspect ratio -- because it's
// derived directly from "never let a marker end up off the screen".
var MARKER_BORDER_MIN = 0;
var MARKER_BORDER_MAX = 100;

function customMarkerHiddenInputsHtml(current) {
  var d = {
    customHourStyle: '0', customHourThickness: '3',
    customHourInnerEcc: '0', customHourOuterEcc: '0', customHourInnerBorder: '20', customHourOuterBorder: '100',
    customHourTranslucent: 'false', customHourColor: '0',
    customSecStyle: '0', customSecThickness: '1',
    customSecInnerEcc: '0', customSecOuterEcc: '0', customSecInnerBorder: '70', customSecOuterBorder: '100',
    customSecTranslucent: 'false', customSecColor: '0',
    markerTextHourMask: '4095', markerTextSecMask: '4095'
  };
  var html = '';
  for (var key in d) {
    html += '<input type="hidden" id="' + key + '" value="' + esc(current[key] || d[key]) + '">';
  }
  return html;
}

// The hour/second custom-marker popup -- kind is 'hour' or 'sec', used
// as an id suffix throughout (cmStyle-hour, cmStyle-sec, ...) so one
// generator serves both. thicknessMax is 20 for hour, 10 for second
// (see MarkerRingConfig in marker_layer.h). All fields here are drafted
// in the popup and only committed to the real customHour*/customSec*
// hidden inputs when OK is pressed -- same "don't touch the real
// settings until Save" pattern as the corner slot editor.
function customMarkerModalHtml(kind, title, thicknessMax) {
  var p = kind === 'hour' ? 'cmHour' : 'cmSec';
  return (
'<div class="modal-overlay" id="customMarkerModal-' + kind + '" onclick="if (event.target === this) closeCustomMarkerEditor(\'' + kind + '\');">' +
'  <div class="modal-box">' +
'    <div class="modal-title">' + esc(title) + '</div>' +
'    <div class="modal-scroll-body">' +

'    <label for="' + p + 'Style">Shape</label>' +
'    <select id="' + p + 'Style">' +
'      <option value="0">Dot (round)</option>' +
'      <option value="1">Line (flat ends)</option>' +
'      <option value="2">Square (blocky ends)</option>' +
'    </select>' +

'    <div class="slider-row">' +
'      <label for="' + p + 'Thickness">Thickness <span class="val" id="' + p + 'ThicknessVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Thickness" min="1" max="' + thicknessMax + '" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">Each mark is drawn directly between its inner and outer border points below -- no separate length setting.</div>' +

'    <div class="slider-row">' +
'      <label for="' + p + 'InnerEcc">Inner eccentricity <span class="val" id="' + p + 'InnerEccVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerEcc\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'InnerEcc" min="0" max="100" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerEcc\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'OuterEcc">Outer eccentricity <span class="val" id="' + p + 'OuterEccVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterEcc\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'OuterEcc" min="0" max="100" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterEcc\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">0 = circle, 100 = a rectangle fitted to the screen edges -- this is what "bends" each mark around corners as it changes.</div>' +

'    <div class="slider-row">' +
'      <label for="' + p + 'InnerBorder">Inner border <span class="val" id="' + p + 'InnerBorderVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerBorder\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'InnerBorder" min="' + MARKER_BORDER_MIN + '" max="' + MARKER_BORDER_MAX + '" step="1" oninput="onCustomMarkerBorderInput(\'' + kind + '\', true)">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerBorder\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'OuterBorder">Outer border <span class="val" id="' + p + 'OuterBorderVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterBorder\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'OuterBorder" min="' + MARKER_BORDER_MIN + '" max="' + MARKER_BORDER_MAX + '" step="1" oninput="onCustomMarkerBorderInput(\'' + kind + '\', false)">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterBorder\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">Outer can\'t go below inner -- it gets pulled up automatically if you drag inner past it.</div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'Translucent">' +
'      <label for="' + p + 'Translucent" style="margin:0;">Semi-transparent</label>' +
'    </div>' +
'    <div class="help">Dithers this ring (independent of the hour/second ring\'s own setting, and of Semi-transparent hands) to ~50% so the sky shows through.</div>' +

'    <label for="' + p + 'Color" style="margin-top:12px;">Color</label>' +
'    <select id="' + p + 'Color">' + schemeColorOptionsHtml('0') + '</select>' +
'    <div class="help">Independent of the hour/second ring\'s own color -- pick a different one for each if you want them to stand apart.</div>' +

'    <label style="margin-top:12px;">Presets (translated from the procedural styles)</label>' +
'    <div class="preset-btn-row">' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'minimal\')">Minimal</button>' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'small\')">Small</button>' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'big\')">Big</button>' +
'    </div>' +
'    <button type="button" class="marker-edit-btn" style="margin-top:8px;" onclick="copyMarkerConfig(\'' + kind + '\')">Copy from ' + (kind === 'hour' ? 'second' : 'hour') + ' markers</button>' +

'    </div>' +
'    <div class="modal-footer">' +
'    <button type="button" onclick="saveCustomMarkerEditor(\'' + kind + '\')" style="width:100%; box-sizing:border-box; padding:14px; font-size:16px; font-weight:600; color:#fff; background:#ff9200; border:none; border-radius:8px; margin-top:14px;">OK</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeCustomMarkerEditor(\'' + kind + '\')">Cancel</button>' +
'    </div>' +
'  </div>' +
'</div>'
  );
}

// The "Edit text markers" popup -- numerals shown on the hour or second
// custom-marker ring (never both). Everything here commits live (same
// as the hour/second button grids always have) rather than draft-then-
// Save, since there's no risk of an inconsistent in-between state the
// way there is with the ring geometry popups.
function textMarkerModalHtml(current) {
  return (
'<div class="modal-overlay" id="textMarkerModal" onclick="if (event.target === this) closeTextMarkerEditor();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Edit text markers</div>' +
'    <div class="modal-scroll-body">' +

'    <label for="markerTextTarget">Numbers</label>' +
'    <select id="markerTextTarget" onchange="onMarkerTextTargetChange()">' +
'      <option value="0"' + (current.markerTextTarget === '0' || !current.markerTextTarget ? ' selected' : '') + '>Off</option>' +
'      <option value="1"' + (current.markerTextTarget === '1' ? ' selected' : '') + '>On hour markers</option>' +
'      <option value="2"' + (current.markerTextTarget === '2' ? ' selected' : '') + '>On second markers (every 5s)</option>' +
'    </select>' +
'    <div class="help">Numbers can go on the hour ring or the second ring, not both at once.</div>' +

'    <div id="markerTextOptions" style="' + (current.markerTextTarget && current.markerTextTarget !== '0' ? '' : 'display:none;') + '">' +
'      <label for="markerTextFont" style="margin-top:10px;">Font</label>' +
'      <select id="markerTextFont" onchange="onMarkerTextFontChange()">' + markerTextFontOptionsHtml(current.markerTextFont) + '</select>' +

'      <div class="checkbox-row" style="margin-top:12px;">' +
'        <input type="checkbox" id="markerTextRoman" ' + (current.markerTextRoman === 'true' && !ROMAN_INCOMPATIBLE_FONTS[current.markerTextFont] ? 'checked' : '') + ' ' + (ROMAN_INCOMPATIBLE_FONTS[current.markerTextFont] ? 'disabled' : '') + '>' +
'        <label for="markerTextRoman" style="margin:0;">Roman numerals</label>' +
'      </div>' +
'      <div class="help" id="markerTextRomanHelp">' + (ROMAN_INCOMPATIBLE_FONTS[current.markerTextFont] ? 'Not available with this font -- its glyphs don\'t support Roman numerals correctly.' : 'Shows I, II, III... instead of 1, 2, 3... -- independent of the font above.') + '</div>' +

'      <div class="slider-row">' +
'        <label for="markerTextOffset">Offset from marker <span class="val" id="markerTextOffsetVal">' + esc(current.markerTextOffset || '0') + 'px</span></label>' +
'        <div class="slider-with-buttons">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'markerTextOffset\', -1)">&minus;</button>' +
'        <input type="range" id="markerTextOffset" min="-50" max="50" step="1" value="' + esc(current.markerTextOffset || '0') + '" oninput="document.getElementById(\'markerTextOffsetVal\').textContent = this.value + \'px\';">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'markerTextOffset\', 1)">+</button>' +
'        </div>' +
'      </div>' +
'      <div class="help">Positive nudges numbers outward (away from center), negative pulls them inward -- so they don\'t overlap the dot/line/square marker.</div>' +

'      <div id="markerTextHourGrid" style="' + (current.markerTextTarget === '1' ? '' : 'display:none;') + '">' +
'        <label style="margin-top:10px;">Which hours get a number</label>' +
          markBtnGridHtml('hour', current.markerTextHourMask !== undefined ? current.markerTextHourMask : '4095') +
'      </div>' +
'      <div id="markerTextSecGrid" style="' + (current.markerTextTarget === '2' ? '' : 'display:none;') + '">' +
'        <label style="margin-top:10px;">Which 5-second marks get a number</label>' +
          markBtnGridHtml('sec', current.markerTextSecMask !== undefined ? current.markerTextSecMask : '4095') +
'      </div>' +
'    </div>' +

'    </div>' +
'    <div class="modal-footer">' +
'    <button type="button" class="modal-cancel-btn" onclick="closeTextMarkerEditor()" style="margin-top:14px;">Close</button>' +
'    </div>' +
'  </div>' +
'</div>'
  );
}

function schemeColorOptionsHtml(selected) {
  var sel = selected || '0';
  return (
'<option value="0"' + (sel === '0' ? ' selected' : '') + '>Main color</option>' +
'<option value="1"' + (sel === '1' ? ' selected' : '') + '>Accent color</option>' +
'<option value="2"' + (sel === '2' ? ' selected' : '') + '>Background color</option>'
  );
}

// The 7 fields per hand (hour/min/sec -- 21 total) that get sent to the
// watch, kept as hidden inputs edited via the 3 popups below, same
// pattern as customMarkerHiddenInputsHtml(). Defaults approximate the
// "Pointy" procedural style so hands are visible immediately, rather
// than defaulting to width/length 0.
function handHiddenInputsHtml(current) {
  var d = {
    handHourStyle: '1', handHourWidth: '12', handHourLength: '51', handHourBackOffset: '0',
    handHourColor: '0', handHourOutlineEnabled: 'false', handHourOutlineColor: '0', handHourTranslucent: 'false',
    handHourShadowEnabled: 'false', handHourShadowDistance: '2',
    handMinStyle: '1', handMinWidth: '18', handMinLength: '78', handMinBackOffset: '0',
    handMinColor: '0', handMinOutlineEnabled: 'false', handMinOutlineColor: '0', handMinTranslucent: 'false',
    handMinShadowEnabled: 'false', handMinShadowDistance: '2',
    handSecStyle: '0', handSecWidth: '2', handSecLength: '85', handSecBackOffset: '0',
    handSecColor: '1', handSecOutlineEnabled: 'false', handSecOutlineColor: '0', handSecTranslucent: 'false',
    handSecShadowEnabled: 'false', handSecShadowDistance: '2'
  };
  var html = '';
  for (var key in d) {
    var val = current[key] !== undefined ? current[key] : d[key];
    html += '<input type="hidden" id="' + key + '" value="' + esc(val) + '">';
  }
  return html;
}

// The hour/minute/second custom-hand popup -- kind is 'hour', 'min', or
// 'sec'. Same "draft in the popup, commit on OK" pattern as
// customMarkerModalHtml().
// Copy-preset direction is fixed per hand (not "the other one" generically,
// per how this was asked for): hour offers to copy minute's settings,
// minute offers hour's, second offers minute's.
var HAND_COPY_SOURCE = { hour: 'min', min: 'hour', sec: 'min' };
var HAND_COPY_SOURCE_LABEL = { hour: 'minute', min: 'hour', sec: 'minute' };

function handEditorModalHtml(kind, title) {
  var p = 'he' + kind.charAt(0).toUpperCase() + kind.slice(1); // heHour / heMin / heSec
  return (
'<div class="modal-overlay" id="handEditorModal-' + kind + '" onclick="if (event.target === this) closeHandEditor(\'' + kind + '\');">' +
'  <div class="modal-box">' +
'    <div class="modal-title">' + esc(title) + '</div>' +
'    <div class="modal-scroll-body">' +

'    <label for="' + p + 'Style">Shape</label>' +
'    <select id="' + p + 'Style">' +
'      <option value="0">Dot (round caps)</option>' +
'      <option value="1">Triangle</option>' +
'      <option value="2">Square (flat caps)</option>' +
'    </select>' +

'    <div class="slider-row">' +
'      <label for="' + p + 'Width">Width <span class="val" id="' + p + 'WidthVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Width\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Width" min="1" max="40" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Width\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'Length">Length <span class="val" id="' + p + 'LengthVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Length\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Length" min="10" max="100" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Length\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'BackOffset">Back offset <span class="val" id="' + p + 'BackOffsetVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'BackOffset\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'BackOffset" min="-40" max="40" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'BackOffset\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">Positive extends a tail behind the pivot; negative starts the hand short of center (a detached gap).</div>' +

'    <label for="' + p + 'Color">Color</label>' +
'    <select id="' + p + 'Color">' + schemeColorOptionsHtml('0') + '<option value="3">None (don\'t fill)</option></select>' +
'    <div class="help">"None" skips the fill entirely -- combine with Outline below for a hollow look.</div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'Translucent">' +
'      <label for="' + p + 'Translucent" style="margin:0;">Semi-transparent</label>' +
'    </div>' +
'    <div class="help">Dithers the fill (and outline, if enabled) to ~50% so the sky shows through.</div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'OutlineEnabled">' +
'      <label for="' + p + 'OutlineEnabled" style="margin:0;">Outline</label>' +
'    </div>' +
'    <label for="' + p + 'OutlineColor">Outline color</label>' +
'    <select id="' + p + 'OutlineColor">' + schemeColorOptionsHtml('0') + '</select>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'ShadowEnabled" onchange="onHandSliderInput(\'' + kind + '\')">' +
'      <label for="' + p + 'ShadowEnabled" style="margin:0;">Shadow</label>' +
'    </div>' +
'    <div class="help">A drop shadow of the hand\'s own shape, offset a fixed distance in a fixed direction (not rotated with the hand). Solid or translucent is set once for every hand in the Style section.</div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'ShadowDistance">Shadow distance <span class="val" id="' + p + 'ShadowDistanceVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'ShadowDistance\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'ShadowDistance" min="1" max="5" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'ShadowDistance\', 1)">+</button>' +
'      </div>' +
'    </div>' +

'    <button type="button" class="marker-edit-btn" style="margin-top:8px;" onclick="copyHandConfig(\'' + kind + '\')">Copy ' + HAND_COPY_SOURCE_LABEL[kind] + ' hand settings</button>' +

'    </div>' +
'    <div class="modal-footer">' +
'    <button type="button" onclick="saveHandEditor(\'' + kind + '\')" style="width:100%; box-sizing:border-box; padding:14px; font-size:16px; font-weight:600; color:#fff; background:#ff9200; border:none; border-radius:8px; margin-top:14px;">OK</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeHandEditor(\'' + kind + '\')">Cancel</button>' +
'    </div>' +
'  </div>' +
'</div>'
  );
}


/**
 * @param {object} current  current settings, as plain values:
 *   { autoLoc, lat, lon, owmKey, updateMins,
 *     clockFont, showSeconds, bottomStyle: 'digital'|'analog'|'biganalog', analogStyle: '0'-'3',
 *     bigAnalogHandStyle: '0'-'2', bigAnalogTransparent,
 *     bigAnalogMarkerStyle: '0'-'8' (8=custom -- see customHour.../customSec.../markerText... below), upperMiddleLine1Content/upperMiddleLine2Content: '0'-'12', upperMiddleLine1Color/upperMiddleLine2Color: '0'-'3',
 *     colorScheme: '0'-'9'|'custom', customBg, customText, customAccent (packed byte strings),
 *     nightEnabled, nightScheme, nightCustomBg, nightCustomText, nightCustomAccent,
 *     showSunTime, showIss, sunMoonSize: '25'|'50'|'75'|'100', shakeLabelSeconds, vibrateOnPhaseChange,
 *     tempUnit: 'C'|'F',
 *     cornerTL, cornerTR, cornerBL, cornerBR: '0'-'9', cornerTLColor, cornerTRColor, cornerBLColor, cornerBRColor: '0'-'3', stepGoal,
 *     customHourStyle/customSecStyle: '0'-'2' (dot/line/square), customHourThickness (1-20)/
 *     customSecThickness (1-10), customHourInnerEcc/customHourOuterEcc/customSecInnerEcc/
 *     customSecOuterEcc: '0'-'100', customHourInnerBorder/customHourOuterBorder/
 *     customSecInnerBorder/customSecOuterBorder: '0'-'100' (% reach, see marker_reach_px()
 *     in marker_layer.c -- each mark spans directly between its inner/outer border points),
 *     markerTextTarget: '0'(off)|'1'(hour)|'2'(second), markerTextFont: '0'-'6', markerTextOffset: '-50'-'50',
 *     markerTextHourMask/markerTextSecMask: 0-4095 (12-bit),
 *     testMode, testDateTime }
 */
function buildConfigHtml(current) {
  var autoLocChecked = current.autoLoc ? 'checked' : '';
  var manualDisabled = current.autoLoc ? 'disabled' : '';
  var testModeChecked = current.testMode ? 'checked' : '';
  var testDisabled = current.testMode ? '' : 'disabled';
  var debugTextareaInitial = (current.debugOverrideEnabled && current.debugOverrideData)
    ? current.debugOverrideData
    : (current.lastSentData || '');
  var bottomStyleVal = current.bottomStyle === 'biganalog' ? 'biganalog' : (current.bottomStyle === 'analog' ? 'analog' : 'digital');
  // Drives whether the "Weather icon style" dropdown in the Weather
  // section starts visible -- true if any of the 12 corner/edge slots
  // is already set to "Weather icon" (31) or "Temp + weather icon" (32).
  var weatherIconFeatureInUse = ['cornerTL', 'cornerTR', 'cornerBL', 'cornerBR',
    'upperMiddleLine1Content', 'upperMiddleLine2Content', 'bottomMiddleLine1Content', 'bottomMiddleLine2Content',
    'middleLeftLine1Content', 'middleLeftLine2Content', 'middleRightLine1Content', 'middleRightLine2Content'
  ].some(function (key) { return current[key] === '31' || current[key] === '32'; });
  var isAnalog = bottomStyleVal === 'analog';
  var isBigAnalog = bottomStyleVal === 'biganalog';
  var secondsUnsupported = (bottomStyleVal === 'digital') && !!FONTS_WITHOUT_SECONDS[current.clockFont];
  var secondsChecked = (current.showSeconds && !secondsUnsupported) ? 'checked' : '';
  var secondsDisabled = secondsUnsupported ? 'disabled' : '';

  // One <button> per example-style slot (see EXAMPLE_STYLE_COUNT's own
  // comment above) -- a screenshot if one's been generated for that
  // slot, otherwise just its number as an empty placeholder tile;
  // disabled (not clickable) until that slot has an actual preset.
  var exampleStylesButtonsHtml = '';
  for (var exStyleI = 1; exStyleI <= EXAMPLE_STYLE_COUNT; exStyleI++) {
    var exStyleImg = EXAMPLE_STYLE_IMAGES[String(exStyleI)];
    var exStyleHasPreset = EXAMPLE_STYLE_PRESETS[String(exStyleI)] != null;
    exampleStylesButtonsHtml +=
      '<button type="button" class="example-style-btn" onclick="openExampleStyleModal(' + exStyleI + ')"' +
      (exStyleHasPreset ? '' : ' disabled') + '>' +
      (exStyleImg
        ? '<img src="' + exStyleImg + '" alt="Example style ' + exStyleI + '">'
        : '<span class="example-style-btn-empty">' + exStyleI + '</span>') +
      '</button>';
  }

  // Which edge-middle slots (upper/bottom/left/right-middle) does the
  // current mode/style support, and are the 4 corners themselves
  // suppressed? Must match corners_layer_update_proc's rules in
  // pebble-eclipse-watch.c exactly, or the settings page would show
  // slots as available that the watch itself won't actually draw.
  var markerStyleNum = parseInt(current.bigAnalogMarkerStyle || '0', 10);
  var isBitmapMarkerStyle = markerStyleNum >= 3 && markerStyleNum !== 8 && markerStyleNum !== 9;
  var edgeAvail = { upper: false, bottom: false, left: false, right: false, cornersGrayed: false };
  if (isBigAnalog) {
    if (markerStyleNum < 3 || markerStyleNum === 8 || markerStyleNum === 9) {
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: false };
    } else if (markerStyleNum === 3 || markerStyleNum === 4 || markerStyleNum === 6) {
      edgeAvail = { upper: true, bottom: true, left: false, right: false, cornersGrayed: true };
    } else if (markerStyleNum === 5 || markerStyleNum === 7) {
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: true };
    } else {
      edgeAvail = { upper: true, bottom: false, left: false, right: false, cornersGrayed: true };
    }
  }
  var fontOptions = CLOCK_FONTS.map(function (f) {
    return '<option value="' + f.id + '" data-preview="' + esc(f.preview) + '" data-seconds="' +
      (FONTS_WITHOUT_SECONDS[f.id] ? '0' : '1') + '"' +
      (current.clockFont === f.id ? ' selected' : '') + '>' + esc(f.label) + '</option>';
  }).join('');

  function schemeOptionsHtml() {
    var opts = '<option value="" selected>Choose a preset...</option>';
    opts += COLOR_SCHEMES.map(function (s) {
      return '<option value="' + s.id + '">' + esc(s.label) + '</option>';
    }).join('');
    return opts;
  }

  function hexFromPackedByte(byte) {
    var b = parseInt(byte, 10);
    if (isNaN(b)) return '#000000';
    var r2 = (b >> 4) & 3, g2 = (b >> 2) & 3, b2 = b & 3;
    function ch(v) { var h = (v * 85).toString(16); return h.length < 2 ? '0' + h : h; }
    return '#' + ch(r2) + ch(g2) + ch(b2);
  }
  // Colors are always three concrete packed bytes now -- there's no
  // "preset vs custom" mode to resolve here. Picking a preset (see
  // onPresetChange() below) just writes its RGB straight into these
  // same three hidden fields, same as tapping each swatch individually
  // would, so the page only ever has one representation of "current
  // colors" to read back on load.
  function resolveInitialColors(bgByte, textByte, accentByte) {
    return {
      bg: hexFromPackedByte(bgByte),
      text: hexFromPackedByte(textByte),
      accent: hexFromPackedByte(accentByte)
    };
  }
  var initialColors = resolveInitialColors(current.customBg || '255', current.customText || '192', current.customAccent || '192');
  var initialNightColors = resolveInitialColors(current.nightCustomBg || '192', current.nightCustomText || '255', current.nightCustomAccent || '255');

  return '<!DOCTYPE html>' +
'<html><head><meta charset="utf-8">' +
'<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">' +
'<title>Eclipz Settings</title>' +
'<style>' +
'  :root { --page-bg: #f4f4f4; --card-bg: #fff; --text: #222; --text-strong: #333; --text-muted: #666; --text-faint: #888; --text-faint2: #555; --text-disabled: #999; --border: #ccc; --border-light: #eee; --border-lighter: #ddd; --btn-bg: #fafafa; }' +
'  @media (prefers-color-scheme: dark) {' +
'    :root { --page-bg: #1c1c1e; --card-bg: #2c2c2e; --text: #f2f2f2; --text-strong: #e5e5e5; --text-muted: #aaa; --text-faint: #999; --text-faint2: #bbb; --text-disabled: #777; --border: #48484a; --border-light: #3a3a3c; --border-lighter: #545456; --btn-bg: #3a3a3c; }' +
'  }' +
'  body { font-family: -apple-system, Helvetica, Arial, sans-serif; margin: 0; padding: 16px 20px 90px; background: var(--page-bg); color: var(--text); }' +
'  html, body { touch-action: manipulation; }' + // belt-and-suspenders alongside the viewport meta tag --
                                                    // some in-app webviews still allow double-tap-to-zoom
                                                    // on individual elements unless this is set too, and a
                                                    // double-tap on a fast-repeating button (the settings
                                                    // and slider buttons below) shouldn't ever zoom the page.
'  button, .mode-btn, .slot-btn, .slider-step-btn { touch-action: manipulation; -webkit-user-select: none; user-select: none; }' +
'  fieldset { border: none; background: var(--card-bg); border-radius: 8px; padding: 14px 16px; margin-bottom: 16px; box-shadow: 0 1px 2px rgba(0,0,0,0.08); }' +
'  legend { font-weight: 600; font-size: 14px; padding: 0; color: var(--text-strong); }' +
'  label { display: block; font-size: 14px; margin: 10px 0 4px; color: var(--text-strong); }' +
'  input[type=text], input[type=number], select { width: 100%; box-sizing: border-box; padding: 8px; font-size: 15px; border: 1px solid var(--border); border-radius: 5px; background: var(--card-bg); color: var(--text); }' +
'  input[disabled], select[disabled] { background: var(--border-light); color: var(--text-disabled); }' +
'  .checkbox-row { display: flex; align-items: center; gap: 10px; }' +
'  .radio-row { display: flex; align-items: center; gap: 10px; margin-top: 6px; }' +
'  input[type=checkbox] { appearance: none; -webkit-appearance: none; width: 30px; height: 30px; flex-shrink: 0; margin: 0; padding: 0; box-sizing: border-box; border: 2px solid var(--border); border-radius: 8px; background: var(--card-bg); position: relative; }' +
'  input[type=checkbox]:checked { background: #ff9200; border-color: #ff9200; }' +
'  input[type=checkbox]:checked::after { content: ""; position: absolute; left: 9px; top: 4px; width: 7px; height: 14px; border: solid #fff; border-width: 0 3px 3px 0; transform: rotate(45deg); }' +
'  input[type=checkbox][disabled] { background: var(--border-light); border-color: var(--border-lighter); }' +
'  input[type=checkbox][disabled]:checked { background: #f0c785; border-color: #f0c785; }' +
'  .help { color: var(--text-faint); font-size: 12px; margin-top: 4px; }' +
'  .radio-row { display: flex; gap: 16px; margin-top: 8px; flex-wrap: wrap; }' +
'  .radio-row label { display: flex; align-items: center; gap: 6px; margin: 0; font-weight: normal; }' +
'  .radio-row input { width: auto; }' +
'  .secondary-btn { width: 100%; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--border-light); border: 1px solid var(--border); border-radius: 8px; margin-top: 12px; }' +
'  .preset-slot-row { display: flex; gap: 6px; align-items: stretch; margin-top: 8px; }' +
'  .preset-apply-btn { flex: 1; box-sizing: border-box; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; text-align: left; }' +
'  .preset-apply-btn:disabled { opacity: 0.45; }' +
'  .preset-icon-btn { width: 44px; flex-shrink: 0; font-size: 18px; background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; color: var(--text-strong); }' +
'  .preset-name-input { flex: 1; box-sizing: border-box; }' +
'  .example-style-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 8px; }' +
'  .example-style-btn { position: relative; aspect-ratio: 1; box-sizing: border-box; border-radius: 8px; border: 1px solid var(--border); background: var(--btn-bg); overflow: hidden; padding: 0; }' +
'  .example-style-btn:disabled { opacity: 0.4; }' +
'  .example-style-btn img { width: 100%; height: 100%; object-fit: cover; display: block; }' +
'  .example-style-btn-empty { display: flex; align-items: center; justify-content: center; width: 100%; height: 100%; font-size: 13px; font-weight: 600; color: var(--text); }' +
'  .secondary-btn:active { background: var(--border-lighter); }' +
'  .save-bar { position: fixed; left: 0; right: 0; bottom: 0; padding: 12px 20px calc(12px + env(safe-area-inset-bottom, 0px)); background: var(--page-bg); box-shadow: 0 -2px 6px rgba(0,0,0,0.1); }' +
'  .save-bar button { width: 100%; padding: 14px; font-size: 16px; font-weight: 600; color: #fff; background: #ff9200; border: none; border-radius: 8px; }' +
'  .save-bar button:active { background: #e08300; }' +
'  #topBar { position: fixed; top: 0; left: 0; right: 0; max-height: 25vh; overflow: hidden; background: var(--page-bg); box-shadow: 0 2px 6px rgba(0,0,0,0.12); display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 10px 14px; padding-top: calc(10px + env(safe-area-inset-top, 0px)); box-sizing: border-box; z-index: 50; }' +
'  .top-bar-left { display: flex; flex-direction: column; justify-content: center; flex: 0 1 66%; min-width: 0; }' +
'  .top-bar-actions { display: flex; gap: 6px; align-items: center; }' +
'  .back-btn { padding: 6px 10px; font-size: 13px; font-weight: 600; color: var(--text-strong); background: var(--card-bg); border: 1px solid var(--border); border-radius: 6px; }' +
'  .back-btn:active { background: var(--border-light); }' +
'  .donate-btn { padding: 6px 10px; font-size: 13px; font-weight: 700; color: #fff; background: linear-gradient(135deg, #ffb347, #ff8c00); border: none; border-radius: 6px; box-shadow: 0 1px 3px rgba(255,140,0,0.5); }' +
'  .donate-btn:active { filter: brightness(0.92); }' +
'  .top-bar-title { font-size: 15px; font-weight: 700; margin-top: 6px; color: var(--text); white-space: nowrap; }' +
'  .top-bar-desc { font-size: 10px; line-height: 1.3; color: var(--text-muted); margin-top: 3px; }' +
'  .top-bar-preview { flex: 0 1 33%; display: flex; justify-content: center; align-items: center; min-width: 0; height: 100%; max-height: calc(25vh - 20px); padding: 1%; box-sizing: border-box; }' +
'  #previewCanvas { height: 50%; max-height: 50%; width: auto; max-width: 98%; border-radius: 4px; }' +
'  .subsection { margin-top: 10px; padding-top: 10px; border-top: 1px solid var(--border-light); }' +
'  .color-role-buttons { display: flex; gap: 8px; margin-top: 6px; }' +
'  .color-role-btn { flex: 1; display: flex; flex-direction: column; align-items: center; gap: 6px; padding: 8px 4px; border: 1px solid var(--border); border-radius: 8px; background: var(--btn-bg); }' +
'  .color-role-btn:active { background: var(--border-light); }' +
'  .color-role-swatch { width: 36px; height: 36px; border-radius: 50%; border: 2px solid var(--border); box-sizing: border-box; }' +
'  .color-role-label { font-size: 11px; color: var(--text-faint2); }' +
'  .modal-overlay { position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.5); display: none; align-items: flex-end; justify-content: center; z-index: 100; }' +
'  .modal-overlay.open { display: flex; }' +
'  .modal-box { background: var(--card-bg); border-radius: 12px 12px 0 0; padding: 16px; width: 100%; max-width: 400px; max-height: 80vh; box-sizing: border-box; display: flex; flex-direction: column; overflow: hidden; }' +
'  .modal-title { font-weight: 600; font-size: 15px; margin-bottom: 10px; text-align: center; color: var(--text); flex: 0 0 auto; }' +
'  .modal-scroll-body { overflow-y: auto; flex: 1 1 auto; min-height: 0; }' +
'  .modal-footer { flex: 0 0 auto; }' +
'  .hex-grid { position: relative; width: 260px; height: 255px; margin: 0 auto; }' +
'  .hex-swatch { position: absolute; width: 26px; height: 30px; margin: -15px 0 0 -13px; clip-path: polygon(50% 0%, 100% 25%, 100% 75%, 50% 100%, 0% 75%, 0% 25%); border: 1px solid rgba(0,0,0,0.15); box-sizing: border-box; }' +
'  .hex-swatch.hollow { border: none; background: transparent !important; pointer-events: none; }' +
'  .hex-swatch.selected { border: 2px solid #ff9200; }' +
'  .modal-cancel-btn { width: 100%; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--border-light); border: none; border-radius: 8px; margin-top: 12px; }' +
'  .modal-confirm-btn { width: 100%; padding: 12px; font-size: 14px; font-weight: 600; color: #fff; background: #ff9200; border: none; border-radius: 8px; margin-top: 8px; }' +
'  .modal-confirm-btn:active { background: #e08300; }' +
'  .example-style-modal-img { width: 100%; border-radius: 8px; display: block; }' +
'  .example-style-modal-title { font-weight: 700; font-size: 16px; margin-top: 10px; text-align: center; color: var(--text-strong); }' +
'  .mode-btn-group { display: flex; width: 100%; margin-top: 6px; border-radius: 6px; overflow: hidden; border: 1px solid var(--border); box-sizing: border-box; }' +
'  .mode-btn { flex: 1; padding: 10px 0; font-size: 12px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: none; border-right: 1px solid var(--border); }' +
'  .mode-btn:last-child { border-right: none; }' +
'  .mode-btn.active { background: #ff9200; color: #fff; box-shadow: inset 0 2px 4px rgba(0,0,0,0.35); }' +
'  .slider-row { margin-top: 12px; }' +
'  .slider-row label { display: flex; justify-content: space-between; font-size: 13px; color: var(--text-faint2); margin-bottom: 2px; }' +
'  .slider-row label .val { font-weight: 700; color: var(--text-strong); }' +
'  input[type=range] { width: 100%; -webkit-appearance: none; appearance: none; height: 30px; background: transparent; margin: 0; }' +
'  input[type=range]::-webkit-slider-runnable-track { height: 6px; border-radius: 3px; background: var(--border-lighter); }' +
'  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 24px; height: 24px; border-radius: 50%; background: #ff9200; border: 2px solid #fff; box-shadow: 0 1px 3px rgba(0,0,0,0.4); margin-top: -9px; }' +
'  input[type=range]::-moz-range-track { height: 6px; border-radius: 3px; background: var(--border-lighter); }' +
'  input[type=range]::-moz-range-thumb { width: 20px; height: 20px; border-radius: 50%; background: #ff9200; border: 2px solid #fff; }' +
'  .mark-btn-grid { display: grid; grid-template-columns: repeat(6, 1fr); gap: 4px; margin-top: 6px; }' +
'  .mark-btn { padding: 8px 0; font-size: 12px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 6px; }' +
'  .mark-btn.active { background: #ff9200; color: #fff; border-color: #ff9200; }' +
'  .preset-btn-row { display: flex; gap: 6px; margin-top: 8px; }' +
'  .preset-btn-row button { flex: 1; padding: 8px 0; font-size: 11px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 6px; }' +
'  .marker-edit-btn { width: 100%; box-sizing: border-box; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; margin-top: 8px; text-align: left; }' +
'  .marker-edit-btn:disabled { opacity: 0.45; }' +
'  .section-legend { cursor: pointer; display: flex; align-items: center; justify-content: space-between; width: 100%; box-sizing: border-box; margin: 0; padding: 6px 0; user-select: none; color: var(--text-strong); }' +
'  .chevron { display: inline-block; font-size: 24px; line-height: 1; transition: transform 0.15s; }' +
'  .chevron.open { transform: rotate(90deg); }' +
'  .slider-with-buttons { display: flex; align-items: center; gap: 8px; }' +
'  .slider-with-buttons input[type=range] { flex: 1; }' +
'  .slider-step-btn { width: 34px; height: 34px; flex-shrink: 0; border-radius: 8px; background: var(--btn-bg); border: 1px solid var(--border); font-size: 20px; font-weight: 700; color: var(--text-strong); line-height: 1; }' +
'  .grayed-out { opacity: 0.4; pointer-events: none; }' +
'  #slotPickerDiagram { position: relative; width: 240px; height: 274px; margin: 10px auto; background: linear-gradient(to bottom, #4a90d9, #bfe3f5); border-radius: 8px; overflow: hidden; }' +
'  .slot-btn { position: absolute; min-width: 54px; padding: 5px 8px; font-size: 11px; font-weight: 700; color: #222; background: rgba(255,255,255,0.85); border: 1px solid rgba(0,0,0,0.2); border-radius: 6px; text-align: center; }' +
'  .slot-btn:active { background: #fff; }' +
'  .slot-btn.slot-off { color: #777; font-weight: 400; }' +
'  .slot-btn.slot-na { color: #aaa; background: rgba(230,230,230,0.7); font-style: italic; pointer-events: none; }' +
'  .slot-corner-tl { left: 6px; top: 6px; }' +
'  .slot-corner-tr { right: 6px; top: 6px; }' +
'  .slot-corner-bl { left: 6px; bottom: 6px; }' +
'  .slot-corner-br { right: 6px; bottom: 6px; }' +
'  .slot-upper-l1 { left: 50%; top: 34px; transform: translateX(-50%); }' +
'  .slot-upper-l2 { left: 50%; top: 62px; transform: translateX(-50%); }' +
'  .slot-bottom-l1 { left: 50%; bottom: 62px; transform: translateX(-50%); }' +
'  .slot-bottom-l2 { left: 50%; bottom: 34px; transform: translateX(-50%); }' +
'  .slot-middle-left-l1 { left: 6px; top: calc(50% - 15px); transform: translateY(-50%); }' +
'  .slot-middle-left-l2 { left: 6px; top: calc(50% + 15px); transform: translateY(-50%); }' +
'  .slot-middle-right-l1 { right: 6px; top: calc(50% - 15px); transform: translateY(-50%); }' +
'  .slot-middle-right-l2 { right: 6px; top: calc(50% + 15px); transform: translateY(-50%); }' +
'  .analog-feature-panel { width: 240px; margin: 10px auto 0; padding: 12px; box-sizing: border-box; border-radius: 8px; background: var(--btn-bg); border: 1px solid var(--border); }' +
'  .analog-feature-panel-label { font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.03em; color: var(--text-faint); margin-bottom: 8px; }' +
'  .analog-feature-panel-row { display: flex; align-items: center; }' +
'  .slot-fake-clock { position: relative; width: 78px; height: 78px; flex-shrink: 0; border-radius: 50%; border: 2px solid rgba(0,0,0,0.55); background: rgba(255,255,255,0.35); }' +
'  .slot-fake-clock-hand { position: absolute; left: 50%; top: 50%; background: #222; border-radius: 2px; transform-origin: 50% 100%; }' +
'  .slot-fake-clock-hour { width: 3px; height: 20px; transform: translate(-50%, -100%) rotate(35deg); }' +
'  .slot-fake-clock-min { width: 2px; height: 28px; transform: translate(-50%, -100%) rotate(110deg); }' +
'  .slot-fake-clock-dot { position: absolute; left: 50%; top: 50%; width: 6px; height: 6px; margin: -3px 0 0 -3px; border-radius: 50%; background: #222; }' +
'  .slot-feature-col { flex: 1; min-width: 0; margin-left: 10px; display: flex; flex-direction: column; gap: 4px; }' +
'  .slot-feature-col .slot-btn { position: static; width: 100%; box-sizing: border-box; text-align: left; }' +
'</style></head>' +
'<body>' +

'<div id="topBar">' +
'  <div class="top-bar-left">' +
'    <div class="top-bar-actions">' +
'      <button type="button" class="back-btn" onclick="goBack()">&lsaquo; Back</button>' +
'      <button type="button" class="donate-btn" onclick="openDonateModal()">&#9825; Donate</button>' +
'    </div>' +
'    <div class="top-bar-title">Eclipz</div>' +
'    <div class="top-bar-desc">Configure where the eclipse geometry should be calculated for, and (optionally) a second weather source.</div>' +
'  </div>' +
'  <div class="top-bar-preview">' +
'    <canvas id="previewCanvas" width="176" height="201"></canvas>' +
'  </div>' +
'</div>' +

'<div class="modal-overlay" id="slotEditModal" onclick="if (event.target === this) closeSlotEditor();">' +
'  <div class="modal-box">' +
'    <div class="modal-title" id="slotEditTitle">Edit slot</div>' +
'    <label for="slotEditCategory">Category</label>' +
'    <select id="slotEditCategory" onchange="onSlotEditCategoryChange()"></select>' +
'    <label for="slotEditContent" style="margin-top:10px;">Content</label>' +
'    <select id="slotEditContent" onchange="onSlotEditContentChange()">' + cornerContentOptionsHtml(0) + '</select>' +
'    <div class="help" id="slotEditCategoryHelp" style="display:none;">DST is calculated for current-era US and EU rules -- Sydney and Auckland don\'t adjust for DST yet.</div>' +
'    <div class="mode-btn-group" id="slotEditColorGroup" style="margin-top:10px;">' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(0)">MONO</button>' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(1)">ACC</button>' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(2)">SEMI</button>' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(3)">COLOR</button>' +
'    </div>' +
'    <div class="help"><b>MONO</b> = your main color, <b>ACC</b> = accent color, <b>SEMI</b> = translucent accent, <b>COLOR</b> = dynamic (changes with the value shown).</div>' +
'    <button type="button" onclick="saveSlotEditor()" style="width:100%; box-sizing:border-box; padding:14px; font-size:16px; font-weight:600; color:#fff; background:#ff9200; border:none; border-radius:8px; margin-top:14px;">OK</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeSlotEditor()">Cancel</button>' +
'  </div>' +
'</div>' +

customMarkerModalHtml('hour', 'Edit hour markers', 20) +
customMarkerModalHtml('sec', 'Edit second markers', 10) +
textMarkerModalHtml(current) +

handEditorModalHtml('hour', 'Edit hour hand') +
handEditorModalHtml('min', 'Edit minute hand') +
handEditorModalHtml('sec', 'Edit second hand') +

'<div class="modal-overlay" id="donateModal">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Support this project</div>' +
'    <a class="secondary-btn" style="display:block; box-sizing:border-box; text-align:center; text-decoration:none;" href="#" onclick="return false;">Donate via PayPal</a>' +
'    <a class="secondary-btn" style="display:block; box-sizing:border-box; text-align:center; text-decoration:none; margin-top:8px;" href="#" onclick="return false;">Donate Bitcoin</a>' +
'    <div class="help" style="text-align:center; margin-top:10px;">Links coming soon.</div>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeDonateModal()">Close</button>' +
'  </div>' +
'</div>' +

// Shared by anything that needs a plain "are you sure?" step before
// acting -- Style Presets' save/apply buttons below. showConfirm()
// stashes the action to run and shows this; confirmModalYes() runs it
// (once) and closes; canceling (or tapping outside) just closes.
'<div class="modal-overlay" id="confirmModal" onclick="if (event.target === this) closeConfirmModal();">' +
'  <div class="modal-box">' +
'    <div class="modal-title" id="confirmModalTitle"></div>' +
'    <div class="help" id="confirmModalMessage" style="text-align:center;"></div>' +
'    <button type="button" class="modal-confirm-btn" onclick="confirmModalYes()">Confirm</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeConfirmModal()">Cancel</button>' +
'  </div>' +
'</div>' +

// Example styles: tapping a tile opens this instead of applying
// immediately -- image preview, bold title, description, then an
// explicit Apply button, per the same "confirm before it takes
// effect" idea as the plain confirm modal above, just with a richer
// preview since there's a real design to show off first.
'<div class="modal-overlay" id="exampleStyleModal" onclick="if (event.target === this) closeExampleStyleModal();">' +
'  <div class="modal-box">' +
'    <img class="example-style-modal-img" id="exampleStyleModalImg" src="" alt="">' +
'    <div class="example-style-modal-title" id="exampleStyleModalTitle"></div>' +
'    <div class="help" id="exampleStyleModalDesc" style="text-align:center;"></div>' +
'    <button type="button" class="modal-confirm-btn" id="exampleStyleModalApplyBtn" onclick="confirmExampleStyleApply()">Apply this style</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeExampleStyleModal()">Cancel</button>' +
'  </div>' +
'</div>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'examples\')">Example styles <span class="chevron" id="chev-examples">&#9656;</span></div>' +
'    <div class="section-body" id="section-examples" style="display:none;">' +
'    <div class="help">Tap a design below to preview it -- each one sets every Style, Colors, and Features setting to match once you confirm, the same as pasting its JSON into "Style Presets" further down.</div>' +
'    <div class="example-style-grid">' + exampleStylesButtonsHtml + '</div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'style\')">Style <span class="chevron" id="chev-style">&#9656;</span></div>' +
'    <div class="section-body" id="section-style" style="display:none;">' +

'    <label>Layout</label>' +
'    <div class="mode-btn-group" id="bottomStyleGroup">' +
'      <button type="button" class="mode-btn' + (bottomStyleVal === 'digital' ? ' active' : '') + '" onclick="selectBottomStyle(\'digital\')">DIGITAL</button>' +
'      <button type="button" class="mode-btn' + (isAnalog ? ' active' : '') + '" onclick="selectBottomStyle(\'analog\')">ANALOG</button>' +
'      <button type="button" class="mode-btn' + (isBigAnalog ? ' active' : '') + '" onclick="selectBottomStyle(\'biganalog\')">BIG ANALOG</button>' +
'    </div>' +
'    <input type="hidden" id="bottomStyleValue" value="' + esc(bottomStyleVal) + '">' +
'    <div class="help">Analog shows a clock face on the left and clouds/location/date/week on the right. Big analog fills the whole screen with fullscreen hands over the sky/eclipse view -- no bottom bar.</div>' +

'    <div id="digitalOnlySettings" class="subsection" style="' + (bottomStyleVal === 'digital' ? '' : 'display:none;') + '">' +
'      <label for="clockFont">Clock font</label>' +
'      <select id="clockFont" onchange="onFontChange()">' + fontOptions + '</select>' +
'    </div>' +

'    <div id="analogOnlySettings" class="subsection" style="' + (isAnalog ? '' : 'display:none;') + '">' +
'      <label for="analogStyle">Analog face style</label>' +
'      <select id="analogStyle" onchange="onAnalogStyleChange()">' +
'        <option value="0"' + (current.analogStyle === '0' || !current.analogStyle ? ' selected' : '') + '>Solid circle</option>' +
'        <option value="1"' + (current.analogStyle === '1' ? ' selected' : '') + '>Hour markers</option>' +
'        <option value="2"' + (current.analogStyle === '2' ? ' selected' : '') + '>Solid circle + hour markers</option>' +
'        <option value="3"' + (current.analogStyle === '3' ? ' selected' : '') + '>12 / 3 / 6 / 9 tiny numerals</option>' +
'      </select>' +
'    </div>' +

'    <div id="bigAnalogSettings" class="subsection" style="' + (isBigAnalog ? '' : 'display:none;') + '">' +
'      <label for="bigAnalogHandStyle">Hand style</label>' +
'      <select id="bigAnalogHandStyle" onchange="onHandStyleChange()">' +
'        <option value="0"' + (current.bigAnalogHandStyle === '0' || !current.bigAnalogHandStyle ? ' selected' : '') + '>Pointy (triangular)</option>' +
'        <option value="1"' + (current.bigAnalogHandStyle === '1' ? ' selected' : '') + '>Square (rectangular)</option>' +
'        <option value="2"' + (current.bigAnalogHandStyle === '2' ? ' selected' : '') + '>Modern (hollow, rounded)</option>' +
'        <option value="3"' + (current.bigAnalogHandStyle === '3' ? ' selected' : '') + '>Rounded (classic Pebble, accent hour hand)</option>' +
'        <option value="4"' + (current.bigAnalogHandStyle === '4' ? ' selected' : '') + '>Custom</option>' +
'      </select>' +
'      <div class="checkbox-row" id="bigAnalogTransparentRow" style="margin-top:12px;' + (current.bigAnalogHandStyle === '4' ? ' display:none;' : '') + '">' +
'        <input type="checkbox" id="bigAnalogTransparent" ' + (current.bigAnalogTransparent ? 'checked' : '') + ' onchange="updatePreview()">' +
'        <label for="bigAnalogTransparent" style="margin:0;">Semi-transparent hands (see the sky through them)</label>' +
'      </div>' +
'      <div class="help">To show the date behind the hands, pick "Short date" as a line in the Features section below (bottom-middle line 1 does this by default).</div>' +
'      <div class="checkbox-row" id="bigAnalogHandsShadowRow" style="margin-top:12px;' + (current.bigAnalogHandStyle === '4' ? ' display:none;' : '') + '">' +
'        <input type="checkbox" id="bigAnalogHandsShadow" ' + (current.bigAnalogHandsShadow ? 'checked' : '') + ' onchange="updatePreview()">' +
'        <label for="bigAnalogHandsShadow" style="margin:0;">Shadow</label>' +
'      </div>' +
'      <div class="help">Drop shadow behind all 3 hands, offset 2px in the angle set below -- pick your own distance per hand instead with the Custom hand style below.</div>' +

'      <label for="shadowTranslucent" style="margin-top:12px;">Shadow style</label>' +
'      <select id="shadowTranslucent" onchange="updatePreview()">' +
'        <option value="true"' + (current.shadowTranslucent !== 'false' ? ' selected' : '') + '>Translucent</option>' +
'        <option value="false"' + (current.shadowTranslucent === 'false' ? ' selected' : '') + '>Solid</option>' +
'      </select>' +
'      <div class="help">Applies to every hand\'s shadow, preset or custom -- translucent dithers to ~50% (~25% for a hand that\'s itself semi-transparent), solid is fully opaque black.</div>' +

'      <div class="slider-row">' +
'        <label for="shadowAngle">Shadow angle <span class="val" id="shadowAngleVal">' + (current.shadowAngle || '120') + '&deg;</span></label>' +
'        <div class="slider-with-buttons">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'shadowAngle\', -1)">&minus;</button>' +
'        <input type="range" id="shadowAngle" min="0" max="359" step="1" value="' + (current.shadowAngle || '120') + '" oninput="onShadowAngleInput()">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'shadowAngle\', 1)">+</button>' +
'        </div>' +
'      </div>' +
'      <div class="help">One shared light-source direction for every hand\'s shadow, preset or custom -- separate angles per hand would just be confusing since they all come from the same light.</div>' +


'      <div id="customHandSection" style="' + (current.bigAnalogHandStyle === '4' ? '' : 'display:none;') + '">' +
'        <button type="button" class="marker-edit-btn" onclick="openHandEditor(\'hour\')">Edit hour hand &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" onclick="openHandEditor(\'min\')">Edit minute hand &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" id="editSecHandBtn" ' + (secondsChecked ? '' : 'disabled') + ' onclick="openHandEditor(\'sec\')">Edit second hand &rsaquo;</button>' +
'        <div class="slider-row">' +
'          <label for="centerCircleRadius">Center circle <span class="val" id="centerCircleRadiusVal">' + esc(current.centerCircleRadius || '3') + 'px</span></label>' +
'        <div class="slider-with-buttons">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'centerCircleRadius\', -1)">&minus;</button>' +
'          <input type="range" id="centerCircleRadius" min="0" max="30" step="1" value="' + esc(current.centerCircleRadius || '3') + '" oninput="document.getElementById(\'centerCircleRadiusVal\').textContent = this.value + \'px\';">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'centerCircleRadius\', 1)">+</button>' +
'        </div>' +
'        </div>' +
'        <div class="help">0 = off.</div>' +
'        <label for="centerCircleColor">Center circle color</label>' +
'        <select id="centerCircleColor">' + schemeColorOptionsHtml(current.centerCircleColor) + '</select>' +
          handHiddenInputsHtml(current) +
'      </div>' +

'      <label for="bigAnalogMarkerStyle" style="margin-top:12px;">Hour/second marker style</label>' +
'      <select id="bigAnalogMarkerStyle" onchange="onMarkerStyleChange()">' +
'        <option value="9"' + (current.bigAnalogMarkerStyle === '9' ? ' selected' : '') + '>None</option>' +
'        <option value="0"' + (current.bigAnalogMarkerStyle === '0' || !current.bigAnalogMarkerStyle ? ' selected' : '') + '>Minimal (thin hour markers only)</option>' +
'        <option value="1"' + (current.bigAnalogMarkerStyle === '1' ? ' selected' : '') + '>Small markers (hour + second)</option>' +
'        <option value="2"' + (current.bigAnalogMarkerStyle === '2' ? ' selected' : '') + '>Big markers (thick hour, thin second)</option>' +
'        <option value="3"' + (current.bigAnalogMarkerStyle === '3' ? ' selected' : '') + '>Modern</option>' +
'        <option value="4"' + (current.bigAnalogMarkerStyle === '4' ? ' selected' : '') + '>Shadow</option>' +
'        <option value="5"' + (current.bigAnalogMarkerStyle === '5' ? ' selected' : '') + '>Tally</option>' +
'        <option value="6"' + (current.bigAnalogMarkerStyle === '6' ? ' selected' : '') + '>Bell</option>' +
'        <option value="7"' + (current.bigAnalogMarkerStyle === '7' ? ' selected' : '') + '>Brown</option>' +
'        <option value="8"' + (current.bigAnalogMarkerStyle === '8' ? ' selected' : '') + '>Custom</option>' +
'      </select>' +
'      <div class="checkbox-row" id="bitmapMarkerTransparentRow" style="margin-top:12px;' + (isBitmapMarkerStyle ? '' : ' display:none;') + '">' +
'        <input type="checkbox" id="bitmapMarkerTransparent" ' + (current.bitmapMarkerTransparent ? 'checked' : '') + ' onchange="updatePreview()">' +
'        <label for="bitmapMarkerTransparent" style="margin:0;">Semi transparent markers (see the sky through them)</label>' +
'      </div>' +
'      <div class="help">Bitmap styles are tinted with your main color (see the preview above) and their mask art shows behind the hands there once you\'ve added a resource PNG for that style. Which edge-middle info slots they support (instead of the 4 corners) varies by style -- see the Features section below.</div>' +
'      <div class="help">When an eclipse is actually happening, the Sun fills the whole screen as a background behind the hands.</div>' +

'      <div class="checkbox-row" id="drawFeaturesBeneathHandsRow" style="margin-top:12px;">' +
'        <input type="checkbox" id="drawFeaturesBeneathHands" ' + (current.drawFeaturesBeneathHands ? 'checked' : '') + ' onchange="updatePreview()">' +
'        <label for="drawFeaturesBeneathHands" style="margin:0;">Draw features beneath hands</label>' +
'      </div>' +
'      <div class="help">Corners/edges info (see the Features section below) normally draws on top of the hands -- enable this to tuck it underneath instead.</div>' +

'      <div id="customMarkerSection" style="' + (current.bigAnalogMarkerStyle === '8' ? '' : 'display:none;') + '">' +
'        <button type="button" class="marker-edit-btn" onclick="openCustomMarkerEditor(\'hour\')">Edit hour markers &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" onclick="openCustomMarkerEditor(\'sec\')">Edit second markers &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" onclick="openTextMarkerEditor()">Edit text markers &rsaquo;</button>' +
          customMarkerHiddenInputsHtml(current) +
'      </div>' +
'    </div>' +

'    <label for="skyMode" style="margin-top:12px;">Sky style</label>' +
'    <select id="skyMode" onchange="onSkyModeChange()">' +
'      <option value="0"' + (current.skyMode === '0' || !current.skyMode ? ' selected' : '') + '>Weather sky</option>' +
'      <option value="1"' + (current.skyMode === '1' ? ' selected' : '') + '>Clear sky</option>' +
'      <option value="2"' + (current.skyMode === '2' ? ' selected' : '') + '>Space view</option>' +
'    </select>' +
'    <div class="help">Weather sky shows clouds/rain/snow and the day-night gradient. Clear sky keeps the gradient but never draws weather. Space view drops the gradient entirely for a fixed dark sky, always shows the Sun/Moon/planets when above the horizon regardless of time of day, and adds a field of bright named stars (tap/shake to reveal names).</div>' +

'    <div id="cloudRenderStyleRow" style="' + (current.skyMode && current.skyMode !== '0' ? 'display:none;' : '') + '">' +
'    <label for="cloudRenderStyle" style="margin-top:12px;">Weather drawing style</label>' +
'    <select id="cloudRenderStyle">' +
'      <option value="1"' + (current.cloudRenderStyle === '1' || !current.cloudRenderStyle ? ' selected' : '') + '>Realistic</option>' +
'      <option value="0"' + (current.cloudRenderStyle === '0' ? ' selected' : '') + '>Simple (battery friendly)</option>' +
'    </select>' +
'    <div class="help">Realistic clouds are a soft painterly shape shaded by the Sun\'s actual position -- costs more battery per redraw, and adds occasional lightning during a storm. Simple uses plain circle puffs instead.</div>' +
'    </div>' +

'    <div class="subsection" id="showSunTimeSection" style="' + (bottomStyleVal === 'digital' ? '' : 'display:none;') + '">' +
'      <label>Week number or sunrise/sunset</label>' +
'      <div class="mode-btn-group" id="showSunTimeGroup">' +
'        <button type="button" class="mode-btn' + (!current.showSunTime ? ' active' : '') + '" onclick="selectSunTimeMode(false)">WEEK #</button>' +
'        <button type="button" class="mode-btn' + (current.showSunTime ? ' active' : '') + '" onclick="selectSunTimeMode(true)">SUN/SET</button>' +
'      </div>' +
'      <input type="hidden" id="showSunTime" value="' + (current.showSunTime ? 'true' : 'false') + '">' +
'      <div class="help">Falls back to the week number once today\'s sunset has passed, until the next refresh rolls over to a new day. Only applies to digital mode -- analog\'s 4 feature rows below can each independently be set to Week number or Sunrise/sunset instead.</div>' +
'    </div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="showSeconds" ' + secondsChecked + ' ' + secondsDisabled + ' onchange="onShowSecondsChange()">' +
'      <label for="showSeconds" style="margin:0;">Show seconds</label>' +
'    </div>' +
'    <div class="help" id="secondsHelp" style="' + (secondsUnsupported ? '' : 'display:none;') + '">This font is too wide to fit seconds alongside it.</div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="outlineEnabled" ' + (current.outlineEnabled !== false ? 'checked' : '') + '>' +
'      <label for="outlineEnabled" style="margin:0;">Outline text, icons, and hands for contrast</label>' +
'    </div>' +
'    <div class="help">Adds a thin outline (in your color scheme\'s background color) behind corner/edge text and icons, the big-analog date, the eclipse phase text, and the hands -- so they stay readable over any part of the sky. Icons and hands only get it outside translucent/transparent mode.</div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="startupClockAnimationEnabled" ' + (current.startupClockAnimationEnabled !== false ? 'checked' : '') + '>' +
'      <label for="startupClockAnimationEnabled" style="margin:0;">Animate clock on start</label>' +
'    </div>' +
'    <div class="help">On for launch: the hands/digits sweep in from a cold-start position up to the real time, under 1.5s, instead of just appearing already showing it.</div>' +

'    <div class="subsection">' +
'      <label>Animate background on start</label>' +
'      <div class="radio-row"><input type="radio" name="bgAnimMode" id="bgAnimMode0" value="0" ' + (current.bgAnimMode === '0' || !current.bgAnimMode ? 'checked' : '') + '><label for="bgAnimMode0" style="margin:0;">Off</label></div>' +
'      <div class="radio-row"><input type="radio" name="bgAnimMode" id="bgAnimMode1" value="1" ' + (current.bgAnimMode === '1' ? 'checked' : '') + '><label for="bgAnimMode1" style="margin:0;">Weather (clouds slide in from the sides)</label></div>' +
'      <div class="radio-row"><input type="radio" name="bgAnimMode" id="bgAnimMode2" value="2" ' + (current.bgAnimMode === '2' ? 'checked' : '') + '><label for="bgAnimMode2" style="margin:0;">Planets (Sun/Moon/planets + sky sweep in from a couple hours ago)</label></div>' +
'      <div class="radio-row"><input type="radio" name="bgAnimMode" id="bgAnimMode3" value="3" ' + (current.bgAnimMode === '3' ? 'checked' : '') + '><label for="bgAnimMode3" style="margin:0;">Markers (big-analog hour markers animate in; seconds draw normally)</label></div>' +
'    </div>' +
'    <div class="help">Off by default: exactly one of the above sweeps into place on launch, under 1.5s.</div>' +

'    <div class="subsection">' +
'      <label>Animate outlines on shake</label>' +
'      <div class="radio-row"><input type="radio" name="shakeAnimMode" id="shakeAnimMode0" value="0" ' + (current.shakeAnimMode === '0' || !current.shakeAnimMode ? 'checked' : '') + '><label for="shakeAnimMode0" style="margin:0;">Off</label></div>' +
'      <div class="radio-row"><input type="radio" name="shakeAnimMode" id="shakeAnimMode1" value="1" ' + (current.shakeAnimMode === '1' ? 'checked' : '') + '><label for="shakeAnimMode1" style="margin:0;">Gradient (outlines sweep through a rainbow)</label></div>' +
'      <div class="radio-row"><input type="radio" name="shakeAnimMode" id="shakeAnimMode2" value="2" ' + (current.shakeAnimMode === '2' ? 'checked' : '') + '><label for="shakeAnimMode2" style="margin:0;">Smooth second hand</label></div>' +
'      <div class="radio-row"><input type="radio" name="shakeAnimMode" id="shakeAnimMode3" value="3" ' + (current.shakeAnimMode === '3' ? 'checked' : '') + '><label for="shakeAnimMode3" style="margin:0;">Both</label></div>' +
'      <div class="radio-row"><input type="radio" name="shakeAnimMode" id="shakeAnimMode4" value="4" ' + (current.shakeAnimMode === '4' ? 'checked' : '') + '><label for="shakeAnimMode4" style="margin:0;">Planet seek</label></div>' +
'    </div>' +
'    <div class="help">Off by default: runs for as long as the shake labels stay up -- see "Shake-to-reveal labels stay on screen for" in the Astronomy section. "Planet seek" points the sky view at whichever 90&deg; slice of the horizon your compass currently faces, repositioning the Sun/Moon/planets to match as you turn -- weather is hidden for the duration, and it never runs on a day with an eclipse.</div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'colors\')">Colors <span class="chevron" id="chev-colors">&#9656;</span></div>' +
'    <div class="section-body" id="section-colors" style="display:none;">' +

'    <div class="subsection">' +
'      <label>Colors</label>' +
'      <div class="color-role-buttons">' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'text\')">' +
'          <span class="color-role-swatch" id="swatchMain" style="background:' + esc(initialColors.text) + ';"></span>' +
'          <span class="color-role-label">Main</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'accent\')">' +
'          <span class="color-role-swatch" id="swatchAccent" style="background:' + esc(initialColors.accent) + ';"></span>' +
'          <span class="color-role-label">Accent</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'bg\')">' +
'          <span class="color-role-swatch" id="swatchBg" style="background:' + esc(initialColors.bg) + ';"></span>' +
'          <span class="color-role-label">Background</span>' +
'        </button>' +
'      </div>' +
'      <label for="colorSchemePreset" style="margin-top:12px;">Or pick a preset</label>' +
'      <select id="colorSchemePreset" onchange="onPresetChange()">' + schemeOptionsHtml() + '</select>' +
'      <div class="help">Applies that preset\'s three colors immediately -- picking one is the same as tapping each swatch above and choosing that exact color.</div>' +
'      <input type="hidden" id="customBgValue" value="' + esc(current.customBg || '255') + '">' +
'      <input type="hidden" id="customTextValue" value="' + esc(current.customText || '192') + '">' +
'      <input type="hidden" id="customAccentValue" value="' + esc(current.customAccent || '192') + '">' +
'    </div>' +

'    <div class="modal-overlay" id="colorPickerModal">' +
'      <div class="modal-box">' +
'        <div class="modal-title" id="colorPickerTitle">Pick a color</div>' +
'        <div class="hex-grid" id="hexColorGrid"></div>' +
'        <button type="button" class="modal-cancel-btn" onclick="closeColorPicker()">Cancel</button>' +
'      </div>' +
'    </div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="nightEnabled" ' + (current.nightEnabled ? 'checked' : '') + ' onchange="onNightToggle()">' +
'      <label for="nightEnabled" style="margin:0;">Use different colors at night</label>' +
'    </div>' +
'    <div id="nightSchemeSettings" style="' + (current.nightEnabled ? '' : 'display:none;') + '">' +
'      <label>Night colors</label>' +
'      <div class="color-role-buttons">' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'text\', \'night\')">' +
'          <span class="color-role-swatch" id="swatchNightMain" style="background:' + esc(initialNightColors.text) + ';"></span>' +
'          <span class="color-role-label">Main</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'accent\', \'night\')">' +
'          <span class="color-role-swatch" id="swatchNightAccent" style="background:' + esc(initialNightColors.accent) + ';"></span>' +
'          <span class="color-role-label">Accent</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'bg\', \'night\')">' +
'          <span class="color-role-swatch" id="swatchNightBg" style="background:' + esc(initialNightColors.bg) + ';"></span>' +
'          <span class="color-role-label">Background</span>' +
'        </button>' +
'      </div>' +
'      <label for="nightSchemePreset" style="margin-top:12px;">Or pick a preset</label>' +
'      <select id="nightSchemePreset" onchange="onPresetChange(\'night\')">' + schemeOptionsHtml() + '</select>' +
'      <div class="help">Applies that preset\'s three colors immediately -- picking one is the same as tapping each swatch above and choosing that exact color.</div>' +
'      <input type="hidden" id="nightCustomBgValue" value="' + esc(current.nightCustomBg || '192') + '">' +
'      <input type="hidden" id="nightCustomTextValue" value="' + esc(current.nightCustomText || '255') + '">' +
'      <input type="hidden" id="nightCustomAccentValue" value="' + esc(current.nightCustomAccent || '255') + '">' +
'    </div>' +
'    <div class="help">Battery and Moon phase are now pickable as Features content below, with their own color style.</div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset id="cornersFieldset">' +
'    <div class="section-legend" onclick="toggleSection(\'corners\')">Features <span class="chevron" id="chev-corners">&#9656;</span></div>' +
'    <div class="section-body" id="section-corners" style="display:none;">' +
'    <div class="help">Features are small info readouts (weather, health, date/time, and more) placed around your watch face. Tap a slot on the diagram below to pick what it shows and how it\'s colored -- grayed-out slots aren\'t available for your current style.</div>' +

'    <div id="slotPickerDiagram">' +
'      <button type="button" class="slot-btn slot-corner-tl" id="slotBtn-cornerTL" onclick="openSlotEditor(\'cornerTL\')"></button>' +
'      <button type="button" class="slot-btn slot-corner-tr" id="slotBtn-cornerTR" onclick="openSlotEditor(\'cornerTR\')"></button>' +
'      <button type="button" class="slot-btn slot-corner-bl" id="slotBtn-cornerBL" onclick="openSlotEditor(\'cornerBL\')"></button>' +
'      <button type="button" class="slot-btn slot-corner-br" id="slotBtn-cornerBR" onclick="openSlotEditor(\'cornerBR\')"></button>' +
'      <button type="button" class="slot-btn slot-upper-l1" id="slotBtn-upperMiddleLine1" onclick="openSlotEditor(\'upperMiddleLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-upper-l2" id="slotBtn-upperMiddleLine2" onclick="openSlotEditor(\'upperMiddleLine2\')"></button>' +
'      <button type="button" class="slot-btn slot-bottom-l1" id="slotBtn-bottomMiddleLine1" onclick="openSlotEditor(\'bottomMiddleLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-bottom-l2" id="slotBtn-bottomMiddleLine2" onclick="openSlotEditor(\'bottomMiddleLine2\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-left-l1" id="slotBtn-middleLeftLine1" onclick="openSlotEditor(\'middleLeftLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-left-l2" id="slotBtn-middleLeftLine2" onclick="openSlotEditor(\'middleLeftLine2\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-right-l1" id="slotBtn-middleRightLine1" onclick="openSlotEditor(\'middleRightLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-right-l2" id="slotBtn-middleRightLine2" onclick="openSlotEditor(\'middleRightLine2\')"></button>' +
'    </div>' +

// Sits BELOW the sky diagram, as its own box -- not inside/overlaid
// on it -- representing the watch's separate bottom-third panel
// (clock + 4 feature rows) rather than anything drawn over the sky
// view itself. Only ever shown for the small-analog layout (see
// onBottomStyleChange()); digital and big-analog never render it.
'    <div class="analog-feature-panel" id="analogFeaturePanel" style="' + (isAnalog ? '' : 'display:none;') + '">' +
'      <div class="analog-feature-panel-label">Small-analog bottom panel</div>' +
'      <div class="analog-feature-panel-row">' +
'        <div class="slot-fake-clock" aria-hidden="true">' +
'          <div class="slot-fake-clock-hand slot-fake-clock-hour"></div>' +
'          <div class="slot-fake-clock-hand slot-fake-clock-min"></div>' +
'          <div class="slot-fake-clock-dot"></div>' +
'        </div>' +
'        <div class="slot-feature-col">' +
'          <button type="button" class="slot-btn slot-feature-btn" id="slotBtn-smallAnalogFeature1" onclick="openSlotEditor(\'smallAnalogFeature1\')"></button>' +
'          <button type="button" class="slot-btn slot-feature-btn" id="slotBtn-smallAnalogFeature2" onclick="openSlotEditor(\'smallAnalogFeature2\')"></button>' +
'          <button type="button" class="slot-btn slot-feature-btn" id="slotBtn-smallAnalogFeature3" onclick="openSlotEditor(\'smallAnalogFeature3\')"></button>' +
'          <button type="button" class="slot-btn slot-feature-btn" id="slotBtn-smallAnalogFeature4" onclick="openSlotEditor(\'smallAnalogFeature4\')"></button>' +
'        </div>' +
'      </div>' +
'    </div>' +

'    <label for="cornerCustomFont">Font</label>' +
'    <select id="cornerCustomFont" onchange="onCornerFontChange()">' +
'      <option value="0"' + (current.cornerCustomFont === '0' || !current.cornerCustomFont ? ' selected' : '') + '>Default (allows size below)</option>' +
'      <option value="1"' + (current.cornerCustomFont === '1' ? ' selected' : '') + '>Digital</option>' +
'      <option value="2"' + (current.cornerCustomFont === '2' ? ' selected' : '') + '>Minecraft</option>' +
'      <option value="3"' + (current.cornerCustomFont === '3' ? ' selected' : '') + '>Pixelate</option>' +
'      <option value="4"' + (current.cornerCustomFont === '4' ? ' selected' : '') + '>Miso</option>' +
'      <option value="5"' + (current.cornerCustomFont === '5' ? ' selected' : '') + '>Bebas</option>' +
'    </select>' +
'    <label for="cornerFontSize">Font size</label>' +
'    <select id="cornerFontSize" onchange="onCornerFontSizeChange()" ' + (current.cornerCustomFont && current.cornerCustomFont !== '0' ? 'disabled' : '') + '>' +
'      <option value="0"' + (current.cornerFontSize === '0' ? ' selected' : '') + '>S</option>' +
'      <option value="1"' + (current.cornerFontSize === '1' || !current.cornerFontSize ? ' selected' : '') + '>M</option>' +
'      <option value="2"' + (current.cornerFontSize === '2' ? ' selected' : '') + '>L</option>' +
'      <option value="3"' + (current.cornerFontSize === '3' ? ' selected' : '') + '>XL</option>' +
'      <option value="4"' + (current.cornerFontSize === '4' ? ' selected' : '') + '>XXL</option>' +
'      <option value="5"' + (current.cornerFontSize === '5' ? ' selected' : '') + '>Roboto</option>' +
'    </select>' +
'    <div class="help">Applies to corner/edge feature text and the big-analog date. A custom font has its own fixed size, so the size option above only applies to "Default".</div>' +

'    <div id="weatherIconStyleRow" style="' + (weatherIconFeatureInUse ? '' : 'display:none;') + '">' +
'      <label for="weatherIconStyle">Weather icon style</label>' +
'      <select id="weatherIconStyle">' +
'        <option value="0"' + (current.weatherIconStyle === '0' ? ' selected' : '') + '>Simple</option>' +
'        <option value="1"' + (current.weatherIconStyle === '1' || !current.weatherIconStyle ? ' selected' : '') + '>Hollow</option>' +
'        <option value="2"' + (current.weatherIconStyle === '2' ? ' selected' : '') + '>Full color</option>' +
'      </select>' +
'      <div class="help">"Simple" is a placeholder for now. Hollow follows the slot\'s own color mode like any other icon; Full color is a genuine multi-color image with its own baked-in colors (see README.md), so it ignores the slot\'s color mode entirely. Shown here because a Weather icon feature is picked below.</div>' +
'    </div>' +

'    <div style="display:none;" id="slotDataStore">' +
'      <select id="cornerTL">' + cornerContentOptionsHtml(current.cornerTL, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerTLColor" value="' + esc(current.cornerTLColor || '0') + '">' +
'      <select id="cornerTR">' + cornerContentOptionsHtml(current.cornerTR, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerTRColor" value="' + esc(current.cornerTRColor || '0') + '">' +
'      <select id="cornerBL">' + cornerContentOptionsHtml(current.cornerBL, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerBLColor" value="' + esc(current.cornerBLColor || '0') + '">' +
'      <select id="cornerBR">' + cornerContentOptionsHtml(current.cornerBR, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerBRColor" value="' + esc(current.cornerBRColor || '0') + '">' +
'      <select id="upperMiddleLine1Content">' + cornerContentOptionsHtml(current.upperMiddleLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="upperMiddleLine1Color" value="' + esc(current.upperMiddleLine1Color || '0') + '">' +
'      <select id="upperMiddleLine2Content">' + cornerContentOptionsHtml(current.upperMiddleLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="upperMiddleLine2Color" value="' + esc(current.upperMiddleLine2Color || '0') + '">' +
'      <select id="bottomMiddleLine1Content">' + cornerContentOptionsHtml(current.bottomMiddleLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="bottomMiddleLine1Color" value="' + esc(current.bottomMiddleLine1Color || '0') + '">' +
'      <select id="bottomMiddleLine2Content">' + cornerContentOptionsHtml(current.bottomMiddleLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="bottomMiddleLine2Color" value="' + esc(current.bottomMiddleLine2Color || '0') + '">' +
'      <select id="middleLeftLine1Content">' + cornerContentOptionsHtml(current.middleLeftLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleLeftLine1Color" value="' + esc(current.middleLeftLine1Color || '0') + '">' +
'      <select id="middleLeftLine2Content">' + cornerContentOptionsHtml(current.middleLeftLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleLeftLine2Color" value="' + esc(current.middleLeftLine2Color || '0') + '">' +
'      <select id="middleRightLine1Content">' + cornerContentOptionsHtml(current.middleRightLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleRightLine1Color" value="' + esc(current.middleRightLine1Color || '0') + '">' +
'      <select id="middleRightLine2Content">' + cornerContentOptionsHtml(current.middleRightLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleRightLine2Color" value="' + esc(current.middleRightLine2Color || '0') + '">' +
'    </div>' +

'    <div class="subsection">' +
'      <label for="stepGoal">Daily step goal (used by "Step goal %")</label>' +
'      <input type="number" id="stepGoal" min="1000" max="60000" step="500" value="' + esc(current.stepGoal || '10000') + '">' +
'      <div class="help">Pebble doesn\'t expose a system step goal, so this app keeps its own -- same as every other Pebble health app.</div>' +
'    </div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'presets\')">Style Presets <span class="chevron" id="chev-presets">&#9656;</span></div>' +
'    <div class="section-body" id="section-presets" style="display:none;">' +
'    <div class="help">Save up to 3 quick-recall snapshots of your whole Style + Colors + Features design below, or export/import it as JSON to back it up or share it.</div>' +

'    <div class="preset-slot-row">' +
'      <button type="button" class="preset-apply-btn" id="presetApplyBtn1" onclick="applyPresetSlot(1)" ' + (current.presetSlot1Json ? '' : 'disabled') + '>' + esc(current.presetSlot1Name || "Preset 1") + '</button>' +
'      <input type="text" class="preset-name-input" id="presetNameInput1" style="display:none;" onblur="commitRenamePresetSlot(1)" onkeydown="if (event.key === \'Enter\') this.blur();">' +
'      <button type="button" class="preset-icon-btn" onclick="savePresetSlot(1)" title="Save current design here">&#128190;</button>' +
'      <button type="button" class="preset-icon-btn" onclick="startRenamePresetSlot(1)" title="Rename">&#9998;</button>' +
'    </div>' +
'    <input type="hidden" id="presetSlot1Name" value="' + esc(current.presetSlot1Name || "Preset 1") + '">' +
'    <input type="hidden" id="presetSlot1Json" value="' + esc(current.presetSlot1Json || "") + '">' +
'    <div class="preset-slot-row">' +
'      <button type="button" class="preset-apply-btn" id="presetApplyBtn2" onclick="applyPresetSlot(2)" ' + (current.presetSlot2Json ? '' : 'disabled') + '>' + esc(current.presetSlot2Name || "Preset 2") + '</button>' +
'      <input type="text" class="preset-name-input" id="presetNameInput2" style="display:none;" onblur="commitRenamePresetSlot(2)" onkeydown="if (event.key === \'Enter\') this.blur();">' +
'      <button type="button" class="preset-icon-btn" onclick="savePresetSlot(2)" title="Save current design here">&#128190;</button>' +
'      <button type="button" class="preset-icon-btn" onclick="startRenamePresetSlot(2)" title="Rename">&#9998;</button>' +
'    </div>' +
'    <input type="hidden" id="presetSlot2Name" value="' + esc(current.presetSlot2Name || "Preset 2") + '">' +
'    <input type="hidden" id="presetSlot2Json" value="' + esc(current.presetSlot2Json || "") + '">' +
'    <div class="preset-slot-row">' +
'      <button type="button" class="preset-apply-btn" id="presetApplyBtn3" onclick="applyPresetSlot(3)" ' + (current.presetSlot3Json ? '' : 'disabled') + '>' + esc(current.presetSlot3Name || "Preset 3") + '</button>' +
'      <input type="text" class="preset-name-input" id="presetNameInput3" style="display:none;" onblur="commitRenamePresetSlot(3)" onkeydown="if (event.key === \'Enter\') this.blur();">' +
'      <button type="button" class="preset-icon-btn" onclick="savePresetSlot(3)" title="Save current design here">&#128190;</button>' +
'      <button type="button" class="preset-icon-btn" onclick="startRenamePresetSlot(3)" title="Rename">&#9998;</button>' +
'    </div>' +
'    <input type="hidden" id="presetSlot3Name" value="' + esc(current.presetSlot3Name || "Preset 3") + '">' +
'    <input type="hidden" id="presetSlot3Json" value="' + esc(current.presetSlot3Json || "") + '">' +

'    <label for="presetExportBox" style="margin-top:12px;">Export current design</label>' +
'    <button type="button" class="secondary-btn" onclick="exportDesignJson()">Generate JSON</button>' +
'    <button type="button" class="secondary-btn" onclick="copyExportBoxToClipboard()">Copy to clipboard</button>' +
'    <textarea id="presetExportBox" readonly rows="6" style="margin-top:8px; font-family:monospace; font-size:11px;" onclick="this.select();"></textarea>' +
'    <div class="help" id="presetExportStatus">Tap the box above then copy the text -- covers everything in the Style, Colors, and Features sections.</div>' +

'    <label for="presetImportBox" style="margin-top:12px;">Import a design</label>' +
'    <button type="button" class="secondary-btn" onclick="pasteImportBoxFromClipboard()">Paste from clipboard</button>' +
'    <textarea id="presetImportBox" rows="6" placeholder="Paste JSON here" style="margin-top:8px; font-family:monospace; font-size:11px;"></textarea>' +
'    <button type="button" class="secondary-btn" onclick="importDesignJson()">Apply</button>' +
'    <div class="help" id="presetImportStatus"></div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'weather\')">Weather <span class="chevron" id="chev-weather">&#9656;</span></div>' +
'    <div class="section-body" id="section-weather" style="display:none;">' +
'    <div class="help">Cloud cover is always pulled from Open-Meteo (no signup needed). Optionally add an OpenWeatherMap API key to average in a second forecast.</div>' +
'    <label for="owmKey">OpenWeatherMap API key (optional)</label>' +
'    <input type="text" id="owmKey" placeholder="leave blank to skip" value="' + esc(current.owmKey) + '">' +

'    <label for="tempUnit" style="margin-top:10px;">Temperature unit</label>' +
'    <select id="tempUnit">' +
'      <option value="C"' + (current.tempUnit === 'C' || !current.tempUnit ? ' selected' : '') + '>Celsius</option>' +
'      <option value="F"' + (current.tempUnit === 'F' ? ' selected' : '') + '>Fahrenheit</option>' +
'      <option value="K"' + (current.tempUnit === 'K' ? ' selected' : '') + '>Kelvin</option>' +
'    </select>' +
'    <div class="help">Used everywhere temperature is shown, including the Features section below.</div>' +

'    <label for="windSpeedUnit" style="margin-top:10px;">Wind speed unit</label>' +
'    <select id="windSpeedUnit">' +
'      <option value="kmh"' + (current.windSpeedUnit === 'kmh' || !current.windSpeedUnit ? ' selected' : '') + '>km/h</option>' +
'      <option value="mph"' + (current.windSpeedUnit === 'mph' ? ' selected' : '') + '>mph</option>' +
'      <option value="ms"' + (current.windSpeedUnit === 'ms' ? ' selected' : '') + '>m/s</option>' +
'      <option value="kn"' + (current.windSpeedUnit === 'kn' ? ' selected' : '') + '>knots</option>' +
'    </select>' +
'    <div class="help">Used by the "Wind" corner content.</div>' +

'    <label for="aqiUnit" style="margin-top:10px;">Air quality index scale</label>' +
'    <select id="aqiUnit">' +
'      <option value="0"' + (current.aqiUnit === '0' || !current.aqiUnit ? ' selected' : '') + '>US AQI (EPA, 0-500)</option>' +
'      <option value="1"' + (current.aqiUnit === '1' ? ' selected' : '') + '>European AQI (0-100+)</option>' +
'    </select>' +
'    <div class="help">Used by the "Air quality" corner content.</div>' +

'    <label for="altitudeUnit" style="margin-top:10px;">Altitude unit</label>' +
'    <select id="altitudeUnit">' +
'      <option value="0"' + (current.altitudeUnit === '0' || !current.altitudeUnit ? ' selected' : '') + '>Meters</option>' +
'      <option value="1"' + (current.altitudeUnit === '1' ? ' selected' : '') + '>Feet</option>' +
'    </select>' +
'    <div class="help">Used by the "Altitude" corner content. Comes from GPS, so it needs "Use GPS automatically" turned on in Location below, and not every phone reports it -- shows "N/A" when it\'s not available.</div>' +

'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'astronomy\')">Astronomy <span class="chevron" id="chev-astronomy">&#9656;</span></div>' +
'    <div class="section-body" id="section-astronomy" style="display:none;">' +

'    <label for="sunMoonSize">Sun &amp; Moon size</label>' +
'    <select id="sunMoonSize">' +
'      <option value="100"' + (current.sunMoonSize === '100' ? ' selected' : '') + '>Large</option>' +
'      <option value="75"' + (current.sunMoonSize === '75' || !current.sunMoonSize ? ' selected' : '') + '>Medium</option>' +
'      <option value="50"' + (current.sunMoonSize === '50' ? ' selected' : '') + '>Small</option>' +
'      <option value="25"' + (current.sunMoonSize === '25' ? ' selected' : '') + '>Extra small</option>' +
'    </select>' +
'    <div class="help">Ignored during an actual eclipse, which sizes the Sun and Moon by their real geometry instead.</div>' +

'    <div class="subsection">' +
'      <label for="shakeLabelSeconds">Shake-to-reveal labels stay on screen for</label>' +
'      <input type="number" id="shakeLabelSeconds" min="1" max="10" step="1" value="' + esc(current.shakeLabelSeconds || '3') + '"> seconds' +
'    </div>' +

'    <label for="labelStyle">Label style</label>' +
'    <select id="labelStyle">' +
'      <option value="0"' + (current.labelStyle === '0' || !current.labelStyle ? ' selected' : '') + '>Boxed</option>' +
'      <option value="1"' + (current.labelStyle === '1' ? ' selected' : '') + '>Outlined</option>' +
'      <option value="2"' + (current.labelStyle === '2' ? ' selected' : '') + '>Soft</option>' +
'    </select>' +
'    <div class="help">Boxed is an opaque rounded box with white text (the original look). Outlined uses your main color with a contrasting outline. Soft is plain light-gray text with no background or outline.</div>' +

'    <div class="subsection">' +
'      <label for="bottomInfoBarMode">Clouds/visibility/location bar (bottom of sky view)</label>' +
'      <select id="bottomInfoBarMode">' +
'        <option value="0"' + (current.bottomInfoBarMode === '0' ? ' selected' : '') + '>Off</option>' +
'        <option value="1"' + (current.bottomInfoBarMode === '1' || !current.bottomInfoBarMode ? ' selected' : '') + '>On shake (with the name labels above)</option>' +
'        <option value="2"' + (current.bottomInfoBarMode === '2' ? ' selected' : '') + '>Permanent</option>' +
'      </select>' +
'      <div class="help">Permanent shifts the sky view up 20px to make room, rather than the bar overlapping it. Not shown in analog mode, which already has this in its persistent info panel.</div>' +
'    </div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="showIss" ' + (current.showIss ? 'checked' : '') + '>' +
'      <label for="showIss" style="margin:0;">Show the ISS when overhead (experimental)</label>' +
'    </div>' +
'    <div class="help">Fetches live orbital data each refresh. Position is a snapshot, not continuously tracked, and doesn\'t account for the station being in Earth\'s shadow -- it can occasionally show when it wouldn\'t really be visible.</div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="auroraEnabled" ' + (current.auroraEnabled ? 'checked' : '') + ' onchange="onAuroraEnabledChange()">' +
'      <label for="auroraEnabled" style="margin:0;">Show auroras (experimental)</label>' +
'    </div>' +
'    <div class="help">Fetches NOAA\'s current planetary Kp index each refresh and estimates whether it\'s bright enough to reach your latitude -- a rough approximation (real aurora visibility also depends on local weather/light pollution), not a precise forecast. When on, an "Aurora Kp index" option becomes available in the Features section below, and the sky itself paints a faint aurora glow when conditions and darkness line up. Turning this off removes that option from every feature slot it might currently be set to.</div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="vibrateOnPhaseChange" ' + (current.vibrateOnPhaseChange ? 'checked' : '') + '>' +
'      <label for="vibrateOnPhaseChange" style="margin:0;">Vibrate when the eclipse reaches its next phase</label>' +
'    </div>' +
'    <div class="help">A brief double buzz right as C1/C2/C3/C4 happens -- not on ordinary day-to-day changes.</div>' +

'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'location\')">Location <span class="chevron" id="chev-location">&#9656;</span></div>' +
'    <div class="section-body" id="section-location" style="display:none;">' +
'    <div class="checkbox-row">' +
'      <input type="checkbox" id="autoLoc" ' + autoLocChecked + ' onchange="toggleManual()">' +
'      <label for="autoLoc" style="margin:0;">Use phone GPS automatically</label>' +
'    </div>' +
'    <label for="locationSearch">Search for a place</label>' +
'    <div style="display:flex; gap:6px;">' +
'      <input type="text" id="locationSearch" style="flex:1;" placeholder="e.g. Innsbruck, Austria" ' + manualDisabled + '>' +
'      <button type="button" id="locationSearchBtn" class="secondary-btn" style="width:auto; margin-top:0; padding:8px 14px;" onclick="searchLocation()" ' + manualDisabled + '>Find</button>' +
'    </div>' +
'    <div class="help" id="locationSearchStatus"></div>' +
'    <label for="lat">Manual latitude (decimal degrees)</label>' +
'    <input type="number" step="any" id="lat" ' + manualDisabled + ' placeholder="e.g. 40.7128" value="' + esc(current.lat) + '">' +
'    <label for="lon">Manual longitude (decimal degrees)</label>' +
'    <input type="number" step="any" id="lon" ' + manualDisabled + ' placeholder="e.g. -74.0060" value="' + esc(current.lon) + '">' +
'    <div class="help">Only used when GPS is turned off above.</div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'updates\')">Updates <span class="chevron" id="chev-updates">&#9656;</span></div>' +
'    <div class="section-body" id="section-updates" style="display:none;">' +
'    <label for="updateMins">Refresh interval (minutes, 5-60)</label>' +
'    <input type="number" id="updateMins" min="5" max="60" step="5" value="' + esc(current.updateMins) + '">' +
'    <div class="help">The watch won\'t re-fetch more often than this unless your location changes by more than ~10km.</div>' +
'    <button type="button" class="secondary-btn" onclick="save(true)">Force refresh now</button>' +
'    <button type="button" class="secondary-btn" onclick="save(true, true)">Force full refresh</button>' +
'    <div class="help">"Force refresh now" fetches fresh data on the same terms as a normal refresh. "Force full refresh" forces a complete resend of every field in one message (not just whatever changed), and saves a copy of it in the Testing section below as "Last Full Refresh Raw Data" -- useful for confirming a full resync actually works, e.g. after a watch app update.</div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
'    <div class="section-legend" onclick="toggleSection(\'testing\')">Testing <span class="chevron" id="chev-testing">&#9656;</span></div>' +
'    <div class="section-body" id="section-testing" style="display:none;">' +
'    <div class="checkbox-row">' +
'      <input type="checkbox" id="testMode" ' + testModeChecked + ' onchange="toggleTestMode()">' +
'      <label for="testMode" style="margin:0;">Use a custom test date/time</label>' +
'    </div>' +
'    <label for="testDateTime">Test date &amp; time</label>' +
'    <input type="datetime-local" id="testDateTime" ' + testDisabled + ' value="' + esc(current.testDateTime) + '">' +
'    <div class="help">Overrides "now" for the eclipse calculation only (e.g. a known historical/future eclipse date), so you can preview the watchface without waiting for one. Set your watch\'s own clock to this same date/time too, so the countdown on-screen lines up with the data sent over.</div>' +

'    <div class="subsection">' +
'      <label for="debugData">Raw data sent to watch (editable)</label>' +
'      <textarea id="debugData" rows="12" style="width:100%; box-sizing:border-box; font-family:monospace; font-size:11px;">' + esc(debugTextareaInitial) + '</textarea>' +
'      <button type="button" class="secondary-btn" style="width:auto; margin-top:6px; padding:6px 12px;" onclick="reloadDebugData()">Reload last sent data</button>' +
'      <button type="button" class="secondary-btn" id="copyDebugDataBtn" style="width:auto; margin-top:6px; margin-left:6px; padding:6px 12px;" onclick="copyDebugData()">Copy</button>' +
'      <div class="checkbox-row" style="margin-top:8px;">' +
'        <input type="checkbox" id="debugOverrideEnabled" ' + (current.debugOverrideEnabled ? 'checked' : '') + '>' +
'        <label for="debugOverrideEnabled" style="margin:0;">Override data sent to watch with the text above</label>' +
'      </div>' +
'      <div class="help">Shows the full JSON payload the app last computed and sent to the watch (weather, location, eclipse timing, every setting). Edit it freely; enabling the checkbox sends exactly this text instead of the normally-computed data on every future refresh, useful for testing specific values without needing real conditions to match. Invalid JSON is ignored and the app falls back to normal data rather than failing to send anything.</div>' +
'    </div>' +

'    <div class="subsection">' +
'      <label for="lastFullRefreshData">Last Full Refresh Raw Data</label>' +
'      <textarea id="lastFullRefreshData" readonly rows="12" style="width:100%; box-sizing:border-box; font-family:monospace; font-size:11px;">' + esc(current.lastFullRefreshData || '') + '</textarea>' +
'      <button type="button" class="secondary-btn" id="copyFullRefreshDataBtn" style="width:auto; margin-top:6px; padding:6px 12px;" onclick="copyFullRefreshData()">Copy</button>' +
'      <div class="help">Only set by the "Force full refresh" button in the Updates section above -- a snapshot of that specific complete resend, kept separate from the general "last sent data" above it (which reflects whatever was sent most recently, of any kind). Empty until you\'ve used that button at least once.</div>' +
'    </div>' +
'    </div>' +
'  </fieldset>' +

'  <div class="save-bar"><button onclick="save()">Save</button></div>' +

'<script>' +
'var MARKER_PREVIEW_IMAGES = ' + JSON.stringify(MARKER_PREVIEW_IMAGES) + ';' +
// EXAMPLE_STYLE_PRESETS is { title, description, preset } per slot
// (or null for an empty one) -- the popup below reads title/
// description directly, and applies `.preset` (the same shape
// applyStyleCornersJson() everywhere else already expects) only once
// the user actually taps "Apply this style". EXAMPLE_STYLE_IMAGES is
// needed client-side too now, for the popup's own <img> -- each
// button's own <img src> above is a separate, already-baked-in copy.
'var EXAMPLE_STYLE_PRESETS = ' + JSON.stringify(EXAMPLE_STYLE_PRESETS) + ';' +
'var EXAMPLE_STYLE_IMAGES = ' + JSON.stringify(EXAMPLE_STYLE_IMAGES) + ';' +
'var s_exampleStyleModalSlot = null;' +
'function openExampleStyleModal(n) {' +
'  var entry = EXAMPLE_STYLE_PRESETS[String(n)];' +
'  if (!entry) return;' +
'  s_exampleStyleModalSlot = n;' +
'  var img = document.getElementById("exampleStyleModalImg");' +
'  var src = EXAMPLE_STYLE_IMAGES[String(n)];' +
'  img.style.display = src ? "" : "none";' +
'  img.src = src || "";' +
'  document.getElementById("exampleStyleModalTitle").textContent = entry.title || ("Example " + n);' +
'  document.getElementById("exampleStyleModalDesc").textContent = entry.description || "";' +
'  document.getElementById("exampleStyleModal").className = "modal-overlay open";' +
'}' +
'function closeExampleStyleModal() {' +
'  document.getElementById("exampleStyleModal").className = "modal-overlay";' +
'  s_exampleStyleModalSlot = null;' +
'}' +
'function confirmExampleStyleApply() {' +
'  var entry = s_exampleStyleModalSlot != null ? EXAMPLE_STYLE_PRESETS[String(s_exampleStyleModalSlot)] : null;' +
'  closeExampleStyleModal();' +
'  if (entry && entry.preset) applyStyleCornersJson(entry.preset);' +
'}' +
'function toggleManual() {' +
'  var on = !document.getElementById("autoLoc").checked;' +
'  document.getElementById("lat").disabled = !on;' +
'  document.getElementById("lon").disabled = !on;' +
'  document.getElementById("locationSearch").disabled = !on;' +
'  document.getElementById("locationSearchBtn").disabled = !on;' +
'}' +
'function toggleTestMode() {' +
'  document.getElementById("testDateTime").disabled = !document.getElementById("testMode").checked;' +
'}' +
'function reloadDebugData() {' +
'  document.getElementById("debugData").value = ' + JSON.stringify(current.lastSentData || '') + ';' +
'}' +
'function copyDebugData() {' +
'  var ta = document.getElementById("debugData");' +
'  ta.focus();' +
'  ta.select();' +
'  ta.setSelectionRange(0, 999999);' +
'  var ok = false;' +
'  try { ok = document.execCommand("copy"); } catch (e) {}' +
'  var btn = document.getElementById("copyDebugDataBtn");' +
'  if (btn) {' +
'    var original = btn.textContent;' +
'    btn.textContent = ok ? "Copied!" : "Copy failed";' +
'    setTimeout(function () { btn.textContent = original; }, 1500);' +
'  }' +
'}' +
'function copyFullRefreshData() {' +
'  var ta = document.getElementById("lastFullRefreshData");' +
'  ta.focus();' +
'  ta.select();' +
'  ta.setSelectionRange(0, 999999);' +
'  var ok = false;' +
'  try { ok = document.execCommand("copy"); } catch (e) {}' +
'  var btn = document.getElementById("copyFullRefreshDataBtn");' +
'  if (btn) {' +
'    var original = btn.textContent;' +
'    btn.textContent = ok ? "Copied!" : "Copy failed";' +
'    setTimeout(function () { btn.textContent = original; }, 1500);' +
'  }' +
'}' +
'function searchLocation() {' +
'  var query = document.getElementById("locationSearch").value;' +
'  query = query ? query.trim() : "";' +
'  if (!query) return;' +
'  var statusEl = document.getElementById("locationSearchStatus");' +
'  statusEl.textContent = "Searching...";' +
'  var xhr = new XMLHttpRequest();' +
'  xhr.open("GET", "https://nominatim.openstreetmap.org/search?format=json&limit=1&q=" + encodeURIComponent(query), true);' +
'  xhr.timeout = 8000;' +
'  xhr.onload = function () {' +
'    try {' +
'      var results = JSON.parse(xhr.responseText);' +
'      if (results && results.length > 0) {' +
'        document.getElementById("lat").value = parseFloat(results[0].lat).toFixed(5);' +
'        document.getElementById("lon").value = parseFloat(results[0].lon).toFixed(5);' +
'        statusEl.textContent = "Found: " + (results[0].display_name || query);' +
'      } else {' +
'        statusEl.textContent = "No results found.";' +
'      }' +
'    } catch (e) {' +
'      statusEl.textContent = "Search failed.";' +
'    }' +
'  };' +
'  xhr.onerror = function () { statusEl.textContent = "Network error."; };' +
'  xhr.ontimeout = function () { statusEl.textContent = "Timed out."; };' +
'  xhr.send();' +
'}' +

'function packedByteFor(r2,g2,b2) { return 0xC0 | (r2<<4) | (g2<<2) | b2; }' +
'function chHex(v) { var h = (v*85).toString(16); return h.length<2 ? "0"+h : h; }' +
'function hexFor(r2,g2,b2) { return "#" + chHex(r2) + chHex(g2) + chHex(b2); }' +
'function hexFromByte(byte) { return hexFor((byte>>4)&3, (byte>>2)&3, byte&3); }' +


'function findPresetById(id) {' +
'  var presets = ' + JSON.stringify(COLOR_SCHEMES) + ';' +
'  for (var i = 0; i < presets.length; i++) {' +
'    if (String(presets[i].id) === String(id)) return presets[i];' +
'  }' +
'  return presets[0];' +
'}' +

'function colorsFor(bgId, textId, accentId) {' +
'  return {' +
'    bg: hexFromByte(parseInt(document.getElementById(bgId).value, 10)),' +
'    text: hexFromByte(parseInt(document.getElementById(textId).value, 10)),' +
'    accent: hexFromByte(parseInt(document.getElementById(accentId).value, 10))' +
'  };' +
'}' +
'function dayColors() {' +
'  return colorsFor("customBgValue", "customTextValue", "customAccentValue");' +
'}' +
'function nightColors() {' +
'  return colorsFor("nightCustomBgValue", "nightCustomTextValue", "nightCustomAccentValue");' +
'}' +

'function canvasFontFor(previewCss, px) {' +
'  var familyMatch = /font-family:\\s*([^;]+);?/.exec(previewCss);' +
'  var family = familyMatch ? familyMatch[1] : "sans-serif";' +
'  var weightMatch = /font-weight:\\s*([^;]+);?/.exec(previewCss);' +
'  var weight = weightMatch ? weightMatch[1].trim() : "400";' +
'  var boldPrefix = (parseInt(weight, 10) >= 600 || weight === "bold") ? "bold " : "";' +
'  return boldPrefix + px + "px " + family;' +
'}' +

'function drawSkyLayer(ctx, x, y, w, h) {' +
'  var grad = ctx.createLinearGradient(0, y, 0, y + h);' +
'  grad.addColorStop(0, "#4a90d9");' +
'  grad.addColorStop(1, "#bfe3f5");' +
'  ctx.fillStyle = grad;' +
'  ctx.fillRect(x, y, w, h);' +
'  ctx.beginPath();' +
'  ctx.arc(x + w * 0.72, y + h * 0.2, Math.max(7, w * 0.09), 0, 2 * Math.PI);' +
'  ctx.fillStyle = "#fff6d0";' +
'  ctx.fill();' +
'  ctx.fillStyle = "rgba(255,255,255,0.9)";' +
'  function puff(cx, cy, r) { ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.fill(); }' +
'  var cy = y + h * 0.6;' +
'  puff(x + w * 0.22, cy, w * 0.08);' +
'  puff(x + w * 0.32, cy - 3, w * 0.1);' +
'  puff(x + w * 0.42, cy, w * 0.07);' +
'}' +

// Rough sample values matching what each corner content type would
// actually show on the watch, purely for preview purposes -- these
// aren't live health/weather data, just illustrative placeholders.
'var CORNER_PREVIEW_LABELS = {' +
'  1: "72", 2: "5234", 3: "68%", 4: "H72 L58", 5: "68F Clear",' +
'  6: "UV5", 7: "R20%", 8: "H45%", 9: "W12", 10: "82%", 11: "Full", 12: "Mon 15",' +
'  13: "Innsbruck", 14: "80%", 15: "45%", 16: "19:42", 17: "LOGO", 18: "12:34", 19: "WK 34", 20: "Connected",' +
'  21: "SEP 11", 22: "11", 23: "MON", 24: "Monday", 25: "SEP", 26: "September", 27: "11/9", 28: "9/11", 29: "24/9/2026", 30: "9/24/26",' +
'  31: "(cloud)", 32: "20C", 34: "1013 hPa", 35: "NW", 36: "AQI 42", 37: "12C", 38: "380m",' +
'  39: "7h 32m", 40: "2h 15m", 41: "42%", 42: "23:45", 43: "07:20",' +
'  44: "LON 12:34", 45: "PAR 13:34", 46: "CAI 14:34", 47: "MOW 15:34", 48: "DXB 16:34", 49: "DEL 18:04",' +
'  50: "DAC 18:34", 51: "BKK 19:34", 52: "BJS 20:34", 53: "TOK 21:34", 54: "SYD 22:34", 55: "AKL 00:34",' +
'  56: "NYC 07:34", 57: "CHI 06:34", 58: "DEN 05:34", 59: "LAX 04:34", 60: "ANC 03:34", 61: "HNL 02:34", 62: "SAO 09:34",' +
'  63: "14:32:07", 64: "07", 65: "7", 66: "7", 67: "5", 68: "05", 69: "8", 70: "08", 71: "3", 72: "8",' +
'  73: "22C", 74: "H 28C", 75: "L 11C", 76: "22 H28 L11C", 77: "FL 20C", 78: "(bt)",' +
'  79: "3 planets", 80: "Perseids", 81: "Rings 12%", 82: "VEN 18:32", 83: "22:47", 84: "Kp 4.3"' +
'};' +

'function hasPreviewContent(contentId) {' +
'  var el = document.getElementById(contentId);' +
'  if (!el) return false;' +
'  var v = parseInt(el.value, 10);' +
'  return v !== 0 && !!CORNER_PREVIEW_LABELS[v];' +
'}' +

'function slotAvailable(wrapId) {' +
'  var avail = computeSlotAvailability();' +
'  switch (wrapId) {' +
'    case "cornerTLWrap": case "cornerTRWrap": case "cornerBLWrap": case "cornerBRWrap":' +
'      return !avail.cornersGrayed;' +
'    case "upperMiddleWrap": return avail.upper;' +
'    case "bottomMiddleWrap": return avail.bottom;' +
'    case "middleLeftWrap": return avail.left;' +
'    case "middleRightWrap": return avail.right;' +
'    default: return false;' +
'  }' +
'}' +

'function drawCornerSlot(ctx, contentId, colorId, x, y, textAlign, colors) {' +
'  var contentEl = document.getElementById(contentId);' +
'  var colorEl = document.getElementById(colorId);' +
'  if (!contentEl || !colorEl) return;' +
'  var content = parseInt(contentEl.value, 10);' +
'  var label = CORNER_PREVIEW_LABELS[content];' +
'  if (!label) return;' +
'  var mode = parseInt(colorEl.value, 10);' +
'  var color = colors.text, alpha = 1;' +
'  if (mode === 1) { color = colors.accent; }' +
'  else if (mode === 2) { color = colors.accent; alpha = 0.55; }' +
'  else if (mode === 3) { color = "#4caf50"; }' +
'  ctx.font = "bold 9px sans-serif";' +
'  ctx.textAlign = textAlign;' +
'  ctx.textBaseline = "top";' +
'  ctx.globalAlpha = alpha;' +
'  ctx.fillStyle = color;' +
'  ctx.fillText(label, x, y);' +
'  ctx.globalAlpha = 1;' +
'}' +

'function drawCornersAndEdges(ctx, w, h, colors, skyBottom) {' +
'  var bottomY = (typeof skyBottom === "number") ? skyBottom : h;' +
'  var lineH = 14;' +
'  if (slotAvailable("cornerTLWrap")) drawCornerSlot(ctx, "cornerTL", "cornerTLColor", 5, 5, "left", colors);' +
'  if (slotAvailable("cornerTRWrap")) drawCornerSlot(ctx, "cornerTR", "cornerTRColor", w - 5, 5, "right", colors);' +
'  if (slotAvailable("cornerBLWrap")) drawCornerSlot(ctx, "cornerBL", "cornerBLColor", 5, bottomY - 14, "left", colors);' +
'  if (slotAvailable("cornerBRWrap")) drawCornerSlot(ctx, "cornerBR", "cornerBRColor", w - 5, bottomY - 14, "right", colors);' +
'  if (slotAvailable("upperMiddleWrap")) {' +
'    var upperHasLine2 = hasPreviewContent("upperMiddleLine2Content");' +
'    drawCornerSlot(ctx, "upperMiddleLine1Content", "upperMiddleLine1Color", w / 2, upperHasLine2 ? 26 : 26 + lineH / 2, "center", colors);' +
'    if (upperHasLine2) drawCornerSlot(ctx, "upperMiddleLine2Content", "upperMiddleLine2Color", w / 2, 26 + lineH, "center", colors);' +
'  }' +
'  if (slotAvailable("bottomMiddleWrap")) {' +
'    var bottomHasLine2 = hasPreviewContent("bottomMiddleLine2Content");' +
'    drawCornerSlot(ctx, "bottomMiddleLine1Content", "bottomMiddleLine1Color", w / 2, bottomHasLine2 ? bottomY - 24 - lineH : bottomY - 24 - lineH / 2, "center", colors);' +
'    if (bottomHasLine2) drawCornerSlot(ctx, "bottomMiddleLine2Content", "bottomMiddleLine2Color", w / 2, bottomY - 24, "center", colors);' +
'  }' +
'  if (slotAvailable("middleLeftWrap")) {' +
'    var midLeftHasLine2 = hasPreviewContent("middleLeftLine2Content");' +
'    drawCornerSlot(ctx, "middleLeftLine1Content", "middleLeftLine1Color", 5, midLeftHasLine2 ? h / 2 - 4 - lineH / 2 : h / 2 - 4, "left", colors);' +
'    if (midLeftHasLine2) drawCornerSlot(ctx, "middleLeftLine2Content", "middleLeftLine2Color", 5, h / 2 - 4 + lineH / 2, "left", colors);' +
'  }' +
'  if (slotAvailable("middleRightWrap")) {' +
'    var midRightHasLine2 = hasPreviewContent("middleRightLine2Content");' +
'    drawCornerSlot(ctx, "middleRightLine1Content", "middleRightLine1Color", w - 5, midRightHasLine2 ? h / 2 - 4 - lineH / 2 : h / 2 - 4, "right", colors);' +
'    if (midRightHasLine2) drawCornerSlot(ctx, "middleRightLine2Content", "middleRightLine2Color", w - 5, h / 2 - 4 + lineH / 2, "right", colors);' +
'  }' +
'}' +

'function drawAnalogPreview(ctx, colors, now, showSeconds, w, panelTop, panelBottom) {' +
'  var panelH = panelBottom - panelTop;' +
'  var cx = panelH * 0.58, cy = panelTop + panelH / 2, r = panelH * 0.4;' +
'  var style = parseInt(document.getElementById("analogStyle").value, 10);' +
'  if (style === 0 || style === 2) {' +
'    ctx.strokeStyle = colors.text; ctx.lineWidth = 2;' +
'    ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.stroke();' +
'  }' +
'  if (style === 1 || style === 2) {' +
'    for (var hIdx = 0; hIdx < 12; hIdx++) {' +
'      var ang = hIdx * Math.PI / 6;' +
'      var outer = r, inner = (hIdx % 3 === 0) ? r - 6 : r - 3;' +
'      ctx.strokeStyle = colors.text; ctx.lineWidth = 1;' +
'      ctx.beginPath();' +
'      ctx.moveTo(cx + outer * Math.sin(ang), cy - outer * Math.cos(ang));' +
'      ctx.lineTo(cx + inner * Math.sin(ang), cy - inner * Math.cos(ang));' +
'      ctx.stroke();' +
'    }' +
'  }' +
'  if (style === 3) {' +
'    ctx.font = "bold 9px monospace";' +
'    ctx.fillStyle = colors.text;' +
'    ctx.textAlign = "center"; ctx.textBaseline = "middle";' +
'    ctx.fillText("12", cx, cy - r + 8);' +
'    ctx.fillText("3", cx + r - 7, cy);' +
'    ctx.fillText("6", cx, cy + r - 8);' +
'    ctx.fillText("9", cx - r + 7, cy);' +
'  }' +
'  var hh = now.getHours() % 12, mm = now.getMinutes(), ss = now.getSeconds();' +
'  var hourAngle = ((hh * 60 + mm) / 720) * 2 * Math.PI;' +
'  var minAngle = (mm / 60) * 2 * Math.PI;' +
'  ctx.strokeStyle = colors.text;' +
'  ctx.lineWidth = 3;' +
'  ctx.beginPath(); ctx.moveTo(cx, cy);' +
'  ctx.lineTo(cx + r * 0.55 * Math.sin(hourAngle), cy - r * 0.55 * Math.cos(hourAngle));' +
'  ctx.stroke();' +
'  ctx.lineWidth = 2;' +
'  ctx.beginPath(); ctx.moveTo(cx, cy);' +
'  ctx.lineTo(cx + r * 0.8 * Math.sin(minAngle), cy - r * 0.8 * Math.cos(minAngle));' +
'  ctx.stroke();' +
'  if (showSeconds) {' +
'    var secAngle = (ss / 60) * 2 * Math.PI;' +
'    ctx.strokeStyle = colors.accent;' +
'    ctx.lineWidth = 1;' +
'    ctx.beginPath(); ctx.moveTo(cx, cy);' +
'    ctx.lineTo(cx + r * 0.88 * Math.sin(secAngle), cy - r * 0.88 * Math.cos(secAngle));' +
'    ctx.stroke();' +
'  }' +
'  ctx.fillStyle = colors.text;' +
'  ctx.beginPath(); ctx.arc(cx, cy, 2, 0, 2 * Math.PI); ctx.fill();' +
'  return cx + r;' + // right edge of the clock, so the caller knows where to start the info panel
'}' +

'function drawBigHandPreview(ctx, cx, cy, angle, length, style, color, transparent) {' +
'  if (style === 3) {' +
'    ctx.save();' +
'    ctx.translate(cx, cy);' +
'    ctx.rotate(angle);' +
'    ctx.lineCap = "round";' +
'    ctx.strokeStyle = color;' +
'    ctx.lineWidth = Math.max(4, length / 7);' +
'    ctx.globalAlpha = transparent ? 0.5 : 1;' +
'    ctx.beginPath();' +
'    ctx.moveTo(0, 0);' +
'    ctx.lineTo(0, -length);' +
'    ctx.stroke();' +
'    ctx.globalAlpha = 1;' +
'    ctx.restore();' +
'    return;' +
'  }' +
'  ctx.save();' +
'  ctx.translate(cx, cy);' +
'  ctx.rotate(angle);' +
'  var hw;' +
'  ctx.beginPath();' +
'  if (style === 1) {' +
'    hw = Math.max(3, length / 8);' +
'    ctx.rect(-hw, -length, hw * 2, length + 6);' +
'  } else if (style === 2) {' +
'    hw = Math.max(2, length / 10);' +
'    ctx.rect(-hw, -length, hw * 2, length + 8);' +
'  } else {' +
'    hw = Math.max(4, length / 6);' +
'    ctx.moveTo(-hw, 6); ctx.lineTo(hw, 6); ctx.lineTo(0, -length); ctx.closePath();' +
'  }' +
'  if (transparent) {' +
'    ctx.globalAlpha = 0.5;' +
'    ctx.fillStyle = color; ctx.fill();' +
'    ctx.globalAlpha = 1;' +
'  } else if (style === 2) {' +
'    ctx.strokeStyle = color; ctx.lineWidth = 1.5; ctx.stroke();' +
'  } else {' +
'    ctx.fillStyle = color; ctx.fill();' +
'  }' +
'  ctx.restore();' +
'}' +

// Loaded lazily and cached per style -- base64 data: URIs decode
// effectively instantly in practice, but img.complete is checked
// before drawing rather than assumed, so a not-yet-ready image is
// simply skipped for this tick (the next one, ~1s later via the
// preview\'s own refresh interval, picks it up once ready) instead of
// drawing nothing or throwing.
'var MARKER_PREVIEW_IMG_CACHE = {};' +
'function getMarkerPreviewImg(styleVal) {' +
'  var src = MARKER_PREVIEW_IMAGES[styleVal];' +
'  if (!src) return null;' +
'  if (!MARKER_PREVIEW_IMG_CACHE[styleVal]) {' +
'    var img = new Image();' +
'    img.src = src;' +
'    MARKER_PREVIEW_IMG_CACHE[styleVal] = img;' +
'  }' +
'  var cached = MARKER_PREVIEW_IMG_CACHE[styleVal];' +
'  return (cached.complete && cached.naturalWidth > 0) ? cached : null;' +
'}' +
'function hexToRgb(hex) {' +
'  var m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex || "");' +
'  if (!m) return { r: 0, g: 0, b: 0 };' +
'  return { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) };' +
'}' +
// Recolors an offscreen copy of the image by directly rewriting pixel
// RGB values while leaving each pixel\'s own alpha untouched -- unlike
// relying on globalCompositeOperation (which isn\'t consistently
// supported across the range of embedded WebViews Pebble phones
// actually ship), this works the same everywhere and mirrors exactly
// what the watch\'s own tint_marker_bitmap() does. Cached per
// style+color combination since it\'s real per-pixel work and the
// preview redraws roughly once a second.
'var MARKER_TINT_CACHE = {};' +
'function getTintedMarkerCanvas(styleVal, tintColor) {' +
'  var cacheKey = styleVal + "|" + tintColor;' +
'  if (MARKER_TINT_CACHE[cacheKey]) return MARKER_TINT_CACHE[cacheKey];' +
'  var img = getMarkerPreviewImg(styleVal);' +
'  if (!img) return null;' +
'  var iw = img.naturalWidth, ih = img.naturalHeight;' +
'  if (!iw || !ih) return null;' +
'  try {' +
'    var off = document.createElement("canvas");' +
'    off.width = iw; off.height = ih;' +
'    var octx = off.getContext("2d");' +
'    octx.drawImage(img, 0, 0, iw, ih);' +
'    var imageData = octx.getImageData(0, 0, iw, ih);' +
'    var rgb = hexToRgb(tintColor);' +
'    var data = imageData.data;' +
'    for (var i = 0; i < data.length; i += 4) {' +
'      if (data[i + 3] > 0) {' +
'        data[i] = rgb.r;' +
'        data[i + 1] = rgb.g;' +
'        data[i + 2] = rgb.b;' +
'      }' +
'    }' +
'    octx.putImageData(imageData, 0, 0);' +
'    MARKER_TINT_CACHE[cacheKey] = off;' +
'    return off;' +
'  } catch (e) {' +
'    return null;' +
'  }' +
'}' +
// Draws the tinted mask stretched to fill (w, h). Returns whether it
// actually drew anything, so the caller can fall back to a
// placeholder when no preview image exists yet for this style.
'function drawTintedMarkerBitmap(ctx, styleVal, w, h, tintColor) {' +
'  var tinted = getTintedMarkerCanvas(styleVal, tintColor);' +
'  if (!tinted) return false;' +
'  ctx.drawImage(tinted, 0, 0, w, h);' +
'  return true;' +
'}' +

'function drawBigAnalogPreview(ctx, colors, now, showSeconds, w, h, markerImageDrawn) {' +
'  var cx = w / 2, cy = h / 2, r = Math.min(w, h) / 2 - 12;' +
'  var style = parseInt(document.getElementById("bigAnalogHandStyle").value, 10);' +
'  var transparent = document.getElementById("bigAnalogTransparent").checked;' +
'  var markerStyle = parseInt(document.getElementById("bigAnalogMarkerStyle").value, 10);' +

'  if (markerStyle <= 2) {' +
'    var showSecondMarkers = markerStyle !== 0;' +
'    var hourOuter = r + (markerStyle === 2 ? 4 : 3);' +
'    var hourWidth = markerStyle === 2 ? 3 : 1;' +
'    for (var hIdx = 0; hIdx < 12; hIdx++) {' +
'      var ang = hIdx * Math.PI / 6;' +
'      var inner = (hIdx % 3 === 0) ? hourOuter - 5 : hourOuter - 3;' +
'      ctx.strokeStyle = colors.text; ctx.lineWidth = hourWidth;' +
'      ctx.beginPath();' +
'      ctx.moveTo(cx + hourOuter * Math.sin(ang), cy - hourOuter * Math.cos(ang));' +
'      ctx.lineTo(cx + inner * Math.sin(ang), cy - inner * Math.cos(ang));' +
'      ctx.stroke();' +
'    }' +
'    if (showSecondMarkers) {' +
'      for (var s = 0; s < 60; s++) {' +
'        if (s % 5 === 0) continue;' +
'        var ang2 = s * Math.PI / 30;' +
'        var outer2 = r + 1, inner2 = r - 1;' +
'        ctx.strokeStyle = colors.text; ctx.lineWidth = 1;' +
'        ctx.beginPath();' +
'        ctx.moveTo(cx + outer2 * Math.sin(ang2), cy - outer2 * Math.cos(ang2));' +
'        ctx.lineTo(cx + inner2 * Math.sin(ang2), cy - inner2 * Math.cos(ang2));' +
'        ctx.stroke();' +
'      }' +
'    }' +
'  } else if (markerStyle === 9) {' +
'    /* none -- no marker ring, no placeholder text either */' +
'  } else if (!markerImageDrawn) {' +
'    ctx.font = "10px sans-serif";' +
'    ctx.fillStyle = colors.text;' +
'    ctx.textAlign = "center"; ctx.textBaseline = "middle";' +
'    ctx.fillText(markerStyle === 8 ? "(custom -- edit below)" : "(bitmap markers)", cx, cy - r - 8);' +
'  }' +

'  var hh = now.getHours() % 12, mm = now.getMinutes(), ss = now.getSeconds();' +
'  var hourAngle = ((hh * 60 + mm) / 720) * 2 * Math.PI;' +
'  var minAngle = (mm / 60) * 2 * Math.PI;' +
'  var hourColor = (style === 3) ? colors.accent : colors.text;' +
'  drawBigHandPreview(ctx, cx, cy, hourAngle, r * 0.55, style, hourColor, transparent);' +
'  drawBigHandPreview(ctx, cx, cy, minAngle, r * 0.85, style, colors.text, transparent);' +

'  if (showSeconds) {' +
'    var secAngle = (ss / 60) * 2 * Math.PI;' +
'    ctx.strokeStyle = colors.accent; ctx.lineWidth = 1;' +
'    ctx.beginPath(); ctx.moveTo(cx, cy);' +
'    ctx.lineTo(cx + r * 0.92 * Math.sin(secAngle), cy - r * 0.92 * Math.cos(secAngle));' +
'    ctx.stroke();' +
'  }' +
'  ctx.fillStyle = colors.text;' +
'  ctx.beginPath(); ctx.arc(cx, cy, 2, 0, 2 * Math.PI); ctx.fill();' +
'}' +

// Mirrors small_analog_feature_count() in pebble-eclipse-watch.c --
// keep the two in sync. Also used by computeSlotAvailability() to
// gray out the 4th "Feature" slot button when it wouldn't fit.
'function computeSmallAnalogFeatureCount() {' +
'  var customFontEl = document.getElementById("cornerCustomFont");' +
'  var fontSizeEl = document.getElementById("cornerFontSize");' +
'  var customFont = customFontEl ? customFontEl.value : "0";' +
'  var fontSize = fontSizeEl ? fontSizeEl.value : "1";' +
'  if (customFont && customFont !== "0") return 3;' +
'  if (fontSize === "2" || fontSize === "3" || fontSize === "4" || fontSize === "5") return 3;' +
'  return 4;' +
'}' +

// The small-analog info panel's rows -- same 4 fields the big-analog
// upper-middle/bottom-middle 2-line slots use (see SLOT_DEFS/the
// smallAnalogFeatureN entries below), drawn left-aligned starting
// just after the clock face, matching the watch.
'function drawInfoPanelPreview(ctx, colors, panelLeft, w, panelTop, panelBottom) {' +
'  var count = computeSmallAnalogFeatureCount();' +
'  var ids = [' +
'    ["upperMiddleLine1Content", "upperMiddleLine1Color"],' +
'    ["upperMiddleLine2Content", "upperMiddleLine2Color"],' +
'    ["bottomMiddleLine1Content", "bottomMiddleLine1Color"],' +
'    ["bottomMiddleLine2Content", "bottomMiddleLine2Color"]' +
'  ];' +
'  var lineH = (panelBottom - panelTop) / count;' +
'  for (var i = 0; i < count; i++) {' +
'    drawCornerSlot(ctx, ids[i][0], ids[i][1], panelLeft + 8, panelTop + lineH * i + (lineH - 9) / 2, "left", colors);' +
'  }' +
'}' +

'function drawDigitalPreview(ctx, colors, now, showSeconds, w, panelTop, panelBottom) {' +
'  var fontSel = document.getElementById("clockFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var hh = now.getHours(), mm = now.getMinutes();' +
'  var txt = (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm;' +
'  if (showSeconds) { var ss = now.getSeconds(); txt += ":" + (ss < 10 ? "0" : "") + ss; }' +
'  ctx.font = canvasFontFor(opt.getAttribute("data-preview") || "", 26);' +
'  ctx.fillStyle = colors.text;' +
'  ctx.textAlign = "center"; ctx.textBaseline = "middle";' +
'  ctx.fillText(txt, w / 2, panelTop + (panelBottom - panelTop) * 0.42);' +
'  ctx.font = "11px sans-serif";' +
'  ctx.fillText(now.toDateString(), w / 2, panelBottom - 12);' +
'}' +

'function updatePreview() {' +
'  var canvas = document.getElementById("previewCanvas");' +
'  if (!canvas || !canvas.getContext) return;' +
'  var ctx = canvas.getContext("2d");' +
'  var w = canvas.width, h = canvas.height;' +
'  ctx.clearRect(0, 0, w, h);' +

'  var colors = dayColors();' +
'  var styleVal = document.getElementById("bottomStyleValue").value;' +
'  var now = new Date();' +
'  var secondsBox = document.getElementById("showSeconds");' +
'  var showSeconds = secondsBox.checked && !secondsBox.disabled;' +

'  if (styleVal === "biganalog") {' +
'    drawSkyLayer(ctx, 0, 0, w, h);' +
'    var markerStyleVal = document.getElementById("bigAnalogMarkerStyle").value;' +
'    var markerStyleInt = parseInt(markerStyleVal, 10);' +
'    var markerImageDrawn = (markerStyleInt >= 3 && markerStyleInt !== 8 && markerStyleInt !== 9) && drawTintedMarkerBitmap(ctx, markerStyleVal, w, h, colors.text);' +
'    drawBigAnalogPreview(ctx, colors, now, showSeconds, w, h, markerImageDrawn);' +
'    drawCornersAndEdges(ctx, w, h, colors, h);' +
'  } else {' +
'    var skyH = Math.round(h * 152 / 228);' +
'    drawSkyLayer(ctx, 0, 0, w, skyH);' +
'    drawCornersAndEdges(ctx, w, h, colors, skyH);' +
'    ctx.fillStyle = colors.bg;' +
'    ctx.fillRect(0, skyH, w, h - skyH);' +
'    if (styleVal === "analog") {' +
'      var clockRight = drawAnalogPreview(ctx, colors, now, showSeconds, w, skyH, h);' +
'      drawInfoPanelPreview(ctx, colors, clockRight, w, skyH, h);' +
'    } else {' +
'      drawDigitalPreview(ctx, colors, now, showSeconds, w, skyH, h);' +
'    }' +
'  }' +
'}' +

'function onBottomStyleChange() {' +
'  var styleVal = document.getElementById("bottomStyleValue").value;' +
'  var isAnalog = styleVal === "analog";' +
'  var isBigAnalog = styleVal === "biganalog";' +
'  document.getElementById("digitalOnlySettings").style.display = (styleVal === "digital") ? "block" : "none";' +
'  document.getElementById("analogOnlySettings").style.display = isAnalog ? "block" : "none";' +
'  document.getElementById("bigAnalogSettings").style.display = isBigAnalog ? "block" : "none";' +
'  document.getElementById("showSunTimeSection").style.display = (styleVal === "digital") ? "block" : "none";' +
'  var analogFeaturePanel = document.getElementById("analogFeaturePanel");' +
'  if (analogFeaturePanel) analogFeaturePanel.style.display = isAnalog ? "block" : "none";' +
'  var secondsBox = document.getElementById("showSeconds");' +
'  var fontSel = document.getElementById("clockFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var fontOk = opt.getAttribute("data-seconds") === "1";' +
'  var handBased = isAnalog || isBigAnalog;' +
'  var secondsUnavailable = !handBased && !fontOk;' +
'  secondsBox.disabled = secondsUnavailable;' +
'  if (secondsUnavailable) secondsBox.checked = false;' +
'  document.getElementById("secondsHelp").style.display = secondsUnavailable ? "block" : "none";' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
// Runtime copy of the category groupings + full item labels (the
// generator-side CORNER_CONTENT_OPTIONS/CORNER_CATEGORIES data can\'t
// be reused here -- this needs to run in the browser, re-populating
// the item dropdown live whenever the category changes, same reason
// CORNER_PREVIEW_LABELS below is its own separate runtime copy rather
// than reusing the generator-side labels). Keep in sync with
// CORNER_CONTENT_OPTIONS/CORNER_CATEGORIES above by hand -- every
// content id 0-83 must appear in exactly one category\'s items list.
'var CONTENT_SELECT_IDS = ["cornerTL", "cornerTR", "cornerBL", "cornerBR", ' +
'  "upperMiddleLine1Content", "upperMiddleLine2Content", "bottomMiddleLine1Content", "bottomMiddleLine2Content", ' +
'  "middleLeftLine1Content", "middleLeftLine2Content", "middleRightLine1Content", "middleRightLine2Content"];' +
'var CORNER_CATEGORIES = [' +
'  { id: "none", label: "None", items: [{ id: 0, label: "None" }] },' +
'  { id: "utilities", label: "Utilities", items: [' +
'    { id: 10, label: "Battery" }, { id: 13, label: "Location" }, { id: 17, label: "Pebble logo /w battery bar" },' +
'    { id: 20, label: "Bluetooth connection" }, { id: 78, label: "Bluetooth status (icon only)" }, { id: 38, label: "Altitude" }' +
'  ] },' +
'  { id: "health", label: "Health", items: [' +
'    { id: 1, label: "Heart rate" }, { id: 2, label: "Steps today" }, { id: 3, label: "Step goal %" },' +
'    { id: 39, label: "Sleep duration" }, { id: 40, label: "Restful sleep duration" }, { id: 41, label: "Sleep quality %" },' +
'    { id: 42, label: "Bed time" }, { id: 43, label: "Wake time" }' +
'  ] },' +
'  { id: "datetime", label: "Date/Time", items: [' +
'    { id: 12, label: "Short date" }, { id: 18, label: "Time" }, { id: 19, label: "Week number" },' +
'    { id: 21, label: "Date: Month Day (SEP 11)" }, { id: 22, label: "Date: Day of month (11)" },' +
'    { id: 23, label: "Date: Weekday, short (MON)" }, { id: 24, label: "Date: Weekday, long (Monday)" },' +
'    { id: 25, label: "Date: Month, short (SEP)" }, { id: 26, label: "Date: Month, long (September)" },' +
'    { id: 27, label: "Date: Day/Month (11/9)" }, { id: 28, label: "Date: Month/Day (9/11)" },' +
'    { id: 29, label: "Date: Full (24/9/2026)" }, { id: 30, label: "Date: Full, imperial (9/24/26)" },' +
'    { id: 63, label: "Time: full (H:M:S)" }, { id: 64, label: "Time: hour, 24h leading zero (07)" },' +
'    { id: 65, label: "Time: hour, 24h (7)" }, { id: 66, label: "Time: hour, 12h (7)" },' +
'    { id: 67, label: "Time: minute (5)" }, { id: 68, label: "Time: minute, leading zero (05)" },' +
'    { id: 69, label: "Time: second (8)" }, { id: 70, label: "Time: second, leading zero (08)" },' +
'    { id: 71, label: "Time: seconds, tens digit" }, { id: 72, label: "Time: seconds, ones digit" }' +
'  ] },' +
'  { id: "timezone", label: "Timezone", items: [' +
'    { id: 44, label: "GMT+0 London" }, { id: 45, label: "GMT+1 Paris / Berlin / Madrid" }, { id: 46, label: "GMT+2 Cairo" },' +
'    { id: 47, label: "GMT+3 Moscow" }, { id: 48, label: "GMT+4 Dubai" }, { id: 49, label: "GMT+5:30 Delhi / Mumbai" },' +
'    { id: 50, label: "GMT+6 Dhaka" }, { id: 51, label: "GMT+7 Bangkok / Jakarta" }, { id: 52, label: "GMT+8 Beijing / Shanghai / Singapore" },' +
'    { id: 53, label: "GMT+9 Tokyo" }, { id: 54, label: "GMT+10 Sydney" }, { id: 55, label: "GMT+12 Auckland" },' +
'    { id: 56, label: "GMT-5 New York" }, { id: 57, label: "GMT-6 Chicago" }, { id: 58, label: "GMT-7 Denver" },' +
'    { id: 59, label: "GMT-8 Los Angeles" }, { id: 60, label: "GMT-9 Anchorage" }, { id: 61, label: "GMT-10 Honolulu" },' +
'    { id: 62, label: "GMT-3 Sao Paulo" }' +
'  ] },' +
'  { id: "weather", label: "Weather", items: [' +
'    { id: 4, label: "High / low temperature" }, { id: 5, label: "Current conditions" }, { id: 6, label: "UV index" },' +
'    { id: 7, label: "Rain chance today" }, { id: 8, label: "Humidity" }, { id: 9, label: "Wind" },' +
'    { id: 14, label: "Visibility" }, { id: 15, label: "Cloud cover" }, { id: 31, label: "Weather icon" },' +
'    { id: 32, label: "Temp + weather icon" }, { id: 34, label: "Pressure" }, { id: 35, label: "Wind direction" },' +
'    { id: 36, label: "Air quality" }, { id: 37, label: "Dew point" }, { id: 73, label: "Current temp" },' +
'    { id: 74, label: "High temp" }, { id: 75, label: "Low temp" }, { id: 76, label: "Weather icon + all temps" },' +
'    { id: 77, label: "Feels like temp" }' +
'  ] },' +
'  { id: "astro", label: "Astronomy", items: [' +
'    { id: 11, label: "Moon phase" }, { id: 16, label: "Sunrise / sunset" },' +
'    { id: 79, label: "Planets visible now" }, { id: 80, label: "Meteor shower" },' +
'    { id: 81, label: "Saturn ring angle" }, { id: 82, label: "Next planet rise" },' +
'    { id: 83, label: "Next ISS pass" }' + (current.auroraEnabled ? ', { id: 84, label: "Aurora Kp index" }' : '') +
'  ] }' +
'];' +
'function categoryForContentId(contentId) {' +
'  var idNum = parseInt(contentId, 10);' +
'  for (var i = 0; i < CORNER_CATEGORIES.length; i++) {' +
'    for (var j = 0; j < CORNER_CATEGORIES[i].items.length; j++) {' +
'      if (CORNER_CATEGORIES[i].items[j].id === idNum) return CORNER_CATEGORIES[i].id;' +
'    }' +
'  }' +
'  return "none";' + // unrecognized id -- fall back rather than leave both dropdowns unset
'}' +
'function findCategory(categoryId) {' +
'  for (var i = 0; i < CORNER_CATEGORIES.length; i++) {' +
'    if (CORNER_CATEGORIES[i].id === categoryId) return CORNER_CATEGORIES[i];' +
'  }' +
'  return CORNER_CATEGORIES[0];' +
'}' +
'function categoryItemOptionsHtml(categoryId, selectedContentId) {' +
'  var items = findCategory(categoryId).items;' +
'  var html = "";' +
'  for (var i = 0; i < items.length; i++) {' +
'    html += "<option value=\\"" + items[i].id + "\\"" + (String(selectedContentId) === String(items[i].id) ? " selected" : "") + ">" + items[i].label + "</option>";' +
'  }' +
'  return html;' +
'}' +
'var SLOT_DEFS = {' +
'  cornerTL: { contentId: "cornerTL", colorId: "cornerTLColor", btnId: "slotBtn-cornerTL", label: "Top-left", avail: function (a) { return !a.cornersGrayed; } },' +
'  cornerTR: { contentId: "cornerTR", colorId: "cornerTRColor", btnId: "slotBtn-cornerTR", label: "Top-right", avail: function (a) { return !a.cornersGrayed; } },' +
'  cornerBL: { contentId: "cornerBL", colorId: "cornerBLColor", btnId: "slotBtn-cornerBL", label: "Bottom-left", avail: function (a) { return !a.cornersGrayed; } },' +
'  cornerBR: { contentId: "cornerBR", colorId: "cornerBRColor", btnId: "slotBtn-cornerBR", label: "Bottom-right", avail: function (a) { return !a.cornersGrayed; } },' +
'  upperMiddleLine1: { contentId: "upperMiddleLine1Content", colorId: "upperMiddleLine1Color", btnId: "slotBtn-upperMiddleLine1", label: "Upper-middle, line 1", avail: function (a) { return a.upper; } },' +
'  upperMiddleLine2: { contentId: "upperMiddleLine2Content", colorId: "upperMiddleLine2Color", btnId: "slotBtn-upperMiddleLine2", label: "Upper-middle, line 2", avail: function (a) { return a.upper; } },' +
'  bottomMiddleLine1: { contentId: "bottomMiddleLine1Content", colorId: "bottomMiddleLine1Color", btnId: "slotBtn-bottomMiddleLine1", label: "Bottom-middle, line 1", avail: function (a) { return a.bottom; } },' +
'  bottomMiddleLine2: { contentId: "bottomMiddleLine2Content", colorId: "bottomMiddleLine2Color", btnId: "slotBtn-bottomMiddleLine2", label: "Bottom-middle, line 2", avail: function (a) { return a.bottom; } },' +
'  middleLeftLine1: { contentId: "middleLeftLine1Content", colorId: "middleLeftLine1Color", btnId: "slotBtn-middleLeftLine1", label: "Middle-left, line 1", avail: function (a) { return a.left; } },' +
'  middleLeftLine2: { contentId: "middleLeftLine2Content", colorId: "middleLeftLine2Color", btnId: "slotBtn-middleLeftLine2", label: "Middle-left, line 2", avail: function (a) { return a.left; } },' +
'  middleRightLine1: { contentId: "middleRightLine1Content", colorId: "middleRightLine1Color", btnId: "slotBtn-middleRightLine1", label: "Middle-right, line 1", avail: function (a) { return a.right; } },' +
'  middleRightLine2: { contentId: "middleRightLine2Content", colorId: "middleRightLine2Color", btnId: "slotBtn-middleRightLine2", label: "Middle-right, line 2", avail: function (a) { return a.right; } },' +
// The small-analog info panel's 4 rows -- deliberately reusing the
// SAME contentId/colorId as upper/bottom-middle line 1/2 above
// rather than separate storage (see the matching comment in
// pebble-eclipse-watch.c's bottom_canvas_update_proc). Both this
// button and the corresponding "Upper-middle"/"Bottom-middle" button
// edit the exact same underlying value -- picking a different label
// for each just makes clear which on-screen row each is standing in
// for right now.
'  smallAnalogFeature1: { contentId: "upperMiddleLine1Content", colorId: "upperMiddleLine1Color", btnId: "slotBtn-smallAnalogFeature1", label: "Feature 1", avail: function (a) { return a.analog; } },' +
'  smallAnalogFeature2: { contentId: "upperMiddleLine2Content", colorId: "upperMiddleLine2Color", btnId: "slotBtn-smallAnalogFeature2", label: "Feature 2", avail: function (a) { return a.analog; } },' +
'  smallAnalogFeature3: { contentId: "bottomMiddleLine1Content", colorId: "bottomMiddleLine1Color", btnId: "slotBtn-smallAnalogFeature3", label: "Feature 3", avail: function (a) { return a.analog; } },' +
'  smallAnalogFeature4: { contentId: "bottomMiddleLine2Content", colorId: "bottomMiddleLine2Color", btnId: "slotBtn-smallAnalogFeature4", label: "Feature 4", avail: function (a) { return a.analog && a.smallAnalogFeatureCount >= 4; } }' +
'};' +
'var CURRENT_SLOT_KEY = null;' +
'var SLOT_EDITOR_DRAFT_COLOR = 0;' +
// Same per-marker-style room rules as before: procedural styles (<3)
// have all 8 slots and the 4 corners; bitmap styles are each limited
// to whatever their own artwork actually has room for and suppress
// the corners entirely (the mask fills most of the screen). analog
// and smallAnalogFeatureCount drive the small-analog "Feature 1-4"
// buttons instead -- see computeSmallAnalogFeatureCount() above,
// which mirrors small_analog_feature_count() in the C code.
'function computeSlotAvailability() {' +
'  var styleVal = document.getElementById("bottomStyleValue").value;' +
'  var isBigAnalog = styleVal === "biganalog";' +
'  var markerStyle = parseInt(document.getElementById("bigAnalogMarkerStyle").value, 10);' +
'  var avail = { upper: false, bottom: false, left: false, right: false, cornersGrayed: false,' +
'                analog: styleVal === "analog", smallAnalogFeatureCount: computeSmallAnalogFeatureCount() };' +
'  if (isBigAnalog) {' +
'    if (markerStyle < 3 || markerStyle === 8 || markerStyle === 9) {' +
'      avail.upper = avail.bottom = avail.left = avail.right = true;' +
'    } else if (markerStyle === 3 || markerStyle === 4 || markerStyle === 6) {' +
'      avail.upper = avail.bottom = true; avail.cornersGrayed = true;' +
'    } else if (markerStyle === 5 || markerStyle === 7) {' +
'      avail.upper = avail.bottom = avail.left = avail.right = true; avail.cornersGrayed = true;' +
'    } else {' +
'      avail.upper = true; avail.cornersGrayed = true;' +
'    }' +
'  }' +
'  return avail;' +
'}' +
// Updates each slot button\'s label (its example preview value, "OFF",
// or "N/A") and styling to match current settings -- called on init
// and whenever bottom-style/marker-style or a slot\'s own content
// changes.
'function renderSlotPicker() {' +
'  var avail = computeSlotAvailability();' +
'  for (var key in SLOT_DEFS) {' +
'    var def = SLOT_DEFS[key];' +
'    var btn = document.getElementById(def.btnId);' +
'    var baseClass = btn.getAttribute("data-base-class");' +
'    if (!baseClass) { baseClass = btn.className; btn.setAttribute("data-base-class", baseClass); }' +
'    if (!def.avail(avail)) {' +
'      btn.textContent = "N/A";' +
'      btn.className = baseClass + " slot-na";' +
'      continue;' +
'    }' +
'    var val = parseInt(document.getElementById(def.contentId).value, 10);' +
'    if (!val || !CORNER_PREVIEW_LABELS[val]) {' +
'      btn.textContent = "OFF";' +
'      btn.className = baseClass + " slot-off";' +
'    } else {' +
'      btn.textContent = CORNER_PREVIEW_LABELS[val];' +
'      btn.className = baseClass;' +
'    }' +
'  }' +
'}' +
'function setSlotEditorColorGroupVisibility(contentVal) {' +
'  document.getElementById("slotEditColorGroup").style.display = (contentVal === "0") ? "none" : "flex";' +
'}' +
'function setSlotEditorColorButtons(value) {' +
'  SLOT_EDITOR_DRAFT_COLOR = parseInt(value, 10) || 0;' +
'  var buttons = document.getElementById("slotEditColorGroup").getElementsByClassName("mode-btn");' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    buttons[i].className = "mode-btn" + (i === SLOT_EDITOR_DRAFT_COLOR ? " active" : "");' +
'  }' +
'}' +
'function slotEditorSelectColor(value) {' +
'  setSlotEditorColorButtons(value);' +
'}' +
'function onSlotEditContentChange() {' +
'  setSlotEditorColorGroupVisibility(document.getElementById("slotEditContent").value);' +
'}' +
// Repopulates the item dropdown to just the newly-chosen category's
// items whenever the category itself changes -- defaults to that
// category\'s first item, since the previously-selected content id
// (from a different category) is never one of the new options.
'function onSlotEditCategoryChange() {' +
'  var categoryId = document.getElementById("slotEditCategory").value;' +
'  var firstItemId = findCategory(categoryId).items[0].id;' +
'  document.getElementById("slotEditContent").innerHTML = categoryItemOptionsHtml(categoryId, firstItemId);' +
'  document.getElementById("slotEditCategoryHelp").style.display = (categoryId === "timezone") ? "" : "none";' +
'  onSlotEditContentChange();' +
'}' +
// Opens the popup pre-filled with this slot\'s current (already-saved)
// content/color -- nothing is written back to the real elements until
// saveSlotEditor() runs, so closing without saving (Cancel, or a tap
// outside the box) leaves the slot exactly as it was.
'function openSlotEditor(slotKey) {' +
'  var def = SLOT_DEFS[slotKey];' +
'  if (!def) return;' +
'  CURRENT_SLOT_KEY = slotKey;' +
'  document.getElementById("slotEditTitle").textContent = def.label;' +
'  var contentVal = document.getElementById(def.contentId).value;' +
'  var colorVal = document.getElementById(def.colorId).value;' +
'  var categoryId = categoryForContentId(contentVal);' +
'  var categorySelect = document.getElementById("slotEditCategory");' +
'  categorySelect.innerHTML = CORNER_CATEGORIES.map(function (c) {' +
'    return "<option value=\\"" + c.id + "\\"" + (c.id === categoryId ? " selected" : "") + ">" + c.label + "</option>";' +
'  }).join("");' +
'  document.getElementById("slotEditContent").innerHTML = categoryItemOptionsHtml(categoryId, contentVal);' +
'  document.getElementById("slotEditCategoryHelp").style.display = (categoryId === "timezone") ? "" : "none";' +
'  setSlotEditorColorGroupVisibility(contentVal);' +
'  setSlotEditorColorButtons(colorVal);' +
'  document.getElementById("slotEditModal").className = "modal-overlay open";' +
'}' +
'function closeSlotEditor() {' +
'  document.getElementById("slotEditModal").className = "modal-overlay";' +
'  CURRENT_SLOT_KEY = null;' +
'}' +
'function saveSlotEditor() {' +
'  if (!CURRENT_SLOT_KEY) return;' +
'  var def = SLOT_DEFS[CURRENT_SLOT_KEY];' +
'  document.getElementById(def.contentId).value = document.getElementById("slotEditContent").value;' +
'  document.getElementById(def.colorId).value = String(SLOT_EDITOR_DRAFT_COLOR);' +
'  closeSlotEditor();' +
'  renderSlotPicker();' +
'  updateWeatherIconStyleVisibility();' +
'  updatePreview();' +
'}' +
// Shows the "Weather icon style" dropdown (in the Weather section) only
// when at least one of the 12 corner/edge slots is currently set to
// "Weather icon" (31) or "Temp + weather icon" (32) -- hidden otherwise,
// per the brief. Slot content only actually changes in saveSlotEditor(),
// so that (plus once at page load) is all that needs to call this.
'var WEATHER_ICON_SLOT_CONTENT_IDS = ["cornerTL", "cornerTR", "cornerBL", "cornerBR",' +
'  "upperMiddleLine1Content", "upperMiddleLine2Content", "bottomMiddleLine1Content", "bottomMiddleLine2Content",' +
'  "middleLeftLine1Content", "middleLeftLine2Content", "middleRightLine1Content", "middleRightLine2Content"];' +
'function updateWeatherIconStyleVisibility() {' +
'  var inUse = WEATHER_ICON_SLOT_CONTENT_IDS.some(function (id) {' +
'    var el = document.getElementById(id);' +
'    return el && (el.value === "31" || el.value === "32");' +
'  });' +
'  var row = document.getElementById("weatherIconStyleRow");' +
'  if (row) row.style.display = inUse ? "" : "none";' +
'}' +

// ---- collapsible sections + slider step buttons ------------------------
'function toggleSection(id) {' +
'  var body = document.getElementById("section-" + id);' +
'  var chev = document.getElementById("chev-" + id);' +
'  if (!body) return;' +
'  var isOpen = body.style.display !== "none";' +
'  body.style.display = isOpen ? "none" : "";' +
'  if (chev) chev.className = isOpen ? "chevron" : "chevron open";' +
'}' +
'function stepSlider(id, delta) {' +
'  var el = document.getElementById(id);' +
'  if (!el) return;' +
'  var min = parseFloat(el.min), max = parseFloat(el.max);' +
'  var v = parseFloat(el.value) + delta;' +
'  if (!isNaN(min) && v < min) v = min;' +
'  if (!isNaN(max) && v > max) v = max;' +
'  el.value = v;' +
'  if (el.oninput) el.oninput();' +
'  else if (el.onchange) el.onchange();' +
'}' +

// Press-and-hold repeat for the +/- slider step buttons. Reads the
// button's own onclick="stepSlider(\'id\', delta)" text to find which
// slider and direction it steps, rather than needing separate
// data attributes on all 20-odd call sites above -- one generic
// listener covers all of them, present and future. A single tap is
// left to the browser's normal click (one call to stepSlider); this
// only starts repeating after a short delay, so a quick tap never
// double-steps.
'var SLIDER_HOLD_TIMER = null;' +
'var SLIDER_HOLD_INTERVAL = null;' +
'function parseStepSliderArgs(btn) {' +
'  var attr = btn.getAttribute("onclick") || "";' +
'  var m = attr.match(/stepSlider\\(\'([^\']+)\',\\s*(-?\\d+)\\)/);' +
'  return m ? { id: m[1], delta: parseInt(m[2], 10) } : null;' +
'}' +
'function stopSliderHold() {' +
'  if (SLIDER_HOLD_TIMER) { clearTimeout(SLIDER_HOLD_TIMER); SLIDER_HOLD_TIMER = null; }' +
'  if (SLIDER_HOLD_INTERVAL) { clearInterval(SLIDER_HOLD_INTERVAL); SLIDER_HOLD_INTERVAL = null; }' +
'}' +
'function startSliderHold(btn) {' +
'  var args = parseStepSliderArgs(btn);' +
'  if (!args) return;' +
'  stopSliderHold();' +
'  SLIDER_HOLD_TIMER = setTimeout(function () {' +
'    SLIDER_HOLD_INTERVAL = setInterval(function () { stepSlider(args.id, args.delta); }, 90);' +
'  }, 400);' +
'}' +
'document.addEventListener("mousedown", function (e) {' +
'  var btn = e.target.closest && e.target.closest(".slider-step-btn");' +
'  if (btn) { startSliderHold(btn); e.preventDefault(); }' + // belt-and-suspenders alongside the CSS
                                                                // user-select:none above -- stops the
                                                                // press-and-hold repeat from also
                                                                // starting a text selection drag
'});' +
'document.addEventListener("touchstart", function (e) {' +
'  var btn = e.target.closest && e.target.closest(".slider-step-btn");' +
'  if (btn) startSliderHold(btn);' +
'}, { passive: true });' +
'["mouseup", "mouseleave", "touchend", "touchcancel"].forEach(function (ev) {' +
'  document.addEventListener(ev, stopSliderHold);' +
'});' +

'function onMarkerStyleChange() {' +
'  var val = document.getElementById("bigAnalogMarkerStyle").value;' +
'  document.getElementById("customMarkerSection").style.display = (val === "8") ? "" : "none";' +
'  var isBitmap = (val === "3" || val === "4" || val === "5" || val === "6" || val === "7");' +
'  document.getElementById("bitmapMarkerTransparentRow").style.display = isBitmap ? "" : "none";' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +

// ---- custom hour/second marker popups --------------------------------
'var CM_FIELDS = ["Style", "Thickness", "InnerEcc", "OuterEcc", "InnerBorder", "OuterBorder", "Translucent", "Color"];' +
'var CM_CHECKBOX_FIELDS = ["Translucent"];' +
'function cmHiddenPrefix(kind) { return kind === "hour" ? "customHour" : "customSec"; }' +
'function cmPopupPrefix(kind) { return kind === "hour" ? "cmHour" : "cmSec"; }' +
'var MARKER_PRESETS = {' +
'  hour: {' +
'    minimal: { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "20", OuterBorder: "100" },' +
'    small:   { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "0", OuterBorder: "100" },' +
'    big:     { Style: "2", Thickness: "3", InnerEcc: "0", OuterEcc: "0", InnerBorder: "0", OuterBorder: "100" }' +
'  },' +
'  sec: {' +
'    minimal: { Style: "0", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "85", OuterBorder: "100" },' +
'    small:   { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "60", OuterBorder: "100" },' +
'    big:     { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "60", OuterBorder: "100" }' +
'  }' +
'};' +
// A rough approximation of the 3 built-in procedural styles, translated
// into border-reach percentages (see marker_reach_px() in
// marker_layer.c) now that a mark's length comes directly from its
// inner/outer border points rather than a separate slider -- a starting
// point to tune from, not an exact match. The "second" ring has no real
// minimal-style equivalent (that style draws no second markers at all),
// so its "minimal" preset is just a short stub near the outer edge.
'function updateCustomMarkerValLabels(kind) {' +
'  var p = cmPopupPrefix(kind);' +
'  ["Thickness", "InnerEcc", "OuterEcc", "InnerBorder", "OuterBorder"].forEach(function (f) {' +
'    var el = document.getElementById(p + f);' +
'    var out = document.getElementById(p + f + "Val");' +
'    if (el && out) out.textContent = el.value + (f === "Thickness" ? "px" : "%");' +
'  });' +
'}' +
'function onCustomMarkerSliderInput(kind) {' +
'  updateCustomMarkerValLabels(kind);' +
'}' +
'function onCustomMarkerBorderInput(kind, isInner) {' +
'  var p = cmPopupPrefix(kind);' +
'  var innerEl = document.getElementById(p + "InnerBorder"), outerEl = document.getElementById(p + "OuterBorder");' +
'  var innerVal = parseInt(innerEl.value, 10), outerVal = parseInt(outerEl.value, 10);' +
'  outerEl.min = innerVal;' +
'  if (outerVal < innerVal) outerEl.value = innerVal;' +
'  updateCustomMarkerValLabels(kind);' +
'}' +
// Pre-fills the popup from the currently-saved customHour*/customSec*
// hidden inputs -- nothing is written back until saveCustomMarkerEditor()
// runs, so Cancel (or tapping outside) leaves the saved config untouched.
'function openCustomMarkerEditor(kind) {' +
'  var hp = cmHiddenPrefix(kind), p = cmPopupPrefix(kind);' +
'  CM_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    if (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) { popupEl.checked = hidden.value === "true"; } else { popupEl.value = hidden.value; }' +
'  });' +
'  document.getElementById(p + "OuterBorder").min = document.getElementById(p + "InnerBorder").value;' +
'  updateCustomMarkerValLabels(kind);' +
'  document.getElementById("customMarkerModal-" + kind).className = "modal-overlay open";' +
'}' +
'function closeCustomMarkerEditor(kind) {' +
'  document.getElementById("customMarkerModal-" + kind).className = "modal-overlay";' +
'}' +
'function saveCustomMarkerEditor(kind) {' +
'  var hp = cmHiddenPrefix(kind), p = cmPopupPrefix(kind);' +
'  CM_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    hidden.value = (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) ? String(popupEl.checked) : popupEl.value;' +
'  });' +
'  closeCustomMarkerEditor(kind);' +
'  updatePreview();' +
'}' +
'function applyMarkerPreset(kind, name) {' +
'  var p = cmPopupPrefix(kind);' +
'  var preset = MARKER_PRESETS[kind][name];' +
'  if (!preset) return;' +
'  CM_FIELDS.forEach(function (f) {' +
'    var el = document.getElementById(p + f);' +
'    if (!el || preset[f] === undefined) return;' +
'    if (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) { el.checked = preset[f] === true || preset[f] === "true"; } else { el.value = preset[f]; }' +
'  });' +
'  document.getElementById(p + "OuterBorder").min = document.getElementById(p + "InnerBorder").value;' +
'  updateCustomMarkerValLabels(kind);' +
'}' +
// Copies the OTHER ring\'s last-saved (not currently-open-popup-draft)
// config into this popup\'s controls -- text-marker settings are never
// touched here, since hour/second numbers already exclude each other.
'function copyMarkerConfig(kind) {' +
'  var otherKind = kind === "hour" ? "sec" : "hour";' +
'  var otherHiddenPrefix = cmHiddenPrefix(otherKind), p = cmPopupPrefix(kind);' +
'  CM_FIELDS.forEach(function (f) {' +
'    var src = document.getElementById(otherHiddenPrefix + f);' +
'    var dst = document.getElementById(p + f);' +
'    if (!src || !dst) return;' +
'    if (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) { dst.checked = src.value === "true"; } else { dst.value = src.value; }' +
'  });' +
'  document.getElementById(p + "OuterBorder").min = document.getElementById(p + "InnerBorder").value;' +
'  updateCustomMarkerValLabels(kind);' +
'}' +
'function onMarkerTextTargetChange() {' +
'  var val = document.getElementById("markerTextTarget").value;' +
'  document.getElementById("markerTextOptions").style.display = (val === "0") ? "none" : "";' +
'  document.getElementById("markerTextHourGrid").style.display = (val === "1") ? "" : "none";' +
'  document.getElementById("markerTextSecGrid").style.display = (val === "2") ? "" : "none";' +
'  updatePreview();' +
'}' +
'function onMarkerTextFontChange() {' +
'  var font = document.getElementById("markerTextFont").value;' +
'  var romanBox = document.getElementById("markerTextRoman");' +
'  var romanHelp = document.getElementById("markerTextRomanHelp");' +
'  var incompatible = !!ROMAN_INCOMPATIBLE_FONTS[font];' +
'  romanBox.disabled = incompatible;' +
'  if (incompatible) romanBox.checked = false;' +
'  if (romanHelp) romanHelp.textContent = incompatible ?' +
'    "Not available with this font -- its glyphs don\'t support Roman numerals correctly." :' +
'    "Shows I, II, III... instead of 1, 2, 3... -- independent of the font above.";' +
'  updatePreview();' +
'}' +
'function openTextMarkerEditor() {' +
'  document.getElementById("textMarkerModal").className = "modal-overlay open";' +
'}' +
'function closeTextMarkerEditor() {' +
'  document.getElementById("textMarkerModal").className = "modal-overlay";' +
'}' +
'function toggleMarkBtn(kind, i) {' +
'  var hiddenId = kind === "hour" ? "markerTextHourMask" : "markerTextSecMask";' +
'  var hidden = document.getElementById(hiddenId);' +
'  var mask = parseInt(hidden.value, 10) || 0;' +
'  var btn = document.getElementById("markBtn-" + kind + "-" + i);' +
'  var bit = 1 << i;' +
'  if (mask & bit) { mask &= ~bit; btn.className = "mark-btn"; } else { mask |= bit; btn.className = "mark-btn active"; }' +
'  hidden.value = String(mask);' +
'}' +

// ---- custom hour/minute/second hand popups ----------------------------
'var HE_FIELDS = ["Style", "Width", "Length", "BackOffset", "Color", "OutlineEnabled", "OutlineColor", "Translucent", "ShadowEnabled", "ShadowDistance"];' +
'var HE_CHECKBOX_FIELDS = ["OutlineEnabled", "Translucent", "ShadowEnabled"];' +
'var HAND_COPY_SOURCE = { hour: "min", min: "hour", sec: "min" };' +
'function heHiddenPrefix(kind) { return kind === "hour" ? "handHour" : (kind === "min" ? "handMin" : "handSec"); }' +
'function hePopupPrefix(kind) { return "he" + kind.charAt(0).toUpperCase() + kind.slice(1); }' +
// Rough translations of the 4 procedural hand styles into the custom
// field set, keyed by bigAnalogHandStyle's own value -- applied to all
// 3 hands whenever that dropdown changes, so switching to "Custom"
// later starts from something close to whichever style was picked.
// Approximated from draw_big_hand()'s constants (see
// pebble-eclipse-watch.c) at a ~92px radius; not an exact match, a
// starting point to tune from.
'var HAND_PRESETS = {' +
'  "0": { hour: {Style:"1",Width:"12",Length:"51",BackOffset:"0",Color:"0"}, min: {Style:"1",Width:"18",Length:"78",BackOffset:"0",Color:"0"}, sec: {Style:"0",Width:"2",Length:"85",BackOffset:"0",Color:"1"} },' +
'  "1": { hour: {Style:"2",Width:"8",Length:"51",BackOffset:"0",Color:"0"}, min: {Style:"2",Width:"12",Length:"78",BackOffset:"0",Color:"0"}, sec: {Style:"0",Width:"2",Length:"85",BackOffset:"0",Color:"1"} },' +
'  "2": { hour: {Style:"2",Width:"6",Length:"51",BackOffset:"0",Color:"0"}, min: {Style:"2",Width:"8",Length:"78",BackOffset:"0",Color:"0"}, sec: {Style:"0",Width:"2",Length:"85",BackOffset:"0",Color:"1"} },' +
'  "3": { hour: {Style:"0",Width:"6",Length:"51",BackOffset:"0",Color:"1"}, min: {Style:"0",Width:"10",Length:"78",BackOffset:"0",Color:"0"}, sec: {Style:"0",Width:"2",Length:"85",BackOffset:"0",Color:"1"} }' +
'};' +
'function applyHandPresetToKind(kind, preset) {' +
'  var hp = heHiddenPrefix(kind);' +
'  for (var f in preset) {' +
'    var hidden = document.getElementById(hp + f);' +
'    if (hidden) hidden.value = preset[f];' +
'  }' +
'}' +
'function onShadowAngleInput() {' +
'  var el = document.getElementById("shadowAngle");' +
'  var out = document.getElementById("shadowAngleVal");' +
'  if (el && out) out.textContent = el.value + "\u00b0";' +
'  updatePreview();' +
'}' +
// ---- Style Presets: export/import + 3 quick-recall slots -----------
// Scoped to exactly the DOM containers the design covers (Style,
// Colors, Features/\'corners\') by walking every input/select/textarea
// with an id inside them -- deliberately NOT a hand-maintained field
// list, so this never goes stale as fields get added to any of them.
'var PRESET_SCOPE_IDS = ["section-style", "section-colors", "section-corners"];' +
'function collectStyleCornersJson() {' +
'  var obj = {};' +
'  PRESET_SCOPE_IDS.forEach(function (containerId) {' +
'    var container = document.getElementById(containerId);' +
'    if (!container) return;' +
'    var els = container.querySelectorAll("input[id], select[id], textarea[id]");' +
'    els.forEach(function (el) {' +
'      obj[el.id] = (el.type === "checkbox") ? el.checked : el.value;' +
'    });' +
'  });' +
'  return obj;' +
'}' +
// Sets every id->value pair from a previously-exported (or a preset
// slot\'s saved) object, then re-runs the same cascade of dependent-UI
// handlers each field\'s own onchange would have triggered by hand --
// show/hide rows, slot picker labels, the preview canvas.
'function applyStyleCornersJson(obj) {' +
'  Object.keys(obj).forEach(function (id) {' +
'    var el = document.getElementById(id);' +
'    if (!el) return;' +
'    if (el.type === "checkbox") el.checked = !!obj[id]; else el.value = obj[id];' +
'  });' +
'  onBottomStyleChange();' +
'  onMarkerStyleChange();' +
'  onHandStyleChange();' +
'  onCornerFontChange();' +
'  onCornerFontSizeChange();' +
'  onSkyModeChange();' +
'  onShowSecondsChange();' +
'  updateColorRoleButtons();' +
'  updateColorRoleButtons("night");' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
'function exportDesignJson() {' +
'  document.getElementById("presetExportBox").value = JSON.stringify(collectStyleCornersJson(), null, 2);' +
'}' +
// navigator.clipboard needs a secure context and isn't guaranteed to
// exist in every webview Pebble's settings page might run inside --
// falls back to just selecting the text (same as tapping the box
// itself already does) so the user can still copy it via the
// device's own selection menu.
'function copyExportBoxToClipboard() {' +
'  var box = document.getElementById("presetExportBox");' +
'  var status = document.getElementById("presetExportStatus");' +
'  if (!box.value) exportDesignJson();' +
'  box.select();' +
'  if (navigator.clipboard && navigator.clipboard.writeText) {' +
'    navigator.clipboard.writeText(box.value).then(function () {' +
'      if (status) status.textContent = "Copied to clipboard.";' +
'    }, function () {' +
'      if (status) status.textContent = "Couldn\'t copy automatically -- text is selected, copy it from there.";' +
'    });' +
'  } else if (status) {' +
'    status.textContent = "Couldn\'t copy automatically -- text is selected, copy it from there.";' +
'  }' +
'}' +
'function pasteImportBoxFromClipboard() {' +
'  var box = document.getElementById("presetImportBox");' +
'  var status = document.getElementById("presetImportStatus");' +
'  if (navigator.clipboard && navigator.clipboard.readText) {' +
'    navigator.clipboard.readText().then(function (text) {' +
'      box.value = text;' +
'      if (status) status.textContent = "Pasted -- tap Apply to use it.";' +
'    }, function () {' +
'      if (status) status.textContent = "Couldn\'t read the clipboard automatically -- paste into the box by hand instead.";' +
'    });' +
'  } else if (status) {' +
'    status.textContent = "Clipboard access isn\'t available here -- paste into the box by hand instead.";' +
'  }' +
'}' +
'function importDesignJson() {' +
'  var status = document.getElementById("presetImportStatus");' +
'  var raw = document.getElementById("presetImportBox").value;' +
'  var obj;' +
'  try { obj = JSON.parse(raw); } catch (e) {' +
'    if (status) status.textContent = "Couldn\'t parse that as JSON.";' +
'    return;' +
'  }' +
'  applyStyleCornersJson(obj);' +
'  if (status) status.textContent = "Applied.";' +
'}' +
// Shared "are you sure?" step -- stashes the action to run and shows
// the confirm modal; confirmModalYes() runs it (once) and closes;
// canceling (or tapping outside the box) just closes without running
// anything.
'var s_pendingConfirmAction = null;' +
'function showConfirm(title, message, onConfirm) {' +
'  document.getElementById("confirmModalTitle").textContent = title;' +
'  document.getElementById("confirmModalMessage").textContent = message;' +
'  s_pendingConfirmAction = onConfirm;' +
'  document.getElementById("confirmModal").className = "modal-overlay open";' +
'}' +
'function closeConfirmModal() {' +
'  document.getElementById("confirmModal").className = "modal-overlay";' +
'  s_pendingConfirmAction = null;' +
'}' +
'function confirmModalYes() {' +
'  var action = s_pendingConfirmAction;' +
'  closeConfirmModal();' +
'  if (action) action();' +
'}' +
'function applyPresetSlot(n) {' +
'  var jsonEl = document.getElementById("presetSlot" + n + "Json");' +
'  if (!jsonEl || !jsonEl.value) return;' +
'  var obj;' +
'  try { obj = JSON.parse(jsonEl.value); } catch (e) { return; }' +
'  var name = document.getElementById("presetSlot" + n + "Name").value || ("Preset " + n);' +
'  showConfirm("Apply preset", \'Apply "\' + name + \'"? This replaces your current Style, Colors, and Features settings.\', function () {' +
'    applyStyleCornersJson(obj);' +
'  });' +
'}' +
'function savePresetSlot(n) {' +
'  var jsonEl = document.getElementById("presetSlot" + n + "Json");' +
'  var btn = document.getElementById("presetApplyBtn" + n);' +
'  if (!jsonEl) return;' +
'  var name = document.getElementById("presetSlot" + n + "Name").value || ("Preset " + n);' +
'  var hadPreset = !!jsonEl.value;' +
'  showConfirm("Save preset", (hadPreset ? \'Overwrite "\' : \'Save your current design into "\') + name + \'"?\' + (hadPreset ? \' This replaces what was saved there.\' : \'\'), function () {' +
'    jsonEl.value = JSON.stringify(collectStyleCornersJson());' +
'    if (btn) btn.disabled = false;' +
'  });' +
'}' +
'function startRenamePresetSlot(n) {' +
'  var btn = document.getElementById("presetApplyBtn" + n);' +
'  var input = document.getElementById("presetNameInput" + n);' +
'  var nameEl = document.getElementById("presetSlot" + n + "Name");' +
'  if (!input || !btn) return;' +
'  input.value = nameEl ? nameEl.value : ("Preset " + n);' +
'  btn.style.display = "none";' +
'  input.style.display = "";' +
'  input.focus();' +
'  input.select();' +
'}' +
'function commitRenamePresetSlot(n) {' +
'  var btn = document.getElementById("presetApplyBtn" + n);' +
'  var input = document.getElementById("presetNameInput" + n);' +
'  var nameEl = document.getElementById("presetSlot" + n + "Name");' +
'  if (!input || !btn) return;' +
'  var newName = input.value.replace(/^\\s+|\\s+$/g, "") || ("Preset " + n);' +
'  if (nameEl) nameEl.value = newName;' +
'  btn.textContent = newName;' +
'  input.style.display = "none";' +
'  btn.style.display = "";' +
'}' +
'function onAuroraEnabledChange() {' +
'  var enabled = document.getElementById("auroraEnabled").checked;' +
'  var astro = findCategory("astro");' +
'  var hasIt = astro.items.some(function (it) { return it.id === 84; });' +
'  if (enabled && !hasIt) astro.items.push({ id: 84, label: "Aurora Kp index" });' +
'  if (!enabled && hasIt) astro.items = astro.items.filter(function (it) { return it.id !== 84; });' +
'  CONTENT_SELECT_IDS.forEach(function (id) {' +
'    var sel = document.getElementById(id);' +
'    if (!sel) return;' +
'    var opt = sel.querySelector(\'option[value="84"]\');' +
'    if (enabled && !opt) {' +
'      opt = document.createElement("option");' +
'      opt.value = "84";' +
'      opt.textContent = "Aurora Kp index";' +
'      sel.appendChild(opt);' +
'    } else if (!enabled && opt) {' +
'      if (sel.value === "84") sel.value = "0";' + // dangling selection on a now-hidden option -- fall back to None
'      opt.remove();' +
'    }' +
'  });' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
'function onShowSecondsChange() {' +
'  var box = document.getElementById("showSeconds");' +
'  var btn = document.getElementById("editSecHandBtn");' +
'  if (btn) btn.disabled = !(box.checked && !box.disabled);' +
'  updatePreview();' +
'}' +
'function onSkyModeChange() {' +
'  var val = document.getElementById("skyMode").value;' +
'  document.getElementById("cloudRenderStyleRow").style.display = (val === "0") ? "" : "none";' +
'  updatePreview();' +
'}' +
'function onHandStyleChange() {' +
'  var val = document.getElementById("bigAnalogHandStyle").value;' +
'  document.getElementById("customHandSection").style.display = (val === "4") ? "" : "none";' +
'  document.getElementById("bigAnalogTransparentRow").style.display = (val === "4") ? "none" : "";' +
'  document.getElementById("bigAnalogHandsShadowRow").style.display = (val === "4") ? "none" : "";' +
'  var preset = HAND_PRESETS[val];' +
'  if (preset) {' +
'    applyHandPresetToKind("hour", preset.hour);' +
'    applyHandPresetToKind("min", preset.min);' +
'    applyHandPresetToKind("sec", preset.sec);' +
'  }' +
'  updatePreview();' +
'}' +
'function updateHandValLabels(kind) {' +
'  var p = hePopupPrefix(kind);' +
'  ["Width", "Length", "BackOffset", "ShadowDistance"].forEach(function (f) {' +
'    var el = document.getElementById(p + f);' +
'    var out = document.getElementById(p + f + "Val");' +
'    if (el && out) out.textContent = el.value + "px";' +
'  });' +
'}' +
'function onHandSliderInput(kind) {' +
'  updateHandValLabels(kind);' +
'}' +
// Pre-fills the popup from the currently-saved handHour*/handMin*/
// handSec* hidden inputs -- nothing is written back until
// saveHandEditor() runs.
'function openHandEditor(kind) {' +
'  var hp = heHiddenPrefix(kind), p = hePopupPrefix(kind);' +
'  HE_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    if (HE_CHECKBOX_FIELDS.indexOf(f) !== -1) { popupEl.checked = hidden.value === "true"; } else { popupEl.value = hidden.value; }' +
'  });' +
'  updateHandValLabels(kind);' +
'  document.getElementById("handEditorModal-" + kind).className = "modal-overlay open";' +
'}' +
'function closeHandEditor(kind) {' +
'  document.getElementById("handEditorModal-" + kind).className = "modal-overlay";' +
'}' +
'function saveHandEditor(kind) {' +
'  var hp = heHiddenPrefix(kind), p = hePopupPrefix(kind);' +
'  HE_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    hidden.value = (HE_CHECKBOX_FIELDS.indexOf(f) !== -1) ? String(popupEl.checked) : popupEl.value;' +
'  });' +
'  closeHandEditor(kind);' +
'  updatePreview();' +
'}' +
// Copies the OTHER hand's last-saved settings into this popup's draft
// controls (not committed until OK) -- direction is fixed per hand, see
// HAND_COPY_SOURCE: hour<-minute, minute<-hour, second<-minute.
'function copyHandConfig(kind) {' +
'  var srcHp = heHiddenPrefix(HAND_COPY_SOURCE[kind]);' +
'  var p = hePopupPrefix(kind);' +
'  HE_FIELDS.forEach(function (f) {' +
'    var src = document.getElementById(srcHp + f);' +
'    var dst = document.getElementById(p + f);' +
'    if (!src || !dst) return;' +
'    if (HE_CHECKBOX_FIELDS.indexOf(f) !== -1) { dst.checked = src.value === "true"; } else { dst.value = src.value; }' +
'  });' +
'  updateHandValLabels(kind);' +
'}' +

'function onCornerFontChange() {' +
'  var custom = document.getElementById("cornerCustomFont").value;' +
'  document.getElementById("cornerFontSize").disabled = (custom !== "0");' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
'function onCornerFontSizeChange() {' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
'function selectSunTimeMode(isSunTime) {' +
'  document.getElementById("showSunTime").value = isSunTime ? "true" : "false";' +
'  var buttons = document.getElementById("showSunTimeGroup").getElementsByClassName("mode-btn");' +
'  buttons[0].className = "mode-btn" + (!isSunTime ? " active" : "");' +
'  buttons[1].className = "mode-btn" + (isSunTime ? " active" : "");' +
'}' +
'function selectBottomStyle(val) {' +
'  document.getElementById("bottomStyleValue").value = val;' +
'  var buttons = document.getElementById("bottomStyleGroup").getElementsByClassName("mode-btn");' +
'  var order = ["digital", "analog", "biganalog"];' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    buttons[i].className = "mode-btn" + (order[i] === val ? " active" : "");' +
'  }' +
'  onBottomStyleChange();' +
'}' +
'function onFontChange() {' +
'  onBottomStyleChange();' +
'}' +
'function onAnalogStyleChange() { updatePreview(); }' +

'function updateColorRoleButtons(scheme) {' +
'  var colors = scheme === "night" ? nightColors() : dayColors();' +
'  var prefix = scheme === "night" ? "swatchNight" : "swatch";' +
'  document.getElementById(prefix + "Main").style.background = colors.text;' +
'  document.getElementById(prefix + "Accent").style.background = colors.accent;' +
'  document.getElementById(prefix + "Bg").style.background = colors.bg;' +
'}' +

'function hexToByte(hex) {' +
'  var r = parseInt(hex.substr(1, 2), 16), g = parseInt(hex.substr(3, 2), 16), b = parseInt(hex.substr(5, 2), 16);' +
'  function to2bit(v) { return Math.round(v / 85); }' +
'  return packedByteFor(to2bit(r), to2bit(g), to2bit(b));' +
'}' +

'function onPresetChange(scheme) {' +
'  var presetSelectId = scheme === "night" ? "nightSchemePreset" : "colorSchemePreset";' +
'  var select = document.getElementById(presetSelectId);' +
'  var picked = select.value;' +
'  if (!picked) return;' +
'  var preset = findPresetById(picked);' +
'  document.getElementById(customHiddenIdFor("text", scheme)).value = hexToByte(preset.text);' +
'  document.getElementById(customHiddenIdFor("accent", scheme)).value = hexToByte(preset.accent);' +
'  document.getElementById(customHiddenIdFor("bg", scheme)).value = hexToByte(preset.bg);' +
// Reset to the placeholder -- this dropdown never represents "current
// state", it's a one-shot apply, same as tapping each swatch and
// picking that color would be.
'  select.value = "";' +
'  updateColorRoleButtons(scheme);' +
'  if (scheme !== "night") updatePreview();' +
'}' +

'var CURRENT_PICKER_ROLE = null;' +
'var CURRENT_PICKER_SCHEME = "day";' +
'function openColorPicker(role, scheme) {' +
'  CURRENT_PICKER_ROLE = role;' +
'  CURRENT_PICKER_SCHEME = scheme || "day";' +
'  var titles = { text: "Pick main color", accent: "Pick accent color", bg: "Pick background color" };' +
'  document.getElementById("colorPickerTitle").textContent = titles[role] || "Pick a color";' +
'  renderHexColorGrid();' +
'  document.getElementById("colorPickerModal").className = "modal-overlay open";' +
'}' +
'function closeColorPicker() {' +
'  document.getElementById("colorPickerModal").className = "modal-overlay";' +
'  CURRENT_PICKER_ROLE = null;' +
'}' +
'function goBack() {' +
'  document.location = getQueryParam("return_to", "pebblejs://close#");' +
'}' +
'function openDonateModal() {' +
'  document.getElementById("donateModal").className = "modal-overlay open";' +
'}' +
'function closeDonateModal() {' +
'  document.getElementById("donateModal").className = "modal-overlay";' +
'}' +
// The bar's own height varies by device (font scaling, safe-area
// insets) and is capped at 25vh by CSS, so this measures it after
// layout rather than assuming a fixed value, and pushes the
// scrollable content down by exactly that much so nothing starts out
// hidden underneath it.
'function adjustTopBarSpacing() {' +
'  var bar = document.getElementById("topBar");' +
'  if (!bar) return;' +
'  document.body.style.paddingTop = bar.offsetHeight + "px";' +
'}' +
'window.addEventListener("load", adjustTopBarSpacing);' +
'window.addEventListener("resize", adjustTopBarSpacing);' +
'function customHiddenIdFor(role, scheme) {' +
'  var prefix = scheme === "night" ? "night" : "";' +
'  if (role === "text") return prefix ? "nightCustomTextValue" : "customTextValue";' +
'  if (role === "accent") return prefix ? "nightCustomAccentValue" : "customAccentValue";' +
'  return prefix ? "nightCustomBgValue" : "customBgValue";' +
'}' +

// Converts 0-255 RGB to HSL (h in degrees 0-360, s/l 0-1) -- used to
// arrange the wheel by hue, the way a real color wheel reads.
// Exact layout of Pebble's real color-picker tool (developer.rebble.io/
// guides/tools-and-resources/color-picker/), extracted directly from
// its SVG: each hexagon's pixel center was converted to axial (q, r)
// hex-grid coordinates (pointy-top orientation -- flat left/right
// sides, pointed top/bottom vertices, unlike the flat-top approximation
// used before), and matched to its packed color byte. The shape isn't
// a simple symmetric hexagon; flood-filling from outside the shape\'s
// bounding box found exactly 5 cells fully enclosed by colored
// neighbors on all sides but not themselves colored -- those are the
// genuine hollow gaps, listed separately below.
'var PEBBLE_WHEEL_POSITIONS = [' +
'{q:-5,r:2,b:239},{q:-4,r:1,b:223},{q:-3,r:-1,b:222},{q:-3,r:0,b:206},{q:-3,r:1,b:207},{q:-3,r:3,b:219},{q:-3,r:4,b:199},{q:-2,r:-4,b:238},{q:-2,r:-3,b:221},{q:-2,r:-2,b:205},{q:-2,r:-1,b:201},{q:-2,r:0,b:202},{q:-2,r:2,b:203},{q:-2,r:3,b:195},{q:-2,r:4,b:215},{q:-2,r:5,b:235},{q:-1,r:-3,b:204},{q:-1,r:-2,b:200},{q:-1,r:-1,b:217},{q:-1,r:0,b:218},{q:-1,r:1,b:198},{q:-1,r:2,b:194},{q:-1,r:3,b:211},{q:-1,r:4,b:214},{q:0,r:-4,b:220},{q:0,r:-2,b:216},{q:0,r:-1,b:196},{q:0,r:0,b:197},{q:0,r:2,b:193},{q:0,r:3,b:210},{q:0,r:4,b:227},{q:0,r:5,b:231},{q:1,r:-5,b:237},{q:1,r:-4,b:236},{q:1,r:-2,b:233},{q:1,r:-1,b:212},{q:1,r:2,b:209},{q:1,r:3,b:226},{q:1,r:4,b:230},{q:2,r:-2,b:232},{q:2,r:1,b:208},{q:2,r:2,b:225},{q:2,r:3,b:243},{q:2,r:4,b:247},{q:3,r:-3,b:252},{q:3,r:-2,b:248},{q:3,r:-1,b:228},{q:3,r:0,b:229},{q:3,r:1,b:224},{q:3,r:2,b:242},{q:3,r:3,b:246},{q:3,r:4,b:251},{q:4,r:-5,b:254},{q:4,r:-4,b:253},{q:4,r:-3,b:249},{q:4,r:-2,b:244},{q:4,r:-1,b:240},{q:4,r:0,b:241},{q:5,r:-1,b:245},{q:6,r:-5,b:234},{q:6,r:-4,b:192},{q:6,r:-2,b:250},{q:7,r:-5,b:255},{q:7,r:-4,b:213}' +
'];' +
'var PEBBLE_WHEEL_HOLLOW = [{q:0,r:1},{q:1,r:0},{q:1,r:1},{q:2,r:-1},{q:2,r:0}];' +

'function renderHexColorGrid() {' +
'  var container = document.getElementById("hexColorGrid");' +
'  container.innerHTML = "";' +
'  var hiddenId = customHiddenIdFor(CURRENT_PICKER_ROLE, CURRENT_PICKER_SCHEME);' +
'  var selected = parseInt(document.getElementById(hiddenId).value, 10);' +
'  var size = 15;' +
'  var centerX = 130, centerY = 127.5;' + // must match .hex-grid's fixed CSS width/height
'  function place(q, r) {' +
'    return {' +
'      x: centerX + size * Math.sqrt(3) * (q + r / 2),' +
'      y: centerY + size * 1.5 * r' +
'    };' +
'  }' +
'  for (var i = 0; i < PEBBLE_WHEEL_POSITIONS.length; i++) {' +
'    var pos = PEBBLE_WHEEL_POSITIONS[i];' +
'    var pt = place(pos.q, pos.r);' +
'    var sw = document.createElement("div");' +
'    sw.style.left = pt.x + "px";' +
'    sw.style.top = pt.y + "px";' +
'    sw.className = "hex-swatch" + (pos.b === selected ? " selected" : "");' +
'    sw.style.background = hexFromByte(pos.b);' +
'    sw.onclick = (function (byte) { return function () { pickColor(byte); }; })(pos.b);' +
'    container.appendChild(sw);' +
'  }' +
'  for (var j = 0; j < PEBBLE_WHEEL_HOLLOW.length; j++) {' +
'    var hp = PEBBLE_WHEEL_HOLLOW[j];' +
'    var hpt = place(hp.q, hp.r);' +
'    var hsw = document.createElement("div");' +
'    hsw.style.left = hpt.x + "px";' +
'    hsw.style.top = hpt.y + "px";' +
'    hsw.className = "hex-swatch hollow";' +
'    container.appendChild(hsw);' +
'  }' +
'}' +
'function pickColor(byte) {' +
'  if (!CURRENT_PICKER_ROLE) return;' +
'  var scheme = CURRENT_PICKER_SCHEME;' +
'  document.getElementById(customHiddenIdFor(CURRENT_PICKER_ROLE, scheme)).value = byte;' +
'  closeColorPicker();' +
'  updateColorRoleButtons(scheme);' +
'  if (scheme !== "night") updatePreview();' +
'}' +
'function onNightToggle() {' +
'  document.getElementById("nightSchemeSettings").style.display = document.getElementById("nightEnabled").checked ? "block" : "none";' +
'}' +

'function radioValue(name, fallback) {' +
'  var checked = document.querySelector("input[name=\\"" + name + "\\"]:checked");' +
'  return checked ? checked.value : fallback;' +
'}' +
'function save(forceRefresh, forceFullRefresh) {' +
'  var mins = parseInt(document.getElementById("updateMins").value, 10);' +
'  if (isNaN(mins) || mins < 5) mins = 20;' +
'  var bottomStyleVal = document.getElementById("bottomStyleValue").value;' +
'  var settings = {' +
'    CONFIG_AUTO_LOC: document.getElementById("autoLoc").checked,' +
'    CONFIG_LAT: document.getElementById("lat").value,' +
'    CONFIG_LON: document.getElementById("lon").value,' +
'    CONFIG_OWM_KEY: document.getElementById("owmKey").value,' +
'    CONFIG_UPDATE_MINS: mins,' +
'    CONFIG_CLOCK_FONT: document.getElementById("clockFont").value,' +
'    CONFIG_TEMP_UNIT: document.getElementById("tempUnit").value,' +
'    CONFIG_WIND_SPEED_UNIT: document.getElementById("windSpeedUnit").value,' +
'    CONFIG_AQI_UNIT: document.getElementById("aqiUnit").value,' +
'    CONFIG_ALTITUDE_UNIT: document.getElementById("altitudeUnit").value,' +
'    CONFIG_CLOUD_RENDER_STYLE: document.getElementById("cloudRenderStyle").value,' +
'    CONFIG_SKY_MODE: document.getElementById("skyMode").value,' +
'    CONFIG_WEATHER_ICON_STYLE: document.getElementById("weatherIconStyle").value,' +
'    CONFIG_SHOW_SECONDS: document.getElementById("showSeconds").checked,' +
'    CONFIG_CUSTOM_BG: document.getElementById("customBgValue").value,' +
'    CONFIG_CUSTOM_TEXT: document.getElementById("customTextValue").value,' +
'    CONFIG_CUSTOM_ACCENT: document.getElementById("customAccentValue").value,' +
'    CONFIG_NIGHT_ENABLED: document.getElementById("nightEnabled").checked,' +
'    CONFIG_NIGHT_CUSTOM_BG: document.getElementById("nightCustomBgValue").value,' +
'    CONFIG_NIGHT_CUSTOM_TEXT: document.getElementById("nightCustomTextValue").value,' +
'    CONFIG_NIGHT_CUSTOM_ACCENT: document.getElementById("nightCustomAccentValue").value,' +
'    CONFIG_BOTTOM_STYLE: bottomStyleVal || "digital",' +
'    CONFIG_ANALOG_STYLE: document.getElementById("analogStyle").value,' +
'    CONFIG_BIG_ANALOG_HAND_STYLE: document.getElementById("bigAnalogHandStyle").value,' +
'    CONFIG_BIG_ANALOG_TRANSPARENT: document.getElementById("bigAnalogTransparent").checked,' +
'    CONFIG_BIG_ANALOG_HANDS_SHADOW: document.getElementById("bigAnalogHandsShadow").checked,' +
'    CONFIG_SHADOW_TRANSLUCENT: document.getElementById("shadowTranslucent").value,' +
'    CONFIG_SHADOW_ANGLE: document.getElementById("shadowAngle").value,' +
'    CONFIG_BIG_ANALOG_MARKER_STYLE: document.getElementById("bigAnalogMarkerStyle").value,' +
'    CONFIG_BITMAP_MARKER_TRANSPARENT: document.getElementById("bitmapMarkerTransparent").checked,' +
'    CONFIG_DRAW_FEATURES_BENEATH_HANDS: document.getElementById("drawFeaturesBeneathHands").checked,' +
'    CONFIG_UPPER_MIDDLE_LINE1_CONTENT: document.getElementById("upperMiddleLine1Content").value,' +
'    CONFIG_UPPER_MIDDLE_LINE1_COLOR: document.getElementById("upperMiddleLine1Color").value,' +
'    CONFIG_UPPER_MIDDLE_LINE2_CONTENT: document.getElementById("upperMiddleLine2Content").value,' +
'    CONFIG_UPPER_MIDDLE_LINE2_COLOR: document.getElementById("upperMiddleLine2Color").value,' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT: document.getElementById("bottomMiddleLine1Content").value,' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_COLOR: document.getElementById("bottomMiddleLine1Color").value,' +
'    CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT: document.getElementById("bottomMiddleLine2Content").value,' +
'    CONFIG_BOTTOM_MIDDLE_LINE2_COLOR: document.getElementById("bottomMiddleLine2Color").value,' +
'    CONFIG_MIDDLE_LEFT_LINE1_CONTENT: document.getElementById("middleLeftLine1Content").value,' +
'    CONFIG_MIDDLE_LEFT_LINE1_COLOR: document.getElementById("middleLeftLine1Color").value,' +
'    CONFIG_MIDDLE_LEFT_LINE2_CONTENT: document.getElementById("middleLeftLine2Content").value,' +
'    CONFIG_MIDDLE_LEFT_LINE2_COLOR: document.getElementById("middleLeftLine2Color").value,' +
'    CONFIG_MIDDLE_RIGHT_LINE1_CONTENT: document.getElementById("middleRightLine1Content").value,' +
'    CONFIG_MIDDLE_RIGHT_LINE1_COLOR: document.getElementById("middleRightLine1Color").value,' +
'    CONFIG_MIDDLE_RIGHT_LINE2_CONTENT: document.getElementById("middleRightLine2Content").value,' +
'    CONFIG_MIDDLE_RIGHT_LINE2_COLOR: document.getElementById("middleRightLine2Color").value,' +
'    CONFIG_SHOW_SUN_TIME: document.getElementById("showSunTime").value === "true",' +
'    CONFIG_SHOW_ISS: document.getElementById("showIss").checked,' +
'    CONFIG_AURORA_ENABLED: document.getElementById("auroraEnabled").checked,' +
'    CONFIG_VIBRATE_ON_PHASE_CHANGE: document.getElementById("vibrateOnPhaseChange").checked,' +
'    CONFIG_STARTUP_CLOCK_ANIMATION_ENABLED: document.getElementById("startupClockAnimationEnabled").checked,' +
'    CONFIG_BG_ANIM_MODE: radioValue("bgAnimMode", "0"),' +
'    CONFIG_SHAKE_ANIM_MODE: radioValue("shakeAnimMode", "0"),' +
'    CONFIG_OUTLINE_ENABLED: document.getElementById("outlineEnabled").checked,' +
'    CONFIG_CORNER_FONT_SIZE: document.getElementById("cornerFontSize").value,' +
'    CONFIG_CORNER_CUSTOM_FONT: document.getElementById("cornerCustomFont").value,' +
'    CONFIG_CORNER_TL: document.getElementById("cornerTL").value,' +
'    CONFIG_CORNER_TR: document.getElementById("cornerTR").value,' +
'    CONFIG_CORNER_BL: document.getElementById("cornerBL").value,' +
'    CONFIG_CORNER_BR: document.getElementById("cornerBR").value,' +
'    CONFIG_CORNER_TL_COLOR: document.getElementById("cornerTLColor").value,' +
'    CONFIG_CORNER_TR_COLOR: document.getElementById("cornerTRColor").value,' +
'    CONFIG_CORNER_BL_COLOR: document.getElementById("cornerBLColor").value,' +
'    CONFIG_CORNER_BR_COLOR: document.getElementById("cornerBRColor").value,' +
'    CONFIG_STEP_GOAL: document.getElementById("stepGoal").value,' +
'    CONFIG_SUN_MOON_SIZE: document.getElementById("sunMoonSize").value,' +
'    CONFIG_SHAKE_LABEL_SECONDS: document.getElementById("shakeLabelSeconds").value,' +
'    CONFIG_LABEL_STYLE: document.getElementById("labelStyle").value,' +
'    CONFIG_BOTTOM_INFO_BAR_MODE: document.getElementById("bottomInfoBarMode").value,' +
'    CONFIG_TEST_MODE: document.getElementById("testMode").checked,' +
'    CONFIG_TEST_DATETIME: document.getElementById("testDateTime").value,' +
'    CONFIG_DEBUG_OVERRIDE_ENABLED: document.getElementById("debugOverrideEnabled").checked,' +
'    CONFIG_CUSTOM_HOUR_STYLE: document.getElementById("customHourStyle").value,' +
'    CONFIG_CUSTOM_HOUR_THICKNESS: document.getElementById("customHourThickness").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_ECC: document.getElementById("customHourInnerEcc").value,' +
'    CONFIG_CUSTOM_HOUR_OUTER_ECC: document.getElementById("customHourOuterEcc").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_BORDER: document.getElementById("customHourInnerBorder").value,' +
'    CONFIG_CUSTOM_HOUR_OUTER_BORDER: document.getElementById("customHourOuterBorder").value,' +
'    CONFIG_CUSTOM_HOUR_TRANSLUCENT: document.getElementById("customHourTranslucent").value === "true",' +
'    CONFIG_CUSTOM_HOUR_COLOR: document.getElementById("customHourColor").value,' +
'    CONFIG_CUSTOM_SEC_STYLE: document.getElementById("customSecStyle").value,' +
'    CONFIG_CUSTOM_SEC_THICKNESS: document.getElementById("customSecThickness").value,' +
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
'    CONFIG_HAND_HOUR_COLOR: document.getElementById("handHourColor").value,' +
'    CONFIG_HAND_HOUR_OUTLINE_ENABLED: document.getElementById("handHourOutlineEnabled").value,' +
'    CONFIG_HAND_HOUR_OUTLINE_COLOR: document.getElementById("handHourOutlineColor").value,' +
'    CONFIG_HAND_HOUR_TRANSLUCENT: document.getElementById("handHourTranslucent").value === "true",' +
'    CONFIG_HAND_HOUR_SHADOW_ENABLED: document.getElementById("handHourShadowEnabled").value === "true",' +
'    CONFIG_HAND_HOUR_SHADOW_DISTANCE: document.getElementById("handHourShadowDistance").value,' +
'    CONFIG_HAND_MIN_STYLE: document.getElementById("handMinStyle").value,' +
'    CONFIG_HAND_MIN_WIDTH: document.getElementById("handMinWidth").value,' +
'    CONFIG_HAND_MIN_LENGTH: document.getElementById("handMinLength").value,' +
'    CONFIG_HAND_MIN_BACK_OFFSET: document.getElementById("handMinBackOffset").value,' +
'    CONFIG_HAND_MIN_COLOR: document.getElementById("handMinColor").value,' +
'    CONFIG_HAND_MIN_OUTLINE_ENABLED: document.getElementById("handMinOutlineEnabled").value,' +
'    CONFIG_HAND_MIN_OUTLINE_COLOR: document.getElementById("handMinOutlineColor").value,' +
'    CONFIG_HAND_MIN_TRANSLUCENT: document.getElementById("handMinTranslucent").value === "true",' +
'    CONFIG_HAND_MIN_SHADOW_ENABLED: document.getElementById("handMinShadowEnabled").value === "true",' +
'    CONFIG_HAND_MIN_SHADOW_DISTANCE: document.getElementById("handMinShadowDistance").value,' +
'    CONFIG_HAND_SEC_STYLE: document.getElementById("handSecStyle").value,' +
'    CONFIG_HAND_SEC_WIDTH: document.getElementById("handSecWidth").value,' +
'    CONFIG_HAND_SEC_LENGTH: document.getElementById("handSecLength").value,' +
'    CONFIG_HAND_SEC_BACK_OFFSET: document.getElementById("handSecBackOffset").value,' +
'    CONFIG_HAND_SEC_COLOR: document.getElementById("handSecColor").value,' +
'    CONFIG_HAND_SEC_OUTLINE_ENABLED: document.getElementById("handSecOutlineEnabled").value,' +
'    CONFIG_HAND_SEC_OUTLINE_COLOR: document.getElementById("handSecOutlineColor").value,' +
'    CONFIG_HAND_SEC_TRANSLUCENT: document.getElementById("handSecTranslucent").value === "true",' +
'    CONFIG_HAND_SEC_SHADOW_ENABLED: document.getElementById("handSecShadowEnabled").value === "true",' +
'    CONFIG_HAND_SEC_SHADOW_DISTANCE: document.getElementById("handSecShadowDistance").value,' +
'    CONFIG_CENTER_CIRCLE_RADIUS: document.getElementById("centerCircleRadius").value,' +
'    CONFIG_CENTER_CIRCLE_COLOR: document.getElementById("centerCircleColor").value,' +
'    CONFIG_DEBUG_OVERRIDE_DATA: document.getElementById("debugData").value,' +
'    CONFIG_PRESET_1_NAME: document.getElementById("presetSlot1Name").value,' +
'    CONFIG_PRESET_1_JSON: document.getElementById("presetSlot1Json").value,' +
'    CONFIG_PRESET_2_NAME: document.getElementById("presetSlot2Name").value,' +
'    CONFIG_PRESET_2_JSON: document.getElementById("presetSlot2Json").value,' +
'    CONFIG_PRESET_3_NAME: document.getElementById("presetSlot3Name").value,' +
'    CONFIG_PRESET_3_JSON: document.getElementById("presetSlot3Json").value' +
'  };' +
// Transient, one-shot -- read once by index.js's webviewclosed
// handler to decide whether this save should force an immediate
// network refetch ("Force refresh now") or just apply cosmetic
// settings and let the normal refresh cadence pick up anything that
// actually needs new data -- never itself persisted via setSetting.
'  settings.CONFIG_FORCE_REFRESH = !!forceRefresh || !!forceFullRefresh;' +
// Also transient/one-shot -- tells webviewclosed to additionally
// capture the resulting complete dict as its own separate
// "LAST_FULL_REFRESH_DICT" snapshot (see the Testing section's "Last
// Full Refresh Raw Data" field), independent of whatever
// LAST_COMPUTED_DICT happens to hold from the most recent send of any
// kind.
'  settings.CONFIG_FORCE_FULL_REFRESH = !!forceFullRefresh;' +
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

'updateColorRoleButtons("day");' +
'updateColorRoleButtons("night");' +
'onBottomStyleChange();' +
'updateWeatherIconStyleVisibility();' +
'adjustTopBarSpacing();' +
'setInterval(updatePreview, 1000);' +
'</script>' +
'</body></html>';
}

module.exports = { buildConfigHtml: buildConfigHtml };
