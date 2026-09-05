#!/usr/bin/env node
// Reads one representative icon (the "partly cloudy" one, per this
// button's own request) from each of the 3 weather icon resource sets
// -- resources/images/icon_simple_partly_cloudy.png,
// icon_hollow_partly_cloudy.png, icon_fullcolor_partly_cloudy.png (see
// package.json's own media list and draw_weather_icon_simple()/
// _hollow()/_filled() in features_layer.c) -- scales each one up with
// nearest-neighbor sampling (never smooth/bilinear interpolation,
// which would blur these into a soft smudge instead of preserving
// their actual on-watch blocky/pixel-art look), and generates
// src/pkjs/weather-icon-style-previews.js as a lookup table of data:
// URIs keyed by weather_icon_style id, the same shape config-page.js's
// other generate-*.js scripts already use (see generate-infographics.js's
// own top-of-file comment for why this has to happen at build time
// rather than at runtime).
//
// This is a real pixel-scale-and-re-encode operation (not just
// packaging an existing file into base64, the way this project's
// other generate-*.js scripts work), so unlike those, this one needs
// an actual PNG decoder -- Node has no built-in one. Requires the
// "pngjs" package (pure JS, no native bindings, one of the most
// widely used PNG libraries for exactly this kind of task):
//   npm install pngjs --save-dev
//
// The nearest-neighbor scaling loop itself was cross-checked against
// Pillow's own NEAREST resize on a test image with partial alpha
// before this script was written, and produced byte-identical output
// -- the algorithm is solid. What ISN'T verified here is pngjs's own
// decode/encode round-trip against these project's REAL icon
// resources, since resources/images/ wasn't present to test against
// when this script was written (only a synthetic 4x4 test bitmap
// was available) -- run this once after restoring your resources/
// folder and eyeball the 3 previews in settings before trusting it
// blindly.
//
// Usage:
//   npm install pngjs --save-dev   (once)
//   node scripts/generate-weather-icon-previews.js
//
// Run this whenever you change any of the 3 "partly cloudy" icon
// source PNGs, before `pebble build`. A missing source PNG just means
// that one style's button shows its plain text name with no icon in
// the picker -- not an error, and doesn't stop the app from building.

var fs = require('fs');
var path = require('path');
var PNG;
try {
  PNG = require('pngjs').PNG;
} catch (e) {
  console.error('generate-weather-icon-previews: the "pngjs" package is required but not installed.');
  console.error('generate-weather-icon-previews: run `npm install pngjs --save-dev` first, then re-run this script.');
  process.exit(1);
}

var SOURCE_DIR = path.join(__dirname, '..', 'resources', 'images');
var OUTPUT_FILE = path.join(__dirname, '..', 'src', 'pkjs', 'weather-icon-style-previews.js');

// Target pixel HEIGHT of the scaled-up preview -- chosen to roughly
// match this popup's own button row text size (see .weather-icon-
// style-preview's own CSS in config-page.js), not any fixed multiple.
// The actual scale factor used is whatever integer this rounds to for
// each source image's own height, so a source that's already close to
// this size barely scales at all, while a genuinely tiny source (the
// on-watch icons are drawn at 16x12px, see ICON_ROWS in
// features_layer.c) scales up several times. Always an INTEGER factor
// -- a fractional one would need to resample between source pixels,
// which is exactly the blur this script exists to avoid.
var TARGET_HEIGHT = 44;

// Matches weather_icon_style (see eclipse_data.h) and the Weather icon
// style picker's own <option value>.
var STYLES = [
  { id: '0', file: 'icon_simple_partly_cloudy.png' },
  { id: '1', file: 'icon_hollow_partly_cloudy.png' },
  { id: '2', file: 'icon_fullcolor_partly_cloudy.png' }
];

function nearestNeighborScale(src, factor) {
  var outW = src.width * factor, outH = src.height * factor;
  var out = new PNG({ width: outW, height: outH });
  for (var y = 0; y < outH; y++) {
    var sy = Math.floor(y / factor);
    for (var x = 0; x < outW; x++) {
      var sx = Math.floor(x / factor);
      var si = (sy * src.width + sx) << 2;
      var di = (y * outW + x) << 2;
      out.data[di] = src.data[si];
      out.data[di + 1] = src.data[si + 1];
      out.data[di + 2] = src.data[si + 2];
      out.data[di + 3] = src.data[si + 3];
    }
  }
  return out;
}

var entries = {};
var found = [];
var totalBytes = 0;

STYLES.forEach(function (s) {
  var filePath = path.join(SOURCE_DIR, s.file);
  if (!fs.existsSync(filePath)) return;
  var srcBuf = fs.readFileSync(filePath);
  var src = PNG.sync.read(srcBuf);
  var factor = Math.max(1, Math.round(TARGET_HEIGHT / src.height));
  var scaled = factor > 1 ? nearestNeighborScale(src, factor) : src;
  var outBuf = PNG.sync.write(scaled);
  totalBytes += outBuf.length;
  entries[s.id] = 'data:image/png;base64,' + outBuf.toString('base64');
  found.push(s.file + ' (' + src.width + 'x' + src.height + ' -> ' + scaled.width + 'x' + scaled.height + ', ' + factor + 'x)');
});

var missing = STYLES.filter(function (s) { return !entries[s.id]; }).map(function (s) { return s.file; });

var lines = [];
lines.push('// GENERATED FILE -- do not edit by hand.');
lines.push('// Produced by scripts/generate-weather-icon-previews.js from');
lines.push('// resources/images/icon_<style>_partly_cloudy.png. Re-run that script after');
lines.push('// changing any of those 3 source icons, before `pebble build`.');
lines.push('module.exports = ' + JSON.stringify(entries, null, 2) + ';');
lines.push('');

fs.writeFileSync(OUTPUT_FILE, lines.join('\n'));

console.log('generate-weather-icon-previews: wrote ' + OUTPUT_FILE);
console.log('generate-weather-icon-previews: embedded ' + found.length + ' preview(s) from ' + SOURCE_DIR + ':');
found.forEach(function (f) { console.log('  ' + f); });
if (missing.length) {
  console.log('generate-weather-icon-previews: no ' + missing.join(', ') +
    ' found. Those style(s) will simply show their plain text name with no icon in the picker until that resource PNG exists.');
}

// To have this run automatically as part of your normal build instead
// of remembering to run it by hand, either:
//   - add an npm "scripts" entry, e.g.
//       "build": "node scripts/generate-weather-icon-previews.js && pebble build"
//     and use `npm run build` instead of `pebble build` directly, or
//   - add `node scripts/generate-weather-icon-previews.js` to whatever
//     shell alias/CI step you already use to invoke `pebble build`.
