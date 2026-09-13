// ---- Open-Meteo provider ---------------------------------------------
//
// Moved out of weather.js (weather providers extraction). Primary
// weather source: free, no API key, global hourly forecast. Used
// unconditionally, unlike OpenWeatherMap (opt-in, needs a user-supplied
// key) -- see weather.js's getEclipseWeather() for how the two combine.

var xhrGetJSON = require('./http').xhrGetJSON;
var conditionFromWmoCode = require('./weather-normalize').conditionFromWmoCode;

// Average hourly cloud-cover (%) across the hours spanning
// [fromDate, toDate], from Open-Meteo's hourly forecast.
function fetchOpenMeteo(lat, lon, fromDate, toDate, cb) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
            '?latitude=' + encodeURIComponent(lat) +
            '&longitude=' + encodeURIComponent(lon) +
            '&hourly=cloudcover' +
            '&timezone=auto' +
            '&forecast_days=2';
  xhrGetJSON(url, 8000, function (err, json) {
    if (err) return cb(err);
    try {
      var times = json.hourly.time;
      var clouds = json.hourly.cloudcover;
      var sum = 0, n = 0;
      for (var i = 0; i < times.length; i++) {
        var t = new Date(times[i]).getTime();
        if (t >= fromDate.getTime() - 3600000 && t <= toDate.getTime() + 3600000) {
          sum += clouds[i];
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

/**
 * Cloud cover matched to an arbitrary array of Date times (used to
 * align with astro.js's computeDaySkySamples() grid, so the sky
 * gradient and the cloud puffs are driven by the same timeline).
 * Open-Meteo only -- its hourly resolution is what we actually need
 * here, whereas a second source mainly earns its keep for the single
 * "eclipse window" headline stat (see weather.js's getEclipseWeather()).
 *
 * Also pulls today's sunrise/sunset from the same call (Open-Meteo's
 * `daily` parameter, essentially free alongside the hourly request)
 * -- these are more accurate than our own low-precision ephemeris,
 * since Open-Meteo accounts for standard atmospheric refraction
 * properly, so index.js uses them to refine the eclipse's `sunset`
 * field when available. And the current weather condition (rain,
 * snow, fog, thunderstorm) nearest to `nowDate`, plus the current
 * temperature (Open-Meteo's `current_weather` block) and today's
 * high/low, for the optional canvas weather readouts. And a rough
 * cloud *altitude* estimate (0=low, 100=high), from Open-Meteo's
 * low/mid/high cloud-cover breakdown -- a weighted average of which
 * band(s) actually have cover right now -- used to bias how high up
 * the canvas draws its cloud clusters. Also today's max UV index, the
 * current-hour UV index, max precipitation probability, current
 * relative humidity, and current wind speed (from the same
 * current_weather block already being fetched), for the corner-
 * overlay readouts.
 *
 * @param {number} lat
 * @param {number} lon
 * @param {Date[]} times
 * @param {Date} nowDate  which hour's weathercode counts as "current"
 * @param {function(Error|null, number[]|null, object)} cb
 *   cloud % per input time, and an object with: sunrise, sunset
 *   (Date|null), condition (0 if unknown), tempC (number|null),
 *   tempHighC (number|null), tempLowC (number|null), cloudAltitudePct
 *   (0-100, 50 if unavailable), uvIndexMax (number|null),
 *   uvIndexCurrent (number|null), rainChancePct (number|null),
 *   humidityPct (number|null), windSpeedKmh (number|null)
 */
function getDailyCloudGrid(lat, lon, times, nowDate, cb) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
            '?latitude=' + encodeURIComponent(lat) +
            '&longitude=' + encodeURIComponent(lon) +
            '&hourly=cloudcover,weathercode,cloudcover_low,cloudcover_mid,cloudcover_high,relativehumidity_2m,dewpoint_2m,surface_pressure,temperature_2m,uv_index' +
            '&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min,uv_index_max,precipitation_probability_max' +
            '&current_weather=true' +
            '&timezone=auto' +
            '&forecast_days=2';
  var emptyExtras = {
    sunrise: null, sunset: null, condition: 0, tempC: null, tempHighC: null, tempLowC: null,
    cloudAltitudePct: 50, uvIndexMax: null, uvIndexCurrent: null, rainChancePct: null, humidityPct: null, windSpeedKmh: null,
    currentCloudPct: null, windDirDeg: null, dewPointC: null, pressureHpa: null, pressureTrend: 0,
    forecastTempC: [null, null, null, null, null, null], forecastCondition: [0, 0, 0, 0, 0, 0]
  };
  xhrGetJSON(url, 8000, function (err, json) {
    if (err) return cb(err, null, emptyExtras);
    try {
      var hourlyTimes = json.hourly.time.map(function (s) { return new Date(s).getTime(); });
      var hourlyClouds = json.hourly.cloudcover;
      var hourlyCodes = json.hourly.weathercode || [];

      function nearestIdx(target) {
        var bestIdx = 0, bestDiff = Infinity;
        for (var i = 0; i < hourlyTimes.length; i++) {
          var diff = Math.abs(hourlyTimes[i] - target);
          if (diff < bestDiff) { bestDiff = diff; bestIdx = i; }
        }
        return bestIdx;
      }

      var nowIdx = nearestIdx(nowDate.getTime());
      var result = times.map(function (t) { return hourlyClouds[nearestIdx(t.getTime())]; });
      // Same source and same "now" index the CLOUD_SAMPLES grid itself
      // interpolates from -- keeping this consistent with what's
      // actually drawn matters more here than an OWM-blended headline
      // figure would, since that one covers a different time window
      // (the eclipse window, or the next 3 hours) and can legitimately
      // disagree with what the sky canvas shows right now.
      var currentCloudPct = (typeof hourlyClouds[nowIdx] === 'number') ? Math.round(hourlyClouds[nowIdx]) : null;

      var condition = 0;
      if (hourlyCodes.length > 0) {
        condition = conditionFromWmoCode(hourlyCodes[nowIdx]);
      }

      var tempC = null, windSpeedKmh = null, windDirDeg = null;
      if (json.current_weather) {
        if (typeof json.current_weather.temperature === 'number') tempC = json.current_weather.temperature;
        if (typeof json.current_weather.windspeed === 'number') windSpeedKmh = json.current_weather.windspeed;
        if (typeof json.current_weather.winddirection === 'number') windDirDeg = json.current_weather.winddirection;
      }

      var humidityPct = null;
      if (json.hourly.relativehumidity_2m && typeof json.hourly.relativehumidity_2m[nowIdx] === 'number') {
        humidityPct = json.hourly.relativehumidity_2m[nowIdx];
      }

      var dewPointC = null;
      if (json.hourly.dewpoint_2m && typeof json.hourly.dewpoint_2m[nowIdx] === 'number') {
        dewPointC = json.hourly.dewpoint_2m[nowIdx];
      }

      // This-hour UV index, as opposed to uv_index_max below (today's
      // whole-day peak) -- same nowIdx the rest of the hourly block
      // already uses for "right now".
      var uvIndexCurrent = null;
      if (json.hourly.uv_index && typeof json.hourly.uv_index[nowIdx] === 'number') {
        uvIndexCurrent = json.hourly.uv_index[nowIdx];
      }

      // Pressure trend: compare now vs. 3 hours ago (a standard
      // meteorological window) -- clamped to the start of the array so
      // this doesn't go negative in the first few hours of the day.
      var pressureHpa = null, pressureTrend = 0;
      if (json.hourly.surface_pressure && typeof json.hourly.surface_pressure[nowIdx] === 'number') {
        pressureHpa = json.hourly.surface_pressure[nowIdx];
        var pastIdx = Math.max(0, nowIdx - 3);
        var pastPressure = json.hourly.surface_pressure[pastIdx];
        if (typeof pastPressure === 'number') {
          var delta = pressureHpa - pastPressure;
          if (delta > 1) pressureTrend = 1; // rising
          else if (delta < -1) pressureTrend = 2; // falling
          else pressureTrend = 0; // flat
        }
      }

      var cloudAltitudePct = 50;
      if (json.hourly.cloudcover_low && json.hourly.cloudcover_mid && json.hourly.cloudcover_high) {
        var low = json.hourly.cloudcover_low[nowIdx] || 0;
        var mid = json.hourly.cloudcover_mid[nowIdx] || 0;
        var high = json.hourly.cloudcover_high[nowIdx] || 0;
        var total = low + mid + high;
        if (total > 0) {
          cloudAltitudePct = Math.round((mid * 50 + high * 100) / total);
        }
      }

      // "Weather in 1-6 hours" content (features_layer.c ids 87-92) --
      // temperature_2m/weathercode at nowIdx+1..nowIdx+6. forecast_days=2
      // gives 48 hourly points starting at hour 0 of today, so nowIdx+6
      // only runs off the end of the array in the last few hours of day
      // 2 -- guarded per-index below rather than assumed safe.
      var forecastTempC = [null, null, null, null, null, null];
      var forecastCondition = [0, 0, 0, 0, 0, 0];
      var hourlyTemps = json.hourly.temperature_2m;
      for (var fh = 0; fh < 6; fh++) {
        var fIdx = nowIdx + 1 + fh;
        if (hourlyTemps && typeof hourlyTemps[fIdx] === 'number') {
          forecastTempC[fh] = Math.round(hourlyTemps[fIdx]);
        }
        if (hourlyCodes && hourlyCodes.length > fIdx) {
          forecastCondition[fh] = conditionFromWmoCode(hourlyCodes[fIdx]);
        }
      }

      var sunrise = null, sunset = null, tempHighC = null, tempLowC = null, uvIndexMax = null, rainChancePct = null;
      if (json.daily) {
        if (json.daily.sunrise && json.daily.sunrise[0]) sunrise = new Date(json.daily.sunrise[0]);
        if (json.daily.sunset && json.daily.sunset[0]) sunset = new Date(json.daily.sunset[0]);
        if (json.daily.temperature_2m_max && typeof json.daily.temperature_2m_max[0] === 'number') {
          tempHighC = json.daily.temperature_2m_max[0];
        }
        if (json.daily.temperature_2m_min && typeof json.daily.temperature_2m_min[0] === 'number') {
          tempLowC = json.daily.temperature_2m_min[0];
        }
        if (json.daily.uv_index_max && typeof json.daily.uv_index_max[0] === 'number') {
          uvIndexMax = json.daily.uv_index_max[0];
        }
        if (json.daily.precipitation_probability_max && typeof json.daily.precipitation_probability_max[0] === 'number') {
          rainChancePct = json.daily.precipitation_probability_max[0];
        }
      }

      cb(null, result, {
        sunrise: sunrise, sunset: sunset, condition: condition,
        tempC: tempC, tempHighC: tempHighC, tempLowC: tempLowC,
        cloudAltitudePct: cloudAltitudePct, uvIndexMax: uvIndexMax, uvIndexCurrent: uvIndexCurrent,
        rainChancePct: rainChancePct, humidityPct: humidityPct, windSpeedKmh: windSpeedKmh,
        currentCloudPct: currentCloudPct, windDirDeg: windDirDeg, dewPointC: dewPointC,
        pressureHpa: pressureHpa, pressureTrend: pressureTrend,
        forecastTempC: forecastTempC, forecastCondition: forecastCondition
      });
    } catch (e) {
      cb(e, null, emptyExtras);
    }
  });
}

module.exports = {
  fetchOpenMeteo: fetchOpenMeteo,
  getDailyCloudGrid: getDailyCloudGrid
};
