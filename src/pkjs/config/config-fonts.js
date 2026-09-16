// ---- font domain: lookup, availability, and <option> rendering -----------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 second slice). Everything here reads only FONT_LOOKUP
// (presets-lookups.js) plus the generic esc() helper (config-
// template.js) -- no dependency on hands, markers, or any other
// domain, and nothing else in config-page.js depends on these except
// by calling them.

var presetsLookups = require('../presets-lookups');
var FONT_LOOKUP = presetsLookups.FONT_LOOKUP;
var esc = require('./config-template').esc;

// Same "don't silently do the opposite of what was written" fix as
// secondsAvailableForDigital()'s parseInt change below, for
// FONT_LOOKUP's boolean fields (mainClock, small, italic, romanOk)
// instead of its numeric ones. A plain `f.mainClock` truthy check treats the
// STRING 'false' as true (any non-empty string is truthy in JS), so a
// value that's accidentally been quoted -- an easy mistake, since
// `mainClock: false` and `small: false` are both written explicitly
// throughout FONT_LOOKUP already, not just omitted -- silently flips
// to the opposite of what was intended instead of erroring. Leaves
// every other value's existing truthy/falsy behavior untouched
// (including a bare `true`/omitted/undefined), so this only closes
// the specific 'true'/'false' string trap, not a general type check.
function fontFlag(v) {
  if (v === 'false') return false;
  if (v === 'true') return true;
  return !!v;
}

// Builds one combined Google Fonts stylesheet URL covering every
// `google` family (at its own specific weight/italic) FONT_LOOKUP
// actually uses, deduplicated -- so the settings page only ever makes
// one request instead of one per font. Used for the live font-picker
// previews (see fontPickerModal below) -- purely cosmetic, the watch
// itself never touches Google Fonts.
function googleFontsHref() {
  var seen = {};
  var params = [];
  FONT_LOOKUP.forEach(function (f) {
    if (!f.google) return;
    var italAxis = fontFlag(f.italic) ? '1' : '0';
    var key = f.google + '|' + italAxis + '|' + f.weight;
    if (seen[key]) return;
    seen[key] = true;
    params.push('family=' + encodeURIComponent(f.google).replace(/%20/g, '+') + ':ital,wght@' + italAxis + ',' + f.weight);
  });
  return 'https://fonts.googleapis.com/css2?' + params.join('&') + '&display=swap';
}

// Same idea as googleFontsHref() above, for the handful of fonts
// (RebelRedux, Digital dream, Fizzy Soda as of this comment -- see
// each entry's own `cdn` field and comment in presets-lookups.js)
// that aren't on Google Fonts at all but ARE hosted as a free webfont
// on cdnfonts.com. Unlike Google Fonts (one combined stylesheet
// request covering every family), cdnfonts.com serves one stylesheet
// per font family, so this returns one <link> per distinct `cdn` slug
// instead of trying to combine them -- still just one request per
// font actually used, not per FONT_LOOKUP entry (Digital Dream Small/
// Digital Dream share the same slug and so the same single link).
function cdnFontLinks() {
  var seen = {};
  var links = [];
  FONT_LOOKUP.forEach(function (f) {
    if (!f.cdn || seen[f.cdn]) return;
    seen[f.cdn] = true;
    links.push('<link rel="stylesheet" href="https://fonts.cdnfonts.com/css/' + encodeURIComponent(f.cdn) + '">');
  });
  return links.join('');
}

// Fastest way to go from an id to its entry -- every font picker
// needs this (rendering the current selection, gating Show Seconds,
// auto-pairing the small companion, etc.).
function fontLookupEntry(id) {
  for (var i = 0; i < FONT_LOOKUP.length; i++) {
    if (FONT_LOOKUP[i].id === id) return FONT_LOOKUP[i];
  }
  return FONT_LOOKUP[0];
}

// Shared by secondsAvailableForDigital() and secondsUnavailableReason()
// below so the "how do we read this field" logic lives in exactly one
// place. parseInt (not a strict typeof check) so a value that's
// accidentally been quoted as a string in FONT_LOOKUP (e.g.
// sidesWithSeconds: '-1' instead of -1) still reads correctly instead
// of silently falling through to the "no value set" default -- that
// default exists for genuinely missing fields, not as a catch-all for
// wrong types, and the two are easy to conflate with a plain typeof
// guard since both look identical from the outside (no error, just a
// wrong tier).
function sidesWithSecondsTier(fontEntry) {
  var raw = parseInt(fontEntry.sidesWithSeconds, 10);
  return isNaN(raw) ? 0 : raw;
}

// Whether "Show seconds" can be offered at all for a mainClock font,
// given which digital side column(s) (if any) are currently active --
// side features compete for the same horizontal space seconds would
// need. Entirely driven by the font's own FONT_LOOKUP
// `sidesWithSeconds` tier now (see its own comment there for what
// each of the 4 values means) -- tier -1 blocks seconds outright
// regardless of digitalSidesVal, tier 0 (the common case) only allows
// it with NO side column active, tier 1 allows it with 0 or 1 side
// column active but not both, and tier 2 allows it regardless. Has a
// client-side twin further down (used by onBottomStyleChange() and
// toggleDigitalSide() for live updates as the user actually toggles a
// side) kept in exact sync with this one -- both read the same
// FONT_LOOKUP field, so there's nothing for the two copies to
// disagree about even if their surrounding code differs.
function secondsAvailableForDigital(fontEntry, digitalSidesVal) {
  var tier = sidesWithSecondsTier(fontEntry);
  if (tier === -1) return false;
  if (tier === 2) return true;
  if (tier === 1) return !digitalSidesVal || digitalSidesVal !== 'both';
  return !digitalSidesVal || digitalSidesVal === 'none';
}

// Companion to secondsAvailableForDigital() -- only ever meaningful to
// call once that's already returned false, so it doesn't re-check
// digitalSidesVal itself: by the time seconds is unavailable, the
// TIER alone (not which specific side combination triggered it)
// determines which of the 3 blocking reasons applies, since each
// tier has exactly one way to end up unavailable. Surfaced in the
// Show Seconds help text (secondsHelp) instead of that field's old
// one-size-fits-all "This font doesn't support showing seconds."
function secondsUnavailableReason(fontEntry) {
  var tier = sidesWithSecondsTier(fontEntry);
  if (tier === -1) return 'This font doesn\'t support showing seconds at all.';
  if (tier === 1) return 'This font requires 1 or fewer side features enabled to show seconds.';
  return 'This font requires no side features enabled to show seconds.';
}

// Full help text for the Side features section's own "?" panel --
// the base explanation already varied by sidesAllowed (room for one
// column vs both), now with an extra sentence appended when this
// font's sidesWithSeconds tier means turning on (more) side features
// will cost the user Show Seconds, so that tradeoff isn't only
// discovered after the fact via the Show Seconds checkbox quietly
// graying out. No extra sentence for tier -1 (seconds was never on
// the table regardless of side features, so mentioning sides here
// would be misleading) or tier 2 (no tradeoff to warn about).
function digitalSidesHelpText(sidesAllowedVal, secondsTier) {
  var base = (sidesAllowedVal === 1)
    ? 'This font only has room for one side column at a time -- pick left OR right, not both.'
    : 'Adds up to 3 short info lines down each side of the digital clock, on the clock\'s own panel -- pick their content on the diagram above. Only offered for narrower clock fonts (this one qualifies); picking both sides assumes the font is already narrow enough to share the panel with them without shrinking the clock any further.';
  if (secondsTier === 0) {
    return base + ' Turning on a side feature will also turn off Show seconds for this font.';
  }
  if (secondsTier === 1) {
    return base + ' Turning on both side features at once will also turn off Show seconds for this font -- one side keeps it available.';
  }
  return base;
}

// Renders <option>s for one of the four font pickers. `onlyMainClock`
// restricts to the Clock font picker's own subset (see FONT_LOOKUP's
// own comment above); the other three pickers (clock's small
// companion, marker text, corner/edge content) get every font.
function fontOptionsHtml(selectedId, onlyMainClock) {
  return FONT_LOOKUP.filter(function (f) {
    return !onlyMainClock || fontFlag(f.mainClock);
  }).map(function (f) {
    // parseInt, not a strict typeof check -- see secondsAvailableForDigital()'s
    // own comment on why: a quoted-string sidesAllowed value would
    // otherwise silently be treated as absent and fall back to 2.
    var sidesAllowedRaw = parseInt(f.sidesAllowed, 10);
    var sidesAllowedVal = isNaN(sidesAllowedRaw) ? 2 : sidesAllowedRaw;
    return '<option value="' + f.id + '" data-preview="' + esc(f.preview) + '" data-height="' + f.height +
      '" data-small="' + (fontFlag(f.small) ? '1' : '0') +
      '" data-sides-allowed="' + sidesAllowedVal + '"' +
      (selectedId === f.id ? ' selected' : '') + '>' + esc(f.label) + '</option>';
  }).join('');
}

module.exports = {
  fontFlag: fontFlag,
  googleFontsHref: googleFontsHref,
  cdnFontLinks: cdnFontLinks,
  fontLookupEntry: fontLookupEntry,
  sidesWithSecondsTier: sidesWithSecondsTier,
  secondsAvailableForDigital: secondsAvailableForDigital,
  secondsUnavailableReason: secondsUnavailableReason,
  digitalSidesHelpText: digitalSidesHelpText,
  fontOptionsHtml: fontOptionsHtml
};
