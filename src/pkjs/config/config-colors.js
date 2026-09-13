// ---- color role dropdown -----------------------------------------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 fifth slice).

function schemeColorOptionsHtml(selected) {
  var sel = selected || '0';
  return (
'<option value="0"' + (sel === '0' ? ' selected' : '') + '>Main color</option>' +
'<option value="1"' + (sel === '1' ? ' selected' : '') + '>Accent color</option>' +
'<option value="2"' + (sel === '2' ? ' selected' : '') + '>Background color</option>'
  );
}

module.exports = {
  schemeColorOptionsHtml: schemeColorOptionsHtml
};
