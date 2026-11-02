// ---- settings page: Pebble plugins (HTML + client script) -----------------------------
//
// The phone app's plugin catalogue is computed by PKJS (plugins/plugin-manager.js) and
// handed to the page as current.pluginCatalogue = { status, message, sources[] }, with
// current.pluginSlots = the 12 watch slots' assigned source keys.
//
// A data source the user picks (in the Features "Plugins" category, or as a
// "Plugin data" element in the custom feature builder) is assigned one of 12 watch
// text slots; the page owns that assignment (PLUGIN_SLOTS) and saves it as
// CONFIG_PLUGIN_SLOTS. Slots no longer referenced by any feature are freed on save.
//
// When plugins are not available (old Pebble app, or switched off) the weather source
// select and the plugin dropdown are disabled and the page says plugins are not enabled.

var esc = require('./config-template').esc;

function catalogueOf(current) {
  var c = current && current.pluginCatalogue;
  if (!c || typeof c !== 'object') {
    return { status: 'unsupported', sources: [], message: 'Plugins are not enabled: this version of the Pebble app does not support plugins.' };
  }
  return c;
}

// Weather section: data source select + plugin status.
function weatherSourceHtml(current) {
  var cat = catalogueOf(current);
  var ok = cat.status === 'ok';
  var stored = String(current.weatherSource || 'plugin');
  if (stored !== 'openmeteo' && !/^plugin(:\S+)?$/.test(stored)) stored = 'plugin';
  var shown = ok ? stored : 'openmeteo';
  var providers = (cat.weatherProviders || []).slice();
  var opts = '<option value="plugin"' + (shown === 'plugin' ? ' selected' : '') + '>Pebble app weather (default)</option>';
  var known = false;
  providers.forEach(function (p) {
    var v = 'plugin:' + p.uuid;
    if (v === shown) known = true;
    opts += '<option value="' + esc(v) + '"' + (v === shown ? ' selected' : '') + '>' + esc(p.name) + ' (plugin)</option>';
  });
  if (shown.indexOf('plugin:') === 0 && !known) opts += '<option value="' + esc(shown) + '" selected>Selected plugin (not installed)</option>';
  opts += '<option value="openmeteo"' + (shown === 'openmeteo' ? ' selected' : '') + '>Open-Meteo</option>';
  return '' +
'    <div class="field-label-row"><label for="weatherSource" style="margin-top:10px;">Weather data source</label><button type="button" class="help-btn" onclick="toggleHelp(\'help-weatherSource\')">?</button></div>' +
'    <select id="weatherSource" data-stored="' + esc(stored) + '"' + (ok ? '' : ' disabled') + '>' + opts + '</select>' +
'    <div class="help" id="help-weatherSource" style="display:none;">The Pebble app\'s weather (or the weather plugin you pick) supplies temperature, high/low, condition, UV and rain chance. Wind, humidity, pressure, cloud cover and the forecast still come from Open-Meteo. If the plugin cannot be read, Open-Meteo is used automatically.</div>' +
(ok ? '' : '    <div class="help" id="pluginNotice" style="margin-top:6px; color:#d9822b;">' + esc(cat.message || 'Plugins are not enabled.') + ' Weather uses Open-Meteo.</div>');
}

function pluginScript(current) {
  var cat = catalogueOf(current);
  var slots = [];
  for (var i = 0; i < 12; i++) slots.push(typeof (current.pluginSlots || [])[i] === 'string' ? current.pluginSlots[i] : '');
  return '' +
'var PLUGIN_INFO = ' + JSON.stringify({ status: cat.status, message: cat.message || '', sources: cat.sources || [] }) + ';\n' +
'var PLUGIN_SLOTS = ' + JSON.stringify(slots) + ';\n' +
'var PLUGIN_CONTENT_FIRST = 200, PLUGIN_SLOT_COUNT = 12;\n' +
'var LIVE_MODE = ' + (current.liveMode ? 'true' : 'false') + ';\n' +
'var PREFERENCES = ' + JSON.stringify(current.preferences || { available: false, units: null, complications: null }).replace(/</g, '\\u003c') + ';\n' +
'function pluginsAvailable() { return PLUGIN_INFO.status === "ok" && PLUGIN_INFO.sources.length > 0; }\n' +
'function pluginUnavailableText() {\n' +
'  if (PLUGIN_INFO.status !== "ok") return PLUGIN_INFO.message || "Plugins are not enabled.";\n' +
'  return PLUGIN_INFO.sources.length ? "No free plugin slot (12 at most): remove a plugin feature first." : "No plugin provides data right now.";\n' +
'}\n' +
// Keys: "<uuid>|<category>|<item>|<property>[#<instance>]"; an empty uuid (the user\'s preferred
// complications) means any plugin serving that category/item/property.
'function pluginKeyParts(key) {\n' +
'  var p = String(key || "").split("|");\n' +
'  if (p.length < 4) return null;\n' +
'  var prop = p.slice(3).join("|"), idx = 0, m = /^(.*)#([0-9]+)$/.exec(prop);\n' +
'  if (m) { prop = m[1]; idx = parseInt(m[2], 10); }\n' +
'  return { plugin: p[0], category: p[1], item: p[2], property: prop, index: idx };\n' +
'}\n' +
'function pluginSourceByKey(key) {\n' +
'  var kp = pluginKeyParts(key);\n' +
'  if (!kp) return null;\n' +
'  for (var i = 0; i < PLUGIN_INFO.sources.length; i++) {\n' +
'    var s = PLUGIN_INFO.sources[i];\n' +
'    if (s.category === kp.category && s.item === kp.item && s.property === kp.property && (!kp.plugin || s.plugin === kp.plugin)) return s;\n' +
'  }\n' +
'  return null;\n' +
'}\n' +
'function pluginIndexSuffix(key) {\n' +
'  var kp = pluginKeyParts(key);\n' +
'  return (kp && kp.index > 0) ? " #" + (kp.index + 1) : "";\n' +
'}\n' +
'function pluginHumanize(s) { return String(s || "").replace(/[_-]+/g, " ").replace(/^\\s*[a-z]/, function (c) { return c.toUpperCase(); }); }\n' +
'function pluginSlotOf(key) { return PLUGIN_SLOTS.indexOf(key); }\n' +
'function pluginSourceLabel(src) { return src.pluginName + " \\u2013 " + src.category + "/" + src.item + ": " + src.label; }\n' +
'function pluginIsTextSlot(slot) { return typeof PLUGIN_SLOTS[slot] === "string" && PLUGIN_SLOTS[slot].indexOf("text|") === 0; }\n' +
'function pluginTextOfSlot(slot) { return pluginIsTextSlot(slot) ? PLUGIN_SLOTS[slot].slice(5) : ""; }\n' +
'function pluginAllocTextSlot(text) {\n' +
'  var used = pluginSlotsInUse();\n' +
'  for (var i = 0; i < PLUGIN_SLOT_COUNT; i++) if (!PLUGIN_SLOTS[i]) { PLUGIN_SLOTS[i] = "text|" + text; return i; }\n' +
'  for (i = 0; i < PLUGIN_SLOT_COUNT; i++) if (!used[i]) { PLUGIN_SLOTS[i] = "text|" + text; return i; }\n' +
'  return -1;\n' +
'}\n' +
'function pluginSlotLabel(slot) {\n' +
'  if (pluginIsTextSlot(slot)) return pluginTextOfSlot(slot);\n' +
'  var key = PLUGIN_SLOTS[slot];\n' +
'  if (!key) return "Plugin " + (slot + 1);\n' +
'  var src = pluginSourceByKey(key);\n' +
'  if (src) return src.label + pluginIndexSuffix(key);\n' +
'  var kp = pluginKeyParts(key);\n' +
'  return (kp && !kp.plugin) ? pluginHumanize(kp.property) + pluginIndexSuffix(key) : "(unavailable)";\n' +
'}\n' +
// Slots referenced by saved features and by the slot editor's open draft.
'function pluginSlotsInUse() {\n' +
'  var used = {};\n' +
'  var els = document.querySelectorAll("input, select");\n' +
'  for (var i = 0; i < els.length; i++) {\n' +
'    var id = els[i].id || "";\n' +
'    if (id.indexOf("CustomPrims") !== -1) {\n' +
'      var items = composerParse(els[i].value);\n' +
'      for (var j = 0; j < items.length; j++) if (items[j].id === PRIM_PLUGIN_TEXT_ID) used[items[j].aux] = true;\n' +
'    } else if (/(Content|^corner(TL|TR|BL|BR))$/.test(id)) {\n' +
'      var n = parseInt(els[i].value, 10);\n' +
'      if (n >= PLUGIN_CONTENT_FIRST && n < PLUGIN_CONTENT_FIRST + PLUGIN_SLOT_COUNT) used[n - PLUGIN_CONTENT_FIRST] = true;\n' +
'    }\n' +
'  }\n' +
'  if (typeof COMPOSER !== "undefined" && COMPOSER.items) {\n' +
'    for (var k = 0; k < COMPOSER.items.length; k++) if (COMPOSER.items[k].id === PRIM_PLUGIN_TEXT_ID) used[COMPOSER.items[k].aux] = true;\n' +
'  }\n' +
'  var sel = document.getElementById("slotEditContent");\n' +
'  if (sel && CURRENT_SLOT_KEY) { var sv = parseInt(sel.value, 10); if (sv >= PLUGIN_CONTENT_FIRST && sv < PLUGIN_CONTENT_FIRST + PLUGIN_SLOT_COUNT) used[sv - PLUGIN_CONTENT_FIRST] = true; }\n' +
'  return used;\n' +
'}\n' +
'function pluginAllocSlot(key) {\n' +
'  var existing = pluginSlotOf(key);\n' +
'  if (existing >= 0) return existing;\n' +
'  var used = pluginSlotsInUse(), i;\n' +
'  for (i = 0; i < PLUGIN_SLOT_COUNT; i++) if (!PLUGIN_SLOTS[i]) { PLUGIN_SLOTS[i] = key; return i; }\n' +
'  for (i = 0; i < PLUGIN_SLOT_COUNT; i++) if (!used[i]) { PLUGIN_SLOTS[i] = key; return i; }\n' +
'  return -1;\n' +
'}\n' +
'function pluginGcSlots() {\n' +
'  var used = pluginSlotsInUse();\n' +
'  for (var i = 0; i < PLUGIN_SLOT_COUNT; i++) if (!used[i]) PLUGIN_SLOTS[i] = "";\n' +
'}\n' +
'// Two-tier picking: first a provider (plugin name + category), then one of its data points.\n' +
'var PLUGIN_PROV = {};\n' +
'function pluginProvKey(src) { return src.plugin; }\n' +
'function pluginProvLabel(src) { return src.pluginName + " – " + src.category; }\n' +
'function pluginProviders() {\n' +
'  var out = [], seen = {};\n' +
'  PLUGIN_INFO.sources.forEach(function (src) {\n' +
'    var k = pluginProvKey(src);\n' +
'    if (!seen[k]) { seen[k] = true; out.push({ key: k, label: pluginProvLabel(src) }); }\n' +
'  });\n' +
'  return out;\n' +
'}\n' +
'function pluginProviderOfSlot(slot) {\n' +
'  var key = PLUGIN_SLOTS[slot];\n' +
'  var src = key ? pluginSourceByKey(key) : null;\n' +
'  return src ? pluginProvKey(src) : "";\n' +
'}\n' +
'function pluginDataLabel(src) { return src.category + " / " + src.item + ": " + src.label; }\n' +
'// Provider select that sits right before dataSel (created on first use).\n' +
'function pluginProvSelect(dataSel) {\n' +
'  var id = dataSel.id + "Prov";\n' +
'  var prov = document.getElementById(id);\n' +
'  if (!prov) {\n' +
'    prov = document.createElement("select");\n' +
'    prov.id = id;\n' +
'    prov.style.cssText = dataSel.style.cssText || "";\n' +
'    if (dataSel.parentNode && dataSel.parentNode.className.indexOf("composer-row") >= 0) { prov.style.flex = "1 1 100%"; dataSel.style.flex = "1 1 100%"; }\n' +
'    else prov.style.marginBottom = "6px";\n' +
'    dataSel.parentNode.insertBefore(prov, dataSel);\n' +
'    prov.onchange = function () {\n' +
'      PLUGIN_PROV[dataSel.id] = prov.value;\n' +
'      if (dataSel.id === "slotEditContent") { pluginSlotEditProviderChanged(); return; }\n' +
'      pluginRefillData(dataSel);\n' +
'      if (dataSel.onchange) dataSel.onchange();\n' +
'    };\n' +
'  }\n' +
'  return prov;\n' +
'}\n' +
'function pluginFillProvider(dataSel, wantedKey) {\n' +
'  var prov = pluginProvSelect(dataSel), provs = pluginProviders();\n' +
'  var cur = wantedKey || PLUGIN_PROV[dataSel.id] || "";\n' +
'  var ok = provs.some(function (p) { return p.key === cur; });\n' +
'  if (!ok) cur = provs.length ? provs[0].key : "";\n' +
'  PLUGIN_PROV[dataSel.id] = cur;\n' +
'  prov.innerHTML = provs.map(function (p) {\n' +
'    return "<option value=\\"" + esc(p.key) + "\\"" + (p.key === cur ? " selected" : "") + ">" + esc(p.label) + "</option>";\n' +
'  }).join("");\n' +
'  prov.style.display = provs.length ? "" : "none";\n' +
'  prov.disabled = !provs.length;\n' +
'  return cur;\n' +
'}\n' +
'function pluginHideProvider(dataSel) {\n' +
'  var prov = document.getElementById(dataSel.id + "Prov");\n' +
'  if (prov) prov.style.display = "none";\n' +
'}\n' +
'// Composer values: "c<i>" = catalogue source i not yet in a slot, "<n>" = slot n.\n' +
'function pluginParamOptions(providerKey) {\n' +
'  var out = [], seen = {};\n' +
'  PLUGIN_INFO.sources.forEach(function (src, i) {\n' +
'    var slot = pluginSlotOf(src.key);\n' +
'    if (slot >= 0) seen[slot] = true;\n' +
'    if (providerKey && pluginProvKey(src) !== providerKey) return;\n' +
'    out.push({ value: slot >= 0 ? String(slot) : "c" + i, label: pluginDataLabel(src) });\n' +
'  });\n' +
'  for (var s = 0; s < PLUGIN_SLOT_COUNT; s++) {\n' +
'    var skey = PLUGIN_SLOTS[s];\n' +
'    if (!skey || seen[s] || skey.indexOf("text|") === 0) continue;\n' +
'    var ssrc = pluginSourceByKey(skey);\n' +
'    if (ssrc) { if (!providerKey || pluginProvKey(ssrc) === providerKey) out.push({ value: String(s), label: pluginDataLabel(ssrc) + pluginIndexSuffix(skey) }); }\n' +
'    else if (!providerKey) out.push({ value: String(s), label: "(unavailable) " + skey.split("|").slice(1).join("/") });\n' +
'  }\n' +
'  return out;\n' +
'}\n' +
'// Fills dataSel for the provider chosen in its provider select (composer pickers).\n' +
'function pluginRefillData(dataSel, wantedValue) {\n' +
'  var cur = PLUGIN_PROV[dataSel.id] || "";\n' +
'  var popts = pluginParamOptions(cur);\n' +
'  dataSel.innerHTML = popts.length ? popts.map(function (o) {\n' +
'    return "<option value=\\"" + o.value + "\\">" + esc(o.label) + "</option>";\n' +
'  }).join("") : "<option value=\\"\\">" + esc(pluginUnavailableText()) + "</option>";\n' +
'  if (wantedValue != null) dataSel.value = wantedValue;\n' +
'}\n' +
'function pluginFillComposerSelect(dataSel, wantedValue) {\n' +
'  var wantedKey = "";\n' +
'  if (wantedValue != null && /^[0-9]+$/.test(wantedValue)) wantedKey = pluginProviderOfSlot(parseInt(wantedValue, 10));\n' +
'  pluginFillProvider(dataSel, wantedKey);\n' +
'  pluginRefillData(dataSel, wantedValue);\n' +
'}\n' +
'function pluginAuxFromValue(value) {\n' +
'  if (value && value.charAt(0) === "c") {\n' +
'    var src = PLUGIN_INFO.sources[parseInt(value.slice(1), 10)];\n' +
'    return src ? pluginAllocSlot(src.key) : -1;\n' +
'  }\n' +
'  var n = parseInt(value, 10);\n' +
'  return isNaN(n) ? -1 : n;\n' +
'}\n' +
'// Slot editor, Plugins category: option value is the content id (200 + slot) or "plug:<i>".\n' +
'function pluginItemOptionsHtml(selectedContentId) {\n' +
'  var dataSel = document.getElementById("slotEditContent");\n' +
'  if (!pluginsAvailable()) {\n' +
'    if (dataSel) pluginHideProvider(dataSel);\n' +
'    return "<option value=\\"0\\">" + esc(PLUGIN_INFO.status !== "ok" ? "Plugins are not enabled" : "No plugin data available") + "</option>";\n' +
'  }\n' +
'  var sel = parseInt(selectedContentId, 10) - PLUGIN_CONTENT_FIRST;\n' +
'  var wantKey = (sel >= 0 && sel < PLUGIN_SLOT_COUNT) ? pluginProviderOfSlot(sel) : "";\n' +
'  var provKey = dataSel ? pluginFillProvider(dataSel, wantKey) : wantKey;\n' +
'  var html = "", seen = {};\n' +
'  PLUGIN_INFO.sources.forEach(function (src, i) {\n' +
'    var slot = pluginSlotOf(src.key);\n' +
'    if (slot >= 0) seen[slot] = true;\n' +
'    if (pluginProvKey(src) !== provKey) return;\n' +
'    var value = slot >= 0 ? String(PLUGIN_CONTENT_FIRST + slot) : "plug:" + i;\n' +
'    html += "<option value=\\"" + value + "\\"" + (String(selectedContentId) === value ? " selected" : "") + ">" + esc(pluginDataLabel(src)) + "</option>";\n' +
'  });\n' +
'  for (var xs = 0; xs < PLUGIN_SLOT_COUNT; xs++) {\n' +
'    var xkey = PLUGIN_SLOTS[xs];\n' +
'    if (!xkey || seen[xs] || xkey.indexOf("text|") === 0) continue;\n' +
'    var xsrc = pluginSourceByKey(xkey);\n' +
'    if (!xsrc || pluginProvKey(xsrc) !== provKey) continue;\n' +
'    var xval = String(PLUGIN_CONTENT_FIRST + xs);\n' +
'    html += "<option value=\\"" + xval + "\\"" + (String(selectedContentId) === xval ? " selected" : "") + ">" + esc(pluginDataLabel(xsrc) + pluginIndexSuffix(xkey)) + "</option>";\n' +
'  }\n' +
'  if (sel >= 0 && sel < PLUGIN_SLOT_COUNT && !pluginProviderOfSlot(sel)) html = "<option value=\\"" + selectedContentId + "\\" selected>(unavailable) " + esc(PLUGIN_SLOTS[sel] || "") + "</option>" + html;\n' +
'  return html;\n' +
'}\n' +
'// Slot editor: provider changed -> list that provider\'s data points.\n' +
'function pluginSlotEditProviderChanged() {\n' +
'  var dataSel = document.getElementById("slotEditContent");\n' +
'  dataSel.innerHTML = pluginItemOptionsHtml("");\n' +
'  onSlotEditContentChange();\n' +
'}\n' +
'function pluginResolveContentSelect(sel) {\n' +
'  var v = sel.value;\n' +
'  if (v.indexOf("plug:") !== 0) return;\n' +
'  var src = PLUGIN_INFO.sources[parseInt(v.slice(5), 10)];\n' +
'  var slot = src ? pluginAllocSlot(src.key) : -1;\n' +
'  if (slot < 0) { alert(pluginUnavailableText()); sel.innerHTML = "<option value=\\"0\\">None</option>"; return; }\n' +
'  sel.options[sel.selectedIndex].value = String(PLUGIN_CONTENT_FIRST + slot);\n' +
'}\n' +
'function pluginButtonText(contentId) {\n' +
'  var slot = parseInt(contentId, 10) - PLUGIN_CONTENT_FIRST;\n' +
'  return pluginSlotLabel(slot);\n' +
'}\n' +
'function slotEditContentDisabled(categoryId) {\n' +
'  return categoryId === "none" || (categoryId === "plugins" && !pluginsAvailable());\n' +
'}\n' +
'function slotEditCategoryHelpUpdate(categoryId) {\n' +
'  if (categoryId !== "plugins") pluginHideProvider(document.getElementById("slotEditContent"));\n' +
'  var help = document.getElementById("slotEditCategoryHelp");\n' +
'  if (categoryId === "plugins") {\n' +
'    help.textContent = pluginsAvailable() ? "Shows the latest value of a data source provided by a Pebble plugin, as plain text." : pluginUnavailableText();\n' +
'    help.style.display = "";\n' +
'  } else if (categoryId === "timezone") {\n' +
'    help.textContent = "Shows the time-zone option used for the selected location. Daylight saving rules may vary by location.";\n' +
'    help.style.display = "";\n' +
'  } else {\n' +
'    help.style.display = "none";\n' +
'  }\n' +
'}\n' +
'// ---- Pebble app user preferences: "Load user preferences" (Features) and "Load units" (Weather) ----\n' +
'// PREFERENCES = what PKJS last read from preferences/units and preferences/complications.\n' +
'var PREF_FEATURE_SLOTS = ["upperMiddleLine2", "bottomMiddleLine1", "middleLeftLine1", "middleRightLine1"];\n' +
'function prefStatus(id, text, isError) {\n' +
'  var el = document.getElementById(id);\n' +
'  if (!el) return;\n' +
'  el.textContent = text;\n' +
'  el.style.color = isError ? "#d9822b" : "";\n' +
'  el.style.display = text ? "" : "none";\n' +
'}\n' +
'// Fresh values through the live bridge when there is one, else what the page was built with.\n' +
'function prefFetch(done) {\n' +
'  if (typeof LIVE !== "undefined" && LIVE.active && typeof Pebble !== "undefined") {\n' +
'    Pebble.sendMessage("pkjs", { type: "preferences" }).then(function (r) {\n' +
'      if (r && r.preferences) PREFERENCES = r.preferences;\n' +
'      done(PREFERENCES);\n' +
'    }, function () { done(PREFERENCES); });\n' +
'  } else {\n' +
'    done(PREFERENCES);\n' +
'  }\n' +
'}\n' +
'function loadPreferredFeatures() {\n' +
'  prefStatus("prefFeaturesStatus", "Reading your preferences…");\n' +
'  prefFetch(function (prefs) {\n' +
'    if (!prefs || !prefs.available) { prefStatus("prefFeaturesStatus", "Plugins are not enabled in the Pebble app, so its preferences cannot be read.", true); return; }\n' +
'    var comps = prefs.complications || [];\n' +
'    if (!comps.length) { prefStatus("prefFeaturesStatus", "No complications are set in the Pebble app (Settings → Plugins → Complications).", true); return; }\n' +
'    var n = Math.min(comps.length, PREF_FEATURE_SLOTS.length), i, def;\n' +
'    for (i = 0; i < PREF_FEATURE_SLOTS.length; i++) {\n' +
'      def = SLOT_DEFS[PREF_FEATURE_SLOTS[i]];\n' +
'      if (i < n) {\n' +
'        document.getElementById(def.contentId).value = "0";\n' +
'        if (def.customPrimsId) document.getElementById(def.customPrimsId).value = "";\n' +
'      }\n' +
'    }\n' +
'    pluginGcSlots();\n' +
'    var filled = 0, hidden = 0, avail = computeSlotAvailability();\n' +
'    for (i = 0; i < n; i++) {\n' +
'      var c = comps[i];\n' +
'      var slot = pluginAllocSlot("|" + c.category + "|" + c.item + "|" + c.property + "#" + (c.index || 0));\n' +
'      if (slot < 0) break;\n' +
'      def = SLOT_DEFS[PREF_FEATURE_SLOTS[i]];\n' +
'      document.getElementById(def.contentId).value = String(PLUGIN_CONTENT_FIRST + slot);\n' +
'      if (!def.avail(avail)) hidden++;\n' +
'      filled++;\n' +
'    }\n' +
'    renderSlotPicker();\n' +
'    updatePreview();\n' +
'    var msg = "Loaded " + filled + " preferred " + (filled === 1 ? "feature" : "features") + " from the Pebble app.";\n' +
'    if (hidden) msg += " " + hidden + " of them " + (hidden === 1 ? "is" : "are") + " in slots your current dial style does not use (see “Incompatible features”).";\n' +
'    prefStatus("prefFeaturesStatus", msg, false);\n' +
'  });\n' +
'}\n' +
'function loadPreferredUnits() {\n' +
'  prefStatus("prefUnitsStatus", "Reading your preferences…");\n' +
'  prefFetch(function (prefs) {\n' +
'    if (!prefs || !prefs.available) { prefStatus("prefUnitsStatus", "Plugins are not enabled in the Pebble app, so its preferences cannot be read.", true); return; }\n' +
'    var u = prefs.units;\n' +
'    if (!u) { prefStatus("prefUnitsStatus", "The Pebble app did not report any unit preferences.", true); return; }\n' +
'    var done = [];\n' +
'    var temp = { "°C": "C", "°F": "F" }[u.temperature];\n' +
'    if (temp) { onTempUnitChange(temp); done.push(u.temperature); }\n' +
'    var wind = { "km/h": "kmh", "mph": "mph", "m/s": "ms", "knots": "kn" }[u.wind_speed];\n' +
'    if (wind) { selectModeButton("windSpeedUnitGroup", "windSpeedUnit", wind); done.push(u.wind_speed); }\n' +
'    var alt = { "km": "0", "mi": "1" }[u.distance];\n' +
'    if (alt) { selectModeButton("altitudeUnitGroup", "altitudeUnit", alt); done.push(alt === "0" ? "metres" : "feet"); }\n' +
'    updatePreview();\n' +
'    try { refreshAllSectionSubheaders(); } catch (e) {}\n' +
'    prefStatus("prefUnitsStatus", done.length ? "Loaded from the Pebble app: " + done.join(", ") + "." : "Nothing to load: the Pebble app’s units have no match here.", !done.length);\n' +
'  });\n' +
'}\n' +
'';
}

module.exports = { weatherSourceHtml: weatherSourceHtml, pluginScript: pluginScript };
