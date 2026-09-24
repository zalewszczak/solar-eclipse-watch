// Runs once per pkjs startup (self-executing on require -- keep this
// required from index.js at the same point it used to run inline, so
// timing relative to the rest of startup is unchanged).
//
// Moved out of index.js verbatim (settings subsystem extraction).
// Behavior is unchanged; only the module boundary is new.

// ---- migration: settings-key wire-format schema version ------------------
//
// C consolidated 86 individual settings AppMessage keys (HAND_HOUR_*,
// CUSTOM_HOUR_*/CUSTOM_SEC_*, MARKER_TEXT_*, the edge-line content/color
// keys, and the CUSTOM_*/NIGHT_CUSTOM_* color keys) into 5 packed-byte-
// array keys -- HANDS, MARKER_RINGS, EDGE_LINES, MARKER_TEXT, COLORS.
// See handsBytes()/markerRingsBytes()/edgeLinesBytes()/markerTextBytes()/
// colorsBytes() in index.js, and apply_consolidated_fields() in
// pebble-eclipse-watch.c for the watch side.
//
// This doesn't touch any of the CONFIG_* localStorage keys the settings
// page reads/writes (those are unchanged, so nobody's saved hand/marker/
// color choices are affected) -- but LAST_FULL_COMPUTED_DICT (see
// sendFlatDict()'s own comment) caches an entire previously-sent flat
// dict verbatim, keyed by the OLD individual AppMessage key names, for
// resendLastFullData()/buildFullKeysetDict() to reuse. An update landing
// on a phone that still has one of those old-format dicts cached would
// otherwise have populateSettingsFields() add the 5 new keys on top
// without ever clearing the ~86 stale old ones already sitting on that
// same object -- enqueueFlatDict() tolerates unknown keys (logs a
// warning and stuffs them in the SETTINGS chunk anyway) rather than
// crashing, but there's no reason to ship that instead of just starting
// clean. Bumping this version wipes the one cache that could carry the
// old key names forward; everything else (the real CONFIG_* settings,
// and a fresh computed dict using the 5 new keys) rebuilds itself
// normally on the very next refresh either way.
// 3 = marker style ids renumbered: Minimal moved 0 -> 10 and 0 became "None"
//     (it was 9; 9 is Classy now). Stored value and saved snapshots remapped.
var SETTINGS_KEY_SCHEMA_VERSION = 3; // 1 = pre-consolidation (86 individual keys), 2 = HANDS/MARKER_RINGS/EDGE_LINES/MARKER_TEXT/COLORS, 3 = marker style renumbering
(function migrateSettingsKeySchema() {
  try {
    var stored = parseInt(localStorage.getItem('SETTINGS_KEY_SCHEMA_VERSION'), 10);
    if (stored === SETTINGS_KEY_SCHEMA_VERSION) return; // already current -- nothing to do
    localStorage.removeItem('LAST_FULL_COMPUTED_DICT'); // may be in the old key format; see comment above

    // v3: "Minimal" used to be bigAnalogMarkerStyle 0 and is now 10 (0 is
    // "None" now), so a stored 0 -- including one inside a saved
    // Style-preset snapshot (CONFIG_PRESET_n_JSON, a JSON dump of the
    // Style section incl. its "bigAnalogMarkerStyle") -- has to follow
    // it or it would silently turn into None. A missing key is left
    // alone (the new default is already Minimal).
    if (isNaN(stored) || stored < 3) {
      if (localStorage.getItem('CONFIG_BIG_ANALOG_MARKER_STYLE') === '0') {
        localStorage.setItem('CONFIG_BIG_ANALOG_MARKER_STYLE', '10');
      }
      for (var n = 1; n <= 6; n++) {
        var key = 'CONFIG_PRESET_' + n + '_JSON';
        try {
          var raw = localStorage.getItem(key);
          if (!raw) continue;
          var snapshot = JSON.parse(raw);
          if (snapshot && snapshot.bigAnalogMarkerStyle === '0') {
            snapshot.bigAnalogMarkerStyle = '10';
            localStorage.setItem(key, JSON.stringify(snapshot));
          }
        } catch (e2) { /* one unreadable snapshot mustn't block the rest */ }
      }
    }

    localStorage.setItem('SETTINGS_KEY_SCHEMA_VERSION', String(SETTINGS_KEY_SCHEMA_VERSION));
    console.log('eclipse-watch: settings-key schema migrated to v' + SETTINGS_KEY_SCHEMA_VERSION + ' (cleared stale LAST_FULL_COMPUTED_DICT, if any; remapped marker style ids)');
  } catch (e) {
    // Storage unavailable/corrupt -- not critical: worst case a stale
    // cached dict's old-format keys ride along in one SETTINGS chunk
    // (enqueueFlatDict() logs and bundles them, doesn't crash), and the
    // very next real refresh recomputes and re-caches in the new format
    // regardless.
  }
})();
