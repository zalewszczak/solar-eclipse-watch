#!/usr/bin/env node
// Emits src/c/message_key_index.h: one MK_<NAME> #define per entry in
// package.json's pebble.messageKeys array, holding that entry's
// position in the array.
//
// Why this exists: the Pebble SDK assigns MESSAGE_KEY_<NAME> values
// at *link* time, sequentially, in the order messageKeys lists them
// (see pebble.messageKeys being a plain array, not an object with
// explicit numbers) -- so MESSAGE_KEY_<NAME> always equals
// MESSAGE_KEY_MESSAGE_TYPE + index_of(<NAME>). SIMPLE_FIELD_MAP in
// pebble-eclipse-watch.c wants a `static const` table, but a plain
// static const initializer can't reference MESSAGE_KEY_* (those are
// extern variables, not compile-time constants). The array *index*,
// on the other hand, is baked in at generation time right here, so
// it compiles as an ordinary integer literal -- the table stores an
// MK_ index and apply_simple_fields() adds MESSAGE_KEY_MESSAGE_TYPE
// back on at runtime (once, per call) to recover the real key.
//
// Run this whenever messageKeys in package.json changes (add/remove/
// reorder a key) -- ideally wired into generate-all.js's STEPS list
// or your own pre-build step, same as the other scripts/generate-*.js
// files. Committing the generated header is fine (and easier to diff
// than re-running on every clone), but it must be regenerated before
// building if messageKeys has moved since the header was last written.
//
// Usage:
//   node scripts/generate-message-keys.js

var fs = require('fs');
var path = require('path');

var ROOT = path.join(__dirname, '..');
var pkg = require(path.join(ROOT, 'package.json'));
var keys = pkg.pebble.messageKeys;

var lines = [];
lines.push('// GENERATED FILE -- do not hand-edit.');
lines.push('// Produced by scripts/generate-message-keys.js from package.json\'s');
lines.push('// pebble.messageKeys array. Each MK_<NAME> is that key\'s position in');
lines.push('// the array, i.e. MESSAGE_KEY_<NAME> - MESSAGE_KEY_MESSAGE_TYPE.');
lines.push('// Re-run the generator any time messageKeys changes.');
lines.push('#pragma once');
lines.push('');
keys.forEach(function (name, i) {
  lines.push('#define MK_' + name + ' ' + i);
});
lines.push('');

var outPath = path.join(ROOT, 'src', 'c', 'message_key_index.h');
fs.writeFileSync(outPath, lines.join('\n'));
console.log('Wrote ' + keys.length + ' key indices to ' + path.relative(ROOT, outPath));
