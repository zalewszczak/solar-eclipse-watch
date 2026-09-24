// ---- preset slot rows -------------------------------------------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 fourth slice).

var esc = require('./config-template').esc;
var presetSchedule = require('./config-preset-schedule');

function presetSlotHtml(current, n) {
  var name = current['presetSlot' + n + 'Name'] || ('Preset ' + n);
  var json = current['presetSlot' + n + 'Json'] || '';
  var image = current['presetSlot' + n + 'Image'] || '';
  var schedule = current['presetSlot' + n + 'Schedule'] || '';
  return (
'    <div class="preset-slot-row">' +
'      <button type="button" class="preset-apply-btn" id="presetApplyBtn' + n + '" onclick="applyPresetSlot(' + n + ')" ' + (json ? '' : 'disabled') + '>' + esc(name) + '</button>' +
'      <input type="text" class="preset-name-input" id="presetNameInput' + n + '" style="display:none;" onblur="commitRenamePresetSlot(' + n + ')" onkeydown="if (event.key === \'Enter\') this.blur();">' +
'      <button type="button" class="preset-icon-btn" onclick="savePresetSlot(' + n + ')" title="Save current design here">&#128190;</button>' +
'      <button type="button" class="preset-icon-btn" onclick="startRenamePresetSlot(' + n + ')" title="Rename">&#9998;</button>' +
presetSchedule.scheduleButtonHtml(n, !!json, schedule) +
'      <button type="button" class="preset-icon-btn" onclick="deletePresetSlot(' + n + ')" title="Delete" id="presetDeleteBtn' + n + '" ' + (json ? '' : 'disabled') + '>&#128465;</button>' +
'    </div>' +
'    <input type="hidden" id="presetSlot' + n + 'Name" value="' + esc(name) + '">' +
'    <input type="hidden" id="presetSlot' + n + 'Json" value="' + esc(json) + '">' +
'    <input type="hidden" id="presetSlot' + n + 'Image" value="' + esc(image) + '">' +
presetSchedule.scheduleHiddenHtml(n, schedule)
  );
}

module.exports = {
  presetSlotHtml: presetSlotHtml
};
