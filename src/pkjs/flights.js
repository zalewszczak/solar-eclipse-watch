/**
 * flights.js -- fetches aircraft currently within a bounding box around the
 * user's location from OpenSky Network's free /api/states/all endpoint
 * (https://openskynetwork.github.io/opensky-api/rest.html), the standard
 * free ADS-B aggregator most amateur flight-tracking tools pull from.
 * Works anonymously with a fairly low rate limit; an account (entered as
 * "username:password" in the settings page's Flights API key field) raises
 * that limit via HTTP Basic auth, same optional-credentials shape as this
 * app's OpenWeatherMap key.
 *
 * Only called on a shake when the cached overhead-objects list is stale
 * (see comms_maybe_request_flights() on the watch side) or missing, same
 * "fetch on demand, not on a timer" spirit as everything else this module
 * feeds into (overhead-objects.js).
 */

var groundLookAngle = require('./astronomy/ground-look-angle').groundLookAngle;

var MAX_FLIGHTS_RETURNED = 5;

function xhrGetJSON(url, timeoutMs, user, pass, cb) {
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
  xhr.onerror = function () { if (!done) { done = true; cb(new Error('network error')); } };
  xhr.ontimeout = function () { if (!done) { done = true; cb(new Error('timeout')); } };
  if (user) {
    xhr.open('GET', url, true, user, pass);
  } else {
    xhr.open('GET', url, true);
  }
  xhr.send();
}

// A degree of latitude is ~111km everywhere; a degree of longitude shrinks
// toward the poles by cos(latitude) -- same approximation this app already
// uses for weather grid bounding boxes.
function boundingBox(lat, lon, radiusKm) {
  var dLat = radiusKm / 111;
  var dLon = radiusKm / (111 * Math.max(0.1, Math.cos(lat * Math.PI / 180)));
  return { lamin: lat - dLat, lamax: lat + dLat, lomin: lon - dLon, lomax: lon + dLon };
}

// Cruise altitude assumed for every aircraft, rather than each one's own
// reported (and sometimes missing/noisy) geo_altitude/baro_altitude --
// good enough to make "further away = lower on the horizon" hold, which
// is the only thing this angle is actually used for on a 150-ish-pixel
// screen; real cruise varies roughly 9-12km but that's well within the
// slop this approximation already has.
var ASSUMED_ALTITUDE_M = 10000;

/**
 * @param {number} lat
 * @param {number} lon
 * @param {number} radiusKm
 * @param {string|null} apiKey  "username:password", or falsy for anonymous
 * @param {function(Error|null, Array<{az:number, alt:number, distanceKm:number, callsign:string}>|null)} cb
 */
function getNearbyFlights(lat, lon, radiusKm, apiKey, cb) {
  var box = boundingBox(lat, lon, radiusKm);
  var url = 'https://opensky-network.org/api/states/all' +
    '?lamin=' + box.lamin + '&lomin=' + box.lomin +
    '&lamax=' + box.lamax + '&lomax=' + box.lomax;

  var user = null, pass = null;
  if (apiKey && apiKey.indexOf(':') !== -1) {
    var parts = apiKey.split(':');
    user = parts[0];
    pass = parts.slice(1).join(':');
  }

  xhrGetJSON(url, 8000, user, pass, function (err, json) {
    if (err) return cb(err, null);
    if (!json || !Array.isArray(json.states)) {
      // No aircraft in the box comes back as states: null -- not an error.
      return cb(null, []);
    }
    var flights = [];
    for (var i = 0; i < json.states.length; i++) {
      var s = json.states[i];
      // Index layout per OpenSky's OpenSkyStateVector: 1=callsign, 5=lon,
      // 6=lat, 8=on_ground.
      if (!s || s[8]) continue; // on_ground
      var fLon = s[5], fLat = s[6];
      if (typeof fLat !== 'number' || typeof fLon !== 'number') continue;

      var look = groundLookAngle(lat, lon, 0, fLat, fLon, ASSUMED_ALTITUDE_M);
      if (look.alt <= 0) continue; // below the horizon from here
      look.callsign = (typeof s[1] === 'string') ? s[1].trim() : '';
      flights.push(look);
    }
    // Closest first, capped -- see MAX_OVERHEAD_OBJECTS on the watch side
    // for why the combined (ISS + flights) list stays small regardless.
    flights.sort(function (a, b) { return a.distanceKm - b.distanceKm; });
    cb(null, flights.slice(0, MAX_FLIGHTS_RETURNED));
  });
}

module.exports = { getNearbyFlights: getNearbyFlights };
