/**
 * Fetch aircraft currently within a bounding box around the user's location
 * from OpenSky's /api/states/all endpoint.
 *
 * Authentication is optional. Anonymous requests use OpenSky's anonymous
 * limits. When an OpenSky API client ID and secret are configured, this
 * module uses the OAuth2 client-credentials flow and keeps the access token
 * only in memory, refreshing it automatically before expiry.
 *
 * The module is called on demand by the watch's shake/overhead-object flow,
 * not by the normal periodic refresh cycle.
 */

var groundLookAngle = require('./astronomy/ground-look-angle').groundLookAngle;

var MAX_FLIGHTS_RETURNED = 5;
var TOKEN_URL = 'https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token';
var TOKEN_REFRESH_MARGIN_SEC = 30;

var oauthToken = null;
var oauthTokenExpiresAt = 0;

function formEncode(value) {
  return encodeURIComponent(String(value == null ? '' : value)).replace(/%20/g, '+');
}

function xhrPostForm(url, body, timeoutMs, cb) {
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
      return;
    }

    var msg = 'HTTP ' + xhr.status;
    var detail = '';
    try {
      var bodyJson = JSON.parse(xhr.responseText || '');
      detail = bodyJson.error_description || bodyJson.error || '';
    } catch (ignore) {}
    if (detail) msg += ' - OpenSky token: ' + detail;
    cb(new Error(msg));
  };

  xhr.onerror = function () {
    if (!done) {
      done = true;
      cb(new Error('network error'));
    }
  };

  xhr.ontimeout = function () {
    if (!done) {
      done = true;
      cb(new Error('timeout'));
    }
  };

  xhr.open('POST', url, true);
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  xhr.send(body);
}

function clearOAuthToken() {
  oauthToken = null;
  oauthTokenExpiresAt = 0;
}

function getOAuthToken(clientId, clientSecret, cb) {
  var now = Math.floor(Date.now() / 1000);
  if (oauthToken && oauthTokenExpiresAt > now + TOKEN_REFRESH_MARGIN_SEC) {
    cb(null, oauthToken);
    return;
  }

  if (!clientId || !clientSecret) {
    cb(new Error('OpenSky OAuth2 credentials not configured'));
    return;
  }

  xhrPostForm(
    TOKEN_URL,
    'grant_type=client_credentials' +
      '&client_id=' + formEncode(clientId) +
      '&client_secret=' + formEncode(clientSecret),
    8000,
    function (err, json) {
      if (err) {
        cb(err);
        return;
      }

      if (!json || !json.access_token) {
        cb(new Error('OpenSky OAuth2 token response missing access_token'));
        return;
      }

      var expiresIn = Number(json.expires_in);
      if (!isFinite(expiresIn) || expiresIn <= 0) expiresIn = 1800;

      oauthToken = json.access_token;
      oauthTokenExpiresAt = Math.floor(Date.now() / 1000) + expiresIn;
      cb(null, oauthToken);
    }
  );
}

function xhrGetJSON(url, timeoutMs, bearerToken, cb) {
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
  if (bearerToken) {
    xhr.setRequestHeader('Authorization', 'Bearer ' + bearerToken);
  }
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

function fetchStates(url, clientId, clientSecret, token, lat, lon, cb) {
  xhrGetJSON(url, 8000, token, function (err, json, status) {
    // OpenSky tokens normally last 30 minutes. A server-side 401 can still
    // occur after revocation/expiry, so invalidate and retry exactly once.
    if (err && status === 401 && token && clientId && clientSecret) {
      clearOAuthToken();
      getOAuthToken(clientId, clientSecret, function (tokenErr, freshToken) {
        if (tokenErr) {
          cb(tokenErr, null);
          return;
        }
        xhrGetJSON(url, 8000, freshToken, function (retryErr, retryJson) {
          processStatesResponse(retryErr, retryJson, lat, lon, cb);
        });
      });
      return;
    }

    processStatesResponse(err, json, lat, lon, cb);
  });
}

/**
 * @param {number} lat
 * @param {number} lon
 * @param {number} radiusKm
 * @param {string|null} clientId OpenSky API client ID, or falsy for anonymous
 * @param {string|null} clientSecret OpenSky API client secret, or falsy for anonymous
 * @param {function(Error|null, Array|null)} cb
 */
function getNearbyFlights(lat, lon, radiusKm, clientId, clientSecret, cb) {
  var box = boundingBox(lat, lon, radiusKm);
  var url = 'https://opensky-network.org/api/states/all' +
    '?lamin=' + box.lamin + '&lomin=' + box.lomin +
    '&lamax=' + box.lamax + '&lomax=' + box.lomax;

  if (clientId && clientSecret) {
    getOAuthToken(clientId, clientSecret, function (err, token) {
      if (err) {
        cb(err, null);
        return;
      }
      fetchStates(url, clientId, clientSecret, token, lat, lon, cb);
    });
    return;
  }

  fetchStates(url, null, null, null, lat, lon, cb);
}

module.exports = { getNearbyFlights: getNearbyFlights };
