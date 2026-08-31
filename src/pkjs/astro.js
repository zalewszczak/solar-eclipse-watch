/**
 * astro.js -- phone-side (PKJS) solar eclipse geometry.
 *
 * This uses Jean Meeus's *low-precision* series for the Sun
 * (Astronomical Algorithms ch.25) and a reduced set of the largest
 * terms of the ELP2000-based lunar series (ch.47), plus a standard
 * parallax-in-right-ascension correction so the Moon's position is
 * *topocentric* (i.e. correct for the observer's actual location --
 * this matters a lot for eclipses, since the Moon's parallax is
 * roughly 1 degree, about twice its own apparent diameter).
 *
 * Accuracy: roughly 1-3 arcminutes on the Moon's position, which
 * translates to contact-time errors on the order of a minute or two.
 * That's not good enough for a professional ephemeris, but it's
 * plenty to drive a live "is the Moon about to bite into the Sun"
 * indicator on your wrist. Treat all times as approximate and
 * cross-check against a proper eclipse calculator (e.g. NASA's) for
 * anything mission-critical (i.e. flying somewhere to see totality).
 */

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

// ---- Sun (low precision, Meeus ch.25) --------------------------------

function sunPosition(T) {
  var L0 = norm360(280.46646 + 36000.76983 * T + 0.0003032 * T * T);
  var M = norm360(357.52911 + 35999.05029 * T - 0.0001537 * T * T);
  var e = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;

  var C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sind(M) +
          (0.019993 - 0.000101 * T) * sind(2 * M) +
          0.000289 * sind(3 * M);

  var trueLong = L0 + C;
  var trueAnom = M + C;
  var R = (1.000001018 * (1 - e * e)) / (1 + e * cosd(trueAnom)); // AU

  var omega = 125.04 - 1934.136 * T;
  var apparentLong = trueLong - 0.00569 - 0.00478 * sind(omega);

  var eps0 = 23 + 26 / 60 + 21.448 / 3600 -
             (46.8150 * T + 0.00059 * T * T - 0.001813 * T * T * T) / 3600;
  var eps = eps0 + 0.00256 * cosd(omega);

  var ra = norm360(atan2d(cosd(eps) * sind(apparentLong), cosd(apparentLong)));
  var dec = asind(sind(eps) * sind(apparentLong));

  // Angular semidiameter in degrees (959.63" at 1 AU).
  var semidiameter = (959.63 / 3600) / R;

  return { ra: ra, dec: dec, distanceAU: R, semidiameterDeg: semidiameter, eclipticLon: norm360(apparentLong) };
}

// ---- Moon (reduced ELP2000 terms, Meeus ch.47) ------------------------

// Each entry: [D, M, Mp, F, coefficient]
var MOON_LON_TERMS = [
  [0, 0, 1, 0, 6288774],
  [2, 0, -1, 0, 1274027],
  [2, 0, 0, 0, 658314],
  [0, 0, 2, 0, 213618],
  [0, 1, 0, 0, -185116],
  [0, 0, 0, 2, -114332],
  [2, 0, -2, 0, 58793],
  [2, -1, -1, 0, 57066],
  [2, 0, 1, 0, 53322],
  [2, -1, 0, 0, 45758],
  [0, 1, -1, 0, -40923],
  [1, 0, 0, 0, -34720],
  [0, 1, 1, 0, -30383]
];

var MOON_LAT_TERMS = [
  [0, 0, 0, 1, 5128122],
  [0, 0, 1, 1, 280602],
  [0, 0, 1, -1, 277693],
  [2, 0, 0, -1, 173237],
  [2, 0, -1, 1, 55413],
  [2, 0, -1, -1, 46271],
  [2, 0, 0, 1, 32573],
  [0, 0, 2, 1, 17198]
];

var MOON_DIST_TERMS = [
  [0, 0, 1, 0, -20905355],
  [2, 0, -1, 0, -3699111],
  [2, 0, 0, 0, -2955968],
  [0, 0, 2, 0, -569925],
  [0, 1, 0, 0, 48888],
  [0, 0, 0, 2, -3149]
];

// Terms containing M (the Sun's mean anomaly) need scaling by E or
// E^2 to account for the Earth orbit's eccentricity drifting over
// time; none of our reduced terms use 2*M, so a single E factor
// covers all of them.
function eFactorForM(Mcoef) {
  return Mcoef === 0 ? 1 : null; // marker, applied below by absolute value
}

function moonPosition(T) {
  var Lp = norm360(218.3164477 + 481267.88123421 * T - 0.0015786 * T * T + T * T * T / 538841);
  var D = norm360(297.8501921 + 445267.1114034 * T - 0.0018819 * T * T + T * T * T / 545868);
  var M = norm360(357.5291092 + 35999.0502909 * T - 0.0001536 * T * T);
  var Mp = norm360(134.9633964 + 477198.8675055 * T + 0.0087414 * T * T + T * T * T / 69699);
  var F = norm360(93.2720950 + 483202.0175233 * T - 0.0036539 * T * T - T * T * T / 3526000);

  var E = 1 - 0.002516 * T - 0.0000074 * T * T;

  function sumTerms(terms, useSin) {
    var sum = 0;
    for (var i = 0; i < terms.length; i++) {
      var d = terms[i][0], m = terms[i][1], mp = terms[i][2], f = terms[i][3], coef = terms[i][4];
      var arg = d * D + m * M + mp * Mp + f * F;
      var val = useSin ? sind(arg) : cosd(arg);
      var scale = coef;
      if (m === 1) scale *= E;
      else if (m === -1) scale *= E;
      sum += scale * val;
    }
    return sum;
  }

  var sumL = sumTerms(MOON_LON_TERMS, true);
  var sumB = sumTerms(MOON_LAT_TERMS, true);
  var sumR = sumTerms(MOON_DIST_TERMS, false);

  var lon = Lp + sumL / 1000000;
  var lat = sumB / 1000000;
  var distanceKm = 385000.56 + sumR / 1000;

  var eps0 = 23.4392911 - 0.0130042 * T; // mean obliquity, low precision

  var ra = norm360(atan2d(sind(lon) * cosd(eps0) - tand(lat) * sind(eps0), cosd(lon)));
  var dec = asind(sind(lat) * cosd(eps0) + cosd(lat) * sind(eps0) * sind(lon));

  return { ra: ra, dec: dec, distanceKm: distanceKm, eclipticLon: norm360(lon) };
}

// ---- Sidereal time & topocentric correction ----------------------------

function greenwichSiderealDeg(jd, T) {
  var gst = 280.46061837 + 360.98564736629 * (jd - 2451545.0) +
            0.000387933 * T * T - (T * T * T) / 38710000;
  return norm360(gst);
}

// Corrects the Moon's geocentric RA/Dec for the observer's location
// (parallax-in-right-ascension method, Meeus ch.40). This is the
// single most important correction for local eclipse timing.
function topocentricMoon(moon, latDeg, lonDeg, lstDeg) {
  var pi = asind(6378.14 / moon.distanceKm); // equatorial horizontal parallax
  var H = norm360(lstDeg - moon.ra);

  var num = -cosd(latDeg) * sind(pi) * sind(H);
  var den = cosd(moon.dec) - cosd(latDeg) * sind(pi) * cosd(H);
  var dRA = atan2d(num, den);

  var raTopo = norm360(moon.ra + dRA);
  var decNum = (sind(moon.dec) - sind(latDeg) * sind(pi)) * cosd(dRA);
  var decTopo = atan2d(decNum, den);

  // Moon's angular semidiameter: true radius 1737.4 km / distance.
  var semidiameter = asind(1737.4 / moon.distanceKm);

  return { ra: raTopo, dec: decTopo, semidiameterDeg: semidiameter };
}

// Great-circle angular separation between two RA/Dec points (degrees).
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

// ---- planets (low-precision Keplerian elements, valid ~1800-2050,
// from the standard set published for approximate major-planet
// positions -- accurate to a few arcminutes, which is all a <=10px
// decorative dot needs). Orbital periods implied by each LDot were
// checked against the real values before use (Mercury 87.97d, Venus
// 224.7d, Mars 1.881yr, Jupiter 11.86yr, Saturn 29.45yr -- all match).

var MERCURY_ELEMENTS = {
  a0: 0.38709927, aDot: 0.00000037,
  e0: 0.20563593, eDot: 0.00001906,
  I0: 7.00497902, IDot: -0.00594749,
  L0: 252.25032350, LDot: 149472.67411175,
  peri0: 77.45779628, periDot: 0.16047689,
  node0: 48.33076593, nodeDot: -0.12534081
};
var VENUS_ELEMENTS = {
  a0: 0.72333566, aDot: 0.00000390,
  e0: 0.00677672, eDot: -0.00004107,
  I0: 3.39467605, IDot: -0.00078890,
  L0: 181.97909950, LDot: 58517.81538729,
  peri0: 131.60246718, periDot: 0.00268329,
  node0: 76.67984255, nodeDot: -0.27769418
};
var MARS_ELEMENTS = {
  a0: 1.52371034, aDot: 0.00001847,
  e0: 0.09339410, eDot: 0.00007882,
  I0: 1.84969142, IDot: -0.00813131,
  L0: -4.55343205, LDot: 19140.30268499,
  peri0: -23.94362959, periDot: 0.44441088,
  node0: 49.55953891, nodeDot: -0.29257343
};
var JUPITER_ELEMENTS = {
  a0: 5.20288700, aDot: -0.00011607,
  e0: 0.04838624, eDot: -0.00013253,
  I0: 1.30439695, IDot: -0.00183714,
  L0: 34.39644051, LDot: 3034.74612775,
  peri0: 14.72847983, periDot: 0.21252668,
  node0: 100.47390909, nodeDot: 0.20469106
};
var SATURN_ELEMENTS = {
  a0: 9.53667594, aDot: 0.00125060,
  e0: 0.05386179, eDot: -0.00050991,
  I0: 2.48599187, IDot: 0.00193609,
  L0: 49.95424423, LDot: 1222.49362201,
  peri0: 92.59887831, periDot: -0.41897216,
  node0: 113.66242448, nodeDot: -0.28867794
};

// Solves Kepler's equation M = E - e*sin(E) for the eccentric
// anomaly E (degrees) via Newton-Raphson, working in radians
// internally since that's what the iteration needs.
function solveKeplerDeg(Mdeg, e) {
  var M = Mdeg * DEG;
  var E = M;
  for (var i = 0; i < 10; i++) {
    var dE = (M - (E - e * Math.sin(E))) / (1 - e * Math.cos(E));
    E += dE;
    if (Math.abs(dE) < 1e-9) break;
  }
  return E / DEG;
}

// Heliocentric ecliptic rectangular coordinates (AU) of a body given
// its Keplerian elements at Julian century T.
function heliocentricEcliptic(elements, T) {
  var a = elements.a0 + elements.aDot * T;
  var e = elements.e0 + elements.eDot * T;
  var I = elements.I0 + elements.IDot * T;
  var L = norm360(elements.L0 + elements.LDot * T);
  var peri = norm360(elements.peri0 + elements.periDot * T);
  var node = norm360(elements.node0 + elements.nodeDot * T);

  var M = norm360(L - peri);
  var E = solveKeplerDeg(M, e);

  var xOrb = a * (cosd(E) - e);
  var yOrb = a * Math.sqrt(1 - e * e) * sind(E);

  var argPeri = peri - node;
  var cosArg = cosd(argPeri), sinArg = sind(argPeri);
  var cosNode = cosd(node), sinNode = sind(node);
  var cosI = cosd(I), sinI = sind(I);

  var xh = (cosNode * cosArg - sinNode * sinArg * cosI) * xOrb +
           (-cosNode * sinArg - sinNode * cosArg * cosI) * yOrb;
  var yh = (sinNode * cosArg + cosNode * sinArg * cosI) * xOrb +
           (-sinNode * sinArg + cosNode * cosArg * cosI) * yOrb;
  var zh = (sinArg * sinI) * xOrb + (cosArg * sinI) * yOrb;

  return { x: xh, y: yh, z: zh };
}

// Earth's own heliocentric position, derived from the Sun's already-
// computed geocentric apparent longitude/distance rather than a
// separate set of Earth elements -- the Sun's apparent position lies
// in the ecliptic (latitude 0) by definition, so Earth's heliocentric
// longitude is just the Sun's geocentric longitude + 180 degrees.
function earthHeliocentric(sun) {
  var lonEarth = norm360(sun.eclipticLon + 180);
  var R = sun.distanceAU;
  return { x: R * cosd(lonEarth), y: R * sind(lonEarth), z: 0 };
}

// Generic geocentric position for any body given its Keplerian
// elements -- returns geocentric ecliptic lon/lat too (not just
// RA/Dec), since Saturn's ring-angle calculation needs those.
function planetPosition(elements, T, sun) {
  var helio = heliocentricEcliptic(elements, T);
  var earth = earthHeliocentric(sun);
  var x = helio.x - earth.x;
  var y = helio.y - earth.y;
  var z = helio.z - earth.z;

  var lon = norm360(atan2d(y, x));
  var dist = Math.sqrt(x * x + y * y + z * z);
  var lat = asind(z / dist);

  var eps0 = 23.4392911 - 0.0130042 * T; // mean obliquity, low precision
  var ra = norm360(atan2d(sind(lon) * cosd(eps0) - tand(lat) * sind(eps0), cosd(lon)));
  var dec = asind(sind(lat) * cosd(eps0) + cosd(lat) * sind(eps0) * sind(lon));

  return { ra: ra, dec: dec, distanceAU: dist, eclipticLon: lon, eclipticLat: lat };
}

/**
 * Saturn's ring-opening angle B (degrees, -26.7..+26.7) as seen from
 * Earth -- 0 means the rings are edge-on (invisible/a thin line),
 * +-26.7 means maximally open. Real ring-plane crossings happen
 * roughly every ~15 years (most recently March 2025); this is why
 * the rings look "narrow" right now and will visibly widen over the
 * next several years. Standard formula (Meeus ch. 45), using
 * Saturn's ring-plane inclination/node (distinct from its orbital
 * elements above -- the ring plane is Saturn's *equatorial* plane,
 * tilted ~26.7 deg to its orbit) and Saturn's own geocentric
 * ecliptic longitude/latitude.
 */
function saturnRingAngle(T, saturnLon, saturnLat) {
  var i = 28.075216 - 0.012998 * T + 0.000004 * T * T;
  var omega = 169.508470 + 1.394681 * T + 0.000412 * T * T;
  var B = asind(sind(i) * cosd(saturnLat) * sind(saturnLon - omega) - cosd(i) * sind(saturnLat));
  return B;
}

/**
 * All months/days below are 0-indexed months (JS Date convention:
 * Jan=0 ... Dec=11), verified against current published peak/active
 * dates for each shower rather than assumed from memory. Quadrantids
 * is the one that crosses the year boundary (late Dec into early
 * Jan) -- activeMeteorShower() below handles that case generically
 * rather than special-casing it.
 */
var METEOR_SHOWERS = [
  { name: 'Quadrantids', startMonth: 11, startDay: 28, peakMonth: 0, peakDay: 3, endMonth: 0, endDay: 12 },
  { name: 'Lyrids', startMonth: 3, startDay: 16, peakMonth: 3, peakDay: 22, endMonth: 3, endDay: 25 },
  { name: 'Eta Aquariids', startMonth: 3, startDay: 19, peakMonth: 4, peakDay: 5, endMonth: 4, endDay: 28 },
  { name: 'Delta Aquariids', startMonth: 6, startDay: 12, peakMonth: 6, peakDay: 30, endMonth: 7, endDay: 23 },
  { name: 'Perseids', startMonth: 6, startDay: 17, peakMonth: 7, peakDay: 12, endMonth: 7, endDay: 24 },
  { name: 'Orionids', startMonth: 9, startDay: 2, peakMonth: 9, peakDay: 21, endMonth: 10, endDay: 7 },
  { name: 'Leonids', startMonth: 10, startDay: 6, peakMonth: 10, peakDay: 17, endMonth: 10, endDay: 30 },
  { name: 'Geminids', startMonth: 11, startDay: 4, peakMonth: 11, peakDay: 14, endMonth: 11, endDay: 17 },
  { name: 'Ursids', startMonth: 11, startDay: 17, peakMonth: 11, peakDay: 22, endMonth: 11, endDay: 26 }
];

// Builds concrete Date objects for one shower's window anchored to a
// given "base year" for its start -- if the window crosses into the
// next calendar year (startMonth > endMonth, e.g. Quadrantids), the
// peak/end land in baseYear+1 accordingly.
function showerWindowForYear(shower, baseYear) {
  var wraps = shower.startMonth > shower.endMonth;
  var peakYear = (wraps && shower.peakMonth < shower.startMonth) ? baseYear + 1 : baseYear;
  var endYear = wraps ? baseYear + 1 : baseYear;
  return {
    start: new Date(baseYear, shower.startMonth, shower.startDay),
    peak: new Date(peakYear, shower.peakMonth, shower.peakDay),
    end: new Date(endYear, shower.endMonth, shower.endDay)
  };
}

function windowIntensity(window, date) {
  if (date < window.start || date > window.end) return 0;
  var daysFromPeak = Math.abs(date.getTime() - window.peak.getTime()) / 86400000;
  var halfWidthMs = date < window.peak ? (window.peak.getTime() - window.start.getTime()) : (window.end.getTime() - window.peak.getTime());
  var halfWidthDays = halfWidthMs / 86400000;
  if (halfWidthDays <= 0) return 100;
  return Math.max(0, Math.round(100 * (1 - daysFromPeak / halfWidthDays)));
}

/**
 * Whichever major annual meteor shower is currently active (if any),
 * as { name, intensity } (0-100, ramping linearly from either edge of
 * its active window up to 100 at its peak) -- or null if none are
 * active right now. A shower, unlike an eclipse, doesn't need precise
 * radiant geometry to be "visible" -- meteors from an active shower
 * show up across much of the sky, not just at the radiant itself, so
 * a dark sky (checked by the caller, using the observer's own local
 * sun altitude) is what actually gates visibility; this just says
 * which shower, if any, is active on the calendar right now.
 *
 * Checks each shower's window anchored to both this year and last --
 * only relevant for the one (Quadrantids) that crosses the year
 * boundary, but cheap and harmless to do uniformly rather than
 * special-casing it.
 */
function activeMeteorShower(date) {
  var year = date.getFullYear();
  var best = null, bestIntensity = 0;
  for (var i = 0; i < METEOR_SHOWERS.length; i++) {
    var shower = METEOR_SHOWERS[i];
    for (var yOffset = -1; yOffset <= 0; yOffset++) {
      var win = showerWindowForYear(shower, year + yOffset);
      var intensity = windowIntensity(win, date);
      if (intensity > bestIntensity) {
        bestIntensity = intensity;
        best = shower.name;
      }
    }
  }
  return best ? { name: best, intensity: bestIntensity } : null;
}

/**
 * Computes the full picture for a given moment: geocentric Sun,
 * topocentric Moon, their separation and each one's apparent radius.
 */
function computeGeometry(date, latDeg, lonDeg) {
  var jd = julianDay(date);
  var T = julianCenturies(jd);
  var sun = sunPosition(T);
  var moonGeo = moonPosition(T);
  var lst = norm360(greenwichSiderealDeg(jd, T) + lonDeg);
  var moon = topocentricMoon(moonGeo, latDeg, lonDeg, lst);

  var sep = angularSeparation(sun.ra, sun.dec, moon.ra, moon.dec);
  var sunAlt = sunAltitude(sun, latDeg, lst);
  var moonAlt = altAz(moon.ra, moon.dec, latDeg, lst).alt;

  var mercury = planetPosition(MERCURY_ELEMENTS, T, sun);
  var venus = planetPosition(VENUS_ELEMENTS, T, sun);
  var mars = planetPosition(MARS_ELEMENTS, T, sun);
  var jupiter = planetPosition(JUPITER_ELEMENTS, T, sun);
  var saturn = planetPosition(SATURN_ELEMENTS, T, sun);

  var mercuryAlt = altAz(mercury.ra, mercury.dec, latDeg, lst).alt;
  var venusAlt = altAz(venus.ra, venus.dec, latDeg, lst).alt;
  var marsAlt = altAz(mars.ra, mars.dec, latDeg, lst).alt;
  var jupiterAlt = altAz(jupiter.ra, jupiter.dec, latDeg, lst).alt;
  var saturnAlt = altAz(saturn.ra, saturn.dec, latDeg, lst).alt;
  var saturnRingB = saturnRingAngle(T, saturn.eclipticLon, saturn.eclipticLat);

  return {
    sep: sep,
    sunRadius: sun.semidiameterDeg,
    moonRadius: moon.semidiameterDeg,
    sunAlt: sunAlt,
    moonAlt: moonAlt,
    mercuryAlt: mercuryAlt,
    venusAlt: venusAlt,
    marsAlt: marsAlt,
    jupiterAlt: jupiterAlt,
    saturnAlt: saturnAlt,
    saturnRingB: saturnRingB,
    sun: sun,
    moon: moon,
    lst: lst
  };
}

// ---- bright named stars (space-view sky mode) -----------------------
// A curated list of the sky's brightest stars, fixed J2000 RA(hours)/
// Dec(degrees)/apparent magnitude -- proper motion and precession are
// both negligible at this app's timescale and pixel precision, so
// (unlike the Sun/Moon/planets) these coordinates never need
// recomputing, just re-projecting to alt/az for "now". Order here is
// the order STAR_ALT_SAMPLES/STAR_AZ_SAMPLES are packed in, and MUST
// match the STARS[] name/magnitude table in background_layer.c exactly
// -- that side owns the display name, this side owns the position.
var STAR_CATALOG = [
  { raH: 6.7525,  decDeg: -16.7161 }, // Sirius
  { raH: 6.3992,  decDeg: -52.6957 }, // Canopus
  { raH: 14.2610, decDeg: 19.1825 },  // Arcturus
  { raH: 18.6156, decDeg: 38.7837 },  // Vega
  { raH: 5.2782,  decDeg: 45.9980 },  // Capella
  { raH: 5.2423,  decDeg: -8.2016 },  // Rigel
  { raH: 7.6550,  decDeg: 5.2250 },   // Procyon
  { raH: 5.9195,  decDeg: 7.4071 },   // Betelgeuse
  { raH: 19.8464, decDeg: 8.8683 },   // Altair
  { raH: 4.5987,  decDeg: 16.5093 },  // Aldebaran
  { raH: 16.4901, decDeg: -26.4320 }, // Antares
  { raH: 13.4199, decDeg: -11.1613 }, // Spica
  { raH: 7.7553,  decDeg: 28.0262 },  // Pollux
  { raH: 22.9608, decDeg: -29.6222 }, // Fomalhaut
  { raH: 20.6905, decDeg: 45.2803 },  // Deneb
  { raH: 10.1395, decDeg: 11.9672 }   // Regulus
];

// alt/az (degrees, az north-based via altAz()) for every cataloged
// star at `date`/`latDeg`/`lonDeg` -- one pass of the same sidereal-
// time machinery computeGeometry() uses for the Moon/planets, just
// against fixed RA/Dec instead of an ephemeris. No visibility
// filtering here (space-view mode wants stars below the horizon
// omitted on-watch, not here) -- see background_layer.c.
function computeVisibleStars(date, latDeg, lonDeg) {
  var jd = julianDay(date);
  var T = julianCenturies(jd);
  var lst = norm360(greenwichSiderealDeg(jd, T) + lonDeg);
  return STAR_CATALOG.map(function (star) {
    var aa = altAz(star.raH * 15, star.decDeg, latDeg, lst);
    return { alt: aa.alt, az: aa.az };
  });
}

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

// Circle/circle overlap area, as a fraction of the Sun's disc area.
// R = sun radius, r = moon radius, d = center separation (same units).
function occlusionFraction(R, r, d) {
  if (d >= R + r) return 0;
  if (d <= Math.abs(R - r)) {
    // One disc entirely inside the other.
    var smaller = Math.min(R, r);
    return Math.min(1, (smaller * smaller) / (R * R));
  }
  var d1 = (d * d - r * r + R * R) / (2 * d);
  var d2 = d - d1;
  var area = R * R * Math.acos(Math.max(-1, Math.min(1, d1 / R))) -
             d1 * Math.sqrt(Math.max(0, R * R - d1 * d1)) +
             r * r * Math.acos(Math.max(-1, Math.min(1, d2 / r))) -
             d2 * Math.sqrt(Math.max(0, r * r - d2 * d2));
  return Math.min(1, area / (Math.PI * R * R));
}

var MAX_SAMPLES = 12;

/**
 * Scans local daylight hours for an eclipse and returns contact
 * times plus a small array of separation samples (degrees) for the
 * watch to interpolate between when animating.
 *
 * @param {Date} dayStart  midnight (local) of the day to scan
 * @param {number} latDeg
 * @param {number} lonDeg
 * @returns {object} eclipse summary, or {hasEclipse:false}
 */
function findEclipse(dayStart, latDeg, lonDeg) {
  var stepMin = 2;
  var stepMs = stepMin * 60000;
  var samples = [];
  var t0 = dayStart.getTime();

  for (var i = 0; i * stepMs <= 24 * 3600000; i++) {
    var t = new Date(t0 + i * stepMs);
    var geo = computeGeometry(t, latDeg, lonDeg);
    samples.push({
      t: t,
      gap: geo.sep - (geo.sunRadius + geo.moonRadius), // <0 once partial phase starts
      innerGap: geo.sep - Math.abs(geo.sunRadius - geo.moonRadius), // <0 once total/annular
      sep: geo.sep,
      sunRadius: geo.sunRadius,
      moonRadius: geo.moonRadius,
      sunAlt: geo.sunAlt
    });
  }

  // Find a contiguous run where gap < 0 (Sun and Moon discs overlap
  // at all) and the Sun is above the horizon.
  var c1Idx = -1, c4Idx = -1;
  for (var j = 1; j < samples.length; j++) {
    if (samples[j - 1].gap >= 0 && samples[j].gap < 0 && samples[j].sunAlt > -1) {
      c1Idx = j;
    }
    if (samples[j - 1].gap < 0 && samples[j].gap >= 0 && c1Idx >= 0 && c4Idx < 0) {
      c4Idx = j;
      break;
    }
  }

  if (c1Idx < 0) {
    return { hasEclipse: false };
  }
  if (c4Idx < 0) c4Idx = samples.length - 1; // ran off the end of the day (rare)

  function interpTime(idxBefore, idxAfter, field) {
    var a = samples[idxBefore], b = samples[idxAfter];
    var frac = a[field] / (a[field] - b[field]);
    return new Date(a.t.getTime() + frac * (b.t.getTime() - a.t.getTime()));
  }

  var c1 = interpTime(c1Idx - 1, c1Idx, 'gap');
  var c4 = interpTime(c4Idx - 1, c4Idx, 'gap');

  // Minimum separation (greatest eclipse) within [c1Idx, c4Idx].
  var minIdx = c1Idx;
  for (var k = c1Idx; k <= c4Idx; k++) {
    if (samples[k].sep < samples[minIdx].sep) minIdx = k;
  }
  var maxT = samples[minIdx].t;
  var minSep = samples[minIdx].sep;
  var sunRadiusAtMax = samples[minIdx].sunRadius;
  var moonRadiusAtMax = samples[minIdx].moonRadius;

  var magnitude = occlusionFraction(sunRadiusAtMax, moonRadiusAtMax, minSep);

  var type = 'partial';
  var c2 = 0, c3 = 0;
  if (minSep <= Math.abs(sunRadiusAtMax - moonRadiusAtMax)) {
    type = moonRadiusAtMax >= sunRadiusAtMax ? 'total' : 'annular';
    // Find inner-gap zero crossings around minIdx for C2/C3.
    var c2Idx = -1, c3Idx = -1;
    for (var m = c1Idx + 1; m <= minIdx; m++) {
      if (samples[m - 1].innerGap >= 0 && samples[m].innerGap < 0) c2Idx = m;
    }
    for (var n = minIdx; n <= c4Idx; n++) {
      if (samples[n - 1] && samples[n - 1].innerGap < 0 && samples[n].innerGap >= 0) {
        c3Idx = n;
        break;
      }
    }
    if (c2Idx > 0) c2 = interpTime(c2Idx - 1, c2Idx, 'innerGap');
    if (c3Idx > 0) c3 = interpTime(c3Idx - 1, c3Idx, 'innerGap');
  }

  // Direction the Moon approaches from, measured on-screen with
  // "up" = 0 deg, clockwise positive. We use the alt/az delta a few
  // minutes before mid-eclipse as a stand-in for the instantaneous
  // relative motion vector, which is nearly constant across the
  // event's short duration.
  var beforeIdx = Math.max(c1Idx, minIdx - 5);
  var geoBefore = computeGeometry(samples[beforeIdx].t, latDeg, lonDeg);
  var sunAA = altAz(geoBefore.sun.ra, geoBefore.sun.dec, latDeg, geoBefore.lst);
  var moonAA = altAz(geoBefore.moon.ra, geoBefore.moon.dec, latDeg, geoBefore.lst);
  var dAlt = moonAA.alt - sunAA.alt;
  var dAz = (moonAA.az - sunAA.az) * cosd(sunAA.alt);
  // Screen: 0deg = up (+alt), 90deg = right (+az), matches the C
  // renderer's sin/cos usage.
  var posAngle = norm360(atan2d(dAz, dAlt));

  // Build interpolation samples spanning exactly [C1, C4] so the
  // watch's first/last sample line up with "discs just touching".
  // magPctSamples tracks the live "% of the Sun's disc covered"
  // alongside the raw separation, so the watch can show a running
  // percentage rather than just the fixed peak magnitude.
  var sampleCount = Math.min(MAX_SAMPLES, samples.length);
  var interval = (c4.getTime() - c1.getTime()) / (sampleCount - 1);
  var sepSamples = [];
  var magPctSamples = [];
  for (var s = 0; s < sampleCount; s++) {
    var st = new Date(c1.getTime() + s * interval);
    var g = computeGeometry(st, latDeg, lonDeg);
    sepSamples.push(Math.round(g.sep * 100)); // centidegrees
    magPctSamples.push(Math.round(occlusionFraction(g.sunRadius, g.moonRadius, g.sep) * 100));
  }

  // Sunset today, used so the watch knows to stop animating once the
  // Sun (and the eclipse with it) drops below the horizon.
  var sunset = null;
  for (var p = 1; p < samples.length; p++) {
    if (samples[p - 1].sunAlt >= 0 && samples[p].sunAlt < 0) {
      sunset = interpTime(p - 1, p, 'sunAlt');
      break;
    }
  }

  return {
    hasEclipse: true,
    type: type,
    c1: c1,
    c2: c2,
    max: maxT,
    c3: c3,
    c4: c4,
    sunset: sunset,
    magnitudePct: Math.round(magnitude * 100),
    // Moon's angular radius as a percentage of the Sun's at greatest
    // eclipse -- <100 means annular (a ring of Sun stays visible even
    // at maximum), >=100 means total (Moon fully covers the Sun).
    // Used on-watch to size the occluding disc so the two look
    // visually distinct rather than identical.
    radiusRatioPct: Math.round((moonRadiusAtMax / sunRadiusAtMax) * 100),
    posAngleDeg: Math.round(posAngle),
    sampleStart: c1,
    sampleIntervalS: Math.round(interval / 1000),
    sepSamplesCentideg: sepSamples,
    magPctSamples: magPctSamples
  };
}

/**
 * Finds the (first) rise and set time of a body within a calendar
 * day, by scanning at fine resolution and linearly interpolating the
 * altitude-zero crossing. Used for the watchface's rise/set
 * animation rather than the coarse hourly grid, since a 3-minute
 * transition window needs better than hourly precision to land in
 * the right place.
 *
 * @param {Date} dayStart
 * @param {function(object):number} altGetter  pulls the body's
 *   altitude (degrees) out of a computeGeometry() result
 * @returns {object} { rise: Date|null, set: Date|null } -- null for
 *   either means the body doesn't cross the horizon that day (up or
 *   down the whole time, e.g. near the poles)
 */
function findRiseSet(dayStart, latDeg, lonDeg, altGetter) {
  var stepMs = 5 * 60000; // 5 minutes
  var steps = Math.floor((24 * 3600000) / stepMs);
  var prevT = dayStart, prevAlt = altGetter(computeGeometry(dayStart, latDeg, lonDeg));
  var rise = null, set = null;

  for (var i = 1; i <= steps; i++) {
    var t = new Date(dayStart.getTime() + i * stepMs);
    var alt = altGetter(computeGeometry(t, latDeg, lonDeg));

    if (prevAlt < 0 && alt >= 0 && !rise) {
      var fracR = -prevAlt / (alt - prevAlt);
      rise = new Date(prevT.getTime() + fracR * (t.getTime() - prevT.getTime()));
    }
    if (prevAlt >= 0 && alt < 0 && rise && !set) {
      var fracS = prevAlt / (prevAlt - alt);
      set = new Date(prevT.getTime() + fracS * (t.getTime() - prevT.getTime()));
      break;
    }

    prevT = t;
    prevAlt = alt;
  }

  return { rise: rise, set: set };
}

/**
 * Illuminated fraction + waxing/waning of the Moon at a given
 * moment. Uses the Sun-Moon geocentric elongation as a stand-in for
 * the true phase angle (they differ by a fraction of a percent at
 * most for this purpose) via the standard k = (1+cos(elongation))/2
 * formula, and compares ecliptic longitudes to tell whether the
 * Moon is heading toward full (waxing) or back toward new (waning).
 *
 * @returns {object} { illuminatedPct: 0-100, waxing: bool }
 */
function computeMoonPhase(date) {
  var jd = julianDay(date);
  var T = julianCenturies(jd);
  var sun = sunPosition(T);
  var moon = moonPosition(T);

  var elongation = angularSeparation(sun.ra, sun.dec, moon.ra, moon.dec);
  var k = (1 + cosd(elongation)) / 2;

  var diff = norm360(moon.eclipticLon - sun.eclipticLon);
  var waxing = diff < 180;

  return { illuminatedPct: Math.round(k * 100), waxing: waxing };
}

/**
 * Samples the Sun's (and Moon's, and the planets') topocentric
 * altitude once an hour across a full day, for the watchface's
 * background sky gradient and the rise/set animation of each body.
 * Deliberately coarse (hourly) -- altitude changes slowly and
 * smoothly, so the watch just linearly interpolates between these
 * on-device.
 *
 * @param {Date} dayStart  midnight (local) of the day to sample
 * @param {number} elevationDipDeg  optional horizon-dip correction
 *   (positive degrees), added to each body's altitude so a higher
 *   vantage point shows them rising earlier / setting later, the way
 *   a physically lower horizon actually looks from up there.
 * @returns {object} { sampleStart, intervalS, times, sunAltDecideg,
 *   moonAltDecideg, mercuryAltDecideg, venusAltDecideg,
 *   marsAltDecideg, jupiterAltDecideg, saturnAltDecideg,
 *   saturnRingOpenPct, scaleMaxAltDecideg }
 */
function computeDaySkySamples(dayStart, latDeg, lonDeg, elevationDipDeg) {
  var stepMs = 60 * 60000; // 1 hour
  var count = 25; // hour 0 through hour 24 inclusive, so it spans the full day
  var dip = elevationDipDeg || 0;
  var times = [];
  var sunAltDecideg = [];
  var moonAltDecideg = [];
  var mercuryAltDecideg = [];
  var venusAltDecideg = [];
  var marsAltDecideg = [];
  var jupiterAltDecideg = [];
  var saturnAltDecideg = [];
  var maxSunAlt = -900;
  var maxMoonAlt = -900;
  var saturnRingOpenPct = 0;

  for (var i = 0; i < count; i++) {
    var t = new Date(dayStart.getTime() + i * stepMs);
    var geo = computeGeometry(t, latDeg, lonDeg);
    var sunA = Math.round((geo.sunAlt + dip) * 10);
    var moonA = Math.round((geo.moonAlt + dip) * 10);
    // The planets are deliberately excluded from the shared scale
    // below -- they're meant to stay small, unobtrusive dots, not
    // stretch the Sun/Moon's vertical scale to accommodate them.
    times.push(t);
    sunAltDecideg.push(sunA);
    moonAltDecideg.push(moonA);
    mercuryAltDecideg.push(Math.round((geo.mercuryAlt + dip) * 10));
    venusAltDecideg.push(Math.round((geo.venusAlt + dip) * 10));
    marsAltDecideg.push(Math.round((geo.marsAlt + dip) * 10));
    jupiterAltDecideg.push(Math.round((geo.jupiterAlt + dip) * 10));
    saturnAltDecideg.push(Math.round((geo.saturnAlt + dip) * 10));
    if (sunA > maxSunAlt) maxSunAlt = sunA;
    if (moonA > maxMoonAlt) maxMoonAlt = moonA;
    // Ring angle changes over years, not hours -- one snapshot
    // (roughly midday) is plenty; done here just to reuse the loop's
    // geometry call rather than a second one.
    if (i === 12) {
      saturnRingOpenPct = Math.round((Math.abs(geo.saturnRingB) / 26.7) * 100);
    }
  }

  return {
    sampleStart: dayStart,
    intervalS: stepMs / 1000,
    times: times,
    sunAltDecideg: sunAltDecideg,
    moonAltDecideg: moonAltDecideg,
    mercuryAltDecideg: mercuryAltDecideg,
    venusAltDecideg: venusAltDecideg,
    marsAltDecideg: marsAltDecideg,
    jupiterAltDecideg: jupiterAltDecideg,
    saturnAltDecideg: saturnAltDecideg,
    saturnRingOpenPct: Math.max(0, Math.min(100, saturnRingOpenPct)),
    scaleMaxAltDecideg: Math.max(maxSunAlt, maxMoonAlt, 50)
  };
}

// ---- ISS (or any other near-Earth satellite given fresh elements) -------

var EARTH_MU = 398600.4418;    // km^3/s^2, standard gravitational parameter
var EARTH_RADIUS_KM = 6378.137; // equatorial radius; spherical approximation
var CIVIL_TWILIGHT_ALT_DEG = -6; // Sun this far below the horizon (or lower) counts as "dark enough to see satellites"
var MIN_VISIBLE_ISS_ALT_DEG = 10; // low passes are usually lost in horizon haze/obstructions anyway

/**
 * The ISS's position in an Earth-centered-inertial-like frame (true
 * equatorial frame of date, same reference plane/direction RA/Dec
 * already use elsewhere in this file) -- factored out of
 * issLookAngle() below so findNextIssPass() can reuse it without
 * recomputing the mean-anomaly/orbital-radius math for every sample,
 * and so issIsSunlit() can test it against the Sun's position without
 * needing a full topocentric look angle first.
 */
function issEciPosition(omm, now) {
  var elapsedDays = (now.getTime() - omm.epoch.getTime()) / 86400000;
  var meanAnomalyNow = norm360(omm.meanAnomalyDeg + omm.meanMotionRevPerDay * 360 * elapsedDays);
  var argLat = norm360(omm.argPerigeeDeg + meanAnomalyNow); // argument of latitude ("u")

  // Semi-major axis from mean motion via Kepler's third law; treated
  // as the (near-)constant orbital radius given the near-circular
  // assumption issLookAngle()'s own comment explains.
  var nRadPerSec = (omm.meanMotionRevPerDay * 2 * Math.PI) / 86400;
  var r = Math.pow(EARTH_MU / (nRadPerSec * nRadPerSec), 1 / 3);

  var i = omm.inclinationDeg, raan = omm.raanDeg;
  return {
    x: r * (cosd(raan) * cosd(argLat) - sind(raan) * sind(argLat) * cosd(i)),
    y: r * (sind(raan) * cosd(argLat) + cosd(raan) * sind(argLat) * cosd(i)),
    z: r * sind(argLat) * sind(i)
  };
}

/**
 * Topocentric alt/az/range of an already-computed ECI position (see
 * issEciPosition() above) -- the other half of what issLookAngle()
 * used to do in one step.
 */
function eciToLookAngle(eciPos, now, latDeg, lonDeg) {
  var jd = julianDay(now);
  var T = julianCenturies(jd);
  var gstDeg = greenwichSiderealDeg(jd, T);
  var xEcef = eciPos.x * cosd(gstDeg) + eciPos.y * sind(gstDeg);
  var yEcef = -eciPos.x * sind(gstDeg) + eciPos.y * cosd(gstDeg);
  var zEcef = eciPos.z;

  var obsX = EARTH_RADIUS_KM * cosd(latDeg) * cosd(lonDeg);
  var obsY = EARTH_RADIUS_KM * cosd(latDeg) * sind(lonDeg);
  var obsZ = EARTH_RADIUS_KM * sind(latDeg);

  var dx = xEcef - obsX, dy = yEcef - obsY, dz = zEcef - obsZ;

  // Standard ECEF-difference -> local East/North/Up "look angle"
  // rotation, the same approach ground-station and amateur satellite
  // tracking software uses. At LEO ranges the parallax is far too
  // large (tens of degrees, not the ~1 degree of the Moon) for the
  // small-angle spherical-trig shortcuts used elsewhere in this file
  // to hold up, so this goes through Cartesian vectors instead.
  var south = sind(latDeg) * cosd(lonDeg) * dx + sind(latDeg) * sind(lonDeg) * dy - cosd(latDeg) * dz;
  var east = -sind(lonDeg) * dx + cosd(lonDeg) * dy;
  var up = cosd(latDeg) * cosd(lonDeg) * dx + cosd(latDeg) * sind(lonDeg) * dy + sind(latDeg) * dz;

  var range = Math.sqrt(south * south + east * east + up * up);
  return {
    alt: asind(up / range),
    az: norm360(atan2d(east, -south)),
    distanceKm: range
  };
}

/**
 * The ISS's current topocentric altitude/azimuth from a fresh OMM
 * element set (as fetched from Celestrak's free GP data API) and the
 * observer's lat/lon.
 *
 * Deliberately NOT a full SGP4 propagator -- that's a genuinely
 * intricate algorithm (many perturbation terms) that's easy to get
 * subtly wrong, and getting it subtly wrong here would be worse than
 * not having this feature at all. Instead: the ISS's orbit is very
 * nearly circular (eccentricity ~0.0006), and index.js re-fetches
 * fresh elements on every refresh cycle (same 5-60 minute interval as
 * everything else), so this only ever has to extrapolate forward by
 * that short a window from a genuinely current epoch -- a plain
 * circular-orbit propagation is a solid approximation over a window
 * that short, even though it would drift from reality over days (real
 * ISS tracking needs to account for drag decay and periodic reboosts,
 * which is exactly why fresh elements every refresh matters more here
 * than for anything else in this app).
 *
 * @param {object} omm  { epoch: Date, meanMotionRevPerDay, eccentricity,
 *   inclinationDeg, raanDeg, argPerigeeDeg, meanAnomalyDeg } -- field
 *   names deliberately mirror Celestrak's OMM JSON keys (minus the
 *   shouting-case and units already folded in) so the fetch layer can
 *   pass the parsed response straight through.
 * @param {Date} now
 * @param {number} latDeg
 * @param {number} lonDeg
 * @returns {{alt: number, az: number, distanceKm: number}}
 */
function issLookAngle(omm, now, latDeg, lonDeg) {
  return eciToLookAngle(issEciPosition(omm, now), now, latDeg, lonDeg);
}

/**
 * Whether the ISS is sunlit at `eciPos`/`now`, rather than inside
 * Earth's shadow -- a simple cylindrical shadow model (ignores the
 * penumbra/Earth's slight oblateness, same "deliberately approximate"
 * standard the rest of this file already applies). The satellite is
 * lit if it's on the sunward side of Earth's center at all, or -- if
 * it's on the night side -- if its distance from the Earth-Sun line
 * still exceeds Earth's radius (i.e. it's not actually behind Earth's
 * disk from the Sun's point of view).
 *
 * @param {object} eciPos  from issEciPosition()
 * @param {object} sun  from sunPosition(T) for the same `now`
 */
function issIsSunlit(eciPos, sun) {
  var sx = cosd(sun.dec) * cosd(sun.ra);
  var sy = cosd(sun.dec) * sind(sun.ra);
  var sz = sind(sun.dec);

  var dot = eciPos.x * sx + eciPos.y * sy + eciPos.z * sz;
  if (dot > 0) return true; // on the sunward side of Earth's center -- always lit

  var perpX = eciPos.x - dot * sx, perpY = eciPos.y - dot * sy, perpZ = eciPos.z - dot * sz;
  var perpDist = Math.sqrt(perpX * perpX + perpY * perpY + perpZ * perpZ);
  return perpDist > EARTH_RADIUS_KM; // outside the cylindrical shadow
}

/**
 * The start time of the next visible ISS pass from `now`, searching
 * forward in 1-minute steps up to `windowHours` ahead. "Visible" means
 * all three of: the observer's sky is dark enough (Sun below
 * CIVIL_TWILIGHT_ALT_DEG), the ISS itself is above
 * MIN_VISIBLE_ISS_ALT_DEG, and the ISS is sunlit (issIsSunlit() above).
 * The cheap sun-altitude check runs first and skips the ISS math
 * entirely for every still-too-bright sample, which is most of a
 * 20-hour window most of the year.
 *
 * Deliberately a look-angle search, not a full rise/culmination/set
 * pass description -- same "good enough over a short window" spirit
 * issLookAngle()'s own comment describes, and the OMM elements this
 * runs on are only ever a few minutes to an hour old at most (index.js
 * re-fetches every refresh), so there's no benefit to a more elaborate
 * result than "when does the next one start".
 *
 * @param {object} omm  same shape issLookAngle() takes
 * @param {Date} now
 * @param {number} latDeg
 * @param {number} lonDeg
 * @param {number} [windowHours]  default 20 -- covers the rest of
 *   today plus early tomorrow morning without searching all night
 * @returns {Date|null}  null if no qualifying pass starts in the window
 */
function findNextIssPass(omm, now, latDeg, lonDeg, windowHours) {
  var stepMs = 60000; // 1 minute
  var steps = Math.floor(((windowHours || 20) * 3600000) / stepMs);

  for (var i = 0; i <= steps; i++) {
    var t = new Date(now.getTime() + i * stepMs);
    var jd = julianDay(t);
    var T = julianCenturies(jd);
    var sun = sunPosition(T);
    var lstDeg = norm360(greenwichSiderealDeg(jd, T) + lonDeg);
    if (sunAltitude(sun, latDeg, lstDeg) > CIVIL_TWILIGHT_ALT_DEG) continue; // still too bright

    var eci = issEciPosition(omm, t);
    var look = eciToLookAngle(eci, t, latDeg, lonDeg);
    if (look.alt < MIN_VISIBLE_ISS_ALT_DEG) continue;

    if (!issIsSunlit(eci, sun)) continue;

    return t;
  }
  return null;
}

module.exports = {
  findEclipse: findEclipse,
  computeGeometry: computeGeometry,
  computeDaySkySamples: computeDaySkySamples,
  computeMoonPhase: computeMoonPhase,
  findRiseSet: findRiseSet,
  activeMeteorShower: activeMeteorShower,
  issLookAngle: issLookAngle,
  findNextIssPass: findNextIssPass,
  computeVisibleStars: computeVisibleStars,
  STAR_CATALOG: STAR_CATALOG,
  geomagneticLatitudeDeg: geomagneticLatitudeDeg,
  auroraVisibilityScore: auroraVisibilityScore
};
