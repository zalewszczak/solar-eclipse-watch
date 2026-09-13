// ---- tiny settings helpers, backed directly by localStorage -------------
//
// Raw access only -- reading/writing a CONFIG_* (or cache) key exactly
// as stored, no wire-format encoding or validation. See
// settings-codecs.js for the layer that turns a raw stored string into
// the clamped numeric/byte value actually sent to the watch.
//
// Moved out of index.js verbatim (settings subsystem extraction).
// Behavior is unchanged; only the module boundary is new.

function getSetting(key, fallback) {
  var v = localStorage.getItem(key);
  return (v === null || v === undefined || v === '') ? fallback : v;
}

function setSetting(key, value) {
  if (value === null || value === undefined) return;
  localStorage.setItem(key, value);
}

module.exports = {
  getSetting: getSetting,
  setSetting: setSetting
};
