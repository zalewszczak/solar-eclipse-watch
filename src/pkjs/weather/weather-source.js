// ---- weather data source: Pebble weather plugin (default) or Open-Meteo ---------------
//
// CONFIG_WEATHER_SOURCE: 'plugin' (default) | 'plugin:<uuid>' | 'openmeteo'.
//
// With 'plugin', the Pebble app's own weather plugin (weather/location, the
// first saved location) supplies the headline values: temperature, high/low,
// condition, UV and chance of rain. The plugin does not offer wind, humidity,
// pressure, dew point, cloud cover or the hourly/daily forecast arrays, so
// Open-Meteo is still queried for those and the plugin values are laid over
// the result. Eclipse/sky weather (cloud samples, eclipse-window cloud/visibility) always
// comes from the real forecast sources. If the plugin is unavailable (old Pebble app, plugins switched
// off, no weather location, timeout) the Open-Meteo snapshot is used as-is, so
// weather keeps working exactly like before.

var weather = require('./weather');
var pluginManager = require('../plugins/plugin-manager');
var getSetting = require('../settings/settings').getSetting;

var PLUGIN_TIMEOUT_MS = 6000;
var PROPS = ['temperature', 'high', 'low', 'condition_code', 'uv_index', 'precipitation'];

function numberOf(payloads) {
  if (!payloads) return null;
  var nv = payloads.numericValue;
  if (nv && typeof nv.value === 'number') {
    var v = nv.value;
    if (nv.unit && /F/i.test(nv.unit)) v = (v - 32) * 5 / 9;
    return v;
  }
  var txt = payloads.shortText && payloads.shortText.text;
  if (typeof txt === 'string') {
    var m = /-?[0-9]+(\.[0-9]+)?/.exec(txt);
    if (m) {
      var n = parseFloat(m[0]);
      return /F\s*$/i.test(txt) ? (n - 32) * 5 / 9 : n;
    }
  }
  return null;
}

function textOf(payloads) {
  if (!payloads) return '';
  var k = payloads.longText || payloads.shortText;
  return (k && typeof k.text === 'string') ? k.text : '';
}

// Plugin condition_code -> { cond: condition enum (0 clear/cloud, 2 rain, 3 snow); wx is the detailed
// class used by Modern's weather backgrounds, which are not part of Eclipz }.
var CODE_MAP = {
  sun: { wx: 0, cond: 0 },
  partly_cloudy: { wx: 1, cond: 0 },
  cloudy: { wx: 2, cond: 0 },
  light_rain: { wx: 4, cond: 2 },
  heavy_rain: { wx: 6, cond: 2 },
  light_snow: { wx: 7, cond: 3 },
  heavy_snow: { wx: 7, cond: 3 },
  rain_and_snow: { wx: 9, cond: 2 }
};

// Reads the plugin once. cb(err, { tempC, tempHighC, tempLowC, uv, rainPct, code })
function fetchPluginWeather(providerUuid, cb) {
  if (!pluginManager.hasApi()) return cb(new Error('plugins not available'));
  var done = false, handle = null, timer = null;
  function finish(err, value) {
    if (done) return;
    done = true;
    if (timer) clearTimeout(timer);
    if (handle && typeof handle.unsubscribe === 'function') { try { handle.unsubscribe(); } catch (e) { /* ignore */ } }
    cb(err, value);
  }
  timer = setTimeout(function () { finish(new Error('weather plugin timed out')); }, PLUGIN_TIMEOUT_MS);
  try {
    handle = Pebble.subscribeToSource({
      category: 'weather',
      item: 'location',
      plugin: providerUuid || undefined,
      properties: PROPS,
      onData: function (env) {
        var inst = env && env.instances && env.instances[0];
        if (!inst || !inst.properties) return finish(new Error('no weather location in the Pebble app'));
        var p = inst.properties;
        var temp = numberOf(p.temperature);
        if (temp === null) return finish(new Error('weather plugin has no temperature'));
        finish(null, {
          tempC: temp, tempHighC: numberOf(p.high), tempLowC: numberOf(p.low),
          uv: numberOf(p.uv_index), rainPct: numberOf(p.precipitation),
          code: textOf(p.condition_code).trim().toLowerCase()
        });
      },
      onError: function (err) { finish(new Error('weather plugin: ' + ((err && err.code) || 'error'))); }
    });
    if (done && handle && typeof handle.unsubscribe === 'function') { try { handle.unsubscribe(); } catch (e) { /* ignore */ } }
  } catch (e) {
    finish(e);
  }
}

function overlay(snap, p) {
  var out = {};
  Object.keys(snap).forEach(function (k) { out[k] = snap[k]; });
  out.tempC = p.tempC;
  if (p.tempHighC !== null) out.tempHighC = p.tempHighC;
  if (p.tempLowC !== null) out.tempLowC = p.tempLowC;
  if (p.uv !== null) { out.uvIndexMax = p.uv; out.uvIndexCurrent = p.uv; }
  if (p.rainPct !== null) out.rainChancePct = Math.max(0, Math.min(100, p.rainPct));
  var mapped = CODE_MAP[p.code];
  if (mapped) out.condition = mapped.cond;
  return out;
}

// 'plugin' = whichever weather plugin the user chose in the Pebble app; 'plugin:<uuid>' = that one.
function sourceSetting() { return String(getSetting('CONFIG_WEATHER_SOURCE', 'plugin')); }
function useWeatherPlugin() {
  return sourceSetting().indexOf('plugin') === 0 && pluginManager.hasApi();
}
function providerUuid() {
  var s = sourceSetting();
  return s.indexOf('plugin:') === 0 ? s.slice(7) : '';
}

// Same signature as weather.getDailyCloudGrid(lat, lon, times, now, cb(err, cloudGrid, extras)).
// The Open-Meteo call always runs: it is the real forecast source for the sky cloud
// samples (aligned to `times`) and the eclipse/sky code, and it supplies the fields the
// plugin lacks (wind, humidity, pressure, cloud cover, forecast arrays). The plugin only
// overrides the headline values; any plugin failure leaves the Open-Meteo result as-is.
function getCloudGridAndSnapshot(lat, lon, times, now, cb) {
  if (!useWeatherPlugin()) return weather.getDailyCloudGrid(lat, lon, times, now, cb);
  fetchPluginWeather(providerUuid(), function (perr, p) {
    weather.getDailyCloudGrid(lat, lon, times, now, function (err, grid, extras) {
      if (perr) {
        console.log('eclipse-watch: weather plugin not used (' + perr.message + '), using Open-Meteo');
        return cb(err, grid, extras);
      }
      console.log('eclipse-watch: weather from the Pebble weather plugin' + (err ? ' (Open-Meteo extras unavailable)' : ''));
      // A plugin success makes the weather block valid even if Open-Meteo failed (the sky
      // cloud grid is then null and the encoder falls back to zeros for it).
      cb(null, grid, overlay(extras || {}, p));
    });
  });
}

module.exports = { getDailyCloudGrid: getCloudGridAndSnapshot, fetchPluginWeather: fetchPluginWeather, useWeatherPlugin: useWeatherPlugin };
