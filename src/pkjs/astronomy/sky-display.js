// ---- astronomy display-value formatting (Feature Primitive Refactor,
//      Phase 7, continuing the Section 5b addendum into the astronomy
//      cluster) --------------------------------------------------------
//
// Same shape as weather-normalize.js's temperature/UV/etc. functions: a
// display-text formatter and, where the watch used a dynamic gradient
// color, a matching packed-color-byte function. See
// feature_value_sky_compute() (feature_value_time.c) for the on-watch
// code these replace.
//
// NOT everything in the astronomy cluster migrates, though -- three of its
// content ids (16 sunrise/sunset, 82 next planet rise, 83 ISS next pass)
// stay watch-computed even after this pass, for two compounding reasons
// this module's functions deliberately don't try to route around:
//   1. Time-dependent SELECTION: "which of these several future instants
//      is the soonest one relative to right now" is not a value PKJS can
//      precompute once and hand over as a static string -- it keeps
//      changing purely because time keeps advancing on the watch, with no
//      new data having arrived at all. PKJS only pushes on a refresh
//      cycle, not continuously, so it cannot keep re-resolving "next" the
//      way the watch's own redraw path already does.
//   2. 12h/24h formatting: every one of those three cases formats a clock
//      time with strftime() using clock_is_24h_style() -- a native Pebble
//      OS service reflecting the paired phone's system clock-format
//      setting, which PKJS has no access to at all (it isn't part of this
//      app's own settings). Any value whose *text* depends on that watch-
//      only setting can't be formatted on the PKJS side, full stop.
// Compass (content 85) stays watch-runtime for the usual reason (Section
// 2.1): the heading itself is read from a live watch-local sensor.

var packGColorByte = require('../comms/gcolor').packGColorByte;

// Verbatim port of celestial_moon_phase_short_name() (celestial_layer.c).
function formatMoonPhaseDisplay(illuminatedPct, waxing) {
  if (typeof illuminatedPct !== 'number' || isNaN(illuminatedPct)) return '';
  var pct = Math.round(illuminatedPct);
  if (pct <= 2) return 'New';
  if (pct >= 98) return 'Full';
  if (waxing) {
    if (pct < 48) return 'WxCr';
    if (pct <= 52) return '1stQ';
    return 'WxGb';
  }
  if (pct > 52) return 'WnGb';
  if (pct >= 48) return '3rdQ';
  return 'WnCr';
}

// "Rings N%" -- plain formatting, no gradient (case 81 always used the
// flat color_mode color, same as weather's dew point).
function formatSaturnRingsDisplay(ringOpenPct) {
  if (typeof ringOpenPct !== 'number' || isNaN(ringOpenPct)) return '';
  return 'Rings ' + Math.round(ringOpenPct) + '%';
}

// Verbatim port of feature_colors_meteor_intensity_gradient() (feature_colors.c):
// dim gray (faint) -> white (strong).
function meteorIntensityGradientRGB(pct) {
  var frac1000 = Math.trunc((pct * 1000) / 100);
  var v = 90 + Math.trunc((165 * frac1000) / 1000);
  return [v, v, v];
}
// Mirrors case 80's own logic exactly: only a real shower name with a
// positive intensity counts as "active" -- everything else (including a
// present-but-zero-intensity name) displays "N/A". The watch's own
// error_code check still runs first and independently on the watch (see
// feature_value_sky_compute()'s case 80): that's a live fetch-health
// signal, not something a stale synced string should ever override.
function formatMeteorDisplay(intensity, name) {
  if (typeof intensity === 'number' && intensity > 0 && name) return name;
  return 'N/A';
}
function meteorGradientColorByte(intensity) {
  var pct = (typeof intensity === 'number' && !isNaN(intensity)) ? intensity : 0;
  var rgb = meteorIntensityGradientRGB(pct);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// "Kp N.N" -- verbatim port of the watch's fixed-point split (x10 value,
// integer/tenths). Gradient: verbatim port of
// feature_colors_white_to_red_gradient() (feature_colors.c).
function formatAuroraKpDisplay(kpX10) {
  if (typeof kpX10 !== 'number' || isNaN(kpX10)) return '';
  var v = Math.round(kpX10);
  return 'Kp ' + Math.trunc(v / 10) + '.' + (v % 10);
}
function whiteToRedGradientRGB(kpX10) {
  if (kpX10 >= 90) return [220, 0, 0];
  var frac1000 = Math.trunc((kpX10 * 1000) / 90);
  var g = 170 - Math.trunc((170 * frac1000) / 1000);
  return [255, g, g];
}
function auroraGradientColorByte(kpX10) {
  var v = (typeof kpX10 === 'number' && !isNaN(kpX10)) ? Math.round(kpX10) : 0;
  var rgb = whiteToRedGradientRGB(v);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

module.exports = {
  formatMoonPhaseDisplay: formatMoonPhaseDisplay,
  formatSaturnRingsDisplay: formatSaturnRingsDisplay,
  formatMeteorDisplay: formatMeteorDisplay,
  meteorGradientColorByte: meteorGradientColorByte,
  formatAuroraKpDisplay: formatAuroraKpDisplay,
  auroraGradientColorByte: auroraGradientColorByte
};
