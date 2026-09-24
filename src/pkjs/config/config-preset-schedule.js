// ---- Scheduled My styles: settings-page popup ---------------------------------
//
// The clock button on each My Style slot opens #scheduleModal. The schedule is
// kept per slot in a hidden input (presetSlot<n>Schedule) as JSON,
//   {"enabled":true,"days":127,"time":"08:00"}     (days: bit 0 = Sunday ... bit 6 = Saturday)
// and travels to PKJS as CONFIG_PRESET_<n>_SCHEDULE with everything else when
// the settings page is saved. What PKJS does with it: see scheduled-style.js.
//
// Exports the exact text spliced into the page, like config-presets.js. The
// client script is generated from readable source; lines are joined without
// newlines, so it must not contain // comments.

var esc = require('./config-template').esc;

// One slot's schedule as stored: is it switched on?
function scheduleIsOn(raw) {
  if (!raw) return false;
  try { return JSON.parse(raw).enabled === true; } catch (e) { return false; }
}

var css =
'  .preset-icon-btn:disabled { opacity: 0.45; }' +
'  .preset-icon-btn svg { display: block; margin: 0 auto; width: 20px; height: 20px; }' +
'  .preset-schedule-btn.scheduled { color: #800020; }' +
'  @media (prefers-color-scheme: dark) { .preset-schedule-btn.scheduled { color: #d4425f; } }' +
'  .sched-day-group { display: flex; width: 100%; margin-top: 6px; border-radius: 6px; overflow: hidden; border: 1px solid var(--border); box-sizing: border-box; }' +
'  .sched-day-btn { flex: 1; padding: 10px 0; font-size: 13px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: none; border-right: 1px solid var(--border); }' +
'  .sched-day-btn:last-child { border-right: none; }' +
'  .sched-day-btn.active { background: #ff9200; color: #fff; box-shadow: inset 0 2px 4px rgba(0,0,0,0.35); }' +
'  .sched-error { color: #e03131; font-size: 13px; text-align: center; margin-top: 8px; }';

// Clock icon: an SVG so the stroke can follow the button's text color (an
// emoji could not turn burgundy).
var CLOCK_SVG = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>';

function scheduleButtonHtml(n, hasPreset, scheduleRaw) {
  var on = hasPreset && scheduleIsOn(scheduleRaw);
  return '      <button type="button" class="preset-icon-btn preset-schedule-btn' + (on ? ' scheduled' : '') + '" id="presetScheduleBtn' + n + '" onclick="openPresetSchedule(' + n + ')" title="' + (on ? 'Schedule (on)' : 'Schedule') + '" ' + (hasPreset ? '' : 'disabled') + '>' + CLOCK_SVG + '</button>';
}

function scheduleHiddenHtml(n, scheduleRaw) {
  return '    <input type="hidden" id="presetSlot' + n + 'Schedule" value="' + esc(scheduleRaw || '') + '">';
}

var DAY_LABELS = ['S', 'M', 'T', 'W', 'T', 'F', 'S'];

var modalHtml =
'<div class="modal-overlay" id="scheduleModal" onclick="if (event.target === this) closeScheduleModal();">' +
'  <div class="modal-box">' +
'    <div class="modal-title" id="scheduleModalTitle">Schedule</div>' +
'    <div class="modal-scroll-body">' +
'      <div class="checkbox-row">' +
'        <input type="checkbox" id="scheduleEnabled" onchange="updateScheduleEnabledState()">' +
'        <label for="scheduleEnabled" style="margin:0;">Enable schedule</label>' +
'      </div>' +
'      <div id="scheduleOptions">' +
'        <label style="margin-top:12px;">Days</label>' +
'        <div class="sched-day-group" id="scheduleDaysGroup">' +
          DAY_LABELS.map(function (label, i) {
            return '<button type="button" class="sched-day-btn active" data-day="' + i + '" onclick="toggleScheduleDay(' + i + ')">' + label + '</button>';
          }).join('') +
'        </div>' +
'        <label style="margin-top:12px;" for="scheduleTime">Apply at</label>' +
'        <input type="time" id="scheduleTime" value="08:00">' +
'      </div>' +
'      <div class="sched-error" id="scheduleError" style="display:none;">Select at least one day.</div>' +
'      <div class="help" style="margin-top:12px;">On the selected days, this style is applied to your watch automatically at that time. Your phone must be connected then; if it is not, the style is applied once it reconnects. Takes effect when you save the settings.</div>' +
'      <div class="help" id="scheduleWarn" style="display:none;"></div>' +
'    </div>' +
'    <div class="modal-footer">' +
'      <button type="button" class="modal-confirm-btn" onclick="saveScheduleModal()">Done</button>' +
'      <button type="button" class="modal-cancel-btn" onclick="closeScheduleModal()">Cancel</button>' +
'    </div>' +
'  </div>' +
'</div>';

// knownIds: every element id scheduled-style.js can apply (used only for the
// developer note in the popup when a saved style contains something it cannot).
function clientJs(knownIds) {
  return (
'var SCHEDULE_KNOWN_IDS = ' + JSON.stringify(knownIds || []) + ';' +
"\n" +
'var scheduleEditSlot = 0;' +
'var scheduleEditDays = 127;' +
'var SCHEDULE_DAY_LABELS = ["S", "M", "T", "W", "T", "F", "S"];' +
'function pad2(v) { return (v < 10 ? "0" : "") + v; }' +
'function parseScheduleValue(raw) {' +
'  var o = { enabled: false, days: 127, time: "08:00" };' +
'  if (!raw) return o;' +
'  try {' +
'    var p = JSON.parse(raw);' +
'    if (p) {' +
'      o.enabled = p.enabled === true;' +
'      var d = parseInt(p.days, 10);' +
'      if (!isNaN(d)) o.days = d & 127;' +
'      var m = /^(\\d{1,2}):(\\d{2})$/.exec(String(p.time || ""));' +
'      if (m) o.time = pad2(parseInt(m[1], 10)) + ":" + m[2];' +
'    }' +
'  } catch (e) {}' +
'  return o;' +
'}' +
'function refreshScheduleButton(n) {' +
'  var btn = document.getElementById("presetScheduleBtn" + n);' +
'  var jsonEl = document.getElementById("presetSlot" + n + "Json");' +
'  var schedEl = document.getElementById("presetSlot" + n + "Schedule");' +
'  if (!btn || !jsonEl || !schedEl) return;' +
'  var has = !!jsonEl.value;' +
'  var on = has && parseScheduleValue(schedEl.value).enabled;' +
'  btn.disabled = !has;' +
'  btn.className = "preset-icon-btn preset-schedule-btn" + (on ? " scheduled" : "");' +
'  btn.title = on ? "Schedule (on)" : "Schedule";' +
'}' +
'function renderScheduleDays() {' +
'  var btns = document.querySelectorAll("#scheduleDaysGroup .sched-day-btn");' +
'  for (var i = 0; i < btns.length; i++) {' +
'    var bit = parseInt(btns[i].getAttribute("data-day"), 10);' +
'    btns[i].className = "sched-day-btn" + ((scheduleEditDays & (1 << bit)) ? " active" : "");' +
'  }' +
'}' +
'function updateScheduleEnabledState() {' +
'  var on = document.getElementById("scheduleEnabled").checked;' +
'  document.getElementById("scheduleOptions").className = on ? "" : "grayed-out";' +
'}' +
'function toggleScheduleDay(bit) {' +
'  scheduleEditDays ^= (1 << bit);' +
'  document.getElementById("scheduleError").style.display = "none";' +
'  renderScheduleDays();' +
'}' +
'function openPresetSchedule(n) {' +
'  var jsonEl = document.getElementById("presetSlot" + n + "Json");' +
'  var schedEl = document.getElementById("presetSlot" + n + "Schedule");' +
'  if (!jsonEl || !schedEl || !jsonEl.value) return;' +
'  scheduleEditSlot = n;' +
'  var s = parseScheduleValue(schedEl.value);' +
'  scheduleEditDays = s.days;' +
'  var name = document.getElementById("presetSlot" + n + "Name").value || ("Preset " + n);' +
'  document.getElementById("scheduleModalTitle").textContent = "Schedule: " + name;' +
'  document.getElementById("scheduleEnabled").checked = s.enabled;' +
'  document.getElementById("scheduleTime").value = s.time;' +
'  document.getElementById("scheduleError").style.display = "none";' +
'  var unknown = [];' +
'  try {' +
'    var preset = JSON.parse(jsonEl.value);' +
'    for (var id in preset) {' +
'      if (preset.hasOwnProperty(id) && SCHEDULE_KNOWN_IDS.indexOf(id) < 0) unknown.push(id);' +
'    }' +
'  } catch (e) {}' +
'  var warn = document.getElementById("scheduleWarn");' +
'  warn.textContent = unknown.length ? ("Developer note: " + unknown.length + " setting(s) in this style are not known to the scheduler and will be skipped: " + unknown.join(", ")) : "";' +
'  warn.style.display = unknown.length ? "" : "none";' +
'  renderScheduleDays();' +
'  updateScheduleEnabledState();' +
'  document.getElementById("scheduleModal").classList.add("open");' +
'}' +
'function closeScheduleModal() {' +
'  document.getElementById("scheduleModal").classList.remove("open");' +
'  scheduleEditSlot = 0;' +
'}' +
'function saveScheduleModal() {' +
'  var n = scheduleEditSlot;' +
'  var schedEl = document.getElementById("presetSlot" + n + "Schedule");' +
'  if (!n || !schedEl) { closeScheduleModal(); return; }' +
'  var enabled = document.getElementById("scheduleEnabled").checked;' +
'  var time = document.getElementById("scheduleTime").value || "08:00";' +
'  if (enabled && scheduleEditDays === 0) {' +
'    document.getElementById("scheduleError").style.display = "";' +
'    return;' +
'  }' +
'  schedEl.value = JSON.stringify({ enabled: enabled, days: scheduleEditDays === 0 ? 127 : scheduleEditDays, time: time });' +
'  refreshScheduleButton(n);' +
'  closeScheduleModal();' +
'}'
  );
}

module.exports = {
  css: css,
  modalHtml: modalHtml,
  clientJs: clientJs,
  scheduleButtonHtml: scheduleButtonHtml,
  scheduleHiddenHtml: scheduleHiddenHtml
};
