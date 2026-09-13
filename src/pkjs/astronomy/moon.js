// ---- Moon (reduced ELP2000 terms, Meeus ch.47) ------------------------
//
// Moved out of astro.js (astronomy split, by mathematical domain).
// Position, topocentric correction, and phase -- computeMoonPhase()
// needs sun.js too (phase is a Sun-Moon geometry question).

var angles = require('./angles');
var sind = angles.sind;
var cosd = angles.cosd;
var tand = angles.tand;
var atan2d = angles.atan2d;
var asind = angles.asind;
var norm360 = angles.norm360;
var julianDay = angles.julianDay;
var julianCenturies = angles.julianCenturies;
var angularSeparation = angles.angularSeparation;
var sun = require('./sun');
var sunPosition = sun.sunPosition;

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

module.exports = {
  moonPosition: moonPosition,
  topocentricMoon: topocentricMoon,
  computeMoonPhase: computeMoonPhase
};
