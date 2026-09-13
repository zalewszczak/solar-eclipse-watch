/**
 * weather.js -- cloud cover / eclipse-visibility lookup, and the
 * public API every other module (refresh-manager.js) consumes.
 *
 * Split into provider-specific modules (weather providers extraction):
 * open-meteo.js (primary source, used unconditionally), openweathermap.js
 * (optional second source, only if the user supplied an API key --
 * averaged with Open-Meteo when both are available, smoothing over the
 * biggest single-model errors), air-quality.js, and weather-normalize.js
 * (the WMO weather-code -> on-watch condition-enum mapping). This file
 * is the orchestrator: it combines the providers (getEclipseWeather)
 * and re-exports each provider's own public function so callers keep
 * requiring just './weather/weather' for everything, same shape as
 * before this split.
 *
 * fetchAuroraKp stays here rather than in its own provider file -- it's
 * a single small NOAA fetch, not a multi-function provider the way
 * Open-Meteo/OpenWeatherMap/air-quality are.
 */

var xhrGetJSON = require('./http').xhrGetJSON;
var openMeteo = require('./open-meteo');
var fetchOpenMeteo = openMeteo.fetchOpenMeteo;
var getDailyCloudGrid = openMeteo.getDailyCloudGrid;
var fetchOpenWeatherMap = require('./openweathermap').fetchOpenWeatherMap;
var fetchAirQuality = require('./air-quality').fetchAirQuality;

/**
 * Combines available sources into a single cloud-cover % and a
 * derived 0-100 "visibility score" (simply 100 - cloud%, but kept as
 * its own field in case we want to fold in humidity/haze later).
 *
 * @param {number} lat
 * @param {number} lon
 * @param {string|null} owmApiKey
 * @param {Date} fromDate  start of the window worth checking (e.g. C1)
 * @param {Date} toDate    end of the window (e.g. C4)
 * @param {function(object)} cb  called with {cloudCoverPct, visScorePct, sourceCount}
 * @param {function(string, Error|null)} [onSourceResult]  optional,
 *   called once per source actually attempted ('openmeteo' always,
 *   'openweathermap' only if owmApiKey is set) with that source's own
 *   error (or null on success) -- lets callers log/report each
 *   upstream service individually rather than just the merged result.
 */
function getEclipseWeather(lat, lon, owmApiKey, fromDate, toDate, cb, onSourceResult) {
  var results = [];
  var pending = owmApiKey ? 2 : 1;

  function finish() {
    pending--;
    if (pending > 0) return;
    if (results.length === 0) {
      // Both sources failed (e.g. offline) -- report "unknown" rather
      // than a misleading 0/100.
      cb({ cloudCoverPct: 255, visScorePct: 255, sourceCount: 0 });
      return;
    }
    var sum = 0;
    for (var i = 0; i < results.length; i++) sum += results[i];
    var avg = Math.round(sum / results.length);
    cb({ cloudCoverPct: avg, visScorePct: 100 - avg, sourceCount: results.length });
  }

  fetchOpenMeteo(lat, lon, fromDate, toDate, function (err, pct) {
    if (onSourceResult) onSourceResult('openmeteo', err);
    if (!err) results.push(pct);
    finish();
  });

  if (owmApiKey) {
    fetchOpenWeatherMap(lat, lon, owmApiKey, fromDate, toDate, function (err, pct) {
      if (onSourceResult) onSourceResult('openweathermap', err);
      if (!err) results.push(pct);
      finish();
    });
  }
}

// Current planetary Kp index (geomagnetic activity, 0-9 in thirds --
// e.g. 4.33/4.67) from NOAA's Space Weather Prediction Center -- free,
// no API key, same "no signup needed" bar as Open-Meteo (open-meteo.js). This
// endpoint publishes the 3-hourly definitive/estimated planetary Kp
// series; the last row is the most recent value. Kp alone doesn't say
// whether aurora is actually visible from any particular place (that
// also depends on geomagnetic latitude -- see astro.js's
// geomagneticLatitudeDeg()/auroraVisibilityScore()), just how
// geomagnetically active the whole planet currently is.
//
// NOAA has served two different shapes for this specific endpoint's
// rows over time -- most other SWPC JSON products use a header row
// (an array of column-name strings) followed by data rows that are
// themselves plain arrays of stringified values in that column order
// (e.g. row = ["2026-09-03 18:00:00", "2.00", "7", "8"], Kp always at
// index 1); this one switched at some point to a plain array of
// objects instead, one per reading, with named fields directly (e.g.
// {"time_tag":"2026-09-03T18:00:00","Kp":2.00,"a_running":7,
// "station_count":8}) -- no header row at all, since none is needed
// when every row already names its own fields. Confirmed directly
// against the live endpoint. Handles both shapes below (object first,
// since that's what NOAA actually serves for this product now) rather
// than assuming either permanently -- this exact mismatch (still only
// ever checking Array.isArray(last) and reading last[1], which is
// never true for a plain object) is what "malformed Kp row" meant:
// every row failed that shape check and got rejected, regardless of
// its actual Kp value.
function fetchAuroraKp(cb) {
  var url = 'https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json';
  xhrGetJSON(url, 8000, function (err, json) {
    if (err || !Array.isArray(json) || json.length < 2) return cb(err || new Error('empty Kp response'), null);
    var last = json[json.length - 1];
    var kp;
    if (last && typeof last === 'object' && !Array.isArray(last)) {
      kp = parseFloat(last.Kp);
    } else if (Array.isArray(last) && last.length >= 2) {
      kp = parseFloat(last[1]);
    } else {
      return cb(new Error('malformed Kp row'), null);
    }
    if (isNaN(kp)) return cb(new Error('bad Kp value'), null);
    cb(null, kp);
  });
}

module.exports = {
  getEclipseWeather: getEclipseWeather,
  getDailyCloudGrid: getDailyCloudGrid,
  fetchAirQuality: fetchAirQuality,
  fetchAuroraKp: fetchAuroraKp
};
