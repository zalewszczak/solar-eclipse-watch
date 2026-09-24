#!/usr/bin/env node

var fs = require('fs');
var path = require('path');
var sharp = require('sharp');

var SOURCE_DIR = path.join(__dirname, '..', 'resources', 'infographics');
var OUTPUT_FILE = path.join(__dirname, '..', 'src', 'pkjs', 'data', 'generated', 'marker-preset-images.js');
var PREFIX = 'marker_preset_';
var KNOWN_NAMES = ['braun', 'minimal', 'classy', 'swiss'];

var entries = {};
var found = [];
var totalBytes = 0;

var files = [];
if (fs.existsSync(SOURCE_DIR)) {
  files = fs.readdirSync(SOURCE_DIR).filter(function (f) { return f.slice(-4) === '.png'; });
}

// ---- marker preset pictures: marker_preset_<name>.png -> "<name>" -------
// Covers only the 4 procedural presets (none/minimal/small/big) --
// the 5 bitmap marker styles keep using generate-marker-previews.js's
// own output (marker-preview-images.js), unchanged.
async function buildPresetPreviews() {
  for (const file of files) {
    if (file.slice(0, PREFIX.length) !== PREFIX) {
//      console.log("dropping " + file + ": prefix doesnt match");
      continue;
    }
    var name = file.slice(PREFIX.length, -4); // strip prefix and ".png"
    if (KNOWN_NAMES.indexOf(name) === -1) {
//      console.log("dropping " + file + ": name doesnt match (" + name + ")");
      continue;
    }
    
    var sourcePath = path.join(SOURCE_DIR, file);
    var sourceBuffer = fs.readFileSync(sourcePath);
    totalBytes += sourceBuffer.length;

    var metadata = await sharp(sourceBuffer).metadata();

    if (!metadata.width || !metadata.height) {
      console.error(
        'generate-font-previews: could not determine dimensions of ' + file
      );
      continue;
    }

    // 1. Crop and extract raw RGBA pixel data
    var rawImage = await sharp(sourceBuffer)
      .extract({
        left: 0,
        top: 0,
        width: metadata.width,
        height: metadata.height
      })
      .ensureAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true });

    var pixels = rawImage.data;
    var channels = rawImage.info.channels; // Guaranteed to be 4 due to ensureAlpha()

    // 2. Iterate through pixels to make pure black transparent
    for (var i = 0; i < pixels.length; i += channels) {
      // If R, G, and B are all 0 (pure black)
      if (pixels[i] === 0 && pixels[i+1] === 0 && pixels[i+2] === 0) {
         pixels[i+3] = 0; // Set Alpha to 0
      }
    }

    // 3. Convert modified raw pixels back to a PNG buffer
    var transparentBuffer = await sharp(pixels, {
      raw: {
        width: rawImage.info.width,
        height: rawImage.info.height,
        channels: channels
      }
    })
    .png()
    .toBuffer();
    
    totalBytes += transparentBuffer.length;
    if (!entries[name]) entries[name] = {};
    entries[name] = 'data:image/png;base64,' + transparentBuffer.toString('base64');
    found.push(file);
  }

  var missing = KNOWN_NAMES.filter(function (n) { return found.indexOf(n) === -1; });

  var lines = [];
  lines.push('// GENERATED FILE -- do not edit by hand.');
  lines.push('// Produced by scripts/generate-infographics.js from');
  lines.push('// resources/infographics/marker_preset_<name>.png. Re-run that script after');
  lines.push('// adding or changing any marker-preset picture, before `pebble build`.');
  lines.push('module.exports = ' + JSON.stringify(entries, null, 2) + ';');
  lines.push('');
  if (!fs.existsSync(path.dirname(OUTPUT_FILE))) fs.mkdirSync(path.dirname(OUTPUT_FILE), { recursive: true });
  fs.writeFileSync(OUTPUT_FILE, lines.join('\n'));

  console.log('generate-infographics: wrote ' + OUTPUT_FILE);
  console.log('generate-infographics: embedded ' + found.length + ' marker-preset picture(s) from ' + SOURCE_DIR +
    ' (' + totalBytes + ' raw bytes before base64): ' + (found.join(', ') || '(none)'));
  if (missing.length) {
    console.log('generate-infographics: no ' + missing.map(function (n) { return PREFIX + n + '.png'; }).join(', ') +
      ' found. Those marker preset buttons will simply show no picture in settings until that resource PNG exists.');
  }
};

// Execute the async function
buildPresetPreviews().catch(console.error);
