// Phase 9 composer -- client-side (settings page) code.
//
// Same non-Node-module shape as config-preview.js/config-runtime.js: this
// file exports client JS *source text* that config-page.js splices into the
// generated <script>. Unlike those, the code below is written as real
// function declarations (embedded via Function.prototype.toString) rather
// than as concatenated string literals, so `node --check` validates it
// directly and there is no quote-escaping to get wrong -- the class of bug
// IMPLEMENTATION_NOTES.md's Update 20 hit with an embedded `//` comment.
//
// Nothing in composerClientBody() ever runs in PKJS; it only runs inside the
// settings page, where these globals exist (defined elsewhere in the
// generated script): esc(), PRIMITIVE_PALETTE, PRIMITIVE_LABEL_BY_ID,
// PRIMITIVE_TRANSPORT_SEQUENCES, PRIMITIVE_TRANSPORT_AUX, TIMEZONE_ZONES,
// PRIM_SPACE_ID, COMPOSER_MAX, PRIMITIVE_PREVIEW_TEXT, FORECAST_SLOTS.
//
// Working-copy semantics (report Section 24): the composer edits COMPOSER.items
// only. Nothing touches the slot's hidden input until saveSlotEditor()
// validates and commits, so Cancel / tapping outside discards every edit.
//
// Storage format of the hidden input / CONFIG_CORNER_*_CUSTOM_PRIMS setting:
// comma-separated "id" or "id:aux" entries (aux omitted when 0). See
// parseCustomPrimsSetting() in primitive-decompose.js, the PKJS-side parser.

function composerClientBody() {
  var COMPOSER = { items: [], selected: -1, addGroup: 0, dragFrom: -1 };

  function composerFindPaletteItem(id, aux) {
    var isTxt = (id === PRIM_PLUGIN_TEXT_ID) && pluginIsTextSlot(aux);
    for (var g = 0; g < PRIMITIVE_PALETTE.length; g++) {
      for (var i = 0; i < PRIMITIVE_PALETTE[g].items.length; i++) {
        var it = PRIMITIVE_PALETTE[g].items[i];
        if (it.id === id && !!it.customText === isTxt) return it;
      }
    }
    // Elements no longer offered for building (e.g. the old "H"/"L" labels) still show up in
    // features that were started from a prebuilt one.
    return PRIMITIVE_LABEL_BY_ID[id] ? { id: id, label: PRIMITIVE_LABEL_BY_ID[id] } : null;
  }
  function composerPaletteByValue(value) {
    if (String(value).indexOf("k:") === 0) {
      for (var g = 0; g < PRIMITIVE_PALETTE.length; g++) {
        for (var i = 0; i < PRIMITIVE_PALETTE[g].items.length; i++) {
          if (PRIMITIVE_PALETTE[g].items[i].key === value.slice(2)) return PRIMITIVE_PALETTE[g].items[i];
        }
      }
      return null;
    }
    var id = parseInt(value, 10), isTxt = false;
    for (var g2 = 0; g2 < PRIMITIVE_PALETTE.length; g2++) {
      for (var j = 0; j < PRIMITIVE_PALETTE[g2].items.length; j++) {
        var it = PRIMITIVE_PALETTE[g2].items[j];
        if (it.id === id && !it.customText) return it;
      }
    }
    return null;
  }

  function composerZoneShortLabel(aux) {
    var z = TIMEZONE_ZONES[aux];
    return z ? (z.abbr || z.label.split(" ")[0]) : ("zone " + aux);
  }

  // Generalizes the zone picker to any parametrized primitive (Update 25
  // adds 'forecast' alongside 'timezone'): which option list backs the
  // picker and what its row is labelled.
  var PARAM_OPTIONS = { timezone: TIMEZONE_ZONES, forecast: FORECAST_SLOTS };
  var PARAM_ROW_LABEL = { timezone: "Zone", forecast: "Forecast slot", plugin: "Plugin data source" };
  function composerParamShortLabel(param, aux) {
    if (param === "timezone") return composerZoneShortLabel(aux);
    if (param === "plugin") return pluginSlotLabel(aux);
    var opts = PARAM_OPTIONS[param] || [];
    var o = null;
    for (var i = 0; i < opts.length; i++) { if (opts[i].index === aux) { o = opts[i]; break; } }
    return o ? o.label : (param + " " + aux);
  }

  function composerItemLabel(item) {
    var pal = composerFindPaletteItem(item.id, item.aux);
    if (!pal) return "?" + item.id;
    if (pal.customText) return "Text [" + pluginTextOfSlot(item.aux) + "]";
    if (pal.param) return pal.label + " [" + composerParamShortLabel(pal.param, item.aux) + "]";
    return pal.label;
  }

  function composerParse(raw) {
    var items = [];
    String(raw || "").split(",").forEach(function (entry) {
      var parts = entry.split(":");
      var id = parseInt(parts[0], 10);
      if (isNaN(id) || id <= 0 || id > 255) return;
      var aux = parts.length > 1 ? parseInt(parts[1], 10) : 0;
      if (isNaN(aux) || aux < 0 || aux > 255) aux = 0;
      items.push({ id: id, aux: aux });
    });
    return items;
  }

  function composerSerialize(items) {
    return items.map(function (it) { return it.aux ? (it.id + ":" + it.aux) : String(it.id); }).join(",");
  }

  // "Prebuilt feature" (report Section 19/20): the primitive sequence a
  // legacy content id transports as, including per-position aux (zone
  // index for the time-zone contents). Empty when that content has no
  // transport sequence (None, astronomy, composites that aren't
  // transport-enabled yet).
  function composerItemsForContent(contentVal) {
    var cid = parseInt(contentVal, 10);
    var seq = PRIMITIVE_TRANSPORT_SEQUENCES[cid] || [];
    var aux = PRIMITIVE_TRANSPORT_AUX[cid] || [];
    var out = [];
    for (var i = 0; i < seq.length; i++) {
      if (seq[i] > 0) out.push({ id: seq[i], aux: aux[i] || 0 });
    }
    return out;
  }

  // Text the settings page's watch preview draws for a corner's stored
  // custom sequence ("id" / "id:aux" entries), or "" when there is none
  // (so the caller falls back to the Content dropdown's label). Zone
  // primitives are rendered from their aux byte: name, a fixed sample
  // time, and PM.
  function composerPreviewLabel(stored) {
    var items = composerParse(stored);
    if (!items.length) return "";
    return items.map(function (it) {
      var pal = composerFindPaletteItem(it.id, it.aux);
      if (pal && pal.customText) return pluginTextOfSlot(it.aux);
      if (pal && pal.param === "timezone") {
        if (pal.label.indexOf("name") >= 0) return composerZoneShortLabel(it.aux);
        return pal.label.indexOf("AM/PM") >= 0 ? "PM" : "12:00";
      }
      if (pal && pal.param === "forecast") {
        var t2 = PRIMITIVE_PREVIEW_TEXT[it.id];
        return (t2 === undefined) ? "" : t2;
      }
      var t = PRIMITIVE_PREVIEW_TEXT[it.id];
      return (t === undefined) ? "" : t;
    }).join("");
  }

  function composerSetError(text) {
    var el = document.getElementById("composerError");
    el.textContent = text || "";
    el.style.display = text ? "" : "none";
  }

  function composerRender() {
    var n = COMPOSER.items.length;
    if (COMPOSER.selected >= n) COMPOSER.selected = n - 1;
    var strip = document.getElementById("composerStrip");
    strip.innerHTML = COMPOSER.items.map(function (it, idx) {
      var cls = "composer-chip" + (idx === COMPOSER.selected ? " selected" : "");
      // Draggable for pointer-based reordering (report's "if feasible"
      // note); <-/-> below remain the primary, keyboard-reachable way to
      // reorder, so drag support is additive, not a replacement.
      return "<button type=\"button\" class=\"" + cls + "\" draggable=\"true\"" +
        " ondragstart=\"composerDragStart(" + idx + ")\" ondragover=\"composerDragOver(event)\" ondrop=\"composerDrop(" + idx + ")\"" +
        " onclick=\"composerSelect(" + idx + ")\">" +
        esc(composerItemLabel(it)) + "</button>";
    }).join("");
    document.getElementById("composerEmpty").style.display = n ? "none" : "";
    document.getElementById("composerCount").textContent = n + " / " + COMPOSER_MAX;
    document.getElementById("composerCount").style.color = (n > COMPOSER_MAX) ? "#d00" : "";
    var sel = COMPOSER.selected;
    document.getElementById("composerLeft").disabled = (sel <= 0);
    document.getElementById("composerRight").disabled = (sel < 0 || sel >= n - 1);
    document.getElementById("composerDelete").disabled = (sel < 0 || n <= 1);
    // Zone picker for the *selected* primitive, only when it takes a zone.
    var zoneRow = document.getElementById("composerSelZoneRow");
    var selPal = sel >= 0 ? composerFindPaletteItem(COMPOSER.items[sel].id, COMPOSER.items[sel].aux) : null;
    if (selPal && selPal.param) {
      zoneRow.style.display = "";
      document.getElementById("composerSelZoneLabel").textContent = PARAM_ROW_LABEL[selPal.param] + " for selected";
      composerFillParamSelect(document.getElementById("composerSelZone"), selPal.param, String(COMPOSER.items[sel].aux));
      if (selPal.param !== "plugin") document.getElementById("composerSelZone").value = String(COMPOSER.items[sel].aux);
    } else {
      zoneRow.style.display = "none";
    }
    var txtRow = document.getElementById("composerSelTextRow");
    if (selPal && selPal.customText) {
      txtRow.style.display = "";
      var txtIn = document.getElementById("composerSelText");
      var curTxt = pluginTextOfSlot(COMPOSER.items[sel].aux);
      if (txtIn.value !== curTxt) txtIn.value = curTxt;
    } else {
      txtRow.style.display = "none";
    }
    // The result is drawn as it would look on the watch by renderSlotEditPreview()
    // (top of the popup), not as chips.
    renderSlotEditPreview();
    composerUpdateAddZoneVisibility();
  }

  function composerSelect(idx) {
    COMPOSER.selected = (COMPOSER.selected === idx) ? -1 : idx;
    composerSetError("");
    composerRender();
  }

  // Top row "+" (report Section 21): appends a SPACE -- a harmless,
  // editable default -- and selects it so the person can immediately swap
  // it via the palette below or move it.
  function composerPlus() {
    COMPOSER.items.push({ id: PRIM_SPACE_ID, aux: 0 });
    COMPOSER.selected = COMPOSER.items.length - 1;
    composerSetError("");
    composerRender();
  }

  function composerMinus() {
    if (COMPOSER.items.length <= 1) return;
    COMPOSER.items.pop();
    composerSetError("");
    composerRender();
  }

  function composerMove(delta) {
    var s = COMPOSER.selected;
    var t = s + delta;
    if (s < 0 || t < 0 || t >= COMPOSER.items.length) return;
    var tmp = COMPOSER.items[s];
    COMPOSER.items[s] = COMPOSER.items[t];
    COMPOSER.items[t] = tmp;
    COMPOSER.selected = t;
    composerRender();
  }

  function composerDelete() {
    var s = COMPOSER.selected;
    if (s < 0 || COMPOSER.items.length <= 1) return;
    COMPOSER.items.splice(s, 1);
    COMPOSER.selected = Math.min(s, COMPOSER.items.length - 1);
    composerSetError("");
    composerRender();
  }

  function composerOnSelTextInput() {
    var s = COMPOSER.selected;
    if (s < 0) return;
    var v = document.getElementById("composerSelText").value.replace(/\s+/g, " ");
    PLUGIN_SLOTS[COMPOSER.items[s].aux] = "text|" + v;
    composerRender();
  }

  function composerOnSelZoneChange() {
    var s = COMPOSER.selected;
    if (s < 0) return;
    var selPal2 = composerFindPaletteItem(COMPOSER.items[s].id, COMPOSER.items[s].aux);
    var newAux = composerParamValueToAux(selPal2 ? selPal2.param : "", document.getElementById("composerSelZone").value);
    if (newAux < 0) { composerSetError(pluginUnavailableText()); composerRender(); return; }
    COMPOSER.items[s].aux = newAux;
    composerRender();
  }

  function composerDragStart(idx) {
    COMPOSER.dragFrom = idx;
  }
  function composerDragOver(ev) {
    ev.preventDefault(); // required for drop to fire (native HTML5 DnD)
  }
  // Drop reorders by removing the dragged chip and reinserting it at the
  // drop target's position -- same one-array-splice approach as the
  // report's "if feasible" note suggested, layered on top of (not
  // replacing) the <-/-> buttons, which remain the accessible/keyboard path.
  function composerDrop(idx) {
    var from = COMPOSER.dragFrom;
    COMPOSER.dragFrom = -1;
    if (from == null || from < 0 || from === idx || from >= COMPOSER.items.length) return;
    var item = COMPOSER.items.splice(from, 1)[0];
    COMPOSER.items.splice(idx, 0, item);
    COMPOSER.selected = idx;
    composerRender();
  }

  // ---- add-from-palette ------------------------------------------------
  // Plugin data sources are listed from what the Pebble app reports (see
  // config-plugins.js); a source not yet assigned a watch slot is offered as
  // "c<catalogue index>" and gets its slot only when actually chosen.
  function composerParamValueToAux(param, value) {
    if (param === "plugin") return pluginAuxFromValue(value);
    return parseInt(value, 10) || 0;
  }

  function composerFillParamSelect(sel, param, wanted) {
    if (param === "plugin") {
      pluginFillComposerSelect(sel, wanted);
      return;
    }
    pluginHideProvider(sel);
    var opts = PARAM_OPTIONS[param] || [];
    sel.innerHTML = opts.map(function (o) {
      return "<option value=\"" + o.index + "\">" + esc(o.label) + "</option>";
    }).join("");
  }

  function composerBuildPalette() {
    var groupSel = document.getElementById("composerAddGroup");
    if (groupSel.getAttribute("data-built")) return; // static, build once
    groupSel.setAttribute("data-built", "1");
    groupSel.innerHTML = PRIMITIVE_PALETTE.map(function (g, gi) {
      return "<option value=\"" + gi + "\">" + esc(g.group) + "</option>";
    }).join("");
    composerOnAddGroupChange();
  }

  function composerOnAddGroupChange() {
    var gi = parseInt(document.getElementById("composerAddGroup").value, 10) || 0;
    document.getElementById("composerAddItem").innerHTML = PRIMITIVE_PALETTE[gi].items.map(function (it) {
      return "<option value=\"" + (it.key ? "k:" + it.key : it.id) + "\">" + esc(it.label) + "</option>";
    }).join("");
    composerUpdateAddZoneVisibility();
  }

  function composerUpdateAddZoneVisibility() {
    var pal = composerPaletteByValue(document.getElementById("composerAddItem").value);
    var row = document.getElementById("composerAddZoneRow");
    document.getElementById("composerAddTextRow").style.display = (pal && pal.customText) ? "" : "none";
    if (pal && pal.param) {
      row.style.display = "";
      document.getElementById("composerAddZoneLabel").textContent = PARAM_ROW_LABEL[pal.param];
      composerFillParamSelect(document.getElementById("composerAddZone"), pal.param);
      if (pal.param !== "plugin") pluginHideProvider(document.getElementById("composerAddZone"));
    } else {
      row.style.display = "none";
    }
  }

  // Selecting a primitive from the palette *appends* it (report Section 20);
  // it never replaces the selected one -- use Delete + Add, or, for a
  // parametrized primitive, the zone picker above the palette.
  function composerAddFromPalette() {
    var pal = composerPaletteByValue(document.getElementById("composerAddItem").value);
    if (!pal) return;
    var id = pal.id;
    var aux = 0;
    if (pal.customText) {
      var txt = document.getElementById("composerAddText").value.replace(/\s+/g, " ").trim();
      if (!txt) { composerSetError("Type the text to show first."); return; }
      aux = pluginAllocTextSlot(txt);
      if (aux < 0) { composerSetError("No free slot: at most 12 plugin and custom text elements can be used in total."); return; }
      document.getElementById("composerAddText").value = "";
    } else if (pal.param) {
      aux = composerParamValueToAux(pal.param, document.getElementById("composerAddZone").value);
      if (aux < 0) { composerSetError(pluginUnavailableText()); return; }
    }
    COMPOSER.items.push({ id: id, aux: aux });
    COMPOSER.selected = COMPOSER.items.length - 1;
    composerSetError("");
    composerRender();
  }

  // ---- lifecycle hooks called from the slot editor ----------------------
  function composerOpen(def) {
    var section = document.getElementById("slotEditCustomPrimsSection");
    if (!def.customPrimsId) { section.style.display = "none"; section.setAttribute("data-available", "0"); document.getElementById("slotEditCustomPrimsToggle").checked = false; document.getElementById("slotEditPrebuiltBlock").style.display = ""; COMPOSER.items = []; slotEditUpdateKindButtons(); return; }
    section.style.display = "";
    section.setAttribute("data-available", "1");
    composerBuildPalette();
    var stored = document.getElementById(def.customPrimsId).value || "";
    COMPOSER.items = composerParse(stored);
    COMPOSER.selected = -1;
    COMPOSER.dragFrom = -1;
    document.getElementById("slotEditCustomPrimsToggle").checked = (COMPOSER.items.length > 0);
    document.getElementById("slotEditCustomPrimsBody").style.display = COMPOSER.items.length ? "" : "none";
    // Building from elements replaces the Category/Content pickers.
    document.getElementById("slotEditPrebuiltBlock").style.display = COMPOSER.items.length ? "none" : "";
    setSlotEditorColorGroupVisibility(document.getElementById("slotEditContent").value);
    composerSetError("");
    slotEditUpdateKindButtons();
    composerRender();
  }

  // Turning the toggle on with an empty working copy seeds it from the
  // Content dropdown's current choice ("start from what's already there").
  // Turning it off keeps the working copy (re-enabling restores it) but
  // saveSlotEditor() then commits nothing.
  function slotEditUpdateKindButtons() {
    var on = document.getElementById("slotEditCustomPrimsToggle").checked;
    var group = document.getElementById("slotEditKindGroup");
    var btns = group.getElementsByClassName("mode-btn");
    for (var i = 0; i < btns.length; i++) btns[i].className = "mode-btn" + ((i === 1) === on ? " active" : "");
    group.style.display = document.getElementById("slotEditCustomPrimsSection").getAttribute("data-available") === "0" ? "none" : "";
  }
  function slotEditSetKind(custom) {
    document.getElementById("slotEditCustomPrimsToggle").checked = !!custom;
    composerOnToggle();
  }

  function composerOnToggle() {
    var on = document.getElementById("slotEditCustomPrimsToggle").checked;
    document.getElementById("slotEditCustomPrimsBody").style.display = on ? "" : "none";
    // Category and Content (with their labels) are hidden while building from
    // elements; the style buttons stay, and the preview follows them.
    document.getElementById("slotEditPrebuiltBlock").style.display = on ? "none" : "";
    setSlotEditorColorGroupVisibility(document.getElementById("slotEditContent").value);
    if (on && !COMPOSER.items.length) {
      COMPOSER.items = composerItemsForContent(document.getElementById("slotEditContent").value);
      COMPOSER.selected = -1;
    }
    composerSetError("");
    slotEditUpdateKindButtons();
    composerRender();
  }

  // ---- live preview at the top of the popup ------------------------------
  // Draws the feature the way the watch shows it: the feature font, the
  // current background colour, the selected style (MONO / ACC / PILL / COLOR)
  // and the outline setting. Icons are the real icon art (same helpers as the
  // main preview). Called whenever anything that affects it changes.
  function slotEditPreviewLabel() {
    var customOn = document.getElementById("slotEditCustomPrimsToggle").checked;
    if (customOn) return composerPreviewLabel(composerSerialize(COMPOSER.items));
    var contentVal = document.getElementById("slotEditContent").value;
    return parseInt(contentVal, 10) ? primitiveDecompositionText(contentVal, true) : "";
  }

  function renderSlotEditPreview() {
    var canvas = document.getElementById("slotEditPreviewCanvas");
    if (!canvas || !canvas.getContext || !document.getElementById("slotEditCustomPrimsToggle")) return;
    var cssW = canvas.clientWidth || 368, cssH = 64;
    var dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(cssW * dpr) || canvas.height !== Math.round(cssH * dpr)) {
      canvas.width = Math.round(cssW * dpr);
      canvas.height = Math.round(cssH * dpr);
    }
    var ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cssW, cssH);

    var nightActive = document.getElementById("nightEnabled").checked && isNightNowApprox();
    var colors = nightActive ? nightColors() : dayColors();
    var mode = SLOT_EDITOR_DRAFT_COLOR;

    // Background: the watch's background colour. With PILL a faint pattern sits
    // behind the capsule, since a pill in the background colour is otherwise
    // invisible on a plain background.
    ctx.fillStyle = colors.bg;
    ctx.fillRect(0, 0, cssW, cssH);
    if (mode === 2) {
      ctx.save();
      ctx.globalAlpha = 0.22;
      ctx.strokeStyle = colors.text;
      ctx.lineWidth = 3;
      for (var sx = -cssH; sx < cssW; sx += 10) {
        ctx.beginPath(); ctx.moveTo(sx, cssH); ctx.lineTo(sx + cssH, 0); ctx.stroke();
      }
      ctx.restore();
    }

    var fontSel = document.getElementById("cornerFont");
    var fontOpt = fontSel.options[fontSel.selectedIndex];
    var entry = fontLookupEntry(fontSel.value);
    var px = Math.max(12, Math.min(30, Math.round(entry.height * 1.6)));
    var scale = px / Math.max(1, entry.height);
    var fontCss = canvasFontFor(fontOpt.getAttribute("data-preview") || "", px, fontSel.value);

    var color = colors.text;
    if (mode === 1) color = colors.accent;
    else if (mode === 3) color = "#4caf50";
    var outHex = previewHexLuma(color) >= 9 ? "#000000" : "#ffffff";
    var oEl = document.getElementById("outlineStyle");
    var outline = oEl ? (parseInt(oEl.value, 10) || 0) : 0;

    var label = slotEditPreviewLabel();
    ctx.font = fontCss;
    ctx.textBaseline = "middle";
    ctx.textAlign = "left";
    var cy = cssH / 2;
    if (!label) {
      ctx.fillStyle = colors.text;
      ctx.globalAlpha = 0.45;
      ctx.fillText("Nothing to show", 12, cy);
      ctx.globalAlpha = 1;
    } else {
      var segs = previewLabelSegments(label, px);
      var total = 0, i;
      for (i = 0; i < segs.length; i++) {
        segs[i].width = segs[i].icon ? segs[i].icon.width : (segs[i].gap || ctx.measureText(segs[i].text).width);
        total += segs[i].width;
      }
      var x0 = Math.max(6, (cssW - total) / 2);
      if (mode === 2) {
        var pw = Math.min(cssW - 8, total + px * 1.6), ph = Math.round(px * 1.9);
        ctx.fillStyle = colors.bg;
        ctx.beginPath();
        if (ctx.roundRect) ctx.roundRect((cssW - pw) / 2, cy - ph / 2, pw, ph, ph / 2);
        else ctx.rect((cssW - pw) / 2, cy - ph / 2, pw, ph);
        ctx.fill();
      }
      var offs = outline >= 2 ? [[-1,0],[1,0],[0,-1],[0,1],[-2,0],[2,0],[0,-2],[0,2],[1,1],[-1,1],[-1,-1],[1,-1]]
                 : (outline === 1 ? [[-1,0],[1,0],[0,-1],[0,1]] : []);
      var cx = x0;
      for (i = 0; i < segs.length; i++) {
        if (segs[i].icon) {
          var ic = segs[i].icon;
          drawPreviewIcon(ctx, ic, cx, cy - ic.height / 2, color);
        } else {
          ctx.font = fontCss;
          ctx.textBaseline = "middle";
          ctx.fillStyle = outHex;
          for (var o = 0; o < offs.length; o++) ctx.fillText(segs[i].text, cx + offs[o][0] * scale, cy + offs[o][1] * scale);
          ctx.fillStyle = color;
          ctx.fillText(segs[i].text, cx, cy);
        }
        cx += segs[i].width;
      }
    }
  }

  // Returns the string to store in the slot's hidden input, or null if the
  // composer refuses to commit (a message is shown and the popup must stay
  // open -- report Section 23: never silently truncate).
  function composerCommitValue() {
    if (!document.getElementById("slotEditCustomPrimsToggle").checked) return "";
    var n = COMPOSER.items.length;
    if (n > COMPOSER_MAX) { composerSetError("Feature can have only " + COMPOSER_MAX + " elements"); return null; }
    if (n < 1) { composerSetError("Add at least one element, or turn off the custom sequence."); return null; }
    return composerSerialize(COMPOSER.items);
  }
}

// Strip the function wrapper: what remains is the plain function
// declarations, ready to be spliced into the generated <script> as globals.
var src = composerClientBody.toString();
module.exports = src.slice(src.indexOf('{') + 1, src.lastIndexOf('}'));
