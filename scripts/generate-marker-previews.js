#!/usr/bin/env node
// Reads the big-analog bitmap marker styles' own resource PNGs --
// resources/images/<name>_background.png, the same files already
// declared in package.json and used on the watch itself -- and
// generates src/pkjs/marker-preview-images.js as a lookup table of
// data: URIs the settings page can embed directly. No separate
// preview-only files needed: whatever art you're already using for a
// given marker style on the watch is exactly what shows as its
// preview in settings.
//
// Why this has to happen at build time rather than at runtime: the
// settings page (config-page.js's buildConfigHtml()) produces one
// big self-contained `data:text/html` URI with no server behind it
// and no way to fetch an external image file when it's actually
// opened on the phone -- so any preview image has to already be
// baked into that HTML as a base64 data: URI by the time the app is
// built, not loaded afterward.
//
// Usage:
//   node scripts/generate-marker-previews.js
//
// Run this whenever you add or change a marker style's PNG in
// resources/images/, before `pebble build` (or wire it into your own
// build script -- see the note at the bottom of this file).

var fs = require('fs');
var path = require('path');

var SOURCE_DIR = path.join(__dirname, '..', 'resources', 'images');
var OUTPUT_FILE = path.join(__dirname, '..', 'src', 'pkjs', 'marker-preview-images.js');
var SUFFIX = '_background.png';

// Must match the big-analog bitmap marker style IDs used throughout
// the app (see bigAnalogMarkerStyle in src/pkjs/config-page.js and
// marker_style_resource_id() in src/c/pebble-eclipse-watch.c) --
// and the resource names already declared in package.json
// (MODERN_BACKGROUND, SWISS_BACKGROUND, etc.), just lowercased since
// that's the actual filename convention on disk.
var STYLE_IDS_BY_NAME = {
  modern: '3',
  swiss: '4',
  tally: '5',
  bell: '6',
  brown: '7'
};

var entries = {};
var found = [];
var unrecognized = [];
var totalBytes = 0;

var files = [];
if (fs.existsSync(SOURCE_DIR)) {
  files = fs.readdirSync(SOURCE_DIR).filter(function (f) {
    return f.slice(-SUFFIX.length) === SUFFIX;
  });
}

files.forEach(function (file) {
  var name = file.slice(0, -SUFFIX.length);
  var styleId = STYLE_IDS_BY_NAME[name];
  if (!styleId) {
    unrecognized.push(file);
    return;
  }
  var buf = fs.readFileSync(path.join(SOURCE_DIR, file));
  totalBytes += buf.length;
  entries[styleId] = 'data:image/png;base64,' + buf.toString('base64');
  found.push(name);
});

var missingNames = Object.keys(STYLE_IDS_BY_NAME).filter(function (name) {
  return found.indexOf(name) === -1;
});

var lines = [];
lines.push('// GENERATED FILE -- do not edit by hand.');
lines.push('// Produced by scripts/generate-marker-previews.js from');
lines.push('// resources/images/*_background.png. Re-run that script after adding or');
lines.push('// changing any marker style\'s PNG, before `pebble build`.');
lines.push('module.exports = ' + JSON.stringify(entries, null, 2) + ';');
lines.push('');

fs.writeFileSync(OUTPUT_FILE, lines.join('\n'));

console.log('generate-marker-previews: wrote ' + OUTPUT_FILE);
console.log('generate-marker-previews: embedded ' + found.length + ' preview image(s) from ' + SOURCE_DIR + ' (' + totalBytes + ' raw bytes before base64, which itself runs about 33% larger): ' + (found.join(', ') || '(none)'));
if (missingNames.length) {
  console.log('generate-marker-previews: no ' + missingNames.map(function (n) { return n + SUFFIX; }).join(', ') + ' found. Those marker styles will simply show no preview image in settings until that resource PNG exists -- this is not an error, and doesn\'t affect the style working on the watch itself.');
}
if (unrecognized.length) {
  console.log('generate-marker-previews: found ' + unrecognized.join(', ') + ' in ' + SOURCE_DIR + ', but the name doesn\'t match a known marker style -- ignored. Expected one of: ' + Object.keys(STYLE_IDS_BY_NAME).map(function (n) { return n + SUFFIX; }).join(', '));
}

// To have this run automatically as part of your normal build instead
// of remembering to run it by hand, either:
//   - add an npm "scripts" entry, e.g.
//       "build": "node scripts/generate-marker-previews.js && pebble build"
//     and use `npm run build` instead of `pebble build` directly, or
//   - add `node scripts/generate-marker-previews.js` to whatever
//     shell alias/CI step you already use to invoke `pebble build`.
// `pebble build` itself has no plugin/hook system to run this
// automatically, so one of the above is required either way.
