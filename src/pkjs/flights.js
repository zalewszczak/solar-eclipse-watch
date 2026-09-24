/**
 * Fetch aircraft currently within a bounding box around the user's location
 * from OpenSky's /api/states/all endpoint.
 *
 * Requests use OpenSky's anonymous access only. Rate-limit responses are
 * surfaced to the caller so the existing error handling can report them.
 *
 * The module is called on demand by the watch's shake/overhead-object flow,
 * not by the normal periodic refresh cycle.
 */

var groundLookAngle = require('./astronomy/ground-look-angle').groundLookAngle;

var MAX_FLIGHTS_RETURNED = 5;

function xhrGetJSON(url, timeoutMs, cb) {
  var xhr = new XMLHttpRequest();
  var done = false;
  xhr.timeout = timeoutMs || 8000;

  xhr.onload = function () {
    if (done) return;
    done = true;

    if (xhr.status >= 200 && xhr.status < 300) {
      try {
        cb(null, JSON.parse(xhr.responseText), xhr.status);
      } catch (e) {
        cb(e, null, xhr.status);
      }
      return;
    }

    var msg = 'HTTP ' + xhr.status;
    if (xhr.status === 429) {
      var retryAfter = xhr.getResponseHeader &&
        xhr.getResponseHeader('X-Rate-Limit-Retry-After-Seconds');
      if (!retryAfter && xhr.getResponseHeader) {
        retryAfter = xhr.getResponseHeader('Retry-After');
      }
      msg = 'HTTP 429 - OpenSky rate limit reached' +
        (retryAfter ? ' (retry after ' + retryAfter + 's)' : '');
    }
    cb(new Error(msg), null, xhr.status);
  };

  xhr.onerror = function () {
    if (!done) {
      done = true;
      cb(new Error('network error'), null, 0);
    }
  };

  xhr.ontimeout = function () {
    if (!done) {
      done = true;
      cb(new Error('timeout'), null, 0);
    }
  };

  xhr.open('GET', url, true);
  xhr.send();
}

// A degree of latitude is ~111km everywhere; longitude shrinks toward the
// poles by cos(latitude), matching the approximation used for weather grids.
function boundingBox(lat, lon, radiusKm) {
  var dLat = radiusKm / 111;
  var dLon = radiusKm / (111 * Math.max(0.1, Math.cos(lat * Math.PI / 180)));
  return {
    lamin: lat - dLat,
    lamax: lat + dLat,
    lomin: lon - dLon,
    lomax: lon + dLon
  };
}

// Used for the visual elevation estimate when an aircraft's own altitude is
// missing/noisy. The watch only needs a glanceable relative position.
var ASSUMED_ALTITUDE_M = 10000;

function processStatesResponse(err, json, lat, lon, cb) {
  if (err) {
    cb(err, null);
    return;
  }

  if (!json || !Array.isArray(json.states)) {
    // No aircraft in the box can be represented by states: null.
    cb(null, []);
    return;
  }

  var flights = [];
  for (var i = 0; i < json.states.length; i++) {
    var s = json.states[i];
    // OpenSky state vector: 1=callsign, 5=longitude, 6=latitude,
    // 8=on_ground.
    if (!s || s[8]) continue;

    var fLon = s[5];
    var fLat = s[6];
    if (typeof fLat !== 'number' || typeof fLon !== 'number') continue;

    var look = groundLookAngle(
      lat, lon, 0, fLat, fLon, ASSUMED_ALTITUDE_M
    );
    if (look.alt <= 0) continue;

    look.callsign = (typeof s[1] === 'string') ? s[1].trim() : '';
    flights.push(look);
  }

  flights.sort(function (a, b) {
    return a.distanceKm - b.distanceKm;
  });

  cb(null, flights.slice(0, MAX_FLIGHTS_RETURNED));
}

function fetchStates(url, lat, lon, cb) {
  xhrGetJSON(url, 8000, function (err, json) {
    processStatesResponse(err, json, lat, lon, cb);
  });
}

/**
 * @param {number} lat
 * @param {number} lon
 * @param {number} radiusKm
 * @param {function(Error|null, Array|null)} cb
 */
function getNearbyFlights(lat, lon, radiusKm, cb) {
  var box = boundingBox(lat, lon, radiusKm);
  var url = 'https://opensky-network.org/api/states/all' +
    '?lamin=' + box.lamin + '&lomin=' + box.lomin +
    '&lamax=' + box.lamax + '&lomax=' + box.lomax;

  fetchStates(url, lat, lon, cb);
}

module.exports = { getNearbyFlights: getNearbyFlights };
