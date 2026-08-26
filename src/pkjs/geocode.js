/**
 * geocode.js -- turns lat/lon into a short human-readable place name
 * ("Innsbruck, Austria") to show alongside the weather readout, just
 * as a sanity check that the app is looking at the right spot.
 *
 * Uses OpenStreetMap's Nominatim reverse-geocoding endpoint: free, no
 * API key. Nominatim's usage policy asks for at most ~1 request/sec
 * and discourages busy polling -- index.js only calls this when the
 * location has actually moved (see the caching wrapper there), so in
 * practice it's one lookup per trip, not per refresh cycle.
 */

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
 * @param {number} lat
 * @param {number} lon
 * @param {function(Error|null, string|null)} cb
 */
function reverseGeocode(lat, lon, cb) {
  var url = 'https://nominatim.openstreetmap.org/reverse' +
            '?format=jsonv2' +
            '&lat=' + encodeURIComponent(lat) +
            '&lon=' + encodeURIComponent(lon) +
            '&zoom=10' +
            '&addressdetails=1';
  xhrGetJSON(url, 8000, function (err, json) {
    if (err) return cb(err, null);
    try {
      var addr = json.address || {};
      var place = addr.city || addr.town || addr.village || addr.hamlet ||
                   addr.municipality || addr.county || json.name;
      var region = addr.state || addr.country;
      var name;
      if (place && region && place !== region) {
        name = place + ', ' + region;
      } else {
        name = place || addr.country || null;
      }
      if (!name) return cb(new Error('no place name in response'), null);
      // Keep it short -- the watch only has one line to show this on,
      // and the receiving buffer is a fixed 32 bytes (C string, so 31
      // usable chars). Plain ASCII "..." rather than a unicode
      // ellipsis, since we can't be sure the watch's font has that
      // glyph and the on-watch text layer already does its own
      // trailing-ellipsis truncation for anything still too long to
      // fit the line.
      if (name.length > 28) name = name.substring(0, 25) + '...';
      cb(null, name);
    } catch (e) {
      cb(e, null);
    }
  });
}

module.exports = { reverseGeocode: reverseGeocode };
