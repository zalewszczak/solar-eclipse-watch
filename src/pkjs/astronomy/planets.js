// ---- planets (low-precision Keplerian elements, valid ~1800-2050,
// from the standard set published for approximate major-planet
// positions -- accurate to a few arcminutes, which is all a <=10px
// decorative dot needs). Orbital periods implied by each LDot were
// checked against the real values before use (Mercury 87.97d, Venus
// 224.7d, Mars 1.881yr, Jupiter 11.86yr, Saturn 29.45yr -- all match).
//
// Moved out of astro.js (astronomy split, by mathematical domain).

var angles = require('./angles');
var DEG = angles.DEG;
var sind = angles.sind;
var cosd = angles.cosd;
var tand = angles.tand;
var atan2d = angles.atan2d;
var asind = angles.asind;
var norm360 = angles.norm360;

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

module.exports = {
  MERCURY_ELEMENTS: MERCURY_ELEMENTS,
  VENUS_ELEMENTS: VENUS_ELEMENTS,
  MARS_ELEMENTS: MARS_ELEMENTS,
  JUPITER_ELEMENTS: JUPITER_ELEMENTS,
  SATURN_ELEMENTS: SATURN_ELEMENTS,
  planetPosition: planetPosition,
  saturnRingAngle: saturnRingAngle
};
