#!/usr/bin/env node
// Reads the settings page's two picker-popup image sets --
//   resources/infographics/hands<n>.png       (n = 1..HAND_PRESETS slot count)
//   resources/infographics/marker_preset_<name>.png  (none/minimal/small/big)
// -- and generates src/pkjs/hand-style-images.js and
// src/pkjs/marker-preset-images.js as lookup tables of data: URIs the
// settings page can embed directly. Same reasoning and same
// generated-file shape as generate-example-style-previews.js and
// generate-marker-previews.js right next to this file (see either of
// those scripts' own top-of-file comments for why this has to happen
// at build time rather than at runtime): the settings page is one
// self-contained data:text/html URI with no server behind it, so any
// preview image has to already be baked in as base64 by build time.
//
// This is the HAND style picker's own image set -- the 5 existing
// bitmap MARKER styles (modern/swiss/tally/bell/brown) already have
// their own preview pipeline in generate-marker-previews.js and
// src/pkjs/marker-preview-images.js; this script only covers the 4
// procedural marker PRESETS (none/minimal/small/big) that sit
// alongside those 5 bitmaps in the marker style picker popup.
//
// Usage:
//   node scripts/generate-infographics.js
//
// Run this whenever you add or change a hand-style or marker-preset
// picture in resources/infographics/, before `pebble build`. A slot
// with no PNG yet just shows an empty placeholder tile in settings --
// not an error, and doesn't stop the app from building or running.

var fs = require('fs');
var path = require('path');

var SOURCE_DIR = path.join(__dirname, '..', 'resources', 'infographics');

function readPng(file) {
  return fs.readFileSync(path.join(SOURCE_DIR, file));
}

var allFiles = [];
if (fs.existsSync(SOURCE_DIR)) {
  allFiles = fs.readdirSync(SOURCE_DIR).filter(function (f) { return f.slice(-4) === '.png'; });
}

// ---- hand style pictures: hands<n>.png -> "n" ---------------------------
// Matching the HAND_PRESETS table's own numbering in config-page.js
// (slot "1" is the first grid button, top-left, and so on) -- doesn't
// hardcode a count, so adding a 10th hands10.png + a matching
// HAND_PRESETS["10"] entry needs no change here.
(function () {
  var OUTPUT_FILE = path.join(__dirname, '..', 'src', 'pkjs', 'hand-style-images.js');
  var HAND_RE = /^hands([0-9]+)\.png$/;
  var entries = {};
  var found = [];
  var totalBytes = 0;

  allFiles.forEach(function (file) {
    var m = HAND_RE.exec(file);
    if (!m) return;
    var buf = readPng(file);
    totalBytes += buf.length;
    entries[m[1]] = 'data:image/png;base64,' + buf.toString('base64');
    found.push(m[1]);
  });

  var lines = [];
  lines.push('// GENERATED FILE -- do not edit by hand.');
  lines.push('// Produced by scripts/generate-infographics.js from');
  lines.push('// resources/infographics/hands<n>.png. Re-run that script after adding or');
  lines.push('// changing any hand-style picture, before `pebble build`.');
  lines.push('module.exports = ' + JSON.stringify(entries, null, 2) + ';');
  lines.push('');
  fs.writeFileSync(OUTPUT_FILE, lines.join('\n'));

  console.log('generate-infographics: wrote ' + OUTPUT_FILE);
  console.log('generate-infographics: embedded ' + found.length + ' hand-style picture(s) from ' + SOURCE_DIR +
    ' (' + totalBytes + ' raw bytes before base64, which itself runs about 33% larger): ' +
    (found.sort(function (a, b) { return Number(a) - Number(b); }).join(', ') || '(none)'));
})();

// ---- marker preset pictures: marker_preset_<name>.png -> "<name>" -------
// Covers only the 4 procedural presets (none/minimal/small/big) --
// the 5 bitmap marker styles keep using generate-marker-previews.js's
// own output (marker-preview-images.js), unchanged.
(function () {
  var OUTPUT_FILE = path.join(__dirname, '..', 'src', 'pkjs', 'marker-preset-images.js');
  var PREFIX = 'marker_preset_';
  var KNOWN_NAMES = ['none', 'minimal', 'small', 'big'];
  var entries = {};
  var found = [];
  var totalBytes = 0;

  allFiles.forEach(function (file) {
    if (file.slice(0, PREFIX.length) !== PREFIX) return;
    var name = file.slice(PREFIX.length, -4); // strip prefix and ".png"
    if (KNOWN_NAMES.indexOf(name) === -1) return;
    var buf = readPng(file);
    totalBytes += buf.length;
    entries[name] = 'data:image/png;base64,' + buf.toString('base64');
    found.push(name);
  });

  var missing = KNOWN_NAMES.filter(function (n) { return found.indexOf(n) === -1; });

  var lines = [];
  lines.push('// GENERATED FILE -- do not edit by hand.');
  lines.push('// Produced by scripts/generate-infographics.js from');
  lines.push('// resources/infographics/marker_preset_<name>.png. Re-run that script after');
  lines.push('// adding or changing any marker-preset picture, before `pebble build`.');
  lines.push('module.exports = ' + JSON.stringify(entries, null, 2) + ';');
  lines.push('');
  fs.writeFileSync(OUTPUT_FILE, lines.join('\n'));

  console.log('generate-infographics: wrote ' + OUTPUT_FILE);
  console.log('generate-infographics: embedded ' + found.length + ' marker-preset picture(s) from ' + SOURCE_DIR +
    ' (' + totalBytes + ' raw bytes before base64): ' + (found.join(', ') || '(none)'));
  if (missing.length) {
    console.log('generate-infographics: no ' + missing.map(function (n) { return PREFIX + n + '.png'; }).join(', ') +
      ' found. Those marker preset buttons will simply show no picture in settings until that resource PNG exists.');
  }
})();

// To have this run automatically as part of your normal build instead
// of remembering to run it by hand, either:
//   - add an npm "scripts" entry, e.g.
//       "build": "node scripts/generate-infographics.js && node scripts/generate-example-style-previews.js && node scripts/generate-marker-previews.js && pebble build"
//     and use `npm run build` instead of `pebble build` directly, or
//   - add this script to whatever shell alias/CI step you already use
//     to invoke `pebble build`.
