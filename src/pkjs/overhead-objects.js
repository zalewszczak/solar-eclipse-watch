/**
 * overhead-objects.js -- the shake ("Planet Seek") view's dynamic list of
 * things currently overhead: the ISS (reusing iss.js's existing Celestrak
 * fetch) and/or nearby flights (flights.js's OpenSky fetch), combined into
 * one small, generic {az, alt, label} list and sent to the watch as a
 * single packed OVERHEAD_OBJECTS blob (see message-encoder.js's
 * overheadObjectsBytes() for the wire format).
 *
 * This is the "universal" version of what used to be ISS-only, on-demand
 * data: only fetched when the watch actually shakes and its cached list
 * has gone stale (see comms_maybe_request_flights() on the watch side),
 * not on the app's regular 5-60 minute refresh cycle like weather/aurora.
 * Both sources fail open independently -- one failing (or being turned
 * off) never blocks the other from still showing up.
 */

var iss = require('./iss');
var flights = require('./flights');
var servicelog = require('./servicelog');

// Must match MAX_OVERHEAD_OBJECTS in eclipse_data.h.
var MAX_OVERHEAD_OBJECTS = 6;

// The watch stores this in a fixed 6-byte field (5 chars + NUL) -- see
// eclipse_data.h's own OverheadObject comment -- so truncate here rather
// than let a long callsign silently get cut off wherever it happens to
// land in the byte-packing step.
var MAX_LABEL_CHARS = 5;

/**
 * @param {number} lat
 * @param {number} lon
 * @param {{showIss: boolean, showFlights: boolean, flightsRadiusKm: number, flightsApiKey: string}} opts
 * @param {function(Array<{az:number, alt:number, label:string}>)} cb  never errors -- a source
 *   that fails just contributes nothing, same as the rest of this app's optional data.
 */
function buildOverheadObjectList(lat, lon, opts, cb) {
  var objects = [];
  var pending = 0;
  var done = false;

  function finish() {
    if (done) return;
    pending--;
    if (pending > 0) return;
    done = true;
    cb(objects.slice(0, MAX_OVERHEAD_OBJECTS));
  }

  if (opts.showIss) {
    pending++;
    iss.getIssPosition(lat, lon, function (err, pos) {
      var cls = servicelog.recordAttempt('iss', err);
      if (!err && pos.alt > 0) {
        objects.push({ az: pos.az, alt: pos.alt, label: 'ISS' });
      } else if (err) {
        console.log('eclipse-watch: ISS fetch failed (overhead objects) - ' + err.message);
      }
      void cls;
      finish();
    });
  }

  if (opts.showFlights) {
    pending++;
    flights.getNearbyFlights(lat, lon, opts.flightsRadiusKm, opts.flightsApiKey, function (err, list) {
      servicelog.recordAttempt('flights', err);
      if (err) {
        console.log('eclipse-watch: flights fetch failed - ' + err.message);
      } else {
        list.forEach(function (look) {
          var label = (look.callsign || '').slice(0, MAX_LABEL_CHARS);
          objects.push({ az: look.az, alt: look.alt, label: label || 'Flight' });
        });
      }
      finish();
    });
  }

  if (pending === 0) {
    // Neither source enabled -- still respond (with an empty list) so the
    // watch's "loading" state clears rather than hanging until the next
    // shake's 3-minute staleness check retries.
    cb([]);
  }
}

module.exports = { buildOverheadObjectList: buildOverheadObjectList, MAX_OVERHEAD_OBJECTS: MAX_OVERHEAD_OBJECTS };
