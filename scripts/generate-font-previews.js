#!/usr/bin/env node
// Reads real on-watch renderings of the settings page's font-picker
// sample text -- resources/font-previews/<fontId>_<role>.png -- and
// generates src/pkjs/font-preview-images.js as a nested lookup table
// ({ fontId: { role: dataUri } }) the settings page can embed
// directly. Same reasoning and same generated-file shape as
// generate-marker-previews.js and generate-infographics.js right next
// to this file (see either of those scripts' own top-of-file comments
// for why this has to happen at build time rather than at runtime):
// the settings page is one self-contained data:text/html URI with no
// server behind it, so any preview image has to already be baked in
// as base64 by build time.
//
// This exists specifically for the system fonts (Gothic/"System",
// Bitham, LECO -- FONT_LOOKUP ids 0-15 in config-page.js) that have no
// real Google Fonts equivalent to render a live text preview with
// (see FONT_LOOKUP's own `approx`/`google: null` comments): Gothic is
// Mark Simonson's Raster Gothic, Bitham is a renamed/relicensed
// Gotham (Hoefler&Co/Typography.com), and LECO 1976 is licensed from
// MyFonts -- none are free to embed as a live webfont. Baking a small
// PNG of fixed sample text instead sidesteps that: you're using a
// font you're already licensed to use ON THE WATCH to render your own
// fixed string to a flattened image, not redistributing the font file
// itself for a browser to render arbitrary text with -- the same
// "flatten to image, don't ship the outlines" distinction font EULAs
// generally draw between ordinary desktop use and a separate webfont
// license. This script only ever touches PNGs you already produced
// and dropped in resources/font-previews/ yourself; it has no way to
// render anything on its own.
//
// FILENAME CONVENTION: <fontId>_<role>.png
//   fontId -- FONT_LOOKUP's own numeric id (0-15 for the system fonts
//             this is meant for, though nothing here stops you from
//             adding one for any other id too)
//   role   -- one of: clock, clockFontSmall, cornerFont, markerTextFont
//             (matching FONT_PICKER_ROLES in config-page.js) -- which
//             sample text ("12:34", "Tue 12", "-10°C", "12"/"XII")
//             that PNG shows
// e.g. resources/font-previews/8_clock.png is Leco XL's own
// FONT_KEY_LECO_42_NUMBERS rendering "12:34".
//
// You do not need one of every (fontId, role) combination -- a
// missing one just falls back to the existing CSS-approximation text
// preview for that specific button, same "a slot with no PNG yet
// isn't an error" philosophy generate-infographics.js already uses.
// The Clock font picker only ever shows a font here if it's also
// `mainClock: true` in FONT_LOOKUP -- for the system fonts that's
// just ids 8, 14, 15 -- so a `clock` image for, say, id 0 (System
// Small) would be baked in but never actually shown anywhere; harmless,
// just wasted bytes.
//
// See README.md (or ask the person who set this up) for how to
// actually PRODUCE these PNGs -- in short: a tiny throwaway watchapp
// that draws each sample string with fonts_get_system_font() and the
// real FONT_KEY_* constant, run in the emulator, screenshotted, and
// cropped tight to the text.
//
// Usage:
//   node scripts/generate-font-previews.js
//
// Run this whenever you add or change a font-preview PNG in
// resources/font-previews/, before `pebble build`.

var fs = require('fs');
var path = require('path');

var SOURCE_DIR = path.join(__dirname, '..', 'resources', 'font-previews');
var OUTPUT_FILE = path.join(__dirname, '..', 'src', 'pkjs', 'font-preview-images.js');
var VALID_ROLES = ['clock', 'clockFontSmall', 'cornerFont', 'markerTextFont'];
var FILE_RE = /^([0-9]+)_(clock|clockFontSmall|cornerFont|markerTextFont)\.png$/;

var entries = {};
var found = [];
var skipped = [];
var totalBytes = 0;

var files = [];
if (fs.existsSync(SOURCE_DIR)) {
  files = fs.readdirSync(SOURCE_DIR).filter(function (f) { return f.slice(-4) === '.png'; });
}

files.forEach(function (file) {
  var m = FILE_RE.exec(file);
  if (!m) {
    skipped.push(file);
    return;
  }
  var fontId = m[1];
  var role = m[2];
  var buf = fs.readFileSync(path.join(SOURCE_DIR, file));
  totalBytes += buf.length;
  if (!entries[fontId]) entries[fontId] = {};
  entries[fontId][role] = 'data:image/png;base64,' + buf.toString('base64');
  found.push(file);
});

var lines = [];
lines.push('// GENERATED FILE -- do not edit by hand.');
lines.push('// Produced by scripts/generate-font-previews.js from');
lines.push('// resources/font-previews/<fontId>_<role>.png. Re-run that script after');
lines.push('// adding or changing any font-preview PNG, before `pebble build`.');
lines.push('module.exports = ' + JSON.stringify(entries, null, 2) + ';');
lines.push('');

fs.writeFileSync(OUTPUT_FILE, lines.join('\n'));

console.log('generate-font-previews: wrote ' + OUTPUT_FILE);
console.log('generate-font-previews: embedded ' + found.length + ' preview image(s) from ' + SOURCE_DIR + ' (' + totalBytes + ' raw bytes before base64, which itself runs about 33% larger): ' + (found.join(', ') || '(none)'));
if (found.length === 0) {
  console.log('generate-font-previews: no font-preview PNGs found yet -- every font-picker button will just show its existing CSS-approximation text preview until you add some. Not an error.');
}
if (skipped.length) {
  console.log('generate-font-previews: found ' + skipped.join(', ') + ' in ' + SOURCE_DIR + ', but the name doesn\'t match "<fontId>_<role>.png" (role must be one of ' + VALID_ROLES.join('/') + ') -- ignored.');
}

// To have this run automatically as part of your normal build instead
// of remembering to run it by hand, either:
//   - add an npm "scripts" entry, e.g.
//       "build": "node scripts/generate-font-previews.js && pebble build"
//     and use `npm run build` instead of `pebble build` directly, or
//   - add `node scripts/generate-font-previews.js` to whatever
//     shell alias/CI step you already use to invoke `pebble build`.
// `pebble build` itself has no plugin/hook system to run this
// automatically, so one of the above is required either way.
