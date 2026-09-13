// ---- air quality provider -------------------------------------------
//
// Moved out of weather.js (weather providers extraction).

var xhrGetJSON = require('./http').xhrGetJSON;

// Separate Open-Meteo service (different subdomain, no signup needed,
// same as the main forecast call) -- both AQI standards come back in
// one request, so which one to actually display is purely a
// settings-page/on-watch choice (CONFIG_AQI_UNIT), not a second fetch.
function fetchAirQuality(lat, lon, cb) {
  var url = 'https://air-quality-api.open-meteo.com/v1/air-quality' +
            '?latitude=' + encodeURIComponent(lat) +
            '&longitude=' + encodeURIComponent(lon) +
            '&current=us_aqi,european_aqi' +
            '&timezone=auto';
  xhrGetJSON(url, 8000, function (err, json) {
    if (err || !json || !json.current) return cb(err, { aqiUs: null, aqiEu: null });
    var aqiUs = (typeof json.current.us_aqi === 'number') ? Math.round(json.current.us_aqi) : null;
    var aqiEu = (typeof json.current.european_aqi === 'number') ? Math.round(json.current.european_aqi) : null;
    cb(null, { aqiUs: aqiUs, aqiEu: aqiEu });
  });
}

module.exports = {
  fetchAirQuality: fetchAirQuality
};
