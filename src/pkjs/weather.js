/**
 * weather.js -- cloud cover / eclipse-visibility lookup.
 *
 * Primary source: Open-Meteo (https://open-meteo.com), free, no API
 * key, global hourly cloud-cover forecast. Used unconditionally.
 *
 * Optional second source: OpenWeatherMap, if the user pastes an API
 * key into the settings page. When both are available we average
 * them, which smooths over the biggest single-model errors -- cloud
 * cover forecasts, especially cumulus/convective cover, are notably
 * model-dependent.
 *
 * Everything here is XHR-based (PKJS has no fetch()).
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
 */
function getEclipseWeather(lat, lon, owmApiKey, fromDate, toDate, cb) {
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
    if (!err) results.push(pct);
    finish();
  });

  if (owmApiKey) {
    fetchOpenWeatherMap(lat, lon, owmApiKey, fromDate, toDate, function (err, pct) {
      if (!err) results.push(pct);
      finish();
    });
  }
}

// Maps an Open-Meteo/WMO weather code to our simplified on-watch
// condition enum. Plain clear/cloudy states need no special effect
// beyond the cloud-cover puffs already driven by CLOUD_SAMPLES, so
// they (and anything unrecognized) fall through to 0.
function conditionFromWmoCode(code) {
  if (code === 45 || code === 48) return 1; // fog
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 2; // drizzle/rain/showers
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 3; // snow
  if (code === 95 || code === 96 || code === 99) return 4; // thunderstorm
  return 0;
}

/**
 * Cloud cover matched to an arbitrary array of Date times (used to
 * align with astro.js's computeDaySkySamples() grid, so the sky
 * gradient and the cloud puffs are driven by the same timeline).
 * Open-Meteo only -- its hourly resolution is what we actually need
 * here, whereas a second source mainly earns its keep for the single
 * "eclipse window" headline stat (see getEclipseWeather above).
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
 * the canvas draws its cloud clusters. Also today's max UV index,
 * max precipitation probability, current relative humidity, and
 * current wind speed (from the same current_weather block already
 * being fetched), for the corner-overlay readouts.
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
 *   rainChancePct (number|null), humidityPct (number|null),
 *   windSpeedKmh (number|null)
 */
function getDailyCloudGrid(lat, lon, times, nowDate, cb) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
            '?latitude=' + encodeURIComponent(lat) +
            '&longitude=' + encodeURIComponent(lon) +
            '&hourly=cloudcover,weathercode,cloudcover_low,cloudcover_mid,cloudcover_high,relativehumidity_2m,dewpoint_2m,surface_pressure' +
            '&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min,uv_index_max,precipitation_probability_max' +
            '&current_weather=true' +
            '&timezone=auto' +
            '&forecast_days=2';
  var emptyExtras = {
    sunrise: null, sunset: null, condition: 0, tempC: null, tempHighC: null, tempLowC: null,
    cloudAltitudePct: 50, uvIndexMax: null, rainChancePct: null, humidityPct: null, windSpeedKmh: null,
    currentCloudPct: null, windDirDeg: null, dewPointC: null, pressureHpa: null, pressureTrend: 0
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
        cloudAltitudePct: cloudAltitudePct, uvIndexMax: uvIndexMax,
        rainChancePct: rainChancePct, humidityPct: humidityPct, windSpeedKmh: windSpeedKmh,
        currentCloudPct: currentCloudPct, windDirDeg: windDirDeg, dewPointC: dewPointC,
        pressureHpa: pressureHpa, pressureTrend: pressureTrend
      });
    } catch (e) {
      cb(e, null, emptyExtras);
    }
  });
}

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

// Current planetary Kp index (geomagnetic activity, 0-9 in thirds --
// e.g. 4.33/4.67) from NOAA's Space Weather Prediction Center -- free,
// no API key, same "no signup needed" bar as Open-Meteo above. This
// endpoint publishes the 3-hourly definitive/estimated planetary Kp
// series; the last row is the most recent value. Kp alone doesn't say
// whether aurora is actually visible from any particular place (that
// also depends on geomagnetic latitude -- see astro.js's
// geomagneticLatitudeDeg()/auroraVisibilityScore()), just how
// geomagnetically active the whole planet currently is.
function fetchAuroraKp(cb) {
  var url = 'https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json';
  xhrGetJSON(url, 8000, function (err, json) {
    if (err || !Array.isArray(json) || json.length < 2) return cb(err || new Error('empty Kp response'), null);
    var last = json[json.length - 1];
    if (!Array.isArray(last) || last.length < 2) return cb(new Error('malformed Kp row'), null);
    var kp = parseFloat(last[1]);
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
