// ---- service-status and raw-message debug rows ------------------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 fourth slice).

var esc = require('./config-template').esc;
var servicelog = require('../servicelog.js');

// One "Update section" status row (colored dot + service name + an
// info button when there's something worth showing) per external
// service servicelog.js tracks -- gray/never attempted, green/last
// attempt worked, yellow/red per serviceStatus()'s own comment. The
// info button (only shown for yellow/red, where there's an actual
// problem worth digging into) opens the shared #serviceLogModal via
// openServiceLog(), passing this service's own last-10-attempts log
// baked in as JSON at page-build time -- see serviceLogsJson below and
// this file's own top comment for why that has to be a snapshot
// rather than a live link into PKJS's servicelog.js.
function serviceStatusRowsHtml(current) {
  var logs = (current && current.serviceLogs) || {};
  return servicelog.SERVICES.map(function (service) {
    var label = servicelog.SERVICE_LABELS[service] || service;
    var status = (logs[service] && logs[service].status) || 'gray';
    var infoBtn = (status === 'yellow' || status === 'red')
      ? '<button type="button" class="service-info-btn" onclick="openServiceLog(\'' + service + '\', \'' + esc(label) + '\')" title="Last 10 results">i</button>'
      : '';
    return (
'    <div class="service-status-row">' +
'      <span class="service-dot service-dot-' + status + '"></span>' +
'      <span class="service-name">' + esc(label) + '</span>' +
      infoBtn +
'    </div>'
    );
  }).join('');
}

// One button per logged raw AppMessage chunk (see recordRawMessage()
// in index.js -- newest first here, though they're stored oldest-
// first), labeled "HH:MM:SS.mmm chunk X/Y" -- X/Y being that chunk's
// 1-based position and total count within whichever enqueueFlatDict()
// batch produced it (e.g. a full refresh's STATUS/ECLIPSE/WEATHER/
// ASTRONOMY/SKY_EFFECTS/FEATURES/SETTINGS chunks show as 1/6..6/6, a
// cosmetic-only settings push's smaller FEATURES/SETTINGS batch as
// 1/2 and 2/2). Clicking one loads that exact chunk's JSON into the
// "Raw data (editable)" textarea below via loadRawMessage().
function rawMessageLogButtonsHtml(current) {
  var entries = (current && current.rawMessageLog) || [];
  if (entries.length === 0) {
    return '<div class="help">Nothing sent yet this session -- send/save something first.</div>';
  }
  var buttons = entries.map(function (e, i) {
    var d = new Date(e.t);
    function pad(n, len) { var s = String(n); while (s.length < (len || 2)) s = '0' + s; return s; }
    var label = pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds()) + '.' + pad(d.getMilliseconds(), 3) +
      ' chunk ' + e.batchIndex + '/' + e.batchTotal;
    return { i: i, label: label };
  }).reverse(); // newest first
  return '<div class="raw-log-btn-grid" id="rawLogBtnGrid">' +
    buttons.map(function (b) {
      return '<button type="button" class="raw-log-btn" id="rawLogBtn' + b.i + '" onclick="loadRawMessage(' + b.i + ')">' + esc(b.label) + '</button>';
    }).join('') +
    '</div>';
}

// ---- fake weather panel -------------------------------------------
//
// A dedicated "spoof one weather push" tool, separate from the Full
// keyset window above: that one round-trips ALL ~148 current fields
// through a textarea (easy to fat-finger, hard to skim), this one is
// just the ~18 WEATHER-chunk fields (see message-schema.js's
// KEY_TYPE_MAP) as labeled sliders/dropdowns, for quickly previewing
// how a given condition/cloud/temp/etc combination actually draws
// (background clouds/precipitation, weather icons, feature slots)
// without waiting on a real fetch or hand-editing JSON. Its own Send
// button (sendFakeWeatherToWatch() in config-page.js) follows the
// exact same "close page, webviewclosed picks up a marker flag,
// enqueueFlatDict() as-is, nothing persisted" path as the Full keyset
// window's sendFullKeysetToWatch() -- see that function's own comment
// in config-page.js. Sending only the WEATHER-chunk keys (plus
// CLOUD_ALTITUDE_PCT, technically an ASTRONOMY-chunk field but grouped
// here since it's what positions the cloud band the background actually
// draws) is safe because the watch only touches keys actually PRESENT
// in a message (dict_find() per field -- see comms_decoder.c) -- so
// this can never accidentally blank out ECLIPSE_TYPE/eclipse times/etc,
// only ever the weather fields it explicitly sets.
//
// Sliders (rather than free-typed numbers) so a value can never
// accidentally leave the range the watch's own field width/rendering
// expects -- see each field's own min/max comment below, matched to
// message-encoder.js's own clamping (extraWeatherFieldsDict()) and
// comms_decoder.c's SIMPLE_FIELD_MAP types (F_U8/F_I16/F_U16).
//
// Seeded from whatever's already sitting in current.fullKeysetJson
// (the same live snapshot the Full keyset window itself starts from --
// see buildFullKeysetDict()'s own comment in message-encoder.js) so
// opening this panel starts from a real, current-looking weather
// reading to tweak, rather than blank/arbitrary defaults every time.
function fakeWeatherDefaults(current) {
  var seed = {};
  try {
    var parsed = JSON.parse(current && current.fullKeysetJson ? current.fullKeysetJson : '{}');
    if (parsed && typeof parsed === 'object') seed = parsed;
  } catch (e) {
    // Corrupt/missing snapshot -- fall through to the plain defaults below.
  }
  function num(key, fallback) {
    return (typeof seed[key] === 'number' && !isNaN(seed[key])) ? seed[key] : fallback;
  }
  return {
    condition: num('WEATHER_CONDITION', 0),
    cloudCover: num('CLOUD_COVER', 20),
    visScore: num('VIS_SCORE', 80),
    cloudAltitude: num('CLOUD_ALTITUDE_PCT', 50),
    tempC: num('WEATHER_TEMP_C', 15),
    tempHighC: num('WEATHER_TEMP_HIGH_C', 20),
    tempLowC: num('WEATHER_TEMP_LOW_C', 10),
    uvMaxX10: num('UV_INDEX_X10', 30),
    uvCurrentX10: num('UV_INDEX_CURRENT_X10', 20),
    rainChance: num('RAIN_CHANCE_PCT', 10),
    humidity: num('HUMIDITY_PCT', 50),
    windSpeed: num('WIND_SPEED_KMH', 10),
    windDir: num('WIND_DIR_DEG', 180),
    dewPoint: num('DEW_POINT_C', 8),
    pressure: num('PRESSURE_HPA', 1013),
    pressureTrend: num('PRESSURE_TREND', 0),
    aqiUs: num('AQI_US', 30),
    aqiEu: num('AQI_EU', 15)
  };
}

// One plain slider row: label, live value readout (formatted by
// `format`, an inline JS expression string evaluated against the
// slider's own `this.value` -- e.g. "this.value + \'%\'"), and an
// extra oninput expression appended for the handful of sliders that
// need to update something else too (the wind-direction compass
// letters). Deliberately no +/- step buttons here (unlike most of
// this page's other sliders -- see stepSlider() in config-page.js):
// this panel already has ~18 of these, and a debug tool for quickly
// sweeping through values benefits more from a wide drag range than
// from precise +/-1 nudges.
function wxSlider(id, label, min, max, value, format, extraOninput) {
  return (
'      <div class="slider-row">' +
'        <label for="' + id + '">' + esc(label) + ' <span class="val" id="' + id + 'Val">' + esc(String(value)) + '</span></label>' +
'        <input type="range" id="' + id + '" min="' + min + '" max="' + max + '" step="1" value="' + esc(String(value)) + '" ' +
          'oninput="document.getElementById(\'' + id + 'Val\').textContent = ' + format + ';' + (extraOninput || '') + '">' +
'      </div>'
  );
}

function fakeWeatherPanelHtml(current) {
  var d = fakeWeatherDefaults(current);
  return (
'    <div class="subsection">' +
'      <div class="field-label-row"><label>Fake weather</label><button type="button" class="help-btn" onclick="toggleHelp(\'help-fakeWeather\')">?</button></div>' +
'      <div class="help" id="help-fakeWeather" style="display:none;">Spoofs the current weather reading to preview how conditions draw (background effects, weather icons, feature slots), independent of the FAQ/keyset checks a real fetch goes through. \"Send fake weather now\" closes this page and sends only these fields to the watch immediately -- nothing here is saved, and no other data is affected.</div>' +

'      <label for="fakeWxCondition">Condition</label>' +
'      <select id="fakeWxCondition" style="width:100%;">' +
'        <option value="0"' + (d.condition === 0 ? ' selected' : '') + '>Clear / cloudy (by cloud cover below)</option>' +
'        <option value="1"' + (d.condition === 1 ? ' selected' : '') + '>Fog</option>' +
'        <option value="2"' + (d.condition === 2 ? ' selected' : '') + '>Rain / drizzle</option>' +
'        <option value="3"' + (d.condition === 3 ? ' selected' : '') + '>Snow</option>' +
'        <option value="4"' + (d.condition === 4 ? ' selected' : '') + '>Thunderstorm</option>' +
'      </select>' +
'      <div class="help">Fog/rain/snow/storm always draw their own icon and background effect regardless of cloud cover; leave on \"Clear / cloudy\" to let cloud cover below pick sunny/partly-cloudy/overcast instead.</div>' +

      wxSlider('fakeWxCloudCover', 'Cloud cover', 0, 100, d.cloudCover, "this.value + '%'") +
      wxSlider('fakeWxCloudAltitude', 'Cloud altitude', 0, 100, d.cloudAltitude, "this.value + '%'") +
      wxSlider('fakeWxVisScore', 'Visibility score', 0, 100, d.visScore, "this.value + '%'") +

      wxSlider('fakeWxTempC', 'Temperature', -40, 50, d.tempC, "this.value + '\\u00b0C'") +
      wxSlider('fakeWxTempHighC', 'High temperature', -40, 50, d.tempHighC, "this.value + '\\u00b0C'") +
      wxSlider('fakeWxTempLowC', 'Low temperature', -40, 50, d.tempLowC, "this.value + '\\u00b0C'") +
      wxSlider('fakeWxDewPoint', 'Dew point', -40, 50, d.dewPoint, "this.value + '\\u00b0C'") +

      wxSlider('fakeWxUvMax', "Today's max UV index", 0, 150, d.uvMaxX10, "(this.value / 10).toFixed(1)") +
      wxSlider('fakeWxUvCurrent', 'Current UV index', 0, 150, d.uvCurrentX10, "(this.value / 10).toFixed(1)") +
      wxSlider('fakeWxRainChance', 'Rain chance', 0, 100, d.rainChance, "this.value + '%'") +
      wxSlider('fakeWxHumidity', 'Humidity', 0, 100, d.humidity, "this.value + '%'") +

      wxSlider('fakeWxWindSpeed', 'Wind speed', 0, 200, d.windSpeed, "this.value + ' km/h'") +
      wxSlider('fakeWxWindDir', 'Wind direction', 0, 359, d.windDir,
        "this.value + '\\u00b0 (' + compassLabelForDeg(this.value) + ')'") +

      wxSlider('fakeWxPressure', 'Pressure', 870, 1085, d.pressure, "this.value + ' hPa'") +
'      <label for="fakeWxPressureTrend">Pressure trend</label>' +
'      <select id="fakeWxPressureTrend" style="width:100%;">' +
'        <option value="0"' + (d.pressureTrend === 0 ? ' selected' : '') + '>Flat</option>' +
'        <option value="1"' + (d.pressureTrend === 1 ? ' selected' : '') + '>Rising</option>' +
'        <option value="2"' + (d.pressureTrend === 2 ? ' selected' : '') + '>Falling</option>' +
'      </select>' +

      wxSlider('fakeWxAqiUs', 'AQI (US)', 0, 500, d.aqiUs, "this.value") +
      wxSlider('fakeWxAqiEu', 'AQI (EU)', 0, 150, d.aqiEu, "this.value") +

'      <button type="button" class="secondary-btn" style="width:100%; margin-top:10px;" onclick="sendFakeWeatherToWatch()">Send fake weather now</button>' +
'    </div>'
  );
}

module.exports = {
  serviceStatusRowsHtml: serviceStatusRowsHtml,
  rawMessageLogButtonsHtml: rawMessageLogButtonsHtml,
  fakeWeatherPanelHtml: fakeWeatherPanelHtml
};
