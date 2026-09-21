// ---- packed GColor8 byte encoding -----------------------------------------
//
// Shared by weather/weather-normalize.js and astronomy/sky-display.js (both
// part of the Feature Primitive Refactor's Phase 7 migration -- see
// FEATURE_PRIMITIVE_REFACTOR_REPORT_REVISED.md) so neither module has to
// require the other just to pack a color byte.
//
// Packs an 8-bit RGB triple (always fully opaque here) into the one-byte
// GColor8 wire format the C side already reads via a raw struct-field
// assignment (eclipse_ui.c's eclipse_ui_color_from_packed(), and
// feature_value_helpers.c's feature_value_color_from_packed()): 2 bits each
// for alpha/red/green/blue, alpha=3 (opaque), each color channel truncated
// (not rounded) to its top 2 bits -- (channel >> 6). This is the same wire
// format the SETTINGS custom-color fields already use (see
// settings-codecs.js's customColorByte() family) -- GColor8 IS its own wire
// byte, no conversion table needed.
function packGColorByte(r, g, b) {
  var a2 = 3;
  var r2 = (r >> 6) & 0x03;
  var g2 = (g >> 6) & 0x03;
  var b2 = (b >> 6) & 0x03;
  return (a2 << 6) | (r2 << 4) | (g2 << 2) | b2;
}

module.exports = {
  packGColorByte: packGColorByte
};
