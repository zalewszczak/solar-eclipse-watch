/**
 * iss.js -- fetches the ISS's current orbital elements from
 * Celestrak's free GP (General Perturbations) data API and computes
 * its current position as seen from the user's location.
 *
 * Celestrak publishes elements for every tracked object, updated
 * regularly from the official USSF catalog -- catalog number 25544
 * is the ISS specifically. This is the standard, well-known free
 * source for this kind of data (the same one basically every
 * satellite-tracking tool, amateur or professional, ultimately pulls
 * from). Usage policy asks for well under 1 request/sec; this app
 * only calls it once per refresh cycle (same 5-60 minute interval as
 * everything else), so that's never a concern.
 *
 * The actual alt/az math lives in astro.js's issLookAngle() -- this
 * module is just the fetch + field-mapping layer between Celestrak's
 * OMM JSON and what that function expects.
 */

var astro = require('./astro.js');

var ISS_CATALOG_NUMBER = 25544;

function xhrGetJSON(url, timeoutMs, cb) {
  var xhr = new XMLHttpRequest();
  var done = false;
  xhr.timeout = timeoutMs || 8000;
  xhr.onload = function () {
    if (done) return;
    done = true;
    if (xhr.status >= 200 && xhr.status < 300) {
      try {
        cb(null, JSON.parse(xhr.responseText));
      } catch (e) {
        cb(e);
      }
    } else {
      cb(new Error('HTTP ' + xhr.status));
    }
  };
  xhr.onerror = function () {
    if (done) return;
    done = true;
    cb(new Error('network error'));
  };
  xhr.ontimeout = function () {
    if (done) return;
    done = true;
    cb(new Error('timeout'));
  };
  xhr.open('GET', url, true);
  xhr.send();
}

/**
 * Fetches fresh ISS elements and computes its current look angle.
 *
 * @param {number} lat
 * @param {number} lon
 * @param {function(Error|null, {alt: number, az: number, distanceKm: number, epoch: Date}|null)} cb
 */
function getIssPosition(lat, lon, cb) {
  var url = 'https://celestrak.org/NORAD/elements/gp.php?CATNR=' + ISS_CATALOG_NUMBER + '&FORMAT=json';
  xhrGetJSON(url, 8000, function (err, json) {
    if (err) return cb(err, null);
    try {
      var rec = Array.isArray(json) ? json[0] : json;
      if (!rec || typeof rec.MEAN_MOTION !== 'number') {
        return cb(new Error('unexpected Celestrak response shape'), null);
      }
      var omm = {
        epoch: new Date(rec.EPOCH + 'Z'), // Celestrak's EPOCH has no zone suffix but is UTC
        meanMotionRevPerDay: rec.MEAN_MOTION,
        eccentricity: rec.ECCENTRICITY,
        inclinationDeg: rec.INCLINATION,
        raanDeg: rec.RA_OF_ASC_NODE,
        argPerigeeDeg: rec.ARG_OF_PERICENTER,
        meanAnomalyDeg: rec.MEAN_ANOMALY
      };
      var look = astro.issLookAngle(omm, new Date(), lat, lon);
      cb(null, { alt: look.alt, az: look.az, distanceKm: look.distanceKm, epoch: omm.epoch });
    } catch (e) {
      cb(e, null);
    }
  });
}

module.exports = { getIssPosition: getIssPosition };
