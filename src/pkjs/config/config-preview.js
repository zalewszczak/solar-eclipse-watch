// ---- client-side miniature-watch preview renderer -----------------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS9 -- preview engine). This is NOT a Node module in the usual
// sense: it exports the exact JavaScript *source text* (as a string)
// that config-page.js splices into the settings page's embedded
// <script> tag, because that script runs in the phone's webview --
// a completely separate JS runtime from PKJS that can't require()
// this or any other Node module. Splitting it out here still buys
// the same thing every other JS8 slice did: this ~1100-line canvas-
// drawing engine (sky/stars/moon phase, corner slots, hand geometry,
// digital-side features, and the updatePreview() orchestrator that
// ties them together) now lives in its own file instead of being
// buried inside one 5000-line function, even though at runtime it's
// still concatenated into the same single HTML string it always was.
//
// Everything here is pure rendering: given already-resolved settings
// values and a canvas 2D context, draw the miniature watch. No event
// handlers, no DOM wiring beyond reading already-resolved input
// values -- the UI/event-handling code that calls updatePreview()
// after each change is still embedded directly in config-page.js's
// own remaining template content (a later slice's job).

module.exports =
// skipElements (Digital top's own reserved panel band only) leaves out
// the weather puffs/starfield that would otherwise draw here -- "only
// sky gradient", matching fill_sky_gradient_ex()\'s own watch-side
// continuation of the SAME gradient into that band with no astronomy
// drawn over it. The plain gradient/flat-black wash itself is still
// drawn either way, so the two bands (this one and the real sky area
// below it) read as one continuous sky rather than a flat void.' +
// gradTop/gradH let a caller draw a fragment of a taller logical
// gradient than the rect actually being filled -- Digital top's panel
// band and the sky area below it are two separate fillRect calls (the
// panel has to be drawn and filled independently, in draw order,
// before the corner/clock text that sits on top of it) but need to
// read as ONE continuous gradient spanning both, not two independent
// top-to-bottom fades restarting at each rect's own height. Default
// to this call's own (y, h) when omitted, which is exactly the old
// behavior for every single-rect caller (bar/analog/plain-digital).
'function drawSkyLayer(ctx, x, y, w, h, skyMode, phase, skipElements, gradTop, gradH) {' +
'  if (skyMode === "2") {' +
'    ctx.fillStyle = "#000000";' +
'    ctx.fillRect(x, y, w, h);' +
'    if (!skipElements) drawStarsPreview(ctx, x, y, w, h);' +
'    return;' +
'  }' +
'  var g = SKY_PHASE_COLORS[phase] || SKY_PHASE_COLORS.day;' +
'  var top = (gradTop === undefined) ? y : gradTop;' +
'  var span = gradH || h;' +
'  var grad = ctx.createLinearGradient(0, top, 0, top + span);' +
'  grad.addColorStop(0, g.top);' +
'  grad.addColorStop(1, g.bottom);' +
'  ctx.fillStyle = grad;' +
'  ctx.fillRect(x, y, w, h);' +
'  if (skyMode !== "1" && !skipElements) {' + // Clear sky never draws weather; Weather sky gets a cloud regardless of time of day
'    ctx.fillStyle = "rgba(255,255,255,0.9)";' +
'    function puff(cx, cy, r) { ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.fill(); }' +
'    var cy = y + h * 0.6;' +
'    puff(x + w * 0.22, cy, w * 0.08);' +
'    puff(x + w * 0.32, cy - 3, w * 0.1);' +
'    puff(x + w * 0.42, cy, w * 0.07);' +
'  }' +
'}' +
// Simple time-of-day heuristic -- this settings page has no real
// astronomy data to work with (no lat/lon-based sunrise/sunset is
// computed client-side, only on the phone/watch), so "day" vs
// "twilight" vs "night" is read straight off the device's own current
// clock instead. Not astronomically precise, but enough to make the
// preview at least somewhat representative of what the watch would be
// showing right now, per the request.
'function currentSkyPhase(now) {' +
'  var hour = now.getHours() + now.getMinutes() / 60;' +
'  if (hour >= 7 && hour < 17) return "day";' +
'  if ((hour >= 5 && hour < 7) || (hour >= 17 && hour < 20)) return "twilight";' +
'  return "night";' +
'}' +
'var SKY_PHASE_COLORS = {' +
'  day: { top: "#4a90d9", bottom: "#bfe3f5" },' +
'  twilight: { top: "#2b3a63", bottom: "#ff9d5c" },' +
'  night: { top: "#050912", bottom: "#141d33" }' +
'};' +
// Fixed relative (0-1) positions -- deliberately NOT Math.random(),
// since updatePreview() re-runs every second (see the setInterval at
// the bottom of this file) and randomizing on every redraw would make
// the whole field visibly jitter/twinkle instead of sitting still like
// a real star field. Space view (sky_mode 2) only.
'var STAR_POSITIONS_PCT = [' +
'  [0.08, 0.12], [0.18, 0.30], [0.30, 0.08], [0.42, 0.24], [0.55, 0.06], [0.66, 0.32],' +
'  [0.78, 0.14], [0.90, 0.26], [0.14, 0.46], [0.60, 0.44], [0.85, 0.50], [0.35, 0.52]' +
'];' +
'function drawStarsPreview(ctx, x, y, w, h) {' +
'  ctx.fillStyle = "#ffffff";' +
'  STAR_POSITIONS_PCT.forEach(function (p, i) {' +
'    var r = (i % 3 === 0) ? 1.6 : 1;' +
'    ctx.beginPath();' +
'    ctx.arc(x + w * p[0], y + h * p[1], r, 0, 2 * Math.PI);' +
'    ctx.fill();' +
'  });' +
'}' +
// Same planet_color() mapping background_layer.c uses -- Space view
// only ("plenty of planets" per the request). Fixed relative positions
// for the same reason STAR_POSITIONS_PCT above is fixed.
'var PLANET_DOTS = [' +
'  { x: 0.15, y: 0.58, color: "#AAAAAA" },' + // Mercury
'  { x: 0.85, y: 0.64, color: "#FFFFFF" },' + // Venus
'  { x: 0.46, y: 0.70, color: "#FF3B30" },' + // Mars
'  { x: 0.62, y: 0.60, color: "#FFD400" },' + // Jupiter
'  { x: 0.30, y: 0.42, color: "#FFD400" }' +  // Saturn
'];' +
// Two-overlapping-circles moon phase trick (same visual result as
// background_layer.c's draw_moon_phase() per-pixel terminator ellipse,
// just built from 2 canvas arcs instead of a scanline fill -- plenty
// accurate for an icon-sized preview disc). k=0 (new): the dark
// "shadow" circle sits exactly on top of the lit one, fully covering
// it. k=1 (full): the shadow is offset a full diameter away, off the
// visible disc entirely. k=0.5: offset by one radius, covering half.
'function drawMoonPhasePreview(ctx, cx, cy, r, phasePct, waxing) {' +
'  var k = Math.max(0, Math.min(100, phasePct)) / 100;' +
'  ctx.save();' +
'  ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.closePath();' +
'  ctx.clip();' +
'  ctx.fillStyle = "#fff6d0";' +
'  ctx.fillRect(cx - r, cy - r, r * 2, r * 2);' +
'  if (k < 0.999) {' +
'    var offset = k * 2 * r;' +
'    var dir = waxing ? -1 : 1;' +
'    ctx.beginPath();' +
'    ctx.arc(cx + dir * offset, cy, r, 0, 2 * Math.PI);' +
'    ctx.fillStyle = "#3a3a3a";' +
'    ctx.fill();' +
'  }' +
'  ctx.restore();' +
'  ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI);' +
'  ctx.strokeStyle = "#000000"; ctx.lineWidth = 1; ctx.stroke();' +
'}' +
// Pure date-based approximation (moon phase doesn\'t depend on
// observer location) -- known synodic month + a reference new moon,
// same simplification astro.js\'s own low-precision Meeus algorithms
// make elsewhere in this app, just inlined here since config-page.js
// runs in its own separate webview context and can\'t require() that
// file directly.
'function approxMoonPhase(date) {' +
'  var synodic = 29.530588853;' +
'  var known = Date.UTC(2000, 0, 6, 18, 14, 0);' +
'  var days = (date.getTime() - known) / 86400000;' +
'  var age = ((days % synodic) + synodic) % synodic;' +
'  var illum = (1 - Math.cos(2 * Math.PI * age / synodic)) / 2;' +
'  return { pct: Math.round(illum * 100), waxing: age < synodic / 2 };' +
'}' +
// draw_label_in_box()\'s 3 label styles (Boxed/Outlined/Soft), ported
// for the shake-to-reveal Sun/Moon/ISS/Aurora name labels below --
// see label_style\'s own comment in eclipse_data.h.
'function contrastingColorFor(hex) {' +
'  var r = parseInt(hex.substr(1, 2), 16) || 0, g = parseInt(hex.substr(3, 2), 16) || 0, b = parseInt(hex.substr(5, 2), 16) || 0;' +
'  return (0.299 * r + 0.587 * g + 0.114 * b) > 140 ? "#000000" : "#ffffff";' +
'}' +
'function drawShakeLabel(ctx, x, y, text, labelStyle, colors) {' +
'  var style = parseInt(labelStyle, 10) || 0;' +
'  ctx.font = "bold 9px sans-serif";' +
'  ctx.textAlign = "left";' +
'  ctx.textBaseline = "middle";' +
'  if (style === 2) {' + // Soft
'    ctx.fillStyle = "#cccccc";' +
'    ctx.fillText(text, x, y);' +
'    return;' +
'  }' +
'  if (style === 1) {' + // Outlined
'    var oc = contrastingColorFor(colors.text);' +
'    ctx.fillStyle = oc;' +
'    [[1, 0], [-1, 0], [0, 1], [0, -1]].forEach(function (o) { ctx.fillText(text, x + o[0], y + o[1]); });' +
'    ctx.fillStyle = colors.text;' +
'    ctx.fillText(text, x, y);' +
'    return;' +
'  }' +
'  var textW = ctx.measureText(text).width;' + // Boxed (default)
'  ctx.fillStyle = "#000000";' +
'  ctx.fillRect(x - 3, y - 7, textW + 6, 14);' +
'  ctx.fillStyle = "#ffffff";' +
'  ctx.fillText(text, x, y);' +
'}' +
// Sun/Moon (sized off the Sun & Moon size setting -- SUN_R_NORMAL/
// MOON_R_NORMAL below are background_layer.c\'s own real on-watch
// values, 20px/16px at 100%), Space view\'s planets/stars, Aurora, and
// the ISS -- plus each one\'s always-on shake-to-reveal label (this
// static preview has no shake gesture to trigger it live, so it\'s
// just always shown, in whatever style/colors are currently picked).
// day/twilight both show the Sun; night shows the Moon instead (Space
// view shows both, plus the planets/stars, regardless of time of day,
// matching background_layer.c\'s own sky_mode==2 handling).
'function drawCelestialPreview(ctx, x, y, w, h, skyMode, phase, colors, now) {' +
'  var scale = w / 200;' +
'  var sizePct = parseInt(document.getElementById("sunMoonSize").value, 10) || 75;' +
'  var sunR = Math.max(3, 20 * scale * sizePct / 100);' +
'  var moonR = Math.max(3, 16 * scale * sizePct / 100);' +
'  var labelStyle = document.getElementById("labelStyle").value;' +
'  var showIss = document.getElementById("showIss").checked;' +
'  var auroraEnabled = document.getElementById("auroraEnabled").checked;' +
'  var isSpace = skyMode === "2";' +
'  var showSun = isSpace || phase !== "night";' +
'  var showMoon = isSpace || phase === "night";' +

'  if (isSpace) {' +
'    PLANET_DOTS.forEach(function (p) {' +
'      ctx.beginPath();' +
'      ctx.arc(x + w * p.x, y + h * p.y, Math.max(1.5, 3 * scale), 0, 2 * Math.PI);' +
'      ctx.fillStyle = p.color;' +
'      ctx.fill();' +
'    });' +
'  }' +

'  var sunX = x + w * 0.72, sunY = y + h * 0.2;' +
'  var moonX = isSpace ? x + w * 0.35 : x + w * 0.72;' +
'  var moonY = isSpace ? y + h * 0.38 : y + h * 0.2;' +

'  if (showSun) {' +
'    ctx.beginPath();' +
'    ctx.arc(sunX, sunY, sunR, 0, 2 * Math.PI);' +
'    ctx.fillStyle = "#fff6d0";' +
'    ctx.fill();' +
'    drawShakeLabel(ctx, sunX + sunR + 4, sunY, "Sun", labelStyle, colors);' +
'  }' +
'  if (showMoon) {' +
'    var moonPhase = approxMoonPhase(now);' +
'    drawMoonPhasePreview(ctx, moonX, moonY, moonR, moonPhase.pct, moonPhase.waxing);' +
'    drawShakeLabel(ctx, moonX + moonR + 4, moonY, "Moon", labelStyle, colors);' +
'  }' +

'  var showAurora = auroraEnabled && phase === "night";' +
'  if (showAurora) {' +
'    var ag = ctx.createLinearGradient(x, y, x, y + h * 0.5);' +
'    ag.addColorStop(0, "rgba(60,220,130,0.55)");' +
'    ag.addColorStop(1, "rgba(60,220,130,0)");' +
'    ctx.fillStyle = ag;' +
'    ctx.fillRect(x, y, w, h * 0.5);' +
'    drawShakeLabel(ctx, x + w * 0.5, y + h * 0.14, "Aurora", labelStyle, colors);' +
'  }' +

'  var showIssDot = showIss && (isSpace || phase === "night");' +
'  if (showIssDot) {' +
'    var issX = x + w * 0.52, issY = y + h * 0.78;' +
'    ctx.beginPath();' +
'    ctx.arc(issX, issY, Math.max(1.5, 3 * scale), 0, 2 * Math.PI);' +
'    ctx.fillStyle = "#ffffff";' +
'    ctx.fill();' +
'    ctx.lineWidth = 1; ctx.strokeStyle = "#000000"; ctx.stroke();' +
'    drawShakeLabel(ctx, issX + 6, issY, "ISS", labelStyle, colors);' +
'  }' +
'}' +

// CORNER_PREVIEW_LABELS is now derived client-side from the same
// CORNER_CATEGORIES injection categoryForContentId()/findCategory()
// use, right after that injection below -- see presets-lookups.js's
// own header comment for why one shared array replaced this and 2
// other independently hand-maintained lists.

'function hasPreviewContent(contentId) {' +
'  var el = document.getElementById(contentId);' +
'  if (!el) return false;' +
'  var v = parseInt(el.value, 10);' +
'  return v !== 0 && !!CORNER_PREVIEW_LABELS[v];' +
'}' +

'function slotAvailable(wrapId) {' +
'  var avail = computeSlotAvailability();' +
'  switch (wrapId) {' +
'    case "cornerTLWrap": case "cornerTRWrap": case "cornerBLWrap": case "cornerBRWrap":' +
'      return !avail.cornersGrayed;' +
'    case "upperMiddleWrap": return avail.upper;' +
'    case "bottomMiddleWrap": return avail.bottom;' +
'    case "middleLeftWrap": return avail.left;' +
'    case "middleRightWrap": return avail.right;' +
'    default: return false;' +
'  }' +
'}' +

'function drawCornerSlot(ctx, contentId, colorId, x, y, textAlign, colors, fontCss) {' +
'  var contentEl = document.getElementById(contentId);' +
'  var colorEl = document.getElementById(colorId);' +
'  if (!contentEl || !colorEl) return;' +
'  var content = parseInt(contentEl.value, 10);' +
'  var label = CORNER_PREVIEW_LABELS[content];' +
'  if (!label) return;' +
'  var mode = parseInt(colorEl.value, 10);' +
'  var color = colors.text, alpha = 1;' +
'  if (mode === 1) { color = colors.accent; }' +
'  else if (mode === 2) { color = colors.accent; alpha = 0.55; }' +
'  else if (mode === 3) { color = "#4caf50"; }' +
'  ctx.font = fontCss;' +
'  ctx.textAlign = textAlign;' +
'  ctx.textBaseline = "top";' +
'  ctx.globalAlpha = alpha;' +
'  ctx.fillStyle = color;' +
'  ctx.fillText(label, x, y);' +
'  ctx.globalAlpha = 1;' +
'}' +

// Direct port of features_recompute_layout()'s own margin computation
// (background_layer.c/features_layer.c) -- how far in from each edge
// the "middle" feature slots (upper/bottom-middle, middle-left/right)
// have to start so they land INSIDE the empty area the current marker
// style/hands actually leave clear, rather than overlapping the
// marker ring. 44/40/30/30 are the same defaults (and BITMAP_STYLE_
// MARGINS\' own values, identical across all 5 bitmap styles) the
// watch falls back to; for the procedural presets (0-2) and custom (8)
// the margin instead grows to wherever that style\'s own inner ring
// boundary reaches at each of the 4 cardinal points, via the exact
// same point_on_ring() technique the marker ring itself is drawn with.
// Values returned are already scaled into this canvas\'s own pixel
// space (see readHandConfig()\'s own `scale` comment for why that\'s
// needed at all).
'function computeMiddleFeatureMargins(w, h) {' +
'  var scale = w / 200;' +
'  var margins = { top: 44 * scale, bottom: 40 * scale, left: 30 * scale, right: 30 * scale };' +
'  var markerStyle = parseInt(document.getElementById("bigAnalogMarkerStyle").value, 10);' +
'  var isBitmapStyle = markerStyle >= 3 && markerStyle !== 8 && markerStyle !== 9;' +
'  if (isBitmapStyle) return margins;' + // BITMAP_STYLE_MARGINS's 5 entries are all identical to the defaults above -- nothing to differ
'  var pct, ecc;' +
//'  if (markerStyle === 8) {' +
'    var hourCfg = readCustomMarkerConfig("hour");' +
'    pct = hourCfg.innerBorderPct; ecc = hourCfg.innerEccentricity;' +
//'  } else if (markerStyle <= 2) {' +
//'    var hour = MARKER_STYLE_HOUR_PRESETS[markerStyle], sec = MARKER_STYLE_SECOND_PRESETS[markerStyle];' +
//'    if (sec.thickness === 0 || hour.innerBorderPct <= sec.innerBorderPct) { pct = hour.innerBorderPct; ecc = hour.innerEccentricity; }' +
//'    else { pct = sec.innerBorderPct; ecc = sec.innerEccentricity; }' +
//'  } else if (markerStyle === 9) {' + // 9 (old "none" and new classy)
//'    var hour = MARKER_STYLE_HOUR_PRESETS[3], sec = MARKER_STYLE_SECOND_PRESETS[3];' +
//'    if (sec.thickness === 0 || hour.innerBorderPct <= sec.innerBorderPct) { pct = hour.innerBorderPct; ecc = hour.innerEccentricity; }' +
//'    else { pct = sec.innerBorderPct; ecc = sec.innerEccentricity; }' +
//'  } else { pct = 100; ecc = 0; }' + // anything unrecognized -- background_marker_inner_reach()'s own "fully retracted" fallback
'  var cx = w / 2, cy = h / 2;' +
'  var topPt = pointOnRing(cx, cy, w, h, 0, pct, ecc);' +
'  var rightPt = pointOnRing(cx, cy, w, h, Math.PI / 2, pct, ecc);' +
'  var bottomPt = pointOnRing(cx, cy, w, h, Math.PI, pct, ecc);' +
'  var leftPt = pointOnRing(cx, cy, w, h, 3 * Math.PI / 2, pct, ecc);' +
'  var margin = 4 * scale;' +
'  if (topPt.y + margin > margins.top) margins.top = topPt.y + margin;' +
'  if (h - bottomPt.y + margin > margins.bottom) margins.bottom = h - bottomPt.y + margin;' +
'  var leftReach = leftPt.x + margin, rightReach = w - rightPt.x + margin;' +
'  if (leftReach > margins.left) margins.left = leftReach;' +
'  if (rightReach > margins.right) margins.right = rightReach;' +
'  return margins;' +
'}' +

// skyTop defaults to 0 (TL/TR sit right at the screen's own top edge --
// Analog and Digital bar both have open sky there). Digital top's own
// call passes the panel's height instead, since that layout's TL/TR
// corners sit at the sky's own top edge instead -- right below the
// transparent clock panel -- mirroring skyBottom\'s existing job of
// pulling BL/BR up above Digital bar\'s own panel the other way.
// Corner/edge feature text used to always render as a flat "bold 9px
// sans-serif" regardless of whatever font was actually picked in the
// Features section's own Font control -- misleading the same way the
// old flat-26px clock preview was, just for the corner readouts
// instead. Now mirrors that same fix: pulls the currently-selected
// cornerFont's real family/weight (via canvasFontFor(), same helper
// the clock preview uses) and its FONT_LOOKUP `height` -- not
// `sizePx`, which is specifically the *big/mainClock* bake size; corner
// text uses the small/row-height reading `height` already represents
// (see font_lookup.c's own font_lookup_height(), "for sizing/
// vertically-centering text boxes", the exact same job this preview
// text is doing) -- scaled into this canvas's own pixel space by the
// same w/200 ratio computeMiddleFeatureMargins() already uses. Loosely
// clamped (7-22px) since a user CAN pick one of the 48px-bake big
// fonts here too (cornerFont's own picker isn't mainClock-restricted),
// and letting a corner readout balloon to a near-clock-sized readout
// would blow out these small, fixed-position boxes rather than just
// looking accurately big.
'function drawCornersAndEdges(ctx, w, h, colors, skyBottom, skyTop) {' +
'  var bottomY = (typeof skyBottom === "number") ? skyBottom : h;' +
'  var topY = (typeof skyTop === "number") ? skyTop : 0;' +
'  var cornerFontSel = document.getElementById("cornerFont");' +
'  var cornerOpt = cornerFontSel.options[cornerFontSel.selectedIndex];' +
'  var cornerEntry = fontLookupEntry(cornerFontSel.value);' +
'  var cornerScale = w / 200;' +
'  var cornerPx = Math.max(7, Math.min(22, Math.round(cornerEntry.height * cornerScale)));' +
'  var cornerFontCss = canvasFontFor(cornerOpt.getAttribute("data-preview") || "", cornerPx, cornerFontSel.value);' +
'  var lineH = Math.round(cornerPx * 1.4);' +
'  if (slotAvailable("cornerTLWrap")) drawCornerSlot(ctx, "cornerTL", "cornerTLColor", 5, topY + 5, "left", colors, cornerFontCss);' +
'  if (slotAvailable("cornerTRWrap")) drawCornerSlot(ctx, "cornerTR", "cornerTRColor", w - 5, topY + 5, "right", colors, cornerFontCss);' +
'  if (slotAvailable("cornerBLWrap")) drawCornerSlot(ctx, "cornerBL", "cornerBLColor", 5, bottomY - 14, "left", colors, cornerFontCss);' +
'  if (slotAvailable("cornerBRWrap")) drawCornerSlot(ctx, "cornerBR", "cornerBRColor", w - 5, bottomY - 14, "right", colors, cornerFontCss);' +
'  var needMiddleMargins = slotAvailable("upperMiddleWrap") || slotAvailable("bottomMiddleWrap") || slotAvailable("middleLeftWrap") || slotAvailable("middleRightWrap");' +
'  var mm = needMiddleMargins ? computeMiddleFeatureMargins(w, h) : null;' +
'  if (slotAvailable("upperMiddleWrap")) {' +
'    var upperHasLine2 = hasPreviewContent("upperMiddleLine2Content");' +
'    drawCornerSlot(ctx, "upperMiddleLine1Content", "upperMiddleLine1Color", w / 2, upperHasLine2 ? mm.top : mm.top + lineH / 2, "center", colors, cornerFontCss);' +
'    if (upperHasLine2) drawCornerSlot(ctx, "upperMiddleLine2Content", "upperMiddleLine2Color", w / 2, mm.top + lineH, "center", colors, cornerFontCss);' +
'  }' +
'  if (slotAvailable("bottomMiddleWrap")) {' +
'    var bottomHasLine2 = hasPreviewContent("bottomMiddleLine2Content");' +
'    drawCornerSlot(ctx, "bottomMiddleLine1Content", "bottomMiddleLine1Color", w / 2, bottomHasLine2 ? bottomY - mm.bottom - lineH : bottomY - mm.bottom - lineH / 2, "center", colors, cornerFontCss);' +
'    if (bottomHasLine2) drawCornerSlot(ctx, "bottomMiddleLine2Content", "bottomMiddleLine2Color", w / 2, bottomY - mm.bottom, "center", colors, cornerFontCss);' +
'  }' +
'  if (slotAvailable("middleLeftWrap")) {' +
'    var midLeftHasLine2 = hasPreviewContent("middleLeftLine2Content");' +
'    drawCornerSlot(ctx, "middleLeftLine1Content", "middleLeftLine1Color", mm.left, midLeftHasLine2 ? h / 2 - 4 - lineH / 2 : h / 2 - 4, "left", colors, cornerFontCss);' +
'    if (midLeftHasLine2) drawCornerSlot(ctx, "middleLeftLine2Content", "middleLeftLine2Color", mm.left, h / 2 - 4 + lineH / 2, "left", colors, cornerFontCss);' +
'  }' +
'  if (slotAvailable("middleRightWrap")) {' +
'    var midRightHasLine2 = hasPreviewContent("middleRightLine2Content");' +
'    drawCornerSlot(ctx, "middleRightLine1Content", "middleRightLine1Color", w - mm.right, midRightHasLine2 ? h / 2 - 4 - lineH / 2 : h / 2 - 4, "right", colors, cornerFontCss);' +
'    if (midRightHasLine2) drawCornerSlot(ctx, "middleRightLine2Content", "middleRightLine2Color", w - mm.right, h / 2 - 4 + lineH / 2, "right", colors, cornerFontCss);' +
'  }' +
'}' +

// ---- hand preview geometry --------------------------------------------
// A direct port of hand_layer.c's compute_hand_geometry_fp() (and the
// draw passes around it) from the watch's own fixed-point trig into
// plain floating-point canvas math -- there\'s no fixed-point precision
// need here the way the watch has, so every SUBPIXEL_BITS/int64 step
// over there is just its equivalent float multiply/sin/cos here. Kept
// in the exact same per-style order/shape as compute_hand_geometry_fp()
// so the two stay easy to compare side by side; see that function\'s
// own per-style comments in hand_layer.c for what each shape/field
// combination actually means -- not re-explained per style here.
//
// angle is in radians, same clockwise-from-12 convention already used
// elsewhere on this page (see hourAngle/minAngle below): x = cx +
// axial*sin(angle), y = cy - axial*cos(angle), matching
// point_at_axial_fp()\'s own dx/dy exactly.
'function handAxialPoint(cx, cy, angle, axial) {' +
'  return { x: cx + axial * Math.sin(angle), y: cy - axial * Math.cos(angle) };' +
'}' +
'function handPerpOffset(angle, halfW) {' +
'  return { dx: halfW * Math.cos(angle), dy: halfW * Math.sin(angle) };' +
'}' +
// cfg fields here are already SCALED to canvas px by the caller (see
// readHandConfig()\'s own comment) -- this function itself has no idea
// the watch is really 200x228; it just draws in whatever unit its
// inputs are given in, same as compute_hand_geometry_fp() does with
// its own already-fixed-point inputs.
'function computeHandGeometry(cx, cy, angle, cfg) {' +
'  var len = cfg.length, back = -cfg.backOffset, mid = cfg.middleOffset;' +
'  var halfW = Math.max(0.5, cfg.width / 2), halfSW = Math.max(0.5, cfg.secondaryWidth / 2);' +
'  var polys = [], circles = [];' +
'  function axial(a) { return handAxialPoint(cx, cy, angle, a); }' +
'  function perp(hw) { return handPerpOffset(angle, hw); }' +
'  function capsule(innerAx, outerAx, hw, roundCaps) {' +
'    var inner = axial(innerAx), outer = axial(outerAx), p = perp(hw);' +
'    polys.push([' +
'      { x: inner.x - p.dx, y: inner.y - p.dy }, { x: inner.x + p.dx, y: inner.y + p.dy },' +
'      { x: outer.x + p.dx, y: outer.y + p.dy }, { x: outer.x - p.dx, y: outer.y - p.dy }' +
'    ]);' +
'    if (roundCaps) { circles.push({ x: inner.x, y: inner.y, r: hw }); circles.push({ x: outer.x, y: outer.y, r: hw }); }' +
'  }' +
'  function taper(baseAx, hw, tipPoint) {' +
'    var base = axial(baseAx), p = perp(hw);' +
'    polys.push([{ x: base.x - p.dx, y: base.y - p.dy }, { x: base.x + p.dx, y: base.y + p.dy }, tipPoint]);' +
'  }' +

'  switch (cfg.style) {' +
'    case 1: { var outer = axial(len); taper(back, halfW, outer); break; }' + // triangle

'    case 3: {' + // dauphine
'      var effBack = Math.max(cfg.backOffset, cfg.middleOffset);' +
'      var backTip = axial(-effBack), topTip = axial(len), midPt = axial(mid), p = perp(halfW);' +
'      polys.push([backTip, { x: midPt.x - p.dx, y: midPt.y - p.dy }, topTip, { x: midPt.x + p.dx, y: midPt.y + p.dy }]);' +
'      break;' +
'    }' +
'    case 4: {' + // sword
'      var midAx = Math.min(mid, len);' +
'      var top = axial(len), base = axial(back), midPt = axial(midAx);' +
'      var pw = perp(halfW), psw = perp(halfSW);' +
'      polys.push([' +
'        { x: base.x - pw.dx, y: base.y - pw.dy }, { x: midPt.x - psw.dx, y: midPt.y - psw.dy }, top,' +
'        { x: midPt.x + psw.dx, y: midPt.y + psw.dy }, { x: base.x + pw.dx, y: base.y + pw.dy }' +
'      ]);' +
'      break;' +
'    }' +
'    case 5: capsule(mid, len, halfW, true); capsule(back, mid, halfSW, false); break;' + // pomme

'    case 6: {' + // spade
'      capsule(back, len, halfW, true);' +
'      var tip = axial(len);' +
'      circles.push({ x: tip.x, y: tip.y, r: halfSW });' +
'      var centerAx = (len + back) / 2, apexAx = centerAx + mid;' +
'      if (apexAx > len) {' +
'        var apex = axial(apexAx), psw = perp(halfSW);' +
'        polys.push([{ x: tip.x - psw.dx, y: tip.y - psw.dy }, { x: tip.x + psw.dx, y: tip.y + psw.dy }, apex]);' +
'      }' +
'      break;' +
'    }' +
'    case 7: {' + // arrow
'      var tip = axial(len);' +
'      taper(back, halfW, tip);' +
'      var apex = axial(len + mid), psw = perp(halfSW);' +
'      polys.push([{ x: tip.x - psw.dx, y: tip.y - psw.dy }, { x: tip.x + psw.dx, y: tip.y + psw.dy }, apex]);' +
'      break;' +
'    }' +
'    case 8: {' + // leaf
'      var centerAx = (back + len) / 2, peakAx = centerAx + mid;' +
'      if (peakAx < back) peakAx = back;' +
'      if (peakAx > len) peakAx = len;' +
'      var N = 2;' +
'      var haveBack = peakAx > back, haveTip = peakAx < len;' +
'      var plusA = [], minusA = [], plusB = [], minusB = [];' +
'      if (haveBack) {' +
'        for (var k = 1; k <= N; k++) {' +
'          var u = k / (N + 1), ax = back + (peakAx - back) * u, w = halfW * Math.sin(Math.PI / 2 * u);' +
'          var p = axial(ax), pw = perp(w);' +
'          plusA.push({ x: p.x + pw.dx, y: p.y + pw.dy }); minusA.push({ x: p.x - pw.dx, y: p.y - pw.dy });' +
'        }' +
'      }' +
'      if (haveTip) {' +
'        for (var k = 1; k <= N; k++) {' +
'          var v = k / (N + 1), ax = peakAx + (len - peakAx) * v, w = halfW * Math.cos(Math.PI / 2 * v);' +
'          var p = axial(ax), pw = perp(w);' +
'          plusB.push({ x: p.x + pw.dx, y: p.y + pw.dy }); minusB.push({ x: p.x - pw.dx, y: p.y - pw.dy });' +
'        }' +
'      }' +
'      var peakP = axial(peakAx), pPerp = perp(halfW);' +
'      var peakPlus = { x: peakP.x + pPerp.dx, y: peakP.y + pPerp.dy }, peakMinus = { x: peakP.x - pPerp.dx, y: peakP.y - pPerp.dy };' +
'      var pts = [];' +
'      if (haveBack) { pts.push(axial(back)); for (var i = 0; i < N; i++) pts.push(plusA[i]); }' +
'      pts.push(peakPlus);' +
'      if (haveTip) { for (var i = 0; i < N; i++) pts.push(plusB[i]); pts.push(axial(len)); for (var i = N - 1; i >= 0; i--) pts.push(minusB[i]); }' +
'      pts.push(peakMinus);' +
'      if (haveBack) { for (var i = N - 1; i >= 0; i--) pts.push(minusA[i]); }' +
'      polys.push(pts);' +
'      break;' +
'    }' +
'    case 9: {' + // syringe
'      capsule(back, len, halfW, false);' +
'      var cornerAx = len + mid, taperLen = Math.max(0.5, halfW - halfSW), tipAx = cornerAx + taperLen;' +
'      var corner = axial(cornerAx), tip = axial(tipAx), pw = perp(halfW), psw = perp(halfSW);' +
'      polys.push([' +
'        { x: corner.x - pw.dx, y: corner.y - pw.dy }, { x: corner.x + pw.dx, y: corner.y + pw.dy },' +
'        { x: tip.x + psw.dx, y: tip.y + psw.dy }, { x: tip.x - psw.dx, y: tip.y - psw.dy }' +
'      ]);' +
'      break;' +
'    }' +
'    case 10: {' + // serpentine
'      var SEG = 6;' +
'      var amp = Math.max(0, halfSW - halfW);' +
'      var diameter = Math.abs(cfg.middleOffset); if (diameter < 4) diameter = 4;' +
'      var period = diameter * 2, dirSign = cfg.middleOffset < 0 ? -1 : 1, span = len - back;' +
'      var verts = [];' +
'      for (var i = 0; i <= SEG; i++) {' +
'        var sRel = span * i / SEG, ax = back + sRel;' +
'        var ang = (sRel / period) * 2 * Math.PI;' +
'        var dev = amp * Math.sin(ang) * dirSign;' +
'        var p = axial(ax), pd = perp(dev);' +
'        verts.push({ x: p.x + pd.dx, y: p.y + pd.dy });' +
'      }' +
'      for (var i = 0; i < SEG; i++) {' +
'        var a = verts[i], b = verts[i + 1];' +
'        var tx = b.x - a.x, ty = b.y - a.y, tlen = Math.sqrt(tx * tx + ty * ty);' +
'        var ox, oy;' +
'        if (tlen === 0) { var pd = perp(halfW); ox = pd.dx; oy = pd.dy; }' +
'        else { ox = -ty * halfW / tlen; oy = tx * halfW / tlen; }' +
'        polys.push([{ x: a.x - ox, y: a.y - oy }, { x: a.x + ox, y: a.y + oy }, { x: b.x + ox, y: b.y + oy }, { x: b.x - ox, y: b.y - oy }]);' +
'      }' +
'      break;' +
'    }' +
'    case 0: case 2: default: capsule(back, len, halfW, cfg.style === 0); break;' + // dot / square
'  }' +
'  return { polys: polys, circles: circles };' +
'}' +

'function handPathPolygon(ctx, pts) {' +
'  ctx.beginPath();' +
'  ctx.moveTo(pts[0].x, pts[0].y);' +
'  for (var i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y);' +
'  ctx.closePath();' +
'}' +
// Approximates fill_polygon_ring_fp()/fill_circle_ring_fp() (an inward
// polygon/circle offset, filled only between the original boundary and
// that offset) with clip+stroke: clip to the exact shape, then stroke
// its own path at 2x the requested thickness -- centered strokes put
// half their width outside the clip (discarded) and half inside,
// leaving exactly a `thickness`-px ring along the inside of the
// boundary, same as the real inset ring.
'function handFillRing(ctx, pathFn, thicknessPx, color) {' +
'  ctx.save();' +
'  pathFn(); ctx.clip();' +
'  pathFn(); ctx.lineWidth = thicknessPx * 2; ctx.strokeStyle = color; ctx.stroke();' +
'  ctx.restore();' +
'}' +
'function handGeometryPaths(geo) {' +
'  return geo.polys.map(function (pts) { return function (ctx) { handPathPolygon(ctx, pts); }; })' +
'    .concat(geo.circles.map(function (c) { return function (ctx) { ctx.beginPath(); ctx.arc(c.x, c.y, Math.max(0, c.r), 0, 2 * Math.PI); }; }));' +
'}' +
// Mirrors draw_hand_shape_from_geometry(): hollow (thickness<=1 = a
// plain 1px stroke, else the inset-ring approximation above) or a
// solid fill, dithering approximated as ~50% opacity throughout this
// preview (same simplification the old placeholder hand preview always
// than a genuine Bayer stipple).
'function drawHandGeometryFill(ctx, geo, color, translucent, hollow, hollowThicknessPx) {' +
'  ctx.globalAlpha = translucent ? 0.5 : 1;' +
'  ctx.fillStyle = color; ctx.strokeStyle = color; ctx.lineWidth = 1;' +
'  handGeometryPaths(geo).forEach(function (pathFn) {' +
'    if (hollow) {' +
'      if (hollowThicknessPx <= 1) { pathFn(ctx); ctx.lineWidth = 1; ctx.stroke(); }' +
'      else handFillRing(ctx, function () { pathFn(ctx); }, hollowThicknessPx, color);' +
'    } else { pathFn(ctx); ctx.fill(); }' +
'  });' +
'  ctx.globalAlpha = 1;' +
'}' +
// Mirrors draw_hand_outline_from_geometry(): a genuine 1px perimeter
// trace, drawn OUTSIDE hollow\'s own inline ring (this is a completely
// separate pass/setting on the watch -- see HandConfig.outline_enabled).
'function drawHandGeometryOutline(ctx, geo, color, translucent) {' +
'  ctx.globalAlpha = translucent ? 0.5 : 1;' +
'  ctx.strokeStyle = color; ctx.lineWidth = 1;' +
'  handGeometryPaths(geo).forEach(function (pathFn) { pathFn(ctx); ctx.stroke(); });' +
'  ctx.globalAlpha = 1;' +
'}' +
// Mirrors draw_hand_shadow_once_fp(): the same geometry, translated by
// shadowDistancePx in shadowAngleDeg\'s direction (a shared light-
// source angle, not rotated with the hand), filled solid or at reduced
// opacity in black -- ~50%/~25% approximating the real ~50%/~25%
// Bayer-dithered density (see that function\'s own threshold comment).
'function drawHandShadow(ctx, cx, cy, angle, cfg, shadowAngleDeg, shadowTranslucentStyle) {' +
'  if (!cfg.shadowEnabled) return;' +
'  var shadowAngleRad = (shadowAngleDeg || 0) * Math.PI / 180;' +
'  var dx = cfg.shadowDistance * Math.sin(shadowAngleRad), dy = -cfg.shadowDistance * Math.cos(shadowAngleRad);' +
'  var geo = computeHandGeometry(cx + dx, cy + dy, angle, cfg);' +
'  ctx.globalAlpha = shadowTranslucentStyle ? (cfg.translucent ? 0.25 : 0.5) : 1;' +
'  ctx.fillStyle = "#000";' +
'  handGeometryPaths(geo).forEach(function (pathFn) { pathFn(ctx); ctx.fill(); });' +
'  ctx.globalAlpha = 1;' +
'}' +
// 0=main, 1=accent, 2=background, 3=none -- same HandConfig.color enum
// handHourColor/handMinColor/handSecColor use. Returns null for "none"
// so callers can skip the fill entirely (see hand_layer_draw()\'s own
// `if (cfg->color != 3)` gate) instead of silently drawing main color.
'function resolveHandPreviewColor(colorVal, colors) {' +
'  if (colorVal === "1") return colors.accent;' +
'  if (colorVal === "2") return colors.bg;' +
'  if (colorVal === "3") return null;' +
'  return colors.text;' +
'}' +
// Reads one hand\'s full HandConfig, scaled from real watch pixels
// (every slider in the editor is a real on-watch px value, 200x228
// screen) into this preview canvas\'s own smaller pixel space via
// `scale` -- length/width/offsets/shadow distance all need it,
// anything already a ratio/flag/enum doesn\'t. Reads the popup\'s own
// LIVE draft fields (hePopupPrefix) while that hand\'s editor is open
// (s_openHandEditorKind), so every slider drag updates the preview in
// real time -- see s_openHandEditorKind\'s own comment -- and the
// committed handHour*/handMin*/handSec* hidden fields otherwise.
'function readHandConfig(kind, scale) {' +
'  var prefix = (s_openHandEditorKind === kind) ? hePopupPrefix(kind) : heHiddenPrefix(kind);' +
'  function num(field, def) { var el = document.getElementById(prefix + field); var v = el ? parseFloat(el.value) : NaN; return isNaN(v) ? def : v; }' +
'  function str(field, def) { var el = document.getElementById(prefix + field); return el ? el.value : def; }' +
'  function bool(field) {' +
'    var el = document.getElementById(prefix + field);' +
'    if (!el) return false;' +
'    return (el.type === "checkbox") ? el.checked : el.value === "true";' +
'  }' +
'  return {' +
'    style: parseInt(str("Style", "0"), 10) || 0,' +
'    width: num("Width", 12) * scale,' +
'    length: num("Length", 51) * scale,' +
'    backOffset: num("BackOffset", 0) * scale,' +
'    middleOffset: num("MiddleOffset", 0) * scale,' +
'    secondaryWidth: num("SecondaryWidth", 6) * scale,' +
'    color: str("Color", "0"),' +
'    outlineEnabled: bool("OutlineEnabled"),' +
'    outlineColor: str("OutlineColor", "0"),' +
'    translucent: bool("Translucent"),' +
'    shadowEnabled: bool("ShadowEnabled"),' +
'    shadowDistance: num("ShadowDistance", 2) * scale,' +
'    hollow: bool("Hollow"),' +
'    hollowThickness: num("HollowThickness", 1) * scale' +
'  };' +
'}' +
// Full hand draw: shadow, then outline (if enabled), then fill (unless
// color is "none") -- same order/gating as hand_layer_draw() itself.
'function drawHandFull(ctx, cx, cy, angle, cfg, colors, shadowAngleDeg, shadowTranslucentStyle) {' +
'  drawHandShadow(ctx, cx, cy, angle, cfg, shadowAngleDeg, shadowTranslucentStyle);' +
'  var geo = computeHandGeometry(cx, cy, angle, cfg);' +
'  if (cfg.outlineEnabled) {' +
'    drawHandGeometryOutline(ctx, geo, resolveHandPreviewColor(cfg.outlineColor, colors) || colors.text, cfg.translucent);' +
'  }' +
'  var fillColor = resolveHandPreviewColor(cfg.color, colors);' +
'  if (fillColor) drawHandGeometryFill(ctx, geo, fillColor, cfg.translucent, cfg.hollow, cfg.hollowThickness);' +
'}' +

// Loaded lazily and cached per style -- base64 data: URIs decode
// effectively instantly in practice, but img.complete is checked
// before drawing rather than assumed, so a not-yet-ready image is
// simply skipped for this tick (the next one, ~1s later via the
// preview\'s own refresh interval, picks it up once ready) instead of
// drawing nothing or throwing.
'var MARKER_PREVIEW_IMG_CACHE = {};' +
'function getMarkerPreviewImg(styleVal) {' +
'  var src = MARKER_PREVIEW_IMAGES[styleVal];' +
'  if (!src) return null;' +
'  if (!MARKER_PREVIEW_IMG_CACHE[styleVal]) {' +
'    var img = new Image();' +
'    img.src = src;' +
'    MARKER_PREVIEW_IMG_CACHE[styleVal] = img;' +
'  }' +
'  var cached = MARKER_PREVIEW_IMG_CACHE[styleVal];' +
'  return (cached.complete && cached.naturalWidth > 0) ? cached : null;' +
'}' +
'function hexToRgb(hex) {' +
'  var m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex || "");' +
'  if (!m) return { r: 0, g: 0, b: 0 };' +
'  return { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) };' +
'}' +
// Recolors an offscreen copy of the image by directly rewriting pixel
// RGB values while leaving each pixel\'s own alpha untouched -- unlike
// relying on globalCompositeOperation (which isn\'t consistently
// supported across the range of embedded WebViews Pebble phones
// actually ship), this works the same everywhere and mirrors exactly
// what the watch\'s own tint_marker_bitmap() does. Cached per
// style+color combination since it\'s real per-pixel work and the
// preview redraws roughly once a second.
'var MARKER_TINT_CACHE = {};' +
'function getTintedMarkerCanvas(styleVal, tintColor) {' +
'  var cacheKey = styleVal + "|" + tintColor;' +
'  if (MARKER_TINT_CACHE[cacheKey]) return MARKER_TINT_CACHE[cacheKey];' +
'  var img = getMarkerPreviewImg(styleVal);' +
'  if (!img) return null;' +
'  var iw = img.naturalWidth, ih = img.naturalHeight;' +
'  if (!iw || !ih) return null;' +
'  try {' +
'    var off = document.createElement("canvas");' +
'    off.width = iw; off.height = ih;' +
'    var octx = off.getContext("2d");' +
'    octx.drawImage(img, 0, 0, iw, ih);' +
'    var imageData = octx.getImageData(0, 0, iw, ih);' +
'    var rgb = hexToRgb(tintColor);' +
'    var data = imageData.data;' +
'    for (var i = 0; i < data.length; i += 4) {' +
'      if (data[i + 3] > 0) {' +
'        data[i] = rgb.r;' +
'        data[i + 1] = rgb.g;' +
'        data[i + 2] = rgb.b;' +
'      }' +
'    }' +
'    octx.putImageData(imageData, 0, 0);' +
'    MARKER_TINT_CACHE[cacheKey] = off;' +
'    return off;' +
'  } catch (e) {' +
'    return null;' +
'  }' +
'}' +
// Draws the tinted mask stretched to fill (w, h). Returns whether it
// actually drew anything, so the caller can fall back to a
// placeholder when no preview image exists yet for this style.
'function drawTintedMarkerBitmap(ctx, styleVal, w, h, tintColor) {' +
'  var tinted = getTintedMarkerCanvas(styleVal, tintColor);' +
'  if (!tinted) return false;' +
'  ctx.drawImage(tinted, 0, 0, w, h);' +
'  return true;' +
'}' +

// Same getImageData/putImageData recolor approach getTintedMarkerCanvas()
// above already uses, reused here for the font-preview clock images
// drawDigitalPreview() draws -- NOT the save()/globalCompositeOperation
// "source-in"/restore() trick this used to be: that one composites the
// fill against whatever's ALREADY on the live preview canvas at that
// spot (the sky/background, already opaque there by the time this
// runs), not just the freshly-drawn image's own alpha, so it painted a
// solid tintColor-filled rectangle over the whole image's bounding box
// instead of just its glyph pixels -- on a light text color, that's a
// blank-looking rectangle standing in for the clock, exactly what this
// was reported as. Recoloring on an isolated OFFSCREEN canvas first
// (starts fully transparent, nothing else drawn on it to composite
// against) and only then drawing the correctly-recolored RESULT onto
// the live canvas avoids that entirely.
'var FONT_IMG_TINT_CACHE = {};' +
'function getTintedFontImageCanvas(src, tintColor) {' +
'  var cacheKey = src + "|" + tintColor;' +
'  if (FONT_IMG_TINT_CACHE[cacheKey]) return FONT_IMG_TINT_CACHE[cacheKey];' +
'  var img = getCachedImage(src);' +
'  if (!img.complete || !img.naturalWidth) return null;' +
'  var iw = img.naturalWidth, ih = img.naturalHeight;' +
'  try {' +
'    var off = document.createElement("canvas");' +
'    off.width = iw; off.height = ih;' +
'    var octx = off.getContext("2d");' +
'    octx.drawImage(img, 0, 0, iw, ih);' +
'    var imageData = octx.getImageData(0, 0, iw, ih);' +
'    var rgb = hexToRgb(tintColor);' +
'    var data = imageData.data;' +
'    for (var i = 0; i < data.length; i += 4) {' +
'      if (data[i + 3] > 0) {' +
'        data[i] = rgb.r;' +
'        data[i + 1] = rgb.g;' +
'        data[i + 2] = rgb.b;' +
'      }' +
'    }' +
'    octx.putImageData(imageData, 0, 0);' +
'    FONT_IMG_TINT_CACHE[cacheKey] = off;' +
'    return off;' +
'  } catch (e) {' +
'    return null;' +
'  }' +
'}' +

// ---- marker ring preview -------------------------------------------------
// Direct port of background_layer.c's point_on_ring_fp()/point_on_ring():
// blends a point on a circle of radius reach(pct) with a point on a
// screen-proportioned rectangle of the same reach along its dominant
// axis, by eccentricityPct (0=circle, 100=rectangle). w/h here are this
// canvas\'s own pixel dimensions -- the same 200:228 aspect ratio the
// real watch screen has (see the canvas element\'s own width/height), so
// the ratios this function relies on come out identical without any
// separate scale factor the way hand lengths need.
'function markerReach(w, h, pct) {' +
'  var reachMin = Math.min(w / 4, h / 4), reachMax = Math.max(w / 2, h / 2);' +
'  return reachMin + (reachMax - reachMin) * pct / 100;' +
'}' +
'function pointOnRing(cx, cy, w, h, angle, pct, eccentricityPct) {' +
'  var sinV = Math.sin(angle), cosV = Math.cos(angle);' +
'  var reach = markerReach(w, h, pct);' +
'  var circlePt = { x: cx + reach * sinV, y: cy - reach * cosV };' +
'  if (!eccentricityPct) return circlePt;' +
'  var hw = w / 2, hh = h / 2, reachMax = Math.max(hw, hh);' +
'  var rectHw = reach * hw / reachMax, rectHh = reach * hh / reachMax;' +
'  var adx = Math.abs(sinV), ady = Math.abs(cosV);' +
'  var tx = adx === 0 ? Infinity : rectHw / adx, ty = ady === 0 ? Infinity : rectHh / ady;' +
'  var t = Math.min(tx, ty);' +
'  var rectPt = { x: cx + t * sinV, y: cy - t * cosV };' +
'  return {' +
'    x: circlePt.x + (rectPt.x - circlePt.x) * eccentricityPct / 100,' +
'    y: circlePt.y + (rectPt.y - circlePt.y) * eccentricityPct / 100' +
'  };' +
'}' +
// Direct port of draw_ring_mark_fp(): a straight quad from inner to
// outer, with the requested cap style -- 0=dot (round, via circles at
// both ends), 1=line (flush ends), 2=square (ends extended outward by
// their own half-thickness, like stroke-linecap:square), 4=tapered
// (same square-style extension as 2, but innerHalfThick/outerHalfThick
// can differ, turning the quad into a trapezoid -- see
// draw_ring_mark_fp()\'s own comment in background_layer.c for why
// each style besides 4 always passes the same value for both).
'function drawRingMark(ctx, inner, outer, angle, innerHalfThick, outerHalfThick, style, color, translucent) {' +
'  ctx.globalAlpha = translucent ? 0.5 : 1;' +
'  ctx.fillStyle = color;' +
'  var sinV = Math.sin(angle), cosV = Math.cos(angle);' +
'  if (inner.x === outer.x && inner.y === outer.y) {' +
'    ctx.beginPath(); ctx.arc(inner.x, inner.y, outerHalfThick, 0, 2 * Math.PI); ctx.fill();' +
'    ctx.globalAlpha = 1; return;' +
'  }' +
'  var innerDxW = innerHalfThick * cosV, innerDyW = innerHalfThick * sinV;' +
'  var outerDxW = outerHalfThick * cosV, outerDyW = outerHalfThick * sinV;' +
'  var a = inner, b = outer;' +
'  if (style === 2 || style === 4) {' +
'    var innerEx = innerHalfThick * sinV, innerEy = innerHalfThick * cosV;' +
'    var outerEx = outerHalfThick * sinV, outerEy = outerHalfThick * cosV;' +
'    a = { x: inner.x - innerEx, y: inner.y + innerEy }; b = { x: outer.x + outerEx, y: outer.y - outerEy };' +
'  }' +
'  ctx.beginPath();' +
'  ctx.moveTo(a.x - innerDxW, a.y - innerDyW); ctx.lineTo(a.x + innerDxW, a.y + innerDyW);' +
'  ctx.lineTo(b.x + outerDxW, b.y + outerDyW); ctx.lineTo(b.x - outerDxW, b.y - outerDyW); ctx.closePath();' +
'  ctx.fill();' +
'  if (style === 0) {' +
'    ctx.beginPath(); ctx.arc(inner.x, inner.y, innerHalfThick, 0, 2 * Math.PI); ctx.fill();' +
'    ctx.beginPath(); ctx.arc(outer.x, outer.y, outerHalfThick, 0, 2 * Math.PI); ctx.fill();' +
'  }' +
'  ctx.globalAlpha = 1;' +
'}' +
// Direct port of draw_marker_ring() (no startup-animation handling --
// nothing here to preview, the watch\'s own marks always settle at
// their final positions almost immediately). cfg: {style, thickness,
// innerThickness, innerEccentricity, outerEccentricity, innerBorderPct,
// outerBorderPct, translucent, color}. skipStep mirrors the C call\'s
// own use (5, for the 60-mark second ring, to skip the marks that
// coincide with hour positions) -- 0 means "skip none".
'function drawMarkerRing(ctx, cx, cy, w, h, cfg, marks, skipStep, colors) {' +
'  if (!cfg || !cfg.thickness) return;' +
'  var color = resolveHandPreviewColor(String(cfg.color), colors) || colors.text;' +
'  var innerPct = cfg.innerBorderPct, outerPct = Math.max(cfg.innerBorderPct, cfg.outerBorderPct);' +
'  var outerHalfThick = Math.max(0.5, cfg.thickness / 2);' +
'  var innerHalfThick = cfg.style === 4 ? Math.max(0.5, (cfg.innerThickness || 0) / 2) : outerHalfThick;' +
'  for (var i = 0; i < marks; i++) {' +
'    if (skipStep > 0 && i % skipStep === 0) continue;' +
'    var angle = i * 2 * Math.PI / marks;' +
'    var outer = pointOnRing(cx, cy, w, h, angle, outerPct, cfg.outerEccentricity);' +
'    var inner = pointOnRing(cx, cy, w, h, angle, innerPct, cfg.innerEccentricity);' +
'    drawRingMark(ctx, inner, outer, angle, innerHalfThick, outerHalfThick, cfg.style, color, cfg.translucent);' +
'  }' +
'}' +
// Matches MARKER_STYLE_HOUR_PRESETS/MARKER_STYLE_SECOND_PRESETS in
// background_layer.c exactly (style/thickness/eccentricity/border by
// index 0=minimal, 1=small, 2=big) -- used for bigAnalogMarkerStyle 0-2.
'var MARKER_STYLE_HOUR_PRESETS = [' +
'  { style: 1, Thickness: 2, innerThickness: 2, innerEccentricity: 100, outerEccentricity: 100, innerBorderPct: 66, outerBorderPct: 100, translucent: false, color: 0 },' +
'  { style: 1, Thickness: 3, innerThickness: 3, innerEccentricity: 100, outerEccentricity: 100, innerBorderPct: 48, outerBorderPct: 100, translucent: false, color: 0 },' +
'  { style: 2, Thickness: 7, innerThickness: 1, innerEccentricity: 70, outerEccentricity: 100, innerBorderPct: 75, outerBorderPct: 100, translucent: false, color: 0 },' +
'  { style: 4, Thickness: 10, innerThickness: 3, innerEccentricity: 0, outerEccentricity: 100, innerBorderPct: 69, outerBorderPct: 100, translucent: false, color: 0 }' +
'];' +
'var MARKER_STYLE_SECOND_PRESETS = [' +
'  { style: 0, thickness: 0, innerThickness: 1, innerEccentricity: 100, outerEccentricity: 100, innerBorderPct: 73, outerBorderPct: 100, translucent: false, color: 0 },' +
'  { style: 2, thickness: 1, innerThickness: 1, innerEccentricity: 100, outerEccentricity: 100, innerBorderPct: 73, outerBorderPct: 100, translucent: false, color: 0 },' +
'  { style: 2, thickness: 1, innerThickness: 1, innerEccentricity: 70, outerEccentricity: 75, innerBorderPct: 75, outerBorderPct: 80, translucent: false, color: 0 },' +
'  { style: 2, thickness: 1, innerThickness: 1, innerEccentricity: 0, outerEccentricity: 0, innerBorderPct: 73, outerBorderPct: 80, translucent: false, color: 0 }' +
'];' +
// Reads the customHour*/customSec* hidden fields into a cfg object in
// drawMarkerRing()\'s own shape, same field-per-field mapping
// customMarkerHiddenInputsHtml() writes them with.
'function readCustomMarkerConfig(kind) {' +
'  var p = "custom" + (kind === "hour" ? "Hour" : "Sec");' +
'  function v(f) { var el = document.getElementById(p + f); return el ? el.value : null; }' +
'  return {' +
'    style: parseInt(v("Style"), 10) || 0,' +
'    thickness: parseFloat(v("Thickness")) || 0,' +
'    innerThickness: parseFloat(v("InnerThickness")) || 0,' +
'    innerEccentricity: parseFloat(v("InnerEcc")) || 0,' +
'    outerEccentricity: parseFloat(v("OuterEcc")) || 0,' +
'    innerBorderPct: parseFloat(v("InnerBorder")) || 0,' +
'    outerBorderPct: parseFloat(v("OuterBorder")) || 0,' +
'    translucent: v("Translucent") === "true",' +
'    color: parseInt(v("Color"), 10) || 0' +
'  };' +
'}' +
// Direct port of draw_text_markers() -- only ever called for the
// custom marker style (bigAnalogMarkerStyle 8), same as on the watch.
// hourCfg/secCfg are whichever MarkerRingConfig-shaped objects the
// caller is currently using for those two rings (preset or custom),
// since text markers piggyback on that ring\'s own innerBorderPct/
// innerEccentricity to decide where the numeral sits.
'function romanNumeral(num) {' +
'  if (num <= 0) return String(num);' +
'  var VALUES = [50, 40, 10, 9, 5, 4, 1], SYMBOLS = ["L", "XL", "X", "IX", "V", "IV", "I"];' +
'  var out = "";' +
'  for (var i = 0; i < VALUES.length && num > 0; i++) { while (num >= VALUES[i]) { out += SYMBOLS[i]; num -= VALUES[i]; } }' +
'  return out;' +
'}' +
'function drawTextMarkers(ctx, cx, cy, w, h, colors) {' +
'  var target = document.getElementById("markerTextTarget").value;' +
'  if (target === "0") return;' +
'  var isHour = target === "1";' +
'  var ring = isHour ? readCustomMarkerConfig("hour") : readCustomMarkerConfig("sec");' +
'  var mask = parseInt(document.getElementById(isHour ? "markerTextHourMask" : "markerTextSecMask").value, 10) || 0;' +
'  if (!mask) return;' +
'  var fontSel = document.getElementById("markerTextFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var roman = document.getElementById("markerTextRoman").checked;' +
'  var offsetPx = parseFloat(document.getElementById("markerTextOffset").value) || 0;' +
'  var scale = w / 200;' +
'  ctx.font = canvasFontFor(opt.getAttribute("data-preview") || "", 14 * scale, fontSel.value);' +
'  ctx.fillStyle = colors.text;' +
'  ctx.textAlign = "center"; ctx.textBaseline = "middle";' +
'  for (var i = 0; i < 12; i++) {' +
'    if (!(mask & (1 << i))) continue;' +
'    var angle = i * 2 * Math.PI / 12;' +
'    var offsetPct = Math.max(0, Math.min(100, ring.innerBorderPct + offsetPx));' +
'    var pos = pointOnRing(cx, cy, w, h, angle, offsetPct, ring.innerEccentricity);' +
'    var label = isHour ? (i === 0 ? 12 : i) : i * 5;' +
'    var text = roman ? (label > 0 ? romanNumeral(label) : "") : String(label);' +
'    ctx.fillText(text, pos.x, pos.y);' +
'  }' +
'}' +

'function drawBigAnalogPreview(ctx, colors, now, showSeconds, w, h, markerImageDrawn) {' +
'  var cx = w / 2, cy = h / 2;' +
'  var scale = w / 200;' + // every hand/shadow dimension below is a real on-watch px value (200x228 screen) -- see readHandConfig()'s own comment
'  var markerStyle = parseInt(document.getElementById("bigAnalogMarkerStyle").value, 10);' +

//'  if (markerStyle <= 2) {' +
//'    var idx = markerStyle;' +
//'    drawMarkerRing(ctx, cx, cy, w, h, MARKER_STYLE_SECOND_PRESETS[idx], 60, 5, colors);' +
//'    drawMarkerRing(ctx, cx, cy, w, h, MARKER_STYLE_HOUR_PRESETS[idx], 12, 0, colors);' +
//'  } else if (markerStyle === 9) {' +
//'    var idx = markerStyle;' +
//'    drawMarkerRing(ctx, cx, cy, w, h, MARKER_STYLE_SECOND_PRESETS[3], 60, 5, colors);' +
//'    drawMarkerRing(ctx, cx, cy, w, h, MARKER_STYLE_HOUR_PRESETS[3], 12, 0, colors);' +
//'  } else if (markerStyle === 8) {' +
'  if (markerStyle <= 2 || markerStyle === 8 || markerStyle === 9) {' +
'    var secCfg = readCustomMarkerConfig("sec"), hourCfg = readCustomMarkerConfig("hour");' +
'    drawMarkerRing(ctx, cx, cy, w, h, secCfg, 60, 5, colors);' +
'    drawMarkerRing(ctx, cx, cy, w, h, hourCfg, 12, 0, colors);' +
'    drawTextMarkers(ctx, cx, cy, w, h, colors);' +
'  } else if (!markerImageDrawn) {' +
'    ctx.font = "10px sans-serif";' +
'    ctx.fillStyle = colors.text;' +
'    ctx.textAlign = "center"; ctx.textBaseline = "middle";' +
'    ctx.fillText("(bitmap markers)", cx, 12);' +
'  }' +

'  var hh = now.getHours() % 12, mm = now.getMinutes(), ss = now.getSeconds();' +
'  var hourAngle = ((hh * 60 + mm) / 720) * 2 * Math.PI;' +
'  var minAngle = (mm / 60) * 2 * Math.PI;' +
'  var secAngle = (ss / 60) * 2 * Math.PI;' +
'  var shadowAngleDeg = parseFloat(document.getElementById("shadowAngle").value) || 0;' +
'  var shadowTranslucentStyle = document.getElementById("shadowTranslucent").value !== "false";' +
'  drawHandFull(ctx, cx, cy, hourAngle, readHandConfig("hour", scale), colors, shadowAngleDeg, shadowTranslucentStyle);' +
'  drawHandFull(ctx, cx, cy, minAngle, readHandConfig("min", scale), colors, shadowAngleDeg, shadowTranslucentStyle);' +
'  if (showSeconds) {' +
'    drawHandFull(ctx, cx, cy, secAngle, readHandConfig("sec", scale), colors, shadowAngleDeg, shadowTranslucentStyle);' +
'  }' +

'  var ccRadius = (parseFloat(document.getElementById("centerCircleRadius").value) || 0) * scale;' +
'  if (ccRadius > 0) {' +
'    var ccColor = resolveHandPreviewColor(document.getElementById("centerCircleColor").value, colors) || colors.text;' +
'    ctx.fillStyle = ccColor;' +
'    ctx.beginPath(); ctx.arc(cx, cy, ccRadius, 0, 2 * Math.PI); ctx.fill();' +
'  }' +
'}' +

'var PREVIEW_IMAGE_CACHE = {};' +
// Cached Image objects for the font-preview PNGs (see FONT_PREVIEW_IMAGES\'s
// own comment) -- data-URIs decode almost instantly, but img.complete can
// still read false for the very first frame drawn right after creating
// one, so this triggers a follow-up updatePreview() once it\'s actually
// ready rather than leaving the fallback text rendering up for a whole
// second until the next scheduled tick.
'function getCachedImage(src) {' +
'  var img = PREVIEW_IMAGE_CACHE[src];' +
'  if (!img) {' +
'    img = new Image();' +
'    img.onload = function () { updatePreview(); };' +
'    img.src = src;' +
'    PREVIEW_IMAGE_CACHE[src] = img;' +
'  }' +
'  return img;' +
'}' +
// Direct port of digital_clock_area() in features_layer.c -- how far
// the digital clock text (and the single always-on "bottom" feature
// under it) shifts away from whichever side column(s) are active, in
// this canvas\'s own pixel space (see readHandConfig()\'s own `scale`
// comment for why that conversion is needed at all).
'function digitalClockArea(digitalSidesVal, w) {' +
'  var boxW = 68 * (w / 200);' +
'  if (digitalSidesVal === "right") return { x: 0, w: w - boxW };' +
'  if (digitalSidesVal === "left") return { x: boxW, w: w - boxW };' +
'  return { x: 0, w: w };' +
'}' +
// Digital mode\'s own reuse of the 8 edge-middle content fields as up
// to 2 three-line side columns plus a single bottom feature -- direct
// port of features_recompute_layout()\'s own digital-mode block in
// features_layer.c (see SLOT_DEFS\' own comment above for exactly which
// underlying element each row reads/writes). Row Y positions are the
// real on-watch box_y values that block computes (screen_h(228) -
// CORNER_ROW_H(24) - CORNER_INSET_PX(4) - bottom_shift, for bottom_shift
// 48/24/0), scaled into canvas px the same way every other geometry
// helper on this page already does.' +
// isTop (Digital top layout) mirrors every one of these rows vertically
// around the panel\'s own two edges rather than recomputing anything from
// scratch: row 1 (nearest the clock) stays anchored to whichever edge is
// actually adjacent to the clock/sky boundary -- the panel\'s own BOTTOM
// now, instead of its top -- and rows 2/3 step AWAY from the clock (up
// toward the screen\'s real top edge) instead of down toward it, the
// same CORNER_ROW_H-sized steps features_recompute_layout()\'s own
// top_offset branch uses on the watch for this same layout.' +
'function drawDigitalSideFeatures(ctx, w, h, colors, clockArea, isTop) {' +
'  var avail = computeSlotAvailability();' +
'  var scale = w / 200;' +
'  var xInset = 5 * scale;' +
'  var panelH = 76 * scale;' +
'  var innerEdgeY = isTop ? panelH : 152 * scale;' +
'  var stepDir = isTop ? -1 : 1;' +
'  var row1Y = innerEdgeY, row2Y = innerEdgeY + stepDir * 24 * scale, row3Y = innerEdgeY + stepDir * 48 * scale;' +
// Same real-font/real-size fix as drawCornersAndEdges() (Analog/Digital
// bottom layouts) below -- see that function's own comment for why
// `height` (not `sizePx`) and this same clamp/scale.
'  var cornerFontSel = document.getElementById("cornerFont");' +
'  var cornerOpt = cornerFontSel.options[cornerFontSel.selectedIndex];' +
'  var cornerEntry = fontLookupEntry(cornerFontSel.value);' +
'  var cornerPx = Math.max(7, Math.min(22, Math.round(cornerEntry.height * scale)));' +
'  var cornerFontCss = canvasFontFor(cornerOpt.getAttribute("data-preview") || "", cornerPx, cornerFontSel.value);' +
'  if (avail.digitalLeft) {' +
'    drawCornerSlot(ctx, "middleLeftLine1Content", "middleLeftLine1Color", xInset, row1Y, "left", colors, cornerFontCss);' +
'    drawCornerSlot(ctx, "middleLeftLine2Content", "middleLeftLine2Color", xInset, row2Y, "left", colors, cornerFontCss);' +
'  }' +
'  if (avail.digitalRight) {' +
'    drawCornerSlot(ctx, "middleRightLine1Content", "middleRightLine1Color", w - xInset, row1Y, "right", colors, cornerFontCss);' +
'    drawCornerSlot(ctx, "middleRightLine2Content", "middleRightLine2Color", w - xInset, row2Y, "right", colors, cornerFontCss);' +
'  }' +
// Row 3 is independent of whether rows 1/2 (avail.digitalLeft/Right,
// font-width-limited) are even on -- see avail.digitalBottomRow's own
// comment in computeSlotAvailability() -- so it\'s drawn unconditionally
// here rather than nested inside either block above.
'  if (avail.digitalBottomRow) {' +
'    drawCornerSlot(ctx, "upperMiddleLine1Content", "upperMiddleLine1Color", xInset, row3Y, "left", colors, cornerFontCss);' +
'    drawCornerSlot(ctx, "upperMiddleLine2Content", "upperMiddleLine2Color", w - xInset, row3Y, "right", colors, cornerFontCss);' +
'  }' +
'  drawCornerSlot(ctx, "bottomMiddleLine1Content", "bottomMiddleLine1Color", clockArea.x + clockArea.w / 2, row3Y, "center", colors, cornerFontCss);' +
'}' +

'var GRID_WEEKDAY_NAMES = ["SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"];' +
'var GRID_MONTH_NAMES = ["JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"];' +
'function gridOrdinalSuffix(day) {' +
'  var mod100 = day % 100;' +
'  if (mod100 >= 11 && mod100 <= 13) return "TH";' +
'  switch (day % 10) { case 1: return "ST"; case 2: return "ND"; case 3: return "RD"; default: return "TH"; }' +
'}' +
// Same 4x4 character grid as grid_display.c's own grid_display_refresh()
// -- HH/MM digits, first 4 letters of the weekday, day-of-month + ordinal
// suffix, first 4 letters of the month -- drawn with the selected clock
// font's own CSS approximation (no baked font-image path here, unlike
// drawDigitalPreview, since there\'s no single "12:34" image that would
// help a 16-character grid).' +
'function drawGridPreview(ctx, colors, now, w, h) {' +
'  var fontSel = document.getElementById("clockFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var entry = fontLookupEntry(fontSel.value);' +
'  var scale = w / 200;' +
'  var padSides = 10 * scale, padTopBottom = 24 * scale;' +
'  var gridW = w - 2 * padSides, gridH = h - 2 * padTopBottom;' +
'  var cellW = gridW / 4, cellH = gridH / 4;' +
'  var fontPx = Math.max(10, Math.round(entry.sizePx * scale));' +
'  ctx.font = canvasFontFor(opt.getAttribute("data-preview") || "", fontPx, fontSel.value);' +
'  ctx.fillStyle = colors.text;' +
'  ctx.textAlign = "center"; ctx.textBaseline = "middle";' +
'  var hh = now.getHours(), mm = now.getMinutes();' +
'  var row0 = ("0" + hh).slice(-2) + ("0" + mm).slice(-2);' +
'  var row1 = (GRID_WEEKDAY_NAMES[now.getDay()] + "    ").slice(0, 4);' +
'  var day = now.getDate();' +
'  var row2 = ("0" + day).slice(-2) + gridOrdinalSuffix(day);' +
'  var row3 = (GRID_MONTH_NAMES[now.getMonth()] + "    ").slice(0, 4);' +
'  var rows = [row0, row1, row2, row3];' +
'  for (var r = 0; r < 4; r++) {' +
'    for (var c = 0; c < 4; c++) {' +
'      ctx.fillText(rows[r].charAt(c), padSides + c * cellW + cellW / 2, padTopBottom + r * cellH + cellH / 2);' +
'    }' +
'  }' +
'}' +

// isTop mirrors the 42%-down-the-panel placement into 42%-UP-from-the-
// panel\'s-bottom instead, so the clock still sits right at the edge
// adjacent to the sky in both layouts (the panel\'s own top edge for
// Digital bar, its own bottom edge for Digital top) rather than always
// reading from the panel\'s top regardless of which edge that actually
// is.' +
'function drawDigitalPreview(ctx, colors, now, showSeconds, w, panelTop, panelBottom, clockArea, isTop) {' +
'  var cx = clockArea ? clockArea.x + clockArea.w / 2 : w / 2;' +
'  var fontSel = document.getElementById("clockFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var hh = now.getHours(), mm = now.getMinutes();' +
'  var txt = (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm;' +
'  if (showSeconds) { var ss = now.getSeconds(); txt += ":" + (ss < 10 ? "0" : "") + ss; }' +
'  var clockY = panelTop + (panelBottom - panelTop) * (isTop ? 0.58 : 0.42);' +
// Used to always draw at a flat 26px no matter which clock font was
// selected -- every font looked the same size here despite genuinely
// ranging ~17-49px on the actual watch (fontPickerPreviewPx()'s own
// comment, just above where it\'s defined, calls this out directly:
// "real on-watch bake size (12-48px)"), which is exactly the
// FONT_LOOKUP `sizePx` field -- not `height`, which for a mainClock
// font is a rough visual-cap-height guess rather than the font\'s real
// export size (see fontLookupEntry() for how the two diverge; drawCornerSlot()\'s
// small-text callers below use `height` instead, for exactly that
// distinction). Scaled into this canvas\'s own pixel space by the same
// w/200 ratio every other geometry helper on this page already uses.
'  var entry = fontLookupEntry(fontSel.value);' +
'  var fontPx = Math.max(10, Math.round(entry.sizePx * (w / 200)));' +
// A real on-watch rendering of this font, when one exists, takes over
// the main preview too -- same FONT_PREVIEW_IMAGES asset the font
// PICKER buttons already use (see fontPreviewInnerHtml()), recolored
// via getTintedFontImageCanvas() (see that function's own comment for
// why an offscreen canvas, not a live-canvas composite trick, is what
// actually works here) rather than CSS mask-image (this is a <canvas>,
// not a DOM element a mask-image could apply to). Only used when
// seconds aren\'t shown -- the baked image is a fixed "12:34", nothing
// it could show a live seconds count with -- the plain font-
// approximation text path below covers that case instead. Sized off
// the same fontPx as the text path (rather than its own flat
// constant) so toggling Show Seconds -- which flips which of these two
// paths draws -- can\'t make the clock visibly jump size.
'  var images = FONT_PREVIEW_IMAGES[fontSel.value];' +
'  var clockImgSrc = images && images.clock;' +
'  var drewImage = false;' +
'  if (clockImgSrc && !showSeconds && !FontManager.customName(fontSel.value)) {' +
'    var tinted = getTintedFontImageCanvas(clockImgSrc, colors.text);' +
'    if (tinted) {' +
'      var targetH = fontPx, targetW = targetH * (tinted.width / tinted.height);' +
'      ctx.drawImage(tinted, cx - targetW / 2, clockY - targetH / 2, targetW, targetH);' +
'      drewImage = true;' +
'    }' +
'  }' +
'  if (!drewImage) {' +
'    ctx.font = canvasFontFor(opt.getAttribute("data-preview") || "", fontPx, fontSel.value);' +
'    ctx.fillStyle = colors.text;' +
'    ctx.textAlign = "center"; ctx.textBaseline = "middle";' +
'    ctx.fillText(txt, cx, clockY);' +
'  }' +
'}' +

'function updatePreview() {' +
'  var canvas = document.getElementById("previewCanvas");' +
'  if (!canvas || !canvas.getContext) return;' +
'  var ctx = canvas.getContext("2d");' +
'  var w = canvas.width, h = canvas.height;' +
'  ctx.clearRect(0, 0, w, h);' +

'  var now = new Date();' +
'  var phase = currentSkyPhase(now);' +
// Night colors only take over the preview when night mode is actually
// on AND the (heuristic, see currentSkyPhase()\'s own comment) current
// phase is night -- matching eclipse_sky_is_bright()\'s own gating on
// the watch, so what the preview shows right now tracks whichever
// scheme the watch itself would actually be using at this moment.
'  var nightActive = document.getElementById("nightEnabled").checked && phase === "night";' +
'  var colors = nightActive ? nightColors() : dayColors();' +
'  var skyMode = document.getElementById("skyMode").value || "0";' +
'  var styleVal = document.getElementById("bottomStyleValue").value;' +
'  var secondsBox = document.getElementById("showSeconds");' +
'  var showSeconds = secondsBox.checked && !secondsBox.disabled;' +

'  if (styleVal === "analog") {' +
'    drawSkyLayer(ctx, 0, 0, w, h, skyMode, phase);' +
'    drawCelestialPreview(ctx, 0, 0, w, h, skyMode, phase, colors, now);' +
'    var markerStyleVal = document.getElementById("bigAnalogMarkerStyle").value;' +
'    var markerStyleInt = parseInt(markerStyleVal, 10);' +
'    var markerImageDrawn = (markerStyleInt >= 3 && markerStyleInt !== 8 && markerStyleInt !== 9) && drawTintedMarkerBitmap(ctx, markerStyleVal, w, h, colors.text);' +
'    drawBigAnalogPreview(ctx, colors, now, showSeconds, w, h, markerImageDrawn);' +
'    drawCornersAndEdges(ctx, w, h, colors, h);' +
'  } else if (styleVal === "digitalTop") {' +
// Mirror image of the Digital bar branch below: sky/celestial fill the
// region BELOW the panel (anchored to the screen's own bottom instead
// of its top -- corner features and any weather/celestial elements
// only ever land there, same "reserved band" idea as bar mode's own
// skyH region, just flipped), the panel's own band gets a gradient-
// only wash (no elements -- see drawSkyLayer's own skipElements
// comment), and it's drawn WITHOUT an opaque fill first (unlike bar
// mode's solid colors.bg rect below) so the gradient shows through
// behind the clock text, matching the transparent panel this layout
// actually draws on the watch.
'    var panelH = Math.round(h * 76 / 228);' +
'    var skyTop = panelH;' +
'    var skyH = h - panelH;' +
'    drawSkyLayer(ctx, 0, skyTop, w, skyH, skyMode, phase, false, 0, h);' +
'    drawCelestialPreview(ctx, 0, skyTop, w, skyH, skyMode, phase, colors, now);' +
'    drawSkyLayer(ctx, 0, 0, w, panelH, skyMode, phase, true, 0, h);' +
'    drawCornersAndEdges(ctx, w, h, colors, h, panelH);' +
'    var digitalSidesValTop = document.getElementById("digitalSides").value;' +
'    var clockAreaTop = digitalClockArea(digitalSidesValTop, w);' +
'    drawDigitalSideFeatures(ctx, w, h, colors, clockAreaTop, true);' +
'    drawDigitalPreview(ctx, colors, now, showSeconds, w, 0, panelH, clockAreaTop, true);' +
'  } else if (styleVal === "bigDigital") {' +
// Full-screen sky, same as Analog -- no separate flat background fill;
// the 4 corners, the 2 reused center features (drawn directly here via
// drawCornerSlot rather than through drawDigitalSideFeatures, which also
// handles the row-1/2 side columns and row-3 left/right pair that don't
// apply to this layout), and the digits themselves all draw on top of it.
'    drawSkyLayer(ctx, 0, 0, w, h, skyMode, phase);' +
'    drawCelestialPreview(ctx, 0, 0, w, h, skyMode, phase, colors, now);' +
'    drawCornersAndEdges(ctx, w, h, colors, h);' +
'    var cornerFontSel = document.getElementById("cornerFont");' +
'    var cornerOpt = cornerFontSel.options[cornerFontSel.selectedIndex];' +
'    var cornerEntry = fontLookupEntry(cornerFontSel.value);' +
'    var cornerScale = w / 200;' +
'    var cornerPx = Math.max(7, Math.min(22, Math.round(cornerEntry.height * cornerScale)));' +
'    var cornerFontCss = canvasFontFor(cornerOpt.getAttribute("data-preview") || "", cornerPx, cornerFontSel.value);' +
'    var bigDigitalInset = 42 * (w / 200);' +
'    drawCornerSlot(ctx, "upperMiddleLine1Content", "upperMiddleLine1Color", w / 2, bigDigitalInset, "center", colors, cornerFontCss);' +
'    drawCornerSlot(ctx, "bottomMiddleLine1Content", "bottomMiddleLine1Color", w / 2, h - bigDigitalInset, "center", colors, cornerFontCss);' +
'    drawDigitalPreview(ctx, colors, now, false, w, 0, h, null, false);' +
'  } else if (styleVal === "grid") {' +
// Same sky/corners/center-feature treatment as Big Digital's own branch
// above -- see that one's comment -- just a 4x4 character grid instead
// of 4 tall digit bitmaps for the clock face itself.
'    drawSkyLayer(ctx, 0, 0, w, h, skyMode, phase);' +
'    drawCelestialPreview(ctx, 0, 0, w, h, skyMode, phase, colors, now);' +
'    drawCornersAndEdges(ctx, w, h, colors, h);' +
'    var gridCornerFontSel = document.getElementById("cornerFont");' +
'    var gridCornerOpt = gridCornerFontSel.options[gridCornerFontSel.selectedIndex];' +
'    var gridCornerEntry = fontLookupEntry(gridCornerFontSel.value);' +
'    var gridCornerScale = w / 200;' +
'    var gridCornerPx = Math.max(7, Math.min(22, Math.round(gridCornerEntry.height * gridCornerScale)));' +
'    var gridCornerFontCss = canvasFontFor(gridCornerOpt.getAttribute("data-preview") || "", gridCornerPx, gridCornerFontSel.value);' +
'    var gridInset = 18 * (w / 200);' +
'    drawCornerSlot(ctx, "upperMiddleLine1Content", "upperMiddleLine1Color", w / 2, gridInset, "center", colors, gridCornerFontCss);' +
'    drawCornerSlot(ctx, "bottomMiddleLine1Content", "bottomMiddleLine1Color", w / 2, h - gridInset, "center", colors, gridCornerFontCss);' +
'    drawGridPreview(ctx, colors, now, w, h);' +
'  } else {' +
'    var skyH = Math.round(h * 152 / 228);' +
'    drawSkyLayer(ctx, 0, 0, w, skyH, skyMode, phase);' +
'    drawCelestialPreview(ctx, 0, 0, w, skyH, skyMode, phase, colors, now);' +
'    drawCornersAndEdges(ctx, w, h, colors, skyH);' +
'    ctx.fillStyle = colors.bg;' +
'    ctx.fillRect(0, skyH, w, h - skyH);' +
'    var digitalSidesVal = document.getElementById("digitalSides").value;' +
'    var clockArea = digitalClockArea(digitalSidesVal, w);' +
'    drawDigitalSideFeatures(ctx, w, h, colors, clockArea);' +
'    drawDigitalPreview(ctx, colors, now, showSeconds, w, skyH, h, clockArea);' +
'  }' +
'}' +
'';
