// ---- settings-page derived state ------------------------------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 third slice). This is the precompute step buildConfigHtml() used
// to run inline at its own top: turning the raw `current` settings
// snapshot into every derived flag/value the template (still in
// config-page.js) reads while building the page -- which layout is
// active, whether seconds/side-columns are available for the current
// font, the example-style button row's HTML, the initial color swatch
// values, and so on.
//
// deriveConfigState(current) is the only export -- one function in,
// one plain object out, no side effects. Nothing here renders HTML
// itself (exampleStylesButtonsHtml is the one exception: it's already
// a small HTML fragment in the original code, kept exactly as-is
// rather than inventing a new boundary for a single field).

var presetsLookups = require('../presets-lookups');
var CORNER_CATEGORIES = presetsLookups.CORNER_CATEGORIES;
var EXAMPLE_STYLE_COUNT = 9;
var EXAMPLE_STYLE_IMAGES = require('../data/generated/example-style-images');
var EXAMPLE_STYLE_PRESETS = require('../example-style-presets');
var configFonts = require('./config-fonts');
var fontLookupEntry = configFonts.fontLookupEntry;
var secondsAvailableForDigital = configFonts.secondsAvailableForDigital;
var fontOptionsHtml = configFonts.fontOptionsHtml;

function deriveConfigState(current) {
  var autoLocChecked = current.autoLoc ? 'checked' : '';
  var manualDisabled = current.autoLoc ? 'disabled' : '';
  var testModeChecked = current.testMode ? 'checked' : '';
  var testDisabled = current.testMode ? '' : 'disabled';
  // Falls back to the most recently sent raw chunk (see
  // rawMessageLogButtonsHtml() in config-debug.js) rather than the old
  // single LAST_COMPUTED_DICT snapshot -- same as clicking the newest of the
  // buttons below would load, just done automatically on page open.
  var rawLogEntries = current.rawMessageLog || [];
  var mostRecentRawChunk = rawLogEntries.length ? rawLogEntries[rawLogEntries.length - 1].dict : null;
  var debugTextareaInitial = (current.debugOverrideEnabled && current.debugOverrideData)
    ? current.debugOverrideData
    : (mostRecentRawChunk ? JSON.stringify(mostRecentRawChunk, null, 2) : '');
  var bottomStyleVal = (current.bottomStyle === 'analog' || current.bottomStyle === 'biganalog') ? 'analog'
    : (current.bottomStyle === 'digitalTop' ? 'digitalTop' : 'digital');
  var isAnalog = bottomStyleVal === 'analog';
  // isDigitalBar/isDigitalTop split what used to be a single "not
  // analog" case into the two digital layouts (opaque panel at the
  // bottom vs transparent panel at the top -- see DIGITAL TOP's own
  // help text below) -- isDigital (either one) covers everywhere the
  // two behave identically (clock font choice, seconds availability,
  // side-feature availability), which is most call sites; the split
  // ones are only where the two layouts actually differ (which panel
  // icon/label is active, and the live preview/slot diagram further
  // down, which really do need to draw the panel in a different place).
  var isDigitalBar = bottomStyleVal === 'digital';
  var isDigitalTop = bottomStyleVal === 'digitalTop';
  var isDigital = isDigitalBar || isDigitalTop;
  var clockFontId = parseInt(current.clockFont || '8', 10);
  // sidesAllowed only ever appears on mainClock fonts (see FONT_LOOKUP's
  // own comment) -- default to 2 (unrestricted) for the rare case a
  // stale/hand-edited clockFont value points at a non-mainClock entry.
  var clockSidesAllowed = typeof fontLookupEntry(clockFontId).sidesAllowed === 'number' ? fontLookupEntry(clockFontId).sidesAllowed : 2;
  var clockFontIsWide = clockSidesAllowed === 0;
  var digitalSidesPreferredVal = current.digitalSidesPreferred || current.digitalSides || 'none';
  // The user's actual preference (digitalSidesPreferredVal, persisted
  // separately -- see index.js's own CONFIG_DIGITAL_SIDES_PREFERRED)
  // survives a restrictive font/layout untouched; digitalSidesVal is
  // just this render's EFFECTIVE value given the CURRENT font, and is
  // what everything else on this page (avail.digitalLeft/Right,
  // edgeVal, the live preview, and what actually gets sent to the
  // watch) has always read. Recomputed fresh here and by
  // updateDigitalSidesVisibility()'s own live copy of this same logic
  // -- never persisted directly -- so a later switch to a more
  // permissive font (or out of Analog and back) brings the original
  // preference back instead of whatever it had collapsed down to.
  var digitalSidesVal = !isDigital ? 'none'
    : clockFontIsWide ? 'none'
    : (clockSidesAllowed === 1 && digitalSidesPreferredVal === 'both') ? 'right'
    : digitalSidesPreferredVal;
  var digitalLeftOn = digitalSidesVal === 'left' || digitalSidesVal === 'both';
  var digitalRightOn = digitalSidesVal === 'right' || digitalSidesVal === 'both';
  // A font's own FONT_LOOKUP sidesWithSeconds tier decides all of
  // this now (see its own comment in presets-lookups.js for what each
  // of the 4 values means): tier -1 blocks seconds outright regardless
  // of digitalSidesVal (the same fonts sidesAllowed 0 already flags as
  // too wide for a side column in the first place); tier 2 allows it
  // no matter what; tier 0 (the common case) only allows it with NO
  // side column active; tier 1 allows it with 0 or 1 side column
  // active but not both -- side columns compete for the same
  // horizontal space seconds would need, so going from 0 to 1 active
  // sides can knock it out even for a font that was fine a moment ago
  // (tier 1 fonts get one more step of headroom before that happens
  // than tier 0 ones do).
  var secondsUnsupported = isDigital && !secondsAvailableForDigital(fontLookupEntry(clockFontId), digitalSidesVal);
  var secondsChecked = (current.showSeconds && !secondsUnsupported) ? 'checked' : '';
  var secondsDisabled = secondsUnsupported ? 'disabled' : '';
  var cornerFontId = parseInt(current.cornerFont || '1', 10);

  // Client-side copy of CORNER_CATEGORIES (see presets-lookups.js's
  // own header comment), filtered the same way the old hand-typed
  // version was: id 84 ("Aurora Kp index") only present when auroras
  // are actually on -- onAuroraEnabledChange() handles adding/removing
  // it live client-side if that checkbox changes without a reload.
  var cornerCategoriesForClient = CORNER_CATEGORIES.map(function (cat) {
    if (cat.id !== 'astro' || current.auroraEnabled) return cat;
    return { id: cat.id, label: cat.label, icon: cat.icon, items: cat.items.filter(function (it) { return it.id !== 84; }) };
  });

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
  // suppressed? Must match computeSlotAvailability()'s client-side
  // logic (and features_recompute_slots's rules in features_layer.c)
  // exactly, or the settings page would show slots as available that
  // the watch itself won't actually draw. (Not currently read by
  // anything below -- computeSlotAvailability() is what actually
  // drives the rendered page -- but kept in sync anyway since this
  // comment already promises it matches, and a future reader/caller
  // shouldn't inherit a silently-stale copy.)
  var markerStyleNum = parseInt(current.bigAnalogMarkerStyle || '0', 10);
  var isBitmapMarkerStyle = markerStyleNum >= 3 && markerStyleNum !== 8 && markerStyleNum !== 9;
  var bitmapCornerOverride = !!current.bitmapCornerOverride;
  var edgeAvail = { upper: false, bottom: false, left: false, right: false, cornersGrayed: false };
  if (isAnalog) {
    if (markerStyleNum < 3 || markerStyleNum === 8 || markerStyleNum === 9) {
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: false };
    } else if (markerStyleNum === 3 || markerStyleNum === 4 || markerStyleNum === 6) {
      edgeAvail = { upper: true, bottom: true, left: bitmapCornerOverride, right: bitmapCornerOverride, cornersGrayed: !bitmapCornerOverride };
    } else if (markerStyleNum === 5) {
      // Tally -- its own mask art leaves all 4 corners clear (unlike
      // every other bitmap style), so it alone keeps them active
      // regardless of the override checkbox.
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: false };
    } else if (markerStyleNum === 7) {
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: !bitmapCornerOverride };
    } else {
      edgeAvail = { upper: true, bottom: bitmapCornerOverride, left: bitmapCornerOverride, right: bitmapCornerOverride, cornersGrayed: !bitmapCornerOverride };
    }
  }
  var fontOptions = fontOptionsHtml(clockFontId, true);

  function hexFromPackedByte(byte) {
    var b = parseInt(byte, 10);
    if (isNaN(b)) return '#000000';
    var r2 = (b >> 4) & 3, g2 = (b >> 2) & 3, b2 = b & 3;
    function ch(v) { var h = (v * 85).toString(16); return h.length < 2 ? '0' + h : h; }
    return '#' + ch(r2) + ch(g2) + ch(b2);
  }
  // Colors are always three concrete packed bytes now -- there's no
  // "preset vs custom" mode stored anywhere. Picking a preset (see
  // chooseColorPreset() below) just writes its RGB straight into these
  // same three hidden fields, same as tapping each swatch individually
  // would, so the page only ever has one representation of "current
  // colors" to read back on load -- whether that happens to currently
  // match one of COLOR_SCHEMES is worked out fresh each time by
  // matchingPresetId(), not tracked as its own separate state.
  function resolveInitialColors(bgByte, textByte, accentByte) {
    return {
      bg: hexFromPackedByte(bgByte),
      text: hexFromPackedByte(textByte),
      accent: hexFromPackedByte(accentByte)
    };
  }
  var initialColors = resolveInitialColors(current.customBg || '255', current.customText || '192', current.customAccent || '192');
  var initialNightColors = resolveInitialColors(current.nightCustomBg || '192', current.nightCustomText || '255', current.nightCustomAccent || '255');

  return {
    autoLocChecked: autoLocChecked,
    manualDisabled: manualDisabled,
    testModeChecked: testModeChecked,
    testDisabled: testDisabled,
    rawLogEntries: rawLogEntries,
    mostRecentRawChunk: mostRecentRawChunk,
    debugTextareaInitial: debugTextareaInitial,
    bottomStyleVal: bottomStyleVal,
    isAnalog: isAnalog,
    isDigitalBar: isDigitalBar,
    isDigitalTop: isDigitalTop,
    isDigital: isDigital,
    clockFontId: clockFontId,
    clockSidesAllowed: clockSidesAllowed,
    clockFontIsWide: clockFontIsWide,
    digitalSidesPreferredVal: digitalSidesPreferredVal,
    digitalSidesVal: digitalSidesVal,
    digitalLeftOn: digitalLeftOn,
    digitalRightOn: digitalRightOn,
    secondsUnsupported: secondsUnsupported,
    secondsChecked: secondsChecked,
    secondsDisabled: secondsDisabled,
    cornerFontId: cornerFontId,
    cornerCategoriesForClient: cornerCategoriesForClient,
    exampleStylesButtonsHtml: exampleStylesButtonsHtml,
    markerStyleNum: markerStyleNum,
    isBitmapMarkerStyle: isBitmapMarkerStyle,
    bitmapCornerOverride: bitmapCornerOverride,
    edgeAvail: edgeAvail,
    fontOptions: fontOptions,
    initialColors: initialColors,
    initialNightColors: initialNightColors
  };
}

module.exports = {
  deriveConfigState: deriveConfigState
};
