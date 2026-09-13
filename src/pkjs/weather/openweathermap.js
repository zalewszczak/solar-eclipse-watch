// ---- OpenWeatherMap provider ----------------------------------------------
//
// Moved out of weather.js (weather providers extraction). Optional
// second cloud-cover source, only used if the user supplied an API
// key in settings -- see weather.js's getEclipseWeather() for how this
// is combined with Open-Meteo.

var xhrGetJSON = require('./http').xhrGetJSON;

// OpenWeatherMap "One Call" style hourly cloud-cover, only used if
// the user supplied an API key in settings.
function fetchOpenWeatherMap(lat, lon, apiKey, fromDate, toDate, cb) {
  var url = 'https://api.openweathermap.org/data/2.5/forecast' +
            '?lat=' + encodeURIComponent(lat) +
            '&lon=' + encodeURIComponent(lon) +
            '&appid=' + encodeURIComponent(apiKey);
  xhrGetJSON(url, 8000, function (err, json) {
    if (err) return cb(err);
    try {
      var list = json.list || [];
      var sum = 0, n = 0;
      for (var i = 0; i < list.length; i++) {
        var t = list[i].dt * 1000;
        if (t >= fromDate.getTime() - 5400000 && t <= toDate.getTime() + 5400000) {
          sum += list[i].clouds.all;
          n++;
        }
      }
      if (n === 0) return cb(new Error('no matching hours'));
      cb(null, Math.round(sum / n));
    } catch (e) {
      cb(e);
    }
  });
}

module.exports = {
  fetchOpenWeatherMap: fetchOpenWeatherMap
};
