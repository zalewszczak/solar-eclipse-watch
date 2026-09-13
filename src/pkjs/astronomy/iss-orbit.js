// ---- ISS (or any other near-Earth satellite given fresh elements) -------
//
// Moved out of astro.js (astronomy split, by mathematical domain).
// Named iss-orbit.js rather than iss.js (the report's suggested name)
// to avoid colliding with the existing top-level src/pkjs/iss.js, which
// is the Celestrak fetch + field-mapping layer, not this orbital math --
// see that file's own header comment for the split between the two.

var angles = require('./angles');
var sind = angles.sind;
var cosd = angles.cosd;
var asind = angles.asind;
var atan2d = angles.atan2d;
var norm360 = angles.norm360;
var julianDay = angles.julianDay;
var julianCenturies = angles.julianCenturies;
var greenwichSiderealDeg = angles.greenwichSiderealDeg;
var sun = require('./sun');
var sunPosition = sun.sunPosition;
var sunAltitude = sun.sunAltitude;

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
  issLookAngle: issLookAngle,
  findNextIssPass: findNextIssPass
};
