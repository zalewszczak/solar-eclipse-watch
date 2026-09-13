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

module.exports = {
  serviceStatusRowsHtml: serviceStatusRowsHtml,
  rawMessageLogButtonsHtml: rawMessageLogButtonsHtml
};
