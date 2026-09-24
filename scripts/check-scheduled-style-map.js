#!/usr/bin/env node
// Checks the element id -> CONFIG_* table in src/pkjs/scheduled-style.js against
// the code it mirrors, so a scheduled style writes exactly what the settings
// page's Save would:
//   * every FIELD_MAP entry must appear in save()'s payload (config-runtime.js)
//     as `CONFIG_KEY: ...getElementById("id")...` with the same key,
//   * every DUAL_MAP entry must appear as a dualCtxVal("id-base", ...) pair,
//   * every key must be stored by the 'webviewclosed' handler in index.js.
// Run after adding or renaming a style setting:  node scripts/check-scheduled-style-map.js
// (New element ids inside the preset scope that are missing from the table are
// reported by the schedule popup itself: it lists them in a "Developer note".)

var fs = require('fs');
var path = require('path');
var root = path.join(__dirname, '..');
var ss = require(path.join(root, 'src/pkjs/scheduled-style.js'));
var runtime = require(path.join(root, 'src/pkjs/config/config-runtime.js'));
var indexSrc = fs.readFileSync(path.join(root, 'src/pkjs/index.js'), 'utf8');

var problems = [];

Object.keys(ss.FIELD_MAP).forEach(function (id) {
  var key = ss.FIELD_MAP[id][0];
  var re = new RegExp(key + ':[^,]*getElementById\\("' + id + '"\\)');
  var special = (id === 'showSeconds' && key === 'CONFIG_SHOW_SECONDS'); // computed as showSecondsVal in save()
  if (!special && !re.test(runtime)) problems.push('FIELD_MAP ' + id + ' -> ' + key + ': not found in save() payload');
  if (indexSrc.indexOf("setSetting('" + key + "'") < 0) problems.push(key + ': never stored by the webviewclosed handler');
});

Object.keys(ss.DUAL_MAP).forEach(function (id) {
  ['analog', 'digital'].forEach(function (ctx, i) {
    var key = ss.DUAL_MAP[id][i];
    var m = /^(.*?)(Content|Color)$/.exec(id);
    var re = new RegExp(key + ':\\s*dualCtxVal\\("' + m[1] + '", "' + m[2] + '", "' + ctx + '"\\)');
    if (!re.test(runtime)) problems.push('DUAL_MAP ' + id + ' (' + ctx + ') -> ' + key + ': not found in save() payload');
    if (indexSrc.indexOf("setSetting('" + key + "'") < 0) problems.push(key + ': never stored by the webviewclosed handler');
  });
});

if (problems.length) {
  problems.forEach(function (p) { console.error('MISMATCH: ' + p); });
  process.exit(1);
}
console.log('scheduled-style map OK: ' + Object.keys(ss.FIELD_MAP).length + ' fields, ' + Object.keys(ss.DUAL_MAP).length + ' dual-context fields');
