#!/usr/bin/env node
// Reads resources/hand-style-icons/<style name>.png -- the small
// silhouette icons for the hand-style picker popup's own button list
// (see scripts/generate_hand_style_icons.py, which renders these from
// this project's own compute_hand_geometry_fp() geometry; run that
// script first if you've changed a shape) -- and generates
// src/pkjs/hand-style-icon-images.js as a lookup table of data: URIs
// keyed by HandConfig.style id, the same shape config-page.js's other
// generate-*.js scripts already use (see generate-infographics.js's
// own top-of-file comment for why this has to happen at build time:
// the settings page is one self-contained data:text/html URI with no
// server behind it).
//
// This is a DIFFERENT image set from generate-infographics.js's own
// resources/infographics/<style name>.png -- those are the full-width
// explainer diagram (with the A/B/C/... slider-label lines) shown at
// the top of the hand EDITOR popup; these are small unlabeled
// silhouettes for the STYLE PICKER's own button list, one per
// HandConfig.style value, deliberately kept in a separate resources/
// subfolder so the two never collide on a filename.
//
// Usage:
//   node scripts/generate-hand-style-icons.js
//
// Run this after (re-)running generate_hand_style_icons.py, before
// `pebble build`. A style with no PNG yet just shows its plain text
// name with no icon in that one button -- not an error.

var fs = require('fs');
var path = require('path');

var SOURCE_DIR = path.join(__dirname, '..', 'resources', 'hand-style-icons');
var OUTPUT_FILE = path.join(__dirname, '..', 'src', 'pkjs', 'hand-style-icon-images.js');

// Matches HandConfig.style (see hand_layer.h) and the Shape picker's
// own <option value> -- same numbering generate-infographics.js's own
// STYLE_IDS_BY_NAME uses for the explainer-diagram set, kept in sync
// by hand since the two scripts have no reason to share code.
var STYLE_IDS_BY_NAME = {
  baton: '0', galba: '1', pencil: '2', dauphine: '3', sword: '4',
  pomme: '5', spade: '6', arrow: '7', leaf: '8', syringe: '9', serpentine: '10'
};

var entries = {};
var found = [];
var totalBytes = 0;

var allFiles = [];
if (fs.existsSync(SOURCE_DIR)) {
  allFiles = fs.readdirSync(SOURCE_DIR).filter(function (f) { return f.slice(-4) === '.png'; });
}

allFiles.forEach(function (file) {
  var name = file.slice(0, -4); // strip ".png"
  var styleId = STYLE_IDS_BY_NAME[name];
  if (!styleId) return;
  var buf = fs.readFileSync(path.join(SOURCE_DIR, file));
  totalBytes += buf.length;
  entries[styleId] = 'data:image/png;base64,' + buf.toString('base64');
  found.push(name);
});

var missing = Object.keys(STYLE_IDS_BY_NAME).filter(function (n) { return found.indexOf(n) === -1; });

var lines = [];
lines.push('// GENERATED FILE -- do not edit by hand.');
lines.push('// Produced by scripts/generate-hand-style-icons.js from');
lines.push('// resources/hand-style-icons/<style name>.png (themselves rendered by');
lines.push('// scripts/generate_hand_style_icons.py). Re-run both, in that order, after');
lines.push('// changing any hand style\'s icon, before `pebble build`.');
lines.push('module.exports = ' + JSON.stringify(entries, null, 2) + ';');
lines.push('');

fs.writeFileSync(OUTPUT_FILE, lines.join('\n'));

console.log('generate-hand-style-icons: wrote ' + OUTPUT_FILE);
console.log('generate-hand-style-icons: embedded ' + found.length + ' hand-style icon(s) from ' + SOURCE_DIR +
  ' (' + totalBytes + ' raw bytes before base64, which itself runs about 33% larger): ' + (found.join(', ') || '(none)'));
if (missing.length) {
  console.log('generate-hand-style-icons: no ' + missing.map(function (n) { return n + '.png'; }).join(', ') +
    ' found. Those styles will simply show their plain text name with no icon in the picker until that resource PNG exists.');
}

// To have this run automatically as part of your normal build instead
// of remembering to run it by hand, either:
//   - add an npm "scripts" entry, e.g.
//       "build": "python3 scripts/generate_hand_style_icons.py && node scripts/generate-hand-style-icons.js && pebble build"
//     and use `npm run build` instead of `pebble build` directly, or
//   - add both of those commands to whatever shell alias/CI step you
//     already use to invoke `pebble build`.
