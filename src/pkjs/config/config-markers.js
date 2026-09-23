// ---- marker domain: hour/second index grids and the ring/text editors ---
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 fifth slice).

var configTemplate = require('./config-template');
var esc = configTemplate.esc;
var modeButtonGroupHtml = configTemplate.modeButtonGroupHtml;
var colorRoleButtonGroupHtml = configTemplate.colorRoleButtonGroupHtml;
var configFonts = require('./config-fonts');
var fontOptionsHtml = configFonts.fontOptionsHtml;
var fontLookupEntry = configFonts.fontLookupEntry;
var fontFlag = configFonts.fontFlag;

function markBtnGridHtml(kind, maskStr) {
  var mask = parseInt(maskStr, 10);
  if (isNaN(mask)) mask = 0;
  var html = '<div class="mark-btn-grid">';
  for (var i = 0; i < 12; i++) {
    var label = kind === 'hour' ? (i === 0 ? 12 : i) : (i * 5);
    var active = (mask & (1 << i)) !== 0;
    html += '<button type="button" class="mark-btn' + (active ? ' active' : '') +
      '" id="markBtn-' + kind + '-' + i + '" onclick="toggleMarkBtn(\'' + kind + '\',' + i + ')">' + label + '</button>';
  }
  return html + '</div>';
}

// The 16 fields that actually get sent to the watch for the custom
// marker system -- kept as hidden inputs (same pattern as the corner
// slots' hidden color inputs) since they're edited inside the two
// popups, not directly on the page. Defaults approximate the "Big"
// procedural preset so the ring is visible rather than invisible
// (thickness 0) the first time someone picks "Custom".
// Border sliders are 0-100% "reach" values -- see marker_reach_px() in
// marker_layer.c for the exact mapping. On a 200x228 screen that's a
// px range of [100,114]: 0% is the largest circle guaranteed to stay
// fully on-screen (min(w,h)/2), 100% is the screen-fitted rectangle's
// own far edge (max(w,h)/2). Deliberately narrow -- the range only
// widens on a screen with a more extreme aspect ratio -- because it's
// derived directly from "never let a marker end up off the screen".
var MARKER_BORDER_MIN = 0;
var MARKER_BORDER_MAX = 100;

function customMarkerHiddenInputsHtml(current) {
  var d = {
    customHourStyle: '0', customHourThickness: '3', customHourInnerThickness: '3',
    customHourInnerEcc: '0', customHourOuterEcc: '0', customHourInnerBorder: '20', customHourOuterBorder: '100',
    customHourTranslucent: 'false', customHourColor: '0',
    customSecStyle: '0', customSecThickness: '1', customSecInnerThickness: '1',
    customSecInnerEcc: '0', customSecOuterEcc: '0', customSecInnerBorder: '70', customSecOuterBorder: '100',
    customSecTranslucent: 'false', customSecColor: '0',
    markerTextHourMask: '4095', markerTextSecMask: '4095'
  };
  var html = '';
  for (var key in d) {
    html += '<input type="hidden" id="' + key + '" value="' + esc(current[key] || d[key]) + '">';
  }
  return html;
}

// The hour/second custom-marker popup -- kind is 'hour' or 'sec', used
// as an id suffix throughout (cmStyle-hour, cmStyle-sec, ...) so one
// generator serves both. thicknessMax is 20 for hour, 10 for second
// (see MarkerRingConfig in marker_layer.h). All fields here are drafted
// in the popup and only committed to the real customHour*/customSec*
// hidden inputs when OK is pressed -- same "don't touch the real
// settings until Save" pattern as the corner slot editor.
function customMarkerModalHtml(kind, title, thicknessMax) {
  var p = kind === 'hour' ? 'cmHour' : 'cmSec';
  var styleChangeFn = kind === 'hour' ? 'cmHourStyleChange' : 'cmSecStyleChange';
  return (
'<div class="modal-overlay" id="customMarkerModal-' + kind + '" onclick="if (event.target === this) closeCustomMarkerEditor(\'' + kind + '\');">' +
'  <div class="modal-box">' +
'    <div class="modal-title">' + esc(title) + '</div>' +
'    <div class="modal-scroll-body">' +

'    <label>Shape</label>' +
      modeButtonGroupHtml(p + 'StyleGroup', p + 'Style', [
        { value: 'none', label: 'None' },
        { value: '0', label: 'Dot' },
        { value: '1', label: 'Line' },
        { value: '2', label: 'Square' },
        { value: '4', label: 'Tapered' }
      ], '0', styleChangeFn) +
'    <div class="help" id="' + p + 'NoneHelp" style="display:none;">Turns this ring off. Choose another shape to restore it with its previous settings.</div>' +

'    <div id="' + p + 'GeometryWrap">' +

'    <div class="slider-row">' +
'      <label for="' + p + 'Thickness">Thickness <span class="val" id="' + p + 'ThicknessVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Thickness" min="1" max="' + thicknessMax + '" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help" id="' + p + 'ThicknessHelp">Sets the thickness of each marker between its inner and outer edges.</div>' +

'    <div class="slider-row" id="' + p + 'InnerThicknessRow" style="display:none;">' +
'      <label for="' + p + 'InnerThickness">Inner thickness <span class="val" id="' + p + 'InnerThicknessVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerThickness\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'InnerThickness" min="1" max="' + thicknessMax + '" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerThickness\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help" id="' + p + 'InnerThicknessHelp" style="display:none;">For Tapered markers, sets the width at the inner edge. Use different values to make the marker wider at one end.</div>' +

'    <div class="slider-row">' +
'      <label for="' + p + 'InnerEcc">Inner eccentricity <span class="val" id="' + p + 'InnerEccVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerEcc\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'InnerEcc" min="0" max="100" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerEcc\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'OuterEcc">Outer eccentricity <span class="val" id="' + p + 'OuterEccVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterEcc\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'OuterEcc" min="0" max="100" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterEcc\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">Controls how the markers follow the watch face: 0 is circular and 100 follows the screen edges.</div>' +

'    <div class="slider-row">' +
'      <label for="' + p + 'InnerBorder">Inner border <span class="val" id="' + p + 'InnerBorderVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerBorder\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'InnerBorder" min="' + MARKER_BORDER_MIN + '" max="' + MARKER_BORDER_MAX + '" step="1" oninput="onCustomMarkerBorderInput(\'' + kind + '\', true)">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerBorder\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'OuterBorder">Outer border <span class="val" id="' + p + 'OuterBorderVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterBorder\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'OuterBorder" min="' + MARKER_BORDER_MIN + '" max="' + MARKER_BORDER_MAX + '" step="1" oninput="onCustomMarkerBorderInput(\'' + kind + '\', false)">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'OuterBorder\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">Sets the inner and outer positions of the marker ring. The outer position cannot move inside the inner position.</div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'Translucent">' +
'      <label for="' + p + 'Translucent" style="margin:0;">Semi-transparent</label>' +
'    </div>' +
'    <div class="help">Makes the markers appear about 50% transparent so the background shows through.</div>' +

'    <label style="margin-top:12px;">Color</label>' +
      colorRoleButtonGroupHtml(p + 'ColorGroup', p + 'Color', '0', false) +
'    <div class="help">Sets the color of this marker ring independently from the other ring.</div>' +

'    <label style="margin-top:12px;">Presets</label>' +
'    <div class="preset-btn-row">' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'minimal\')">Minimal</button>' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'braun\')">Small</button>' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'swiss\')">Big</button>' +
'    </div>' +
'    <button type="button" class="marker-edit-btn" style="margin-top:8px;" onclick="copyMarkerConfig(\'' + kind + '\')">Copy from ' + (kind === 'hour' ? 'seconds' : 'hour') + ' indices</button>' +

'    </div>' +

'    </div>' +
'    <div class="modal-footer">' +
'    <button type="button" onclick="saveCustomMarkerEditor(\'' + kind + '\')" style="width:100%; box-sizing:border-box; padding:14px; font-size:16px; font-weight:600; color:#fff; background:#ff9200; border:none; border-radius:8px; margin-top:14px;">OK</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeCustomMarkerEditor(\'' + kind + '\')">Cancel</button>' +
'    </div>' +
'  </div>' +
'</div>'
  );
}

// The "Edit numerals" popup -- numerals shown on the hour or second
// custom-index ring (never both). Everything here commits live (same
// as the hour/second button grids always have) rather than draft-then-
// Save, since there's no risk of an inconsistent in-between state the
// way there is with the ring geometry popups.
function textMarkerModalHtml(current) {
  var markerTextFontId = parseInt(current.markerTextFont || '0', 10);
  var markerTextRomanOk = fontFlag(fontLookupEntry(markerTextFontId).romanOk);
  return (
'<div class="modal-overlay" id="textMarkerModal" onclick="if (event.target === this) closeTextMarkerEditor();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Edit numerals</div>' +
'    <div class="modal-scroll-body">' +

'    <label>Numbers</label>' +
      modeButtonGroupHtml('markerTextTargetGroup', 'markerTextTarget', [
        { value: '0', label: 'Off' },
        { value: '1', label: 'On hours' },
        { value: '2', label: 'Every 5s' }
      ], current.markerTextTarget || '0', 'selectMarkerTextTarget') +
'    <div class="help">Choose whether to show numbers on the hour ring, every 5-second mark, or neither.</div>' +

'    <div id="markerTextOptions" style="' + (current.markerTextTarget && current.markerTextTarget !== '0' ? '' : 'display:none;') + '">' +
'      <label for="markerTextFont" style="margin-top:10px;">Font</label>' +
'      <select id="markerTextFont" onchange="onMarkerTextFontChange()" style="display:none;">' + fontOptionsHtml(parseInt(current.markerTextFont || '0', 10), false) + '</select>' +
'      <button type="button" class="font-picker-btn font-picker-trigger" id="markerTextFontTrigger" onclick="openFontPicker(\'markerTextFont\')">' +
'        <span class="font-picker-preview" id="markerTextFontTriggerPreview"></span>' +
'        <span class="font-picker-name" id="markerTextFontTriggerName"></span>' +
'      </button>' +

'      <div class="checkbox-row" style="margin-top:12px;">' +
'        <input type="checkbox" id="markerTextRoman" onchange="refreshAllFontTriggerLabels()" ' + (current.markerTextRoman === 'true' && markerTextRomanOk ? 'checked' : '') + ' ' + (markerTextRomanOk ? '' : 'disabled') + '>' +
'        <label for="markerTextRoman" style="margin:0;">Roman numerals</label>' +
'      </div>' +
'      <div class="help" id="markerTextRomanHelp">' + (markerTextRomanOk ? 'Shows I, II, III... instead of 1, 2, 3....' : 'This font does not support Roman numerals correctly.') + '</div>' +

'      <div class="slider-row">' +
'        <label for="markerTextOffset">Offset from index <span class="val" id="markerTextOffsetVal">' + esc(current.markerTextOffset || '0') + 'px</span></label>' +
'        <div class="slider-with-buttons">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'markerTextOffset\', -1)">&minus;</button>' +
'        <input type="range" id="markerTextOffset" min="-50" max="50" step="1" value="' + esc(current.markerTextOffset || '0') + '" oninput="document.getElementById(\'markerTextOffsetVal\').textContent = this.value + \'px\';">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'markerTextOffset\', 1)">+</button>' +
'        </div>' +
'      </div>' +
'      <div class="help">Moves the numbers outward or inward relative to the marker ring.</div>' +

'      <div id="markerTextHourGrid" style="' + (current.markerTextTarget === '1' ? '' : 'display:none;') + '">' +
'        <label style="margin-top:10px;">Which hours get a number</label>' +
          markBtnGridHtml('hour', current.markerTextHourMask !== undefined ? current.markerTextHourMask : '4095') +
'      </div>' +
'      <div id="markerTextSecGrid" style="' + (current.markerTextTarget === '2' ? '' : 'display:none;') + '">' +
'        <label style="margin-top:10px;">Which 5-second marks get a number</label>' +
          markBtnGridHtml('sec', current.markerTextSecMask !== undefined ? current.markerTextSecMask : '4095') +
'      </div>' +
'    </div>' +

'    </div>' +
'    <div class="modal-footer">' +
'    <button type="button" class="modal-cancel-btn" onclick="closeTextMarkerEditor()" style="margin-top:14px;">Close</button>' +
'    </div>' +
'  </div>' +
'</div>'
  );
}

module.exports = {
  markBtnGridHtml: markBtnGridHtml,
  MARKER_BORDER_MIN: MARKER_BORDER_MIN,
  MARKER_BORDER_MAX: MARKER_BORDER_MAX,
  customMarkerHiddenInputsHtml: customMarkerHiddenInputsHtml,
  customMarkerModalHtml: customMarkerModalHtml,
  textMarkerModalHtml: textMarkerModalHtml
};
