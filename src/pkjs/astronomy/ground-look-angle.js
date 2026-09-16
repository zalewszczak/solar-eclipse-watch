// ---- generic ground-observer look angle ----------------------------------
//
// Bearing + elevation from an observer's lat/lon (at ~ground level) to a
// nearby point given by its own lat/lon and altitude in meters -- used for
// aircraft (flights.js), which are close enough (tens of km, not hundreds)
// that a flat-Earth approximation over the great-circle ground distance is
// plenty accurate; no Earth-curvature or refraction correction, same
// "deliberately approximate" standard iss-orbit.js's own header comment
// describes for its circular-orbit approximation.
//
// This is deliberately NOT the same math as iss-orbit.js's issLookAngle():
// that one works in an Earth-centered-inertial frame because a satellite's
// position has to account for Earth's rotation under it between the OMM
// epoch and "now". A ground/near-ground target's lat/lon is already in the
// frame the observer stands in, so there's no rotation to correct for --
// this is genuinely simpler geometry, not a shortcut.

var EARTH_RADIUS_M = 6371000;

function toRad(deg) { return deg * Math.PI / 180; }
function toDeg(rad) { return rad * 180 / Math.PI; }

/**
 * @param {number} obsLat
 * @param {number} obsLon
 * @param {number} obsAltM   observer's altitude in meters (0 if unknown)
 * @param {number} targetLat
 * @param {number} targetLon
 * @param {number} targetAltM
 * @returns {{az: number, alt: number, distanceKm: number}}
 */
function groundLookAngle(obsLat, obsLon, obsAltM, targetLat, targetLon, targetAltM) {
  var lat1 = toRad(obsLat), lat2 = toRad(targetLat);
  var dLat = toRad(targetLat - obsLat);
  var dLon = toRad(targetLon - obsLon);

  var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) * Math.sin(dLon / 2);
  var groundDistM = EARTH_RADIUS_M * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));

  var bearingRad = Math.atan2(
    Math.sin(dLon) * Math.cos(lat2),
    Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon)
  );
  var az = (toDeg(bearingRad) + 360) % 360;

  var heightDiffM = (targetAltM || 0) - (obsAltM || 0);
  // groundDistM is 0 directly overhead; atan2 handles that fine (returns 90).
  var alt = toDeg(Math.atan2(heightDiffM, groundDistM));

  return { az: az, alt: alt, distanceKm: groundDistM / 1000 };
}

module.exports = { groundLookAngle: groundLookAngle };
