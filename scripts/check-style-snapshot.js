#!/usr/bin/env node
// Checks that a saved style (the settings page's collectStyleCornersJson() snapshot)
// and the scheduler agree: every id the page puts into a style must be one that
// scheduled-style.js can apply. Needs playwright (already used for screenshots).
//   node scripts/check-style-snapshot.js
// Run after adding a control to the Style, Colors, Features or Animation sections
// or to the Center circle / Shadow style popups (see PRESET_SCOPE_IDS in
// src/pkjs/config-page.js). Complements check-scheduled-style-map.js, which checks
// the scheduler's table against the save payload.
var fs = require('fs');
var os = require('os');
var path = require('path');
var root = path.join(__dirname, '..');

global.Pebble = { addEventListener: function () {}, getActiveWatchInfo: function () { return { platform: 'emery' }; } };
global.localStorage = { getItem: function () { return null; }, setItem: function () {} };
var html = require(path.join(root, 'src/pkjs/config-page.js')).buildConfigHtml({});
var ss = require(path.join(root, 'src/pkjs/scheduled-style.js'));
var tmp = path.join(os.tmpdir(), 'eclipz-style-snapshot-check.html');
fs.writeFileSync(tmp, html);

var chromium = require('playwright').chromium;
(async function () {
  var browser = await chromium.launch();
  var page = await browser.newPage();
  await page.goto('file://' + tmp);
  await page.waitForFunction('typeof collectStyleCornersJson === "function"');
  await page.waitForTimeout(3000);
  var ids = await page.evaluate(function () { return Object.keys(collectStyleCornersJson()); });
  await browser.close();
  var known = ss.knownIds();
  var unknown = ids.filter(function (id) { return known.indexOf(id) < 0; });
  if (unknown.length) {
    console.error('MISMATCH: in a saved style but unknown to the scheduler: ' + unknown.join(', '));
    process.exit(1);
  }
  console.log('style snapshot OK: ' + ids.length + ' ids, all known to the scheduler');
})().catch(function (e) { console.error(e); process.exit(1); });
