// Moved out of astro.js (astronomy split, by mathematical domain).

var angles = require('./angles');
var sind = angles.sind;
var cosd = angles.cosd;
var asind = angles.asind;

// ---- aurora (Kp-index-based visibility estimate) ----------------------
// Geomagnetic north pole location, ~2020s epoch (IGRF), rounded --
// drifts a fraction of a degree per year, nowhere near enough to
// matter for this app's purposes.
var GEOMAG_POLE_LAT = 80.7;
var GEOMAG_POLE_LON = -72.7;

// Geographic -> geomagnetic latitude via the standard eccentric-dipole-
// free "simple dipole" approximation (single spherical rotation onto
// the geomagnetic pole) -- accurate to a few degrees, which is all a
// decorative "is aurora roughly in reach of your latitude tonight"
// feature needs. Real aurora visibility also depends on local weather,
// light pollution, and the (much more complex, satellite-driven)
// OVATION auroral oval model NOAA itself uses for precision forecasts
// -- this is a rough approximation, not a substitute for a dedicated
// aurora-forecast app if you're chasing a specific display.
function geomagneticLatitudeDeg(latDeg, lonDeg) {
  var dLon = lonDeg - GEOMAG_POLE_LON;
  return asind(sind(latDeg) * sind(GEOMAG_POLE_LAT) +
               cosd(latDeg) * cosd(GEOMAG_POLE_LAT) * cosd(dLon));
}

// 0 (no realistic chance) - 100 (well within the auroral oval) for the
// current Kp index at a given (absolute) geomagnetic latitude. The
// oval's equatorward edge is commonly approximated as sitting around
// 65.5 - 2.2*Kp degrees geomagnetic latitude; this tapers smoothly
// across a 15-degree band around that edge rather than a hard cutoff,
// since the real boundary is fuzzy (aurora can glow on the horizon
// well equatorward of the oval's "overhead" edge on an active night).
function auroraVisibilityScore(kp, geomagLatAbsDeg) {
  var boundary = 65.5 - 2.2 * kp;
  var margin = geomagLatAbsDeg - boundary;
  if (margin >= 3) return 100;
  if (margin <= -12) return 0;
  return Math.round(((margin + 12) / 15) * 100);
}

module.exports = {
  geomagneticLatitudeDeg: geomagneticLatitudeDeg,
  auroraVisibilityScore: auroraVisibilityScore
};
