// ---- foundational math -----------------------------------------------
//
// Moved out of astro.js (astronomy split, by mathematical domain).
// Degree-based trig, Julian date conversion, and the coordinate-
// transform primitives (sidereal time, angular separation, alt/az)
// every other astronomy module builds on. Kept together rather than
// split further -- these are all tiny, used identically across every
// domain (sun/moon/planets/stars/eclipse/ISS), and none of them
// belongs more to one domain than another.

var DEG = Math.PI / 180;

function sind(x) { return Math.sin(x * DEG); }
function cosd(x) { return Math.cos(x * DEG); }
function tand(x) { return Math.tan(x * DEG); }
function atan2d(y, x) { return Math.atan2(y, x) / DEG; }
function asind(x) { return Math.asin(x) / DEG; }
function acosd(x) { return Math.acos(x) / DEG; }
function norm360(x) { x = x % 360; return x < 0 ? x + 360 : x; }

function julianDay(date) {
  return date.getTime() / 86400000 + 2440587.5;
}

function julianCenturies(jd) {
  return (jd - 2451545.0) / 36525.0;
}
function greenwichSiderealDeg(jd, T) {
  var gst = 280.46061837 + 360.98564736629 * (jd - 2451545.0) +
            0.000387933 * T * T - (T * T * T) / 38710000;
  return norm360(gst);
}
function angularSeparation(ra1, dec1, ra2, dec2) {
  var cosSep = sind(dec1) * sind(dec2) + cosd(dec1) * cosd(dec2) * cosd(ra1 - ra2);
  cosSep = Math.max(-1, Math.min(1, cosSep));
  return acosd(cosSep);
}

// Sun's topocentric altitude (parallax on the Sun is ~8.8" and
// ignored here -- irrelevant at this precision).
function sunAltitude(sun, latDeg, lstDeg) {
  var H = norm360(lstDeg - sun.ra);
  return asind(sind(latDeg) * sind(sun.dec) + cosd(latDeg) * cosd(sun.dec) * cosd(H));
}

// Alt/az of a body, used only to derive the on-screen "which way does
// the Moon approach from" angle.
function altAz(raDeg, decDeg, latDeg, lstDeg) {
  var H = norm360(lstDeg - raDeg);
  var alt = asind(sind(latDeg) * sind(decDeg) + cosd(latDeg) * cosd(decDeg) * cosd(H));
  var az = atan2d(sind(H), cosd(H) * sind(latDeg) - tand(decDeg) * cosd(latDeg));
  return { alt: alt, az: norm360(az + 180) }; // north-based azimuth
}

module.exports = {
  DEG: DEG,
  sind: sind,
  cosd: cosd,
  tand: tand,
  atan2d: atan2d,
  asind: asind,
  acosd: acosd,
  norm360: norm360,
  julianDay: julianDay,
  julianCenturies: julianCenturies,
  greenwichSiderealDeg: greenwichSiderealDeg,
  angularSeparation: angularSeparation,
  altAz: altAz
};
