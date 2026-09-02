/**
 * servicelog.js -- shared error classification plus a small persisted
 * "last 10 attempts" log per external service (weather, aurora, ISS,
 * air quality, OpenWeatherMap, geocode). Two things read this:
 *
 *  - index.js, to decide what (if any) ERR CODE to send the watch for
 *    a given refresh (see WEATHER_ERROR_CODE/AURORA_ERROR_CODE/
 *    ISS_ERROR_CODE in sendEclipseData()/sendNoEclipseToday()/
 *    extraWeatherFieldsDict()/issFieldsDict()).
 *  - the settings page's Update section, for the status lights and
 *    "last 10 results" popup -- see buildConfigHtml()'s serviceLogsJson
 *    and config-page.js's renderServiceLights()/openServiceLog().
 *
 * Runs entirely in the PKJS sandbox; the settings page is a separate
 * webview with its own JS context and can't reach this module or its
 * localStorage directly, so whatever it needs gets baked into the
 * `current` object passed to buildConfigHtml() as a JSON snapshot --
 * same pattern index.js already uses for LAST_COMPUTED_DICT.
 */

var LOG_MAX_ENTRIES = 10;

// A handful of failure modes that never got an HTTP response at all,
// kept out of the way of real status codes (100-599) so a log entry's
// `code` can always be shown as a single short token -- either one of
// these, or the literal HTTP status.
var ERR_NETWORK = 1; // no response -- offline, DNS failure, connection refused
var ERR_TIMEOUT = 2; // request sent but never completed in time
var ERR_PARSE = 3;   // got a response, couldn't parse it as JSON
var ERR_DATA = 4;    // parsed fine, but the shape/contents weren't usable

/**
 * Turns a fetch callback's Error (or null, for success) into a short
 * numeric code plus a human-readable label. HTTP statuses pass through
 * as-is (matches what "ERR 404" etc. means to anyone who's used a REST
 * API) since every status code in real use (100-599) fits in a uint8;
 * the rare non-standard status above 255 clamps down to 255 rather
 * than wrapping into a misleading small number.
 *
 * @param {Error|null} err
 * @returns {{code: number, label: string}}
 */
function classifyError(err) {
  if (!err) return { code: 0, label: 'OK' };
  var msg = err.message || String(err);
  var m = /^HTTP (\d+)$/.exec(msg);
  if (m) {
    var status = parseInt(m[1], 10);
    return { code: Math.min(status, 255), label: 'HTTP ' + status };
  }
  if (msg === 'network error') return { code: ERR_NETWORK, label: 'Network error' };
  if (msg === 'timeout') return { code: ERR_TIMEOUT, label: 'Timeout' };
  // JSON.parse's own SyntaxError, or this app's "unexpected ...
  // response shape" style messages, both mean "got bytes back, but
  // couldn't make sense of them" -- distinguish only cosmetically
  // (the log message itself already says which).
  if (err instanceof SyntaxError || /JSON/i.test(msg)) return { code: ERR_PARSE, label: 'Bad response: ' + msg };
  return { code: ERR_DATA, label: msg };
}

function logKey(service) { return 'SVCLOG_' + service; }

function loadLog(service) {
  try {
    var raw = localStorage.getItem(logKey(service));
    var parsed = raw ? JSON.parse(raw) : [];
    return Array.isArray(parsed) ? parsed : [];
  } catch (e) {
    return [];
  }
}

function saveLog(service, entries) {
  try { localStorage.setItem(logKey(service), JSON.stringify(entries)); } catch (e) { /* storage full/unavailable -- logging is best-effort */ }
}

/**
 * Records one fetch attempt (success or failure) for `service`,
 * trimming to the last LOG_MAX_ENTRIES. Returns the same
 * {code, label} classifyError() would, so callers can use it directly
 * without re-classifying.
 *
 * @param {string} service one of SERVICES below
 * @param {Error|null} err
 */
function recordAttempt(service, err) {
  var cls = classifyError(err);
  var entries = loadLog(service);
  entries.push({ t: Date.now(), ok: cls.code === 0, code: cls.code, label: cls.label });
  if (entries.length > LOG_MAX_ENTRIES) entries = entries.slice(entries.length - LOG_MAX_ENTRIES);
  saveLog(service, entries);
  return cls;
}

/**
 * gray = never attempted; green = most recent attempt succeeded;
 * yellow = most recent failed but at least one of the (up to 10)
 * logged attempts succeeded; red = every logged attempt failed.
 * "Last 10" here means "up to the last 10 ever logged" -- a brand new
 * install with only 2 attempts so far, both failed, already reads red
 * rather than waiting to accumulate a full 10 failures first.
 */
function serviceStatus(service) {
  var entries = loadLog(service);
  if (entries.length === 0) return 'gray';
  var last = entries[entries.length - 1];
  if (last.ok) return 'green';
  var allFailed = entries.every(function (e) { return !e.ok; });
  return allFailed ? 'red' : 'yellow';
}

var SERVICES = ['weather', 'openweathermap', 'airquality', 'aurora', 'iss', 'geocode'];
var SERVICE_LABELS = {
  weather: 'Weather',
  openweathermap: 'OpenWeatherMap',
  airquality: 'Air quality',
  aurora: 'Aurora',
  iss: 'ISS',
  geocode: 'Geocoding'
};

/**
 * Snapshot of every service's status + log, for baking into
 * buildConfigHtml()'s `current` object (see this file's own top
 * comment for why it has to be a snapshot rather than a live link).
 */
function snapshotAll() {
  var out = {};
  SERVICES.forEach(function (service) {
    out[service] = { status: serviceStatus(service), log: loadLog(service) };
  });
  return out;
}

module.exports = {
  classifyError: classifyError,
  recordAttempt: recordAttempt,
  loadLog: loadLog,
  serviceStatus: serviceStatus,
  snapshotAll: snapshotAll,
  SERVICES: SERVICES,
  SERVICE_LABELS: SERVICE_LABELS
};
