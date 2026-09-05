#!/usr/bin/env node
// Runs every build-time generator script in scripts/, in the one
// order that actually matters (generate_hand_style_icons.py has to
// run before generate-hand-style-icons.js, since the latter bakes the
// former's own PNG output -- see both scripts' own top-of-file
// comments), so you don't have to remember the right order/invocation
// (node vs python3) for each one by hand before `pebble build`.
//
// Deliberately keeps going if one script fails (a missing optional
// dependency -- e.g. pngjs for generate-weather-icon-previews.js, or
// Python/Pillow for generate_hand_style_icons.py -- or a missing
// source resource is exactly the kind of "not an error, that one
// preview just won't have an image yet" situation every one of these
// scripts already handles gracefully on its own) rather than aborting
// the whole batch on the first problem -- prints a summary at the end
// so you can see at a glance which ones actually ran clean.
//
// Usage:
//   node scripts/generate-all.js
//
// Run this before `pebble build` (or wire it into your own build
// step -- see README.md's "Running it automatically" section) any
// time you've changed a source PNG/geometry parameter for any of the
// generated preview sets this project uses.

var path = require('path');
var execSync = require('child_process').execSync;

var SCRIPTS_DIR = __dirname;

// { label, command } -- command is run with SCRIPTS_DIR's parent (the
// project root) as the working directory, same as how these are
// normally invoked by hand ("node scripts/xyz.js" from the repo root).
var STEPS = [
  { label: 'generate_hand_style_icons.py (renders the 11 hand-style icon PNGs)', command: 'python3 scripts/generate_hand_style_icons.py' },
  { label: 'generate-hand-style-icons.js (bakes those PNGs for the settings page)', command: 'node scripts/generate-hand-style-icons.js' },
  { label: 'generate-font-previews.js', command: 'node scripts/generate-font-previews.js' },
  { label: 'generate-weather-icon-previews.js', command: 'node scripts/generate-weather-icon-previews.js' },
  { label: 'generate-marker-previews.js', command: 'node scripts/generate-marker-previews.js' },
  { label: 'generate-infographics.js', command: 'node scripts/generate-infographics.js' },
  { label: 'generate-example-style-previews.js', command: 'node scripts/generate-example-style-previews.js' }
];

var PROJECT_ROOT = path.join(SCRIPTS_DIR, '..');

var results = [];
STEPS.forEach(function (step) {
  console.log('\n=== ' + step.label + ' ===');
  try {
    execSync(step.command, { cwd: PROJECT_ROOT, stdio: 'inherit' });
    results.push({ step: step.label, ok: true });
  } catch (e) {
    // execSync already streamed the failing script's own stderr
    // (stdio: 'inherit'), so there's nothing more useful to print
    // here than which step it was.
    results.push({ step: step.label, ok: false });
  }
});

console.log('\n=== Summary ===');
results.forEach(function (r) {
  console.log((r.ok ? '  OK   ' : '  FAIL ') + r.step);
});
var failed = results.filter(function (r) { return !r.ok; });
if (failed.length) {
  console.log('\n' + failed.length + ' of ' + results.length + ' script(s) failed -- see their own output above for why ' +
    '(often just a missing optional dependency or source file, not a real problem -- each script explains its own case).');
  process.exitCode = 1;
} else {
  console.log('\nAll ' + results.length + ' scripts ran clean.');
}
