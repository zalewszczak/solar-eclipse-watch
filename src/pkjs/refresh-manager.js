// ---- refresh manager --------------------------------------------------
//
// Moved to refresh-manager.js (refresh manager extraction). Owns: the
// smart-refresh skip/resend cache, location + reverse-geocode lookup,
// the optional ISS/air-quality/aurora fetches, the main refresh cycle
// (refreshAndSend), and its two timers (the periodic interval and the
// battery-saver hourly check). Behavior is unchanged; only the module
// boundary is new.
//
// Public API: refreshAndSend, scheduleRefresh, startBatterySaverHourlyCheck,
// setBatterySaverPhase -- the last one exists because lastBatterySaverPhase
// is written from index.js's own 'appmessage' handler (the watch reports
// its phase there) but read only here, so index.js needs a setter rather
// than reaching into this module's private state directly.

var astro = require('./astro');
var weather = require('./weather/weather');
var geocode = require('./geocode');
var iss = require('./iss');
var servicelog = require('./servicelog');
var settings = require('./settings/settings');
var getSetting = settings.getSetting;
var setSetting = settings.setSetting;
var messageEncoder = require('./comms/message-encoder');
var sendInvalid = messageEncoder.sendInvalid;
var sendEclipseData = messageEncoder.sendEclipseData;
var sendNoEclipseToday = messageEncoder.sendNoEclipseToday;

var refreshTimer = null;
// Battery saver: last BATTERY_SAVER_PHASE the watch has reported (0=awake,
// 1=sleep, 2=deep sleep -- see shake_anim_mode's neighboring comment block
// in eclipse_data.h/pebble-eclipse-watch.c for the full state machine on
// the watch side). While non-zero, the watch itself has said nobody's
// shaken it in 2h+, so the periodic refresh below holds off except right
// at the top of the hour (see scheduleRefresh()/startBatterySaverHourlyCheck()) --
// "stop requesting updates from the phone till full hour" only makes sense
// here, phone-side, since the periodic refresh is a PKJS setInterval with
// no watch-side equivalent to turn off.
var lastBatterySaverPhase = 0;
// Guards startBatterySaverHourlyCheck()'s once-a-minute check against
// firing more than once inside the same top-of-hour minute -- a plain
// "is it :00 right now" test alone would refresh every time that timer
// callback happens to land during minute 0, not just the first.
var lastHourlyRefreshHourKey = null;
var batterySaverHourlyTimer = null;
// Guards against a slow, older refresh's response arriving AFTER a
// newer one and overwriting it with stale data -- e.g. the periodic
// background timer firing (using the phone's real GPS location) right
// as the user saves a manual-coordinates override in settings: without
// this, if that older in-flight request's chain of network calls
// happens to finish later than the new forced one, it would clobber
// the correct new location/weather with the old location's data. Each
// refreshAndSend() call claims the next generation number; only the
// call that's still the current generation when its data is finally
// ready is allowed to actually send it.
var s_refreshGeneration = 0;

// Parses an HTML5 datetime-local value ("YYYY-MM-DDTHH:mm", always in
// this exact format per spec) into a local Date, by hand rather than
// via Date.parse/new Date(string) -- PKJS's JS engine's string-parsing
// support is not guaranteed to match a desktop browser's.
function parseDateTimeLocal(str) {
  if (!str) return null;
  var m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(str);
  if (!m) return null;
  return new Date(
    parseInt(m[1], 10), parseInt(m[2], 10) - 1, parseInt(m[3], 10),
    parseInt(m[4], 10), parseInt(m[5], 10), 0, 0
  );
}

// "Now" for the purposes of the eclipse calculation -- normally the
// real current time, but overridable from settings so you can preview
// the watchface against a known eclipse date without waiting for one.
// This only affects what PKJS calculates and sends; it doesn't touch
// the watch's own clock, so set that to match by hand if you want the
// on-screen countdown to line up too.
function getEffectiveNow() {
  var testMode = getSetting('CONFIG_TEST_MODE', 'false') === 'true';
  if (testMode) {
    var d = parseDateTimeLocal(getSetting('CONFIG_TEST_DATETIME', ''));
    if (d) {
      console.log('eclipse-watch: TEST MODE active, using ' + d.toString());
      return d;
    }
    console.log('eclipse-watch: test mode on but no valid test date set, using real time');
  }
  return new Date();
}

// ---- location naming (cached reverse geocode) -----------------------------

// Only re-geocode when the location has moved more than ~5km (0.05
// degrees is a rough approximation, fine at the scale this matters
// for), so a stationary watch doesn't hit Nominatim on every refresh
// -- keeps us well within their usage policy and avoids pointless
// network calls for a name that isn't going to have changed.
var GEOCODE_MOVE_THRESHOLD_DEG = 0.05;

function getLocationName(lat, lon, cb) {
  var cachedLat = parseFloat(getSetting('CACHE_GEOCODE_LAT', ''));
  var cachedLon = parseFloat(getSetting('CACHE_GEOCODE_LON', ''));
  var cachedName = getSetting('CACHE_GEOCODE_NAME', '');

  var moved = isNaN(cachedLat) || isNaN(cachedLon) ||
    Math.abs(cachedLat - lat) > GEOCODE_MOVE_THRESHOLD_DEG ||
    Math.abs(cachedLon - lon) > GEOCODE_MOVE_THRESHOLD_DEG;

  if (!moved && cachedName) {
    cb(cachedName);
    return;
  }

  geocode.reverseGeocode(lat, lon, function (err, name) {
    servicelog.recordAttempt('geocode', err);
    if (err) {
      console.log('eclipse-watch: reverse geocode failed - ' + err.message);
      cb(cachedName || ''); // fall back to a stale name rather than nothing
      return;
    }
    setSetting('CACHE_GEOCODE_LAT', String(lat));
    setSetting('CACHE_GEOCODE_LON', String(lon));
    setSetting('CACHE_GEOCODE_NAME', name);
    cb(name);
  });
}

function getLocation(cb) {
  var autoLoc = getSetting('CONFIG_AUTO_LOC', 'true') !== 'false';

  if (!autoLoc) {
    var lat = parseFloat(getSetting('CONFIG_LAT', ''));
    var lon = parseFloat(getSetting('CONFIG_LON', ''));
    if (!isNaN(lat) && !isNaN(lon)) {
      cb(null, lat, lon, null); // no altitude in manual-coordinates mode
      return;
    }
    console.log('eclipse-watch: manual location selected but not set yet, falling back to GPS');
  }

  if (!navigator.geolocation) {
    cb(new Error('no geolocation available'));
    return;
  }

  navigator.geolocation.getCurrentPosition(
    function (pos) {
      // altitude is meters above the WGS84 ellipsoid when the device
      // can supply it (GPS fix with altitude support) -- null on many
      // phones/emulators, in which case we just skip the horizon-dip
      // correction rather than guessing.
      cb(null, pos.coords.latitude, pos.coords.longitude, pos.coords.altitude);
    },
    function (err) {
      var reason = 'unknown (' + err.code + ')';
      if (err.code === 1) reason = 'permission denied -- check the location permission for this app on your phone';
      else if (err.code === 2) reason = 'position unavailable -- no GPS/network fix';
      else if (err.code === 3) reason = 'timed out waiting for a GPS fix';
      cb(new Error('geolocation failed: ' + reason));
    },
    { timeout: 15000, maximumAge: 600000, enableHighAccuracy: false }
  );
}

// Standard horizon-dip approximation: dip (arcminutes) ~= 1.76 *
// sqrt(height in meters). A higher vantage point genuinely sees a
// lower horizon, so the Sun/Moon should appear to rise a little
// earlier and set a little later -- modeled here simply by adding
// this to their computed altitude before mapping to a screen
// position, which is equivalent for our purposes.
function horizonDipDeg(altitudeMeters) {
  if (!altitudeMeters || altitudeMeters <= 0) return 0;
  return (1.76 * Math.sqrt(altitudeMeters)) / 60;
}

// ---- smart refresh (skip re-fetching if nothing's likely changed) --------

function haversineKm(lat1, lon1, lat2, lon2) {
  var R = 6371;
  var dLat = (lat2 - lat1) * Math.PI / 180;
  var dLon = (lon2 - lon1) * Math.PI / 180;
  var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
          Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
          Math.sin(dLon / 2) * Math.sin(dLon / 2);
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

function markRefreshDone(lat, lon) {
  setSetting('CACHE_LAST_FETCH_TS', String(Date.now()));
  setSetting('CACHE_LAST_FETCH_LAT', String(lat));
  setSetting('CACHE_LAST_FETCH_LON', String(lon));
}

// True if we fetched recently enough (within the user's refresh
// interval) AND haven't moved more than ~10km since -- in which case
// there's nothing meaningful to gain from hitting the network again.
// A location change bypasses this regardless of how recently we
// fetched, since that's exactly the case where stale data would
// actually be wrong rather than just slightly dated. Works the same
// way whether lat/lon came from GPS or the manual-coordinates setting
// -- both flow through getLocation() into refreshAndSend() as plain
// numbers before either shouldSkipRefresh() or markRefreshDone() ever
// see them, so there's nothing location-source-specific for this
// distance check to get wrong or needs to special-case.
function shouldSkipRefresh(lat, lon) {
  var lastTs = parseInt(getSetting('CACHE_LAST_FETCH_TS', '0'), 10);
  var lastLat = parseFloat(getSetting('CACHE_LAST_FETCH_LAT', ''));
  var lastLon = parseFloat(getSetting('CACHE_LAST_FETCH_LON', ''));
  if (!lastTs || isNaN(lastLat) || isNaN(lastLon)) return false;

  var mins = parseInt(getSetting('CONFIG_UPDATE_MINS', '20'), 10);
  if (isNaN(mins) || mins < 5) mins = 20;
  if (Date.now() - lastTs >= mins * 60000) return false;

  return haversineKm(lat, lon, lastLat, lastLon) < 10;
}

// Called instead of a real refetch when refreshAndSend() was given
// resendOnSkip=true AND shouldSkipRefresh() says there's nothing new
// worth fetching -- but "nothing new to fetch" and "the watch already
// has this" are NOT the same thing: the watch's own persisted data
// can go missing independently of anything PKJS tracks (most commonly
// right after a watch app update changes its data struct's layout,
// which invalidates whatever was saved under the old one -- see
// EclipseData's own persistence comment in eclipse_data.h). When that
// happens the watch shows "No data yet, waiting for phone" and starts
// asking for a resend via REQUEST_UPDATE (see request_retry_callback()
// in pebble-eclipse-watch.c) -- and before this existed, that request
// landed right back on shouldSkipRefresh() and got silently dropped,
// since from PKJS's side nothing had changed since the last
// successful fetch. Resending the last full computed result (cheap --
// no network involved) instead of nothing at all is what actually
// answers that request.
//
// resendOnSkip is deliberately NOT the default, and this function is
// deliberately not called unconditionally on every skip -- it used to
// be, which was itself a real bug: a settings save also goes through
// refreshAndSend(), and its own skip (nothing location/weather-wise
// needs refetching) would resend this cache anyway -- but the cache
// only updates on a genuine full refresh, not the cosmetic-only push
// a save already sends immediately (see sendFlatDict()'s own
// LAST_FULL_COMPUTED_DICT comment), so it'd be resending whatever was
// cached from BEFORE the save. From the watch's side that looked like
// "I just changed a setting, and a few seconds later it reverted" --
// every one of the 7 full-refresh chunks arriving right on the heels
// of the save's own 2 cosmetic ones, carrying the old values. Only
// the REQUEST_UPDATE handler passes resendOnSkip=true now, since
// that's the one caller actually asking "does the watch have
// anything at all," not "did anything change."
//
// Returns true if there was something to resend, false if the cache
// was empty/corrupt (in which case the caller should fall through to
// a real fetch instead).
//
// The cached dict's own settings fields are stale by construction --
// it's only ever (re)written on a genuine full refresh (see
// sendFlatDict()'s own LAST_FULL_COMPUTED_DICT comment), so any
// settings save that happened since then (the common case: this
// function's whole reason to exist is answering a REQUEST_UPDATE,
// which the watch sends on every relaunch, including a completely
// ordinary one long after the day's one real weather refresh already
// ran) is invisible to it. Sending the cached dict as-is would still
// be exactly the bug the comment above already describes fixing for
// every OTHER caller ("I just changed a setting, and a bit later it
// reverted") -- just via this one remaining path instead: settings
// save correctly reaches the watch immediately (sendFlatDict({}) in
// the webviewclosed handler) and it displays right, then the watch
// happens to relaunch (locked/unlocked, app-switched away and back,
// a plain reboot) before the next real weather refresh, its
// REQUEST_UPDATE lands here, shouldSkipRefresh() correctly says
// there's nothing weather-wise to refetch, and the stale cached
// settings from before the save go right back out over the top of
// the correct ones the watch already had. sendFlatDict(dict) re-
// derives every settings field fresh into the cached dict before it
// goes out (mutating it in place; harmless to the astronomy/weather
// fields already in it, which sendFlatDict never touches) rather than
// sending that dict unmodified -- and re-caches the corrected result
// too, since sendFlatDict() re-checks for C1_TIME itself, so the
// cache self-heals rather than staying stale until the next real fetch.
function resendLastFullData() {
  try {
    var raw = localStorage.getItem('LAST_FULL_COMPUTED_DICT');
    if (!raw) return false;
    var dict = JSON.parse(raw);
    if (!dict || typeof dict !== 'object') return false;
    sendFlatDict(dict);
    return true;
  } catch (e) {
    return false;
  }
}

// Only fetches ISS elements when the user has explicitly opted in
// (per the brief -- this needs a live external data source, unlike
// the planets, so it's opt-in rather than always-on). Fails open: a
// fetch error just means no ISS this cycle, not a hard refresh
// failure -- everything else in the payload is still good. Fires
// either when the sky-view "Show ISS" toggle is on, or when the
// separate "Next ISS pass" corner content (id 83, text-only, no sky
// dot) is picked anywhere -- same "only fetch if actually shown
// somewhere" reasoning as fetchAirQualityIfEnabled below, since both
// draw from the exact same Celestrak fetch + astro.js computation.
function fetchIssIfEnabled(lat, lon, cb) {
  var nextPassInUse = AQI_SLOT_CONTENT_KEYS.some(function (key) { return getSetting(key, '0') === '83'; });
  if (getSetting('CONFIG_SHOW_ISS', 'false') !== 'true' && !nextPassInUse) {
    return cb(null, 0);
  }
  iss.getIssPosition(lat, lon, function (err, pos) {
    var cls = servicelog.recordAttempt('iss', err);
    if (err) {
      console.log('eclipse-watch: ISS fetch failed - ' + err.message);
      return cb(null, cls.code);
    }
    cb(pos, 0);
  });
}

// Same "only fetch if actually shown somewhere" gate as fetchIssIfEnabled
// above -- there's no dedicated CONFIG_SHOW_AQI checkbox, so this checks
// the 12 corner/edge slots directly for content id 36 ("Air quality").
var AQI_SLOT_CONTENT_KEYS = ['CONFIG_CORNER_TL', 'CONFIG_CORNER_TR',
  'CONFIG_CORNER_BL', 'CONFIG_CORNER_BR',
  'CONFIG_UPPER_MIDDLE_LINE1_CONTENT', 'CONFIG_UPPER_MIDDLE_LINE2_CONTENT',
  'CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT', 'CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT',
  'CONFIG_MIDDLE_LEFT_LINE1_CONTENT', 'CONFIG_MIDDLE_LEFT_LINE2_CONTENT',
  'CONFIG_MIDDLE_RIGHT_LINE1_CONTENT', 'CONFIG_MIDDLE_RIGHT_LINE2_CONTENT'];
function fetchAirQualityIfEnabled(lat, lon, cb) {
  var inUse = AQI_SLOT_CONTENT_KEYS.some(function (key) { return getSetting(key, '0') === '36'; });
  if (!inUse) return cb({ aqiUs: null, aqiEu: null });
  weather.fetchAirQuality(lat, lon, function (err, aqi) {
    servicelog.recordAttempt('airquality', err);
    if (err) {
      console.log('eclipse-watch: air quality fetch failed - ' + err.message);
      return cb({ aqiUs: null, aqiEu: null });
    }
    cb(aqi);
  });
}

// Only fetches the Kp index when the user has opted in via the
// Astronomy section's "Show auroras" checkbox -- same "opt-in, needs a
// live external source" reasoning fetchIssIfEnabled above already
// uses. Fails open, same as every other optional fetch here: an error
// just means no aurora reading this cycle (score 0), not a hard
// refresh failure.
function fetchAuroraIfEnabled(lat, lon, cb) {
  if (getSetting('CONFIG_AURORA_ENABLED', 'false') !== 'true') return cb({ kp: null, visibilityPct: 0 }, 0);
  weather.fetchAuroraKp(function (err, kp) {
    var cls = servicelog.recordAttempt('aurora', err || (typeof kp !== 'number' ? new Error('no data') : null));
    if (err || typeof kp !== 'number') {
      console.log('eclipse-watch: aurora Kp fetch failed - ' + (err ? err.message : 'no data'));
      // null (not 0) -- distinguishes "fetch failed, don't touch the
      // watch's last reading" from a genuine 0% visibility estimate,
      // same as every other network-sourced field in extraWeatherFieldsDict.
      return cb({ kp: null, visibilityPct: null }, cls.code);
    }
    var geomagLat = Math.abs(astro.geomagneticLatitudeDeg(lat, lon));
    var score = astro.auroraVisibilityScore(kp, geomagLat);
    cb({ kp: kp, visibilityPct: score }, 0);
  });
}

// ---- main refresh cycle --------------------------------------------------

// resendOnSkip: only true from the REQUEST_UPDATE handler in index.js's
// 'appmessage' listener -- see its own comment for why that specific
// trigger needs the skip path to still push something to the watch. Every other caller
// (a settings save, the periodic timer, PKJS's own startup) defaults
// to false, so a skip there really does mean "send nothing" the way
// it always did -- see resendLastFullData()'s own comment for why
// resending unconditionally on every skip was itself a bug: a settings
// save already pushes its own fresh cosmetic-only chunk immediately
// (see sendFlatDict()), and if THIS call then skipped-with-resend, it
// would fire moments later with whatever full data was cached from
// BEFORE the save -- silently overwriting the save's own fresh
// settings with stale ones, which is exactly what it looked like from
// the watch's side: "I just changed something, and a few seconds
// later it reverted."
function refreshAndSend(force, resendOnSkip) {
  console.log('eclipse-watch: refresh starting' + (force ? ' (forced)' : ''));
  s_refreshGeneration++;
  var myGeneration = s_refreshGeneration;
  // True once a newer refreshAndSend() call has started since this one
  // did -- checked before every point below that would send data to
  // the watch, so a slow older request can never clobber a newer one's
  // result (see the s_refreshGeneration comment above).
  function isStale() { return myGeneration !== s_refreshGeneration; }

  getLocation(function (err, lat, lon, altitudeMeters) {
    if (isStale()) { console.log('eclipse-watch: refresh superseded, discarding (location step)'); return; }
    if (err) {
      console.log('eclipse-watch: LOCATION FAILED - ' + err.message);
      sendInvalid(1);
      return;
    }
    console.log('eclipse-watch: location = ' + lat + ', ' + lon +
      (altitudeMeters ? (', altitude ' + Math.round(altitudeMeters) + 'm') : ''));

    if (!force && shouldSkipRefresh(lat, lon)) {
      if (!resendOnSkip) {
        console.log('eclipse-watch: fetched recently and location unchanged (<10km) - nothing to do');
        return;
      }
      if (resendLastFullData()) {
        console.log('eclipse-watch: fetched recently and location unchanged (<10km) - resent last known data to the watch instead of refetching');
        return;
      }
      console.log('eclipse-watch: fetched recently and location unchanged (<10km), but nothing cached to resend - fetching after all');
    }

    var now = getEffectiveNow();
    var dayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0, 0);
    var dip = horizonDipDeg(altitudeMeters);

    if (getSetting('CONFIG_TEST_MODE', 'false') === 'true') {
      console.log('eclipse-watch: *** TEST MODE IS ON *** using simulated time ' + now.toString() +
        ' instead of the real current time -- if the sky looks stuck/wrong, turn this off in settings ' +
        '(and remember it only overrides what the PHONE calculates; the watch\'s own clock is separate).');
    }

    var result, sky, stars, moonPhase, riseSet, meteorShower;
    try {
      result = astro.findEclipse(dayStart, lat, lon);
      sky = astro.computeDaySkySamples(dayStart, lat, lon, dip);
      stars = astro.computeVisibleStars(now, lat, lon);
      moonPhase = astro.computeMoonPhase(now);
      riseSet = {
        sun: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.sunAlt; }),
        moon: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.moonAlt; }),
        mercury: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.mercuryAlt; }),
        venus: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.venusAlt; }),
        mars: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.marsAlt; }),
        jupiter: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.jupiterAlt; }),
        saturn: astro.findRiseSet(dayStart, lat, lon, function (g) { return g.saturnAlt; })
      };
      // Tomorrow's sunrise too -- once today's sunset has passed, the
      // watch has nothing left to count down to otherwise (today's
      // sun_rise is already in the past), and used to just show "--:--".
      var nextDayStart = new Date(dayStart.getTime() + 86400000);
      var sunRiseTomorrow = astro.findRiseSet(nextDayStart, lat, lon, function (g) { return g.sunAlt; }).rise;
      meteorShower = astro.activeMeteorShower(now);
    } catch (e) {
      console.log('eclipse-watch: CALC FAILED - ' + e.message);
      if (!isStale()) sendInvalid(2);
      return;
    }
    console.log('eclipse-watch: ' + (result.hasEclipse ? ('eclipse found, type=' + result.type) : 'no eclipse today at this location') +
      '; moon ' + moonPhase.illuminatedPct + '% lit, ' + (moonPhase.waxing ? 'waxing' : 'waning') +
      (meteorShower ? ('; ' + meteorShower.name + ' intensity ' + meteorShower.intensity) : ''));

    // The headline "Clouds X% Vis Y%" stat covers the eclipse window
    // if there is one, otherwise just the next few hours -- separate
    // from (and optionally blended across more sources than) the
    // full-day background grid below.
    var headlineFrom = result.hasEclipse ? result.c1 : now;
    var headlineTo = result.hasEclipse ? result.c4 : new Date(now.getTime() + 3 * 3600000);
    var owmKey = getSetting('CONFIG_OWM_KEY', '');

    getLocationName(lat, lon, function (locationName) {
      weather.getDailyCloudGrid(lat, lon, sky.times, now, function (gridErr, cloudGrid, extras) {
        var weatherCls = servicelog.recordAttempt('weather', gridErr);
        if (gridErr) console.log('eclipse-watch: sky cloud grid fetch failed - ' + gridErr.message);

        // Open-Meteo's own sunrise/sunset accounts for standard
        // atmospheric refraction properly and is more accurate than
        // our reduced-term ephemeris -- use it to refine the
        // eclipse's sunset field when we have it and it's actually
        // today's (not tomorrow's, if the API's day boundary landed
        // differently than ours).
        if (result.hasEclipse && extras.sunset &&
            extras.sunset.getFullYear() === dayStart.getFullYear() &&
            extras.sunset.getMonth() === dayStart.getMonth() &&
            extras.sunset.getDate() === dayStart.getDate()) {
          result.sunset = extras.sunset;
        }

        weather.getEclipseWeather(lat, lon, owmKey || null, headlineFrom, headlineTo, function (w) {
          var headlineCloud = (w.cloudCoverPct === 255) ? 0 : w.cloudCoverPct;
          var headlineSources = w.sourceCount;

          fetchIssIfEnabled(lat, lon, function (issPos, issErrorCode) {
            fetchAirQualityIfEnabled(lat, lon, function (aqi) {
              fetchAuroraIfEnabled(lat, lon, function (aurora, auroraErrorCode) {
                if (isStale()) { console.log('eclipse-watch: refresh superseded, discarding (final step)'); return; }
                var extraWeather = {
                  windDirDeg: extras.windDirDeg, dewPointC: extras.dewPointC,
                  pressureHpa: extras.pressureHpa, pressureTrend: extras.pressureTrend,
                  aqiUs: aqi.aqiUs, aqiEu: aqi.aqiEu, altitudeMeters: altitudeMeters,
                  auroraKpX10: (typeof aurora.kp === 'number') ? Math.round(aurora.kp * 10) : null,
                  auroraVisibilityPct: (typeof aurora.visibilityPct === 'number') ? aurora.visibilityPct : null,
                  auroraErrorCode: auroraErrorCode,
                  forecastTempC: extras.forecastTempC, forecastCondition: extras.forecastCondition
                };
                if (result.hasEclipse) {
                  sendEclipseData(result, sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, extras.condition, extras.tempC, meteorShower, extras.cloudAltitudePct, extras.tempHighC, extras.tempLowC, issPos, extras.uvIndexMax, extras.uvIndexCurrent, extras.rainChancePct, extras.humidityPct, extras.windSpeedKmh, extras.currentCloudPct, sunRiseTomorrow, extraWeather, stars, !gridErr, weatherCls.code, issErrorCode);
                } else {
                  sendNoEclipseToday(sky, cloudGrid, headlineCloud, headlineSources, locationName, moonPhase, riseSet, extras.condition, extras.tempC, meteorShower, extras.cloudAltitudePct, extras.tempHighC, extras.tempLowC, issPos, extras.uvIndexMax, extras.uvIndexCurrent, extras.rainChancePct, extras.humidityPct, extras.windSpeedKmh, extras.currentCloudPct, sunRiseTomorrow, extraWeather, stars, !gridErr, weatherCls.code, issErrorCode);
                }
                markRefreshDone(lat, lon);
              });
            });
          });
        }, function (source, srcErr) { servicelog.recordAttempt(source, srcErr); });
      });
    });
  });
}

function scheduleRefresh() {
  if (refreshTimer) clearInterval(refreshTimer);
  var mins = parseInt(getSetting('CONFIG_UPDATE_MINS', '20'), 10);
  if (isNaN(mins) || mins < 5) mins = 20;
  refreshTimer = setInterval(function () {
    // Battery saver: the watch has told us (BATTERY_SAVER_PHASE) it's
    // gone 2h+ without a shake -- hold off on this normal cadence
    // entirely and let startBatterySaverHourlyCheck() below handle
    // refreshing, at most once per full hour, instead.
    if (lastBatterySaverPhase !== 0) return;
    refreshAndSend(false, false);
  }, mins * 60000);
}

// Battery saver's "till full hour" cadence while the watch reports
// sleep/deep sleep -- runs alongside scheduleRefresh()'s own timer
// (which self-skips above while resting) rather than replacing it, so
// a normal cadence resumes immediately, with nothing further to
// re-arm, the moment the watch reports being awake again. A plain
// once-a-minute check for "is it :00 right now" rather than trying to
// compute and re-schedule a timer landing exactly on the next hour
// boundary -- simpler, and the watch's own once-a-minute tick while
// resting already has this exact same shape (see tick_handler()'s
// deep_sleep_skip in pebble-eclipse-watch.c).
function startBatterySaverHourlyCheck() {
  if (batterySaverHourlyTimer) clearInterval(batterySaverHourlyTimer);
  batterySaverHourlyTimer = setInterval(function () {
    if (lastBatterySaverPhase === 0) return;
    var now = new Date();
    if (now.getMinutes() !== 0) return;
    var hourKey = now.getFullYear() + '-' + now.getMonth() + '-' + now.getDate() + '-' + now.getHours();
    if (hourKey === lastHourlyRefreshHourKey) return; // already refreshed this hour
    lastHourlyRefreshHourKey = hourKey;
    refreshAndSend(false, false);
  }, 60000);
}

function setBatterySaverPhase(phase) {
  lastBatterySaverPhase = phase;
}

module.exports = {
  refreshAndSend: refreshAndSend,
  scheduleRefresh: scheduleRefresh,
  startBatterySaverHourlyCheck: startBatterySaverHourlyCheck,
  setBatterySaverPhase: setBatterySaverPhase,
  getLocation: getLocation
};
