// ---- eclipse geometry and event search ---------------------------------
//
// Moved out of astro.js (astronomy split, by mathematical domain).
// computeGeometry() is the shared hub every function below (and
// stars.js's computeVisibleStars) resolves a moment down to: it pulls
// in sun.js, moon.js, and planets.js and does the sidereal-time/alt-az
// work itself via angles.js, so nothing else needs to touch those
// three position modules directly.

var angles = require('./angles');
var cosd = angles.cosd;
var atan2d = angles.atan2d;
var norm360 = angles.norm360;
var julianDay = angles.julianDay;
var julianCenturies = angles.julianCenturies;
var greenwichSiderealDeg = angles.greenwichSiderealDeg;
var angularSeparation = angles.angularSeparation;
var altAz = angles.altAz;
var sun = require('./sun');
var sunPosition = sun.sunPosition;
var sunAltitude = sun.sunAltitude;
var moon = require('./moon');
var moonPosition = moon.moonPosition;
var topocentricMoon = moon.topocentricMoon;
var planets = require('./planets');
var MERCURY_ELEMENTS = planets.MERCURY_ELEMENTS;
var VENUS_ELEMENTS = planets.VENUS_ELEMENTS;
var MARS_ELEMENTS = planets.MARS_ELEMENTS;
var JUPITER_ELEMENTS = planets.JUPITER_ELEMENTS;
var SATURN_ELEMENTS = planets.SATURN_ELEMENTS;
var planetPosition = planets.planetPosition;
var saturnRingAngle = planets.saturnRingAngle;

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
  // sunAltitude() above only ever returns altitude (it's the simpler,
  // slightly cheaper of the two -- fine for the many places that only
  // ever needed alt), so azimuth needs this project's own general
  // altAz() instead, same as the Moon/planets already use just below.
  var sunAz = altAz(sun.ra, sun.dec, latDeg, lst).az;
  var moonAltAz = altAz(moon.ra, moon.dec, latDeg, lst);
  var moonAlt = moonAltAz.alt;
  var moonAz = moonAltAz.az;

  var mercury = planetPosition(MERCURY_ELEMENTS, T, sun);
  var venus = planetPosition(VENUS_ELEMENTS, T, sun);
  var mars = planetPosition(MARS_ELEMENTS, T, sun);
  var jupiter = planetPosition(JUPITER_ELEMENTS, T, sun);
  var saturn = planetPosition(SATURN_ELEMENTS, T, sun);

  var mercuryAltAz = altAz(mercury.ra, mercury.dec, latDeg, lst);
  var venusAltAz = altAz(venus.ra, venus.dec, latDeg, lst);
  var marsAltAz = altAz(mars.ra, mars.dec, latDeg, lst);
  var jupiterAltAz = altAz(jupiter.ra, jupiter.dec, latDeg, lst);
  var saturnAltAz = altAz(saturn.ra, saturn.dec, latDeg, lst);
  var saturnRingB = saturnRingAngle(T, saturn.eclipticLon, saturn.eclipticLat);

  return {
    sep: sep,
    sunRadius: sun.semidiameterDeg,
    moonRadius: moon.semidiameterDeg,
    sunAlt: sunAlt,
    sunAz: sunAz,
    moonAlt: moonAlt,
    moonAz: moonAz,
    mercuryAlt: mercuryAltAz.alt,
    mercuryAz: mercuryAltAz.az,
    venusAlt: venusAltAz.alt,
    venusAz: venusAltAz.az,
    marsAlt: marsAltAz.alt,
    marsAz: marsAltAz.az,
    jupiterAlt: jupiterAltAz.alt,
    jupiterAz: jupiterAltAz.az,
    saturnAlt: saturnAltAz.alt,
    saturnAz: saturnAltAz.az,
    saturnRingB: saturnRingB,
    sun: sun,
    moon: moon,
    lst: lst
  };
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
 *   sunAzDecideg, moonAltDecideg, moonAzDecideg, mercuryAltDecideg,
 *   mercuryAzDecideg, venusAltDecideg, venusAzDecideg, marsAltDecideg,
 *   marsAzDecideg, jupiterAltDecideg, jupiterAzDecideg,
 *   saturnAltDecideg, saturnAzDecideg, saturnRingOpenPct,
 *   scaleMaxAltDecideg }
 */
function computeDaySkySamples(dayStart, latDeg, lonDeg, elevationDipDeg) {
  var stepMs = 60 * 60000; // 1 hour
  var count = 25; // hour 0 through hour 24 inclusive, so it spans the full day
  var dip = elevationDipDeg || 0;
  var times = [];
  var sunAltDecideg = [];
  var sunAzDecideg = [];
  var moonAltDecideg = [];
  var moonAzDecideg = [];
  var mercuryAltDecideg = [];
  var mercuryAzDecideg = [];
  var venusAltDecideg = [];
  var venusAzDecideg = [];
  var marsAltDecideg = [];
  var marsAzDecideg = [];
  var jupiterAltDecideg = [];
  var jupiterAzDecideg = [];
  var saturnAltDecideg = [];
  var saturnAzDecideg = [];
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
    // Azimuth (0-3599, tenths of a degree, true-north-based -- same
    // convention astro.js's own altAz() already returns) alongside
    // each existing altitude sample -- for "Planet seek", which needs
    // real compass-relative bearings, not just how high up something
    // is. elevationDipDeg only affects the vertical (altitude) reading,
    // so it's never applied to any of these Az values.
    sunAzDecideg.push(Math.round(geo.sunAz * 10));
    moonAltDecideg.push(moonA);
    moonAzDecideg.push(Math.round(geo.moonAz * 10));
    mercuryAltDecideg.push(Math.round((geo.mercuryAlt + dip) * 10));
    mercuryAzDecideg.push(Math.round(geo.mercuryAz * 10));
    venusAltDecideg.push(Math.round((geo.venusAlt + dip) * 10));
    venusAzDecideg.push(Math.round(geo.venusAz * 10));
    marsAltDecideg.push(Math.round((geo.marsAlt + dip) * 10));
    marsAzDecideg.push(Math.round(geo.marsAz * 10));
    jupiterAltDecideg.push(Math.round((geo.jupiterAlt + dip) * 10));
    jupiterAzDecideg.push(Math.round(geo.jupiterAz * 10));
    saturnAltDecideg.push(Math.round((geo.saturnAlt + dip) * 10));
    saturnAzDecideg.push(Math.round(geo.saturnAz * 10));
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
    sunAzDecideg: sunAzDecideg,
    moonAltDecideg: moonAltDecideg,
    moonAzDecideg: moonAzDecideg,
    mercuryAltDecideg: mercuryAltDecideg,
    mercuryAzDecideg: mercuryAzDecideg,
    venusAltDecideg: venusAltDecideg,
    venusAzDecideg: venusAzDecideg,
    marsAltDecideg: marsAltDecideg,
    marsAzDecideg: marsAzDecideg,
    jupiterAltDecideg: jupiterAltDecideg,
    jupiterAzDecideg: jupiterAzDecideg,
    saturnAltDecideg: saturnAltDecideg,
    saturnAzDecideg: saturnAzDecideg,
    saturnRingOpenPct: Math.max(0, Math.min(100, saturnRingOpenPct)),
    scaleMaxAltDecideg: Math.max(maxSunAlt, maxMoonAlt, 50)
  };
}

module.exports = {
  computeGeometry: computeGeometry,
  findEclipse: findEclipse,
  findRiseSet: findRiseSet,
  computeDaySkySamples: computeDaySkySamples
};
