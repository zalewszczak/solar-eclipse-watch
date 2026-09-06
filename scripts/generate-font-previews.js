#!/usr/bin/env node
// Reads real on-watch renderings of the settings page's font-picker...
// (Keep all original header comments from the file)

var fs = require('fs');
var path = require('path');
var sharp = require('sharp');

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

// Wrap the execution in an async function to handle Promises
async function buildPreviews() {
  for (const file of files) {
    var m = FILE_RE.exec(file);
    if (!m) {
      skipped.push(file);
      continue;
    }
    var fontId = m[1];
    var role = m[2];
    
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

    var cropTop = Math.floor(metadata.height / 11);
    var cropHeight = Math.floor(metadata.height / 3) - cropTop;

    // 1. Crop and extract raw RGBA pixel data
    var rawImage = await sharp(sourceBuffer)
      .extract({
        left: 0,
        top: cropTop,
        width: metadata.width,
        height: cropHeight
      })
      .ensureAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true });

    var pixels = rawImage.data;
    var channels = rawImage.info.channels; // Guaranteed to be 4 due to ensureAlpha()

    // 2. Iterate through pixels to make pure white transparent
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
    if (!entries[fontId]) entries[fontId] = {};
    entries[fontId][role] = 'data:image/png;base64,' + transparentBuffer.toString('base64');
    found.push(file);
  }

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
}

// Execute the async function
buildPreviews().catch(console.error);
