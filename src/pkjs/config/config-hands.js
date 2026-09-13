// ---- hand domain: hidden inputs and the per-hand editor popup ----------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 fifth slice).

var esc = require('./config-template').esc;
var schemeColorOptionsHtml = require('./config-colors').schemeColorOptionsHtml;

// The 14 fields per hand (hour/min/sec -- 42 total) that get sent to the
// watch, kept as hidden inputs edited via the 3 popups below, same
// pattern as customMarkerHiddenInputsHtml() (config-markers.js).
// Defaults approximate the "Pointy" procedural style so hands are
// visible immediately, rather than defaulting to width/length 0.

function handHiddenInputsHtml(current) {
  var d = {
    handHourStyle: '1', handHourWidth: '12', handHourLength: '51', handHourBackOffset: '0',
    handHourMiddleOffset: '0', handHourSecondaryWidth: '6',
    handHourColor: '0', handHourOutlineEnabled: 'false', handHourOutlineColor: '0', handHourTranslucent: 'false',
    handHourShadowEnabled: 'false', handHourShadowDistance: '2',
    handHourHollow: 'false', handHourHollowThickness: '1',
    handMinStyle: '1', handMinWidth: '18', handMinLength: '78', handMinBackOffset: '0',
    handMinMiddleOffset: '0', handMinSecondaryWidth: '6',
    handMinColor: '0', handMinOutlineEnabled: 'false', handMinOutlineColor: '0', handMinTranslucent: 'false',
    handMinShadowEnabled: 'false', handMinShadowDistance: '2',
    handMinHollow: 'false', handMinHollowThickness: '1',
    handSecStyle: '0', handSecWidth: '2', handSecLength: '85', handSecBackOffset: '0',
    handSecMiddleOffset: '0', handSecSecondaryWidth: '6',
    handSecColor: '1', handSecOutlineEnabled: 'false', handSecOutlineColor: '0', handSecTranslucent: 'false',
    handSecShadowEnabled: 'false', handSecShadowDistance: '2',
    handSecHollow: 'false', handSecHollowThickness: '1'
  };
  var html = '';
  for (var key in d) {
    var val = current[key] !== undefined ? current[key] : d[key];
    html += '<input type="hidden" id="' + key + '" value="' + esc(val) + '">';
  }
  return html;
}

// The hour/minute/second custom-hand popup -- kind is 'hour', 'min', or
// 'sec'. Same "draft in the popup, commit on OK" pattern as
// customMarkerModalHtml().
// Copy-preset direction is fixed per hand (not "the other one" generically,
// per how this was asked for): hour offers to copy minute's settings,
// minute offers hour's, second offers minute's.
var HAND_COPY_SOURCE = { hour: 'min', min: 'hour', sec: 'min' };
var HAND_COPY_SOURCE_LABEL = { hour: 'minute', min: 'hour', sec: 'minute' };

function handEditorModalHtml(kind, title) {
  var p = 'he' + kind.charAt(0).toUpperCase() + kind.slice(1); // heHour / heMin / heSec
  return (
'<div class="modal-overlay" id="handEditorModal-' + kind + '" onclick="if (event.target === this) closeHandEditor(\'' + kind + '\');">' +
'  <div class="modal-box">' +
'    <div class="modal-title">' + esc(title) + '</div>' +
'    <img class="hand-editor-diagram" id="' + p + 'Diagram" src="" alt="">' +
'    <div class="modal-scroll-body">' +

'    <label>Shape</label>' +
'    <select id="' + p + 'Style" style="display:none;" onchange="onCustomHandStyleChange(\'' + kind + '\')">' +
'      <option value="0">Baton</option>' +
'      <option value="1">Galba</option>' +
'      <option value="2">Pencil</option>' +
'      <option value="3">Dauphine</option>' +
'      <option value="4">Sword</option>' +
'      <option value="5">Pomme</option>' +
'      <option value="6">Spade</option>' +
'      <option value="7">Arrow</option>' +
'      <option value="8">Leaf</option>' +
'      <option value="9">Syringe</option>' +
'      <option value="10">Serpentine</option>' +
'    </select>' +
'    <button type="button" class="font-picker-btn font-picker-trigger" id="' + p + 'StyleTrigger" onclick="openHandStyleIconPicker(\'' + p + '\', \'' + kind + '\')">' +
'      <span class="font-picker-preview hand-style-icon-preview"></span>' +
'      <span class="font-picker-name"></span>' +
'    </button>' +

'    <div class="slider-row">' +
'      <label for="' + p + 'Width">A. Width <span class="val" id="' + p + 'WidthVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Width\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Width" min="1" max="40" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Width\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'Length">B. Length <span class="val" id="' + p + 'LengthVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Length\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Length" min="10" max="100" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Length\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'BackOffset">C. Back offset <span class="val" id="' + p + 'BackOffsetVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'BackOffset\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'BackOffset" min="-40" max="40" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'BackOffset\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">Positive extends a tail behind the pivot; negative starts the hand short of center (a detached gap).</div>' +

'    <div class="slider-row" id="' + p + 'MiddleOffsetRow">' +
'      <label for="' + p + 'MiddleOffset">D. Middle offset <span class="val" id="' + p + 'MiddleOffsetVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'MiddleOffset\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'MiddleOffset" min="-40" max="80" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'MiddleOffset\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="slider-row" id="' + p + 'SecondaryWidthRow">' +
'      <label for="' + p + 'SecondaryWidth">E. Secondary width <span class="val" id="' + p + 'SecondaryWidthVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'SecondaryWidth\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'SecondaryWidth" min="1" max="40" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'SecondaryWidth\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help" id="' + p + 'MiddleSecondaryHelp">Middle offset: Dauphine\'s side points, Sword\'s mid-bulge position, Pomme\'s thick/thin joint, Spade\'s droplet point height, Arrow\'s tip-triangle height, Leaf\'s peak position, Syringe\'s needle corner position, Serpentine\'s curve diameter/direction. Secondary width (all except Leaf): Sword\'s mid-bulge width, Pomme\'s tail width, Spade\'s droplet diameter, Arrow\'s tip-triangle width, Syringe\'s needle-tip width, Serpentine\'s squiggle envelope.</div>' +

'    <label for="' + p + 'Color">Color</label>' +
'    <select id="' + p + 'Color" onchange="onHandSliderInput(\'' + kind + '\')">' + schemeColorOptionsHtml('0') + '<option value="3">None (don\'t fill)</option></select>' +
'    <div class="help">"None" skips the fill entirely -- combine with Outline below for a hollow look.</div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'Translucent" onchange="onHandSliderInput(\'' + kind + '\')">' +
'      <label for="' + p + 'Translucent" style="margin:0;">Semi-transparent</label>' +
'    </div>' +
'    <div class="help">Dithers the fill (and outline, if enabled) to ~50% so the sky shows through.</div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'Hollow" onchange="onHandHollowChange(\'' + kind + '\')">' +
'      <label for="' + p + 'Hollow" style="margin:0;">Hollow</label>' +
'    </div>' +
'    <div class="help">Draws a thick inline stroke of the shape\'s own outline instead of a solid fill -- within the shape\'s bounds, unlike Outline below which marks it from the outside.</div>' +
'    <div class="slider-row" id="' + p + 'HollowThicknessRow">' +
'      <label for="' + p + 'HollowThickness">F. Hollow thickness <span class="val" id="' + p + 'HollowThicknessVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'HollowThickness\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'HollowThickness" min="1" max="40" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'HollowThickness\', 1)">+</button>' +
'      </div>' +
'    </div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'OutlineEnabled" onchange="onHandSliderInput(\'' + kind + '\')">' +
'      <label for="' + p + 'OutlineEnabled" style="margin:0;">Outline</label>' +
'    </div>' +
'    <label for="' + p + 'OutlineColor">Outline color</label>' +
'    <select id="' + p + 'OutlineColor" onchange="onHandSliderInput(\'' + kind + '\')">' + schemeColorOptionsHtml('0') + '</select>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'ShadowEnabled" onchange="onHandSliderInput(\'' + kind + '\')">' +
'      <label for="' + p + 'ShadowEnabled" style="margin:0;">Shadow</label>' +
'    </div>' +
'    <div class="help">A drop shadow of the hand\'s own shape, offset a fixed distance in a fixed direction (not rotated with the hand). Solid or translucent is set once for every hand in the Style section.</div>' +
'    <div class="slider-row">' +
'      <label for="' + p + 'ShadowDistance">G. Shadow distance <span class="val" id="' + p + 'ShadowDistanceVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'ShadowDistance\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'ShadowDistance" min="1" max="5" step="1" oninput="onHandSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'ShadowDistance\', 1)">+</button>' +
'      </div>' +
'    </div>' +

'    <button type="button" class="marker-edit-btn" style="margin-top:8px;" onclick="copyHandConfig(\'' + kind + '\')">Copy ' + HAND_COPY_SOURCE_LABEL[kind] + ' hand settings</button>' +

'    </div>' +
'    <div class="modal-footer">' +
'    <button type="button" onclick="saveHandEditor(\'' + kind + '\')" style="width:100%; box-sizing:border-box; padding:14px; font-size:16px; font-weight:600; color:#fff; background:#ff9200; border:none; border-radius:8px; margin-top:14px;">OK</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeHandEditor(\'' + kind + '\')">Cancel</button>' +
'    </div>' +
'  </div>' +
'</div>'
  );
}

module.exports = {
  handHiddenInputsHtml: handHiddenInputsHtml,
  HAND_COPY_SOURCE: HAND_COPY_SOURCE,
  HAND_COPY_SOURCE_LABEL: HAND_COPY_SOURCE_LABEL,
  handEditorModalHtml: handEditorModalHtml
};
