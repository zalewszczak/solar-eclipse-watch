'use strict';

// Extracted unchanged from config-page.js. It intentionally remains a
// generated-string expression so Pebble PKJS behavior stays identical.
module.exports = function buildPageCss() {
  return (
'<style>' +
'  :root { --page-bg: #f4f4f4; --card-bg: #fff; --text: #222; --text-strong: #333; --text-muted: #666; --text-faint: #888; --text-faint2: #555; --text-disabled: #999; --border: #ccc; --border-light: #eee; --border-lighter: #ddd; --btn-bg: #fafafa; }' +
'  @media (prefers-color-scheme: dark) {' +
'    :root { --page-bg: #1c1c1e; --card-bg: #2c2c2e; --text: #f2f2f2; --text-strong: #e5e5e5; --text-muted: #aaa; --text-faint: #999; --text-faint2: #bbb; --text-disabled: #777; --border: #48484a; --border-light: #3a3a3c; --border-lighter: #545456; --btn-bg: #3a3a3c; }' +
'    .bitmap-marker-img { filter: none; }' +
'    .hand-style-icon-preview img { filter: invert(1); }' + // opposite polarity from the other two -- these are black-ink, not white-ink; see that rule's own comment
'  }' +
'  body { font-family: -apple-system, Helvetica, Arial, sans-serif; margin: 0; padding: 16px 20px 90px; background: var(--page-bg); color: var(--text); }' +
'  html, body { touch-action: manipulation; }' + // belt-and-suspenders alongside the viewport meta tag --
                                                    // some in-app webviews still allow double-tap-to-zoom
                                                    // on individual elements unless this is set too, and a
                                                    // double-tap on a fast-repeating button (the settings
                                                    // and slider buttons below) shouldn't ever zoom the page.
'  button, .mode-btn, .slot-btn, .slider-step-btn { touch-action: manipulation; -webkit-user-select: none; user-select: none; }' +
// min-width: 0 overrides the browser's own default UA-stylesheet
// min-width on <fieldset> (effectively "min-content" -- fieldsets
// refuse to shrink narrower than their own content's natural width by
// default, unlike a plain <div>). Without this, the section header's
// long sub-header text (see .section-legend-sub's own min-width: 0
// comment) still forced the WHOLE fieldset -- and so the whole
// section "button" card -- wider to fit it, even though that span
// itself already had nowrap/overflow/ellipsis set correctly; the
// ellipsis was truncating relative to an already-too-wide box instead
// of the page's actual width. This is the actual root cause; the
// flex-item-level fix wasn\'t enough on its own because the fieldset
// ancestor was the thing refusing to shrink in the first place.
'  fieldset { border: none; background: var(--card-bg); border-radius: 8px; padding: 14px 16px; margin-bottom: 16px; box-shadow: 0 1px 2px rgba(0,0,0,0.08); min-width: 0; }' +
'  legend { font-weight: 600; font-size: 14px; padding: 0; color: var(--text-strong); }' +
'  label { display: block; font-size: 14px; margin: 10px 0 4px; color: var(--text-strong); }' +
'  input[type=text], input[type=number], select { width: 100%; box-sizing: border-box; padding: 8px; font-size: 15px; border: 1px solid var(--border); border-radius: 5px; background: var(--card-bg); color: var(--text); }' +
'  input[disabled], select[disabled] { background: var(--border-light); color: var(--text-disabled); }' +
'  .checkbox-row { display: flex; align-items: center; gap: 10px; }' +
'  .radio-row { display: flex; align-items: center; gap: 10px; margin-top: 6px; }' +
'  input[type=checkbox] { appearance: none; -webkit-appearance: none; width: 30px; height: 30px; flex-shrink: 0; margin: 0; padding: 0; box-sizing: border-box; border: 2px solid var(--border); border-radius: 8px; background: var(--card-bg); position: relative; }' +
'  input[type=checkbox]:checked { background: #ff9200; border-color: #ff9200; }' +
'  input[type=checkbox]:checked::after { content: ""; position: absolute; left: 9px; top: 4px; width: 7px; height: 14px; border: solid #fff; border-width: 0 3px 3px 0; transform: rotate(45deg); }' +
'  input[type=checkbox][disabled] { background: var(--border-light); border-color: var(--border-lighter); }' +
'  input[type=checkbox][disabled]:checked { background: #f0c785; border-color: #f0c785; }' +
'  .help { color: var(--text-faint); font-size: 12px; margin-top: 4px; }' +
'  .radio-row { display: flex; gap: 16px; margin-top: 8px; flex-wrap: wrap; }' +
'  .radio-row label { display: flex; align-items: center; gap: 6px; margin: 0; font-weight: normal; }' +
'  .radio-row input { width: auto; }' +
'  .secondary-btn { width: 100%; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--border-light); border: 1px solid var(--border); border-radius: 8px; margin-top: 12px; }' +
'  .preset-slot-row { display: flex; gap: 6px; align-items: stretch; margin-top: 8px; }' +
'  .preset-apply-btn { flex: 1; box-sizing: border-box; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; text-align: left; }' +
'  .preset-apply-btn:disabled { opacity: 0.45; }' +
'  .preset-icon-btn { width: 44px; flex-shrink: 0; font-size: 18px; background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; color: var(--text-strong); }' +
'  .preset-name-input { flex: 1; box-sizing: border-box; }' +
'  .example-style-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 8px; }' +
'  .example-style-btn { position: relative; aspect-ratio: 200 / 228; box-sizing: border-box; border-radius: 8px; border: 1px solid var(--border); background: var(--btn-bg); overflow: hidden; padding: 0; }' +
'  .example-style-btn:disabled { opacity: 0.4; }' +
'  .example-style-btn img { width: 100%; height: 100%; object-fit: contain; display: block; }' +
'  .example-style-btn-empty { display: flex; align-items: center; justify-content: center; width: 100%; height: 100%; font-size: 13px; font-weight: 600; color: var(--text); }' +
'  .style-picker-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 8px; }' +
'  .style-picker-btn { position: relative; aspect-ratio: 200 / 228; box-sizing: border-box; border-radius: 8px; border: 1px solid var(--border); background: var(--btn-bg); overflow: hidden; padding: 0; }' +
'  .style-picker-btn img { width: 100%; height: 100%; object-fit: contain; display: block; }' +
// The 5 bitmap marker style thumbnails (Modern/Shadow/Tally/Bell/Fancy)
// are the SAME mask art the watch itself tints with the user's main
// color -- i.e. drawn in white on transparent, meant to be recolored
// before display, never shown as-is. Shown as-is here (no tint
// applied, just the raw watch resource -- see MARKER_PREVIEW_IMAGES),
// that reads fine against this page's own dark/night theme (white on
// a dark button background) but is invisible against its light/day
// theme (white on a near-white button background) -- inverted here so
// day mode gets black-on-light instead, and un-inverted back in the
// dark-mode block below so night mode keeps its already-fine look.
// Doesn\'t apply to the 4 procedural marker-preset thumbnails or the 9
// hand-style thumbnails (style-picker-btn img above, unaffected) --
// those are ordinary full-color pictures already visible in both
// themes, not tintable masks.
'  .bitmap-marker-img { filter: invert(1); }' +
'  .style-picker-btn-empty { display: flex; align-items: center; justify-content: center; width: 100%; height: 100%; font-size: 11px; font-weight: 600; color: var(--text); text-align: center; padding: 4px; box-sizing: border-box; }' +
'  .style-picker-btn-cap { position: absolute; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.55); color: #fff; font-size: 10px; font-weight: 700; padding: 3px 2px; text-align: center; line-height: 1.2; }' +
// Hand style selector specifically wants its name at the TOP of the
// image rather than the bottom every other style-picker-btn grid
// (marker/indices style) still uses -- same look, just anchored to
// top:0 instead of bottom:0.
'  .style-picker-btn-cap-top { position: absolute; left: 0; right: 0; top: 0; background: rgba(0,0,0,0.55); color: #fff; font-size: 10px; font-weight: 700; padding: 3px 2px; text-align: center; line-height: 1.2; }' +
'  .style-picker-custom-btn { width: 100%; box-sizing: border-box; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; margin-top: 10px; }' +
'  .style-picker-custom-btn:active { background: var(--border-light); }' +
// Same shape/size as "Custom" above -- reddish hue is purely to signal
// "this turns the feature off" the same way other destructive/off
// controls on this page read, not a different button style.
'  .style-picker-none-btn { color: #b23a3a; border-color: #d99; background: rgba(178,58,58,0.08); }' +
'  .style-picker-none-btn:active { background: rgba(178,58,58,0.16); }' +
// Font picker -- one full-width row per font, left third showing that
// font's own live preview text set in its actual (Google Fonts, where
// available -- see FONT_LOOKUP's own comment) family at a size scaled
// toward its real on-watch bake size, right two-thirds the font's
// plain-text name at the page's normal UI size. Same overall shape as
// .mode-btn-group-vertical's stacked rows, just with an internal
// left/right split instead of plain centered text.
'  .font-picker-btn { display: flex; align-items: stretch; width: 100%; box-sizing: border-box; text-align: left; padding: 0; background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; margin-top: 8px; overflow: hidden; }' +
'  .font-picker-btn:active { background: var(--border-light); }' +
'  .font-picker-btn.selected { border-color: #ff9200; border-width: 2px; }' +
'  .font-picker-preview { flex: 0 0 34%; display: flex; align-items: center; justify-content: center; padding: 10px 4px; box-sizing: border-box; border-right: 1px solid var(--border); overflow: hidden; white-space: nowrap; color: var(--text-strong); line-height: 1.1; }' +
'  .font-picker-name { flex: 1 1 auto; display: flex; align-items: center; padding: 10px 12px; font-size: 13px; font-weight: 600; color: var(--text-strong); box-sizing: border-box; }' +
// Real on-watch renderings (see FONT_PREVIEW_IMAGES's own comment)
// rather than styled text, for the fonts that have one. Unlike
// .bitmap-marker-img/.hand-style-icon-preview img just below (which
// stay plain <img>s, filtered to flip black/white polarity for the
// page's own light/dark mode), these render as a CSS mask instead: the
// source PNG is white ink on transparent (see generate-font-previews.js),
// so using it as a mask and filling with an arbitrary background-color
// lets fontPreviewInnerHtml() paint these in the exact same color as
// every live-Google-Fonts-text preview around them (the current color
// scheme's own text color, not just page-theme black/white) -- a plain
// filter can only flip polarity, not recolor to red/yellow/whatever a
// scheme's text color actually is.
'  .font-preview-img { display: inline-block; width: 100%; height: 100%; -webkit-mask-repeat: no-repeat; mask-repeat: no-repeat; -webkit-mask-position: center; mask-position: center; -webkit-mask-size: contain; mask-size: contain; }' +
// Hand-style icon picker buttons -- same .font-picker-btn/-preview/
// -name shape the font picker uses (left cell + right cell, trigger
// variant included), just a 1/4-3/4 split instead of that one's own
// ~1/3-2/3, per this button\'s own request. The icons themselves are
// plain black-on-transparent PNGs (see generate_hand_style_icons.py)
// -- the OPPOSITE polarity from .font-preview-img/.bitmap-marker-img
// above/below (those are white-on-transparent) -- so they need the
// mirror-image treatment: shown as-is (filter: none) in light mode,
// where black-on-light is already visible, and inverted to white
// only in dark mode, where black-on-dark would otherwise disappear.
// Previously used the same invert(1)-by-default rule as the white-ink
// images, which made these invisible in light mode instead (black
// inverted to white, on a light page background) -- fixed below; see
// the dark-mode override further up for the other half of this.
'  .hand-style-icon-preview { flex: 0 0 25%; }' +
'  .hand-style-icon-preview img { max-width: 100%; max-height: 100%; display: block; filter: none; }' +
// Weather icon style previews -- unlike the plain black-on-transparent
// hand-style icons just above, these are actual on-watch artwork (a
// real image with its own colors/background, per features_layer.c's
// own comment on how these are authored), so shown as-is here with no
// invert-for-dark-mode filter -- inverting real artwork would misrepresent
// what it actually looks like rather than just adapting a silhouette's
// polarity to the theme.
'  .weather-icon-style-preview { flex: 0 0 25%; }' +
// Fixed pixel cap rather than max-width/max-height:100% -- percentages
// here resolve against this flex cell's own auto height, which is
// itself set BY the image (align-items:center row, nothing else
// tall in it), so "100%" was really "however big the source PNG
// happens to be" (64x48 native, see generate-weather-icon-previews.js)
// with nothing actually constraining it. That's what made the icons
// -- and with them the whole button row, padding included -- too
// big. A real pixel size keeps every button the same compact height
// no matter what resolution the 3 source icons are authored at.
'  .weather-icon-style-preview img { width: 28px; height: 28px; object-fit: contain; display: block; image-rendering: pixelated; }' +
// The always-visible trigger button that replaces each plain <select>
// -- looks like one .font-picker-btn row (so the CURRENTLY chosen
// font is already shown in its own real typeface before the popup
// even opens), just outside the grid and with its own top margin
// matching where the <select> it replaces used to sit.
'  .font-picker-trigger { margin-top: 6px; }' +
// Color preset picker -- one full-width button per COLOR_SCHEMES entry
// (see renderColorPresetGrid()'s own comment), the button's own
// background set to that preset's actual background color so it reads
// as a real preview, not just a swatch next to text. "selected" (the
// one preset -- if any -- whose 3 colors exactly match the current
// ones, see matchingPresetId()) gets a visibly pressed-in look
// regardless of how light or dark that particular preset\'s own
// background happens to be.
'  .color-preset-btn { display: block; width: 100%; text-align: left; padding: 10px 14px; border: 1px solid var(--border); border-radius: 8px; margin-top: 8px; box-sizing: border-box; }' +
'  .color-preset-btn.selected { border: 2px solid #ff9200; box-shadow: inset 0 2px 4px rgba(0,0,0,0.35); }' +
'  .color-preset-main-line { display: block; font-size: 15px; font-weight: 700; }' +
'  .color-preset-accent-line { display: block; font-size: 12px; font-weight: 600; margin-top: 2px; }' +
// The trigger itself needs to visibly read as "opens something" --
// unlike a plain .color-preset-btn grid item (which IS the
// destination, not a door to one), it shows the CURRENT state filling
// its whole background/text, which on its own looked identical to a
// plain non-interactive status readout. A trailing chevron (the same
// "&rsaquo;" affordance every other button-that-opens-a-popup in this
// file already uses -- see .marker-edit-btn) fixes that; needs its own
// flex row (text block on the left, chevron pinned right) since the
// plain .color-preset-btn layout above is just a stacked block with no
// room reserved for one.
'  .color-preset-trigger { margin-top: 6px; display: flex; align-items: center; justify-content: space-between; gap: 10px; }' +
'  .color-preset-trigger-text { display: flex; flex-direction: column; min-width: 0; }' +
'  .color-preset-chevron { font-size: 24px; font-weight: 700; opacity: 0.5; flex-shrink: 0; }' +
'  .secondary-btn:active { background: var(--border-lighter); }' +
'  .save-bar { position: fixed; left: 0; right: 0; bottom: 0; padding: 12px 20px calc(12px + env(safe-area-inset-bottom, 0px)); background: var(--page-bg); box-shadow: 0 -2px 6px rgba(0,0,0,0.1); }' +
'  .save-bar button { width: 100%; padding: 14px; font-size: 16px; font-weight: 600; color: #fff; background: #ff9200; border: none; border-radius: 8px; }' +
'  .save-bar button:active { background: #e08300; }' +
'  #topBar { position: fixed; top: 0; left: 0; right: 0; max-height: 25vh; overflow: hidden; background: var(--page-bg); box-shadow: 0 2px 6px rgba(0,0,0,0.12); display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 10px 14px; padding-top: calc(10px + env(safe-area-inset-top, 0px)); box-sizing: border-box; z-index: 50; }' +
'  .top-bar-left { display: flex; flex-direction: column; justify-content: center; flex: 0 1 66%; min-width: 0; }' +
'  .top-bar-actions { display: flex; gap: 6px; align-items: center; }' +
'  .back-btn { padding: 6px 10px; font-size: 13px; font-weight: 600; color: var(--text-strong); background: var(--card-bg); border: 1px solid var(--border); border-radius: 6px; }' +
'  .back-btn:active { background: var(--border-light); }' +
'  .donate-btn { padding: 6px 10px; font-size: 13px; font-weight: 700; color: #fff; background: linear-gradient(135deg, #ffb347, #ff8c00); border: none; border-radius: 6px; box-shadow: 0 1px 3px rgba(255,140,0,0.5); }' +
'  .donate-btn:active { filter: brightness(0.92); }' +
'  .top-bar-title { font-size: 15px; font-weight: 700; margin-top: 6px; color: var(--text); white-space: nowrap; }' +
'  .top-bar-desc { font-size: 10px; line-height: 1.3; color: var(--text-muted); margin-top: 3px; }' +
'  .top-bar-preview { flex: 0 1 33%; display: flex; justify-content: center; align-items: center; min-width: 0; height: 100%; max-height: calc(25vh - 20px); padding: 1%; box-sizing: border-box; }' +
'  #previewCanvas { height: 50%; max-height: 50%; width: auto; max-width: 98%; border-radius: 4px; }' +
'  .subsection { margin-top: 10px; padding-top: 10px; border-top: 1px solid var(--border-light); }' +
// Flags whichever of the day/night color sets isn't the one actually
// in effect right now, per the request -- the controls underneath
// stay fully clickable either way (editing the inactive set is a
// completely legitimate thing to do, e.g. setting up night colors in
// the middle of the day); this is purely a visual hint so a change
// that appears to do nothing is immediately explained rather than
// confusing. Both badges are always shown together once night colors
// are on (see updateSchemeActiveHighlight()) -- green "Active now" on
// whichever set is actually in effect, red "Not active now" (the
// .inactive modifier below) on the other -- rather than graying the
// inactive section out, so it stays exactly as legible as the active
// one while still being unambiguous about which is which.
'  .scheme-active-badge { font-size: 11px; font-weight: 700; color: #2e8b3d; background: rgba(46,139,61,0.15); border-radius: 4px; padding: 2px 6px; margin-left: 8px; vertical-align: middle; }' +
'  .scheme-active-badge.inactive { color: #c0392b; background: rgba(192,57,43,0.15); }' +
'  .color-role-buttons { display: flex; gap: 8px; margin-top: 6px; }' +
'  .color-role-btn { flex: 1; display: flex; flex-direction: column; align-items: center; gap: 6px; padding: 8px 4px; border: 1px solid var(--border); border-radius: 8px; background: var(--btn-bg); }' +
'  .color-role-btn:active { background: var(--border-light); }' +
'  .color-role-swatch { width: 36px; height: 36px; border-radius: 50%; border: 2px solid var(--border); box-sizing: border-box; }' +
'  .color-role-label { font-size: 11px; color: var(--text-faint2); }' +
'  .service-status-row { display: flex; align-items: center; gap: 8px; padding: 6px 0; }' +
'  .service-dot { width: 12px; height: 12px; border-radius: 50%; flex: 0 0 auto; border: 1px solid var(--border); }' +
'  .service-dot-gray { background: #999; }' +
'  .service-dot-green { background: #2ecc71; }' +
'  .service-dot-yellow { background: #f1c40f; }' +
'  .service-dot-red { background: #e74c3c; }' +
'  .service-name { flex: 1 1 auto; font-size: 13px; color: var(--text); }' +
'  .service-info-btn { width: 20px; height: 20px; border-radius: 50%; border: 1px solid var(--border); background: var(--btn-bg); color: var(--text-faint2); font-size: 12px; font-style: italic; font-weight: 700; line-height: 18px; text-align: center; padding: 0; flex: 0 0 auto; }' +
'  .service-log-line { font-family: monospace; font-size: 11px; white-space: pre-wrap; word-break: break-word; padding: 3px 0; border-bottom: 1px solid var(--border-light); color: var(--text); }' +
'  .modal-overlay { position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.5); display: none; align-items: flex-end; justify-content: center; z-index: 100; }' +
'  .modal-overlay.open { display: flex; }' +
'  .modal-box { background: var(--card-bg); border-radius: 12px 12px 0 0; padding: 16px; width: 100%; max-width: 400px; max-height: 80vh; box-sizing: border-box; display: flex; flex-direction: column; overflow: hidden; }' +
'  .modal-title { font-weight: 600; font-size: 15px; margin-bottom: 10px; text-align: center; color: var(--text); flex: 0 0 auto; }' +
'  .hand-editor-diagram { width: 100%; height: auto; display: block; border-radius: 8px; border: 1px solid var(--border); margin-bottom: 10px; flex: 0 0 auto; }' +
'  .hand-editor-diagram:not([src]), .hand-editor-diagram[src=""] { display: none; }' +
'  .modal-scroll-body { overflow-y: auto; flex: 1 1 auto; min-height: 0; }' +
'  .modal-footer { flex: 0 0 auto; }' +
'  .hex-grid { position: relative; width: 260px; height: 255px; margin: 0 auto; }' +
'  .hex-swatch { position: absolute; width: 26px; height: 30px; margin: -15px 0 0 -13px; clip-path: polygon(50% 0%, 100% 25%, 100% 75%, 50% 100%, 0% 75%, 0% 25%); border: 1px solid rgba(0,0,0,0.15); box-sizing: border-box; }' +
'  .hex-swatch.hollow { border: none; background: transparent !important; pointer-events: none; }' +
'  .hex-swatch.selected { border: 2px solid #ff9200; }' +
'  .modal-cancel-btn { width: 100%; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--border-light); border: none; border-radius: 8px; margin-top: 12px; }' +
'  .modal-confirm-btn { width: 100%; padding: 12px; font-size: 14px; font-weight: 600; color: #fff; background: #ff9200; border: none; border-radius: 8px; margin-top: 8px; }' +
'  .modal-confirm-btn:active { background: #e08300; }' +
'  .example-style-modal-img { max-width: min(50%, 3cm); width: auto; height: auto; margin: 0 auto; border-radius: 8px; display: block; }' +
'  .example-style-modal-title { font-weight: 700; font-size: 16px; margin-top: 10px; text-align: center; color: var(--text-strong); }' +
'  .mode-btn-group { display: flex; width: 100%; margin-top: 6px; border-radius: 6px; overflow: hidden; border: 1px solid var(--border); box-sizing: border-box; }' +
// 9 icon-only, roughly-square buttons for the slot editor's category
// picker (replacing what used to be a plain <select>). A fixed 44px
// button size (>= this page's other buttons -- .slider-step-btn is
// 34px, .mode-btn\'s own padding alone is 18px plus content) with
// flex-wrap means the browser decides 1 vs 2 rows purely from
// available width: 9 * 44px plus gaps doesn\'t fit the modal\'s own
// max-width (400px) at all, let alone narrower phone screens, so this
// always wraps to two rows in practice -- but stays correct (and
// would use one row) if the modal is ever widened.
'  .category-btn-group { display: flex; flex-wrap: wrap; margin-top: 6px; border: 1px solid var(--border); border-radius: 8px; overflow: hidden; }' +
'  .category-btn { width: 35px; height: 35px; flex: 0 0 auto; display: flex; align-items: center; justify-content: center; padding: 0; box-sizing: border-box; background: var(--btn-bg); border: none; border-right: 1px solid var(--border); border-bottom: 1px solid var(--border); border-radius: 0; margin: 0; color: var(--text-strong); }' +
'  .category-btn:last-child { border-right: none; }' +
'  .category-btn.active { background: #ff9200; border-color: #ff9200; color: #fff; }' +
'  .category-btn svg { width: 21px; height: 21px; pointer-events: none; }' +
'  .mode-btn { flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 4px; padding: 8px 0 10px; font-size: 12px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: none; border-right: 1px solid var(--border); }' +
'  .mode-btn svg { width: 23px; height: 26px; display: block; }' +
'  .mode-btn:last-child { border-right: none; }' +
'  .mode-btn.active { background: #ff9200; color: #fff; box-shadow: inset 0 2px 4px rgba(0,0,0,0.35); }' +
'  .mode-btn-group-vertical { display: flex; flex-direction: column; width: 100%; margin-top: 6px; border-radius: 6px; overflow: hidden; border: 1px solid var(--border); box-sizing: border-box; }' +
'  .mode-btn-vertical { display: block; width: 100%; text-align: left; padding: 10px 12px; font-size: 13px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: none; border-bottom: 1px solid var(--border); box-sizing: border-box; }' +
'  .mode-btn-vertical:last-child { border-bottom: none; }' +
'  .mode-btn-vertical.active { background: #ff9200; color: #fff; box-shadow: inset 0 2px 4px rgba(0,0,0,0.35); }' +
'  .slider-row { margin-top: 12px; }' +
'  .slider-row label { display: flex; justify-content: space-between; font-size: 13px; color: var(--text-faint2); margin-bottom: 2px; }' +
'  .slider-row label .val { font-weight: 700; color: var(--text-strong); }' +
'  input[type=range] { width: 100%; -webkit-appearance: none; appearance: none; height: 30px; background: transparent; margin: 0; }' +
'  input[type=range]::-webkit-slider-runnable-track { height: 6px; border-radius: 3px; background: var(--border-lighter); }' +
'  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 24px; height: 24px; border-radius: 50%; background: #ff9200; border: 2px solid #fff; box-shadow: 0 1px 3px rgba(0,0,0,0.4); margin-top: -9px; }' +
'  input[type=range]::-moz-range-track { height: 6px; border-radius: 3px; background: var(--border-lighter); }' +
'  input[type=range]::-moz-range-thumb { width: 20px; height: 20px; border-radius: 50%; background: #ff9200; border: 2px solid #fff; }' +
'  .mark-btn-grid { display: grid; grid-template-columns: repeat(6, 1fr); gap: 4px; margin-top: 6px; }' +
'  .mark-btn { padding: 8px 0; font-size: 12px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 6px; }' +
'  .mark-btn.active { background: #ff9200; color: #fff; border-color: #ff9200; }' +
'  .raw-log-btn-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 4px; margin-top: 6px; }' +
'  .raw-log-btn { padding: 8px 2px; font-size: 11px; font-weight: 600; font-family: monospace; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 6px; }' +
'  .raw-log-btn.active { background: #ff9200; color: #fff; border-color: #ff9200; }' +
'  .preset-btn-row { display: flex; gap: 6px; margin-top: 8px; }' +
'  .preset-btn-row button { flex: 1; padding: 8px 0; font-size: 11px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 6px; }' +
'  .marker-edit-btn { width: 100%; box-sizing: border-box; padding: 12px; font-size: 14px; font-weight: 600; color: var(--text-strong); background: var(--btn-bg); border: 1px solid var(--border); border-radius: 8px; margin-top: 8px; text-align: left; }' +
'  .marker-edit-btn:disabled { opacity: 0.45; }' +
// Icon + stacked title/sub-header + chevron, replacing what used to
// be a single plain-text line (see sectionLegendHtml() above). Kept
// deliberately tight -- smaller title size, near-zero line-heights,
// and less top/bottom padding than a plain label would get -- so
// adding a whole second (sub-header) line doesn't make the collapsed
// button noticeably taller than it was before; the icon square is
// sized to roughly match that same two-line block's own height rather
// than to any fixed larger "icon size", so the row still reads as one
// compact tappable bar.
'  .section-legend { cursor: pointer; display: flex; align-items: center; gap: 10px; width: 100%; box-sizing: border-box; margin: 0; padding: 4px 0; user-select: none; color: var(--text-strong); }' +
'  .section-icon { width: 30px; height: 30px; border-radius: 8px; flex: 0 0 auto; display: flex; align-items: center; justify-content: center; color: #fff; box-shadow: 0 1px 2px rgba(0,0,0,0.25); }' +
'  .section-icon svg { width: 17px; height: 17px; display: block; }' +
// The one icon that's actually animated (Animation's own) -- spins
// forever, independent of any setting, purely to say "this section is
// about motion" at a glance.
'  @keyframes sectionIconSpin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }' +
'  .section-icon-animated svg { animation: sectionIconSpin 2.2s linear infinite; transform-origin: 50% 50%; }' +
'  .section-legend-text { flex: 1 1 auto; min-width: 0; display: flex; flex-direction: column; justify-content: center; }' +
'  .section-legend-title { font-size: 15px; font-weight: 700; line-height: 1.15; }' +
// Roughly half the title's own font-size, per the request -- hidden
// entirely once the section is expanded (see toggleSection() below)
// since the full controls underneath make it redundant at that point.
// min-width: 0 is needed here (not just on the .section-legend-text
// column flex container above) because flex items default to
// min-width: auto, not 0 -- without overriding that on THIS element
// too, a long sub-header string's own natural nowrap width becomes an
// unshrinkable floor, silently defeating text-overflow: ellipsis and
// forcing the whole header row (and the section toggle "button" it's
// inside) wider than it should be instead of actually clipping.
'  .section-legend-sub { font-size: 8px; line-height: 1.25; color: var(--text-faint); margin-top: 1px; min-width: 0; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }' +
// Colors section sub-header's own "3 dots" (current main/accent/
// background) -- see computeColorsSubheaderHtml() further down.
'  .subhead-dot { display: inline-block; width: 6px; height: 6px; border-radius: 50%; margin-left: 3px; border: 1px solid rgba(0,0,0,0.25); vertical-align: middle; }' +
'  .chevron { display: inline-block; flex: 0 0 auto; font-size: 24px; line-height: 1; transition: transform 0.15s; }' +
'  .chevron.open { transform: rotate(90deg); }' +
'  .slider-with-buttons { display: flex; align-items: center; gap: 8px; }' +
'  .slider-with-buttons input[type=range] { flex: 1; }' +
'  .slider-step-btn { width: 34px; height: 34px; flex-shrink: 0; border-radius: 8px; background: var(--btn-bg); border: 1px solid var(--border); font-size: 20px; font-weight: 700; color: var(--text-strong); line-height: 1; }' +
'  .grayed-out { opacity: 0.4; pointer-events: none; }' +
'  #slotPickerDiagram { position: relative; width: 240px; height: 274px; margin: 10px auto; background: linear-gradient(to bottom, #4a90d9, #bfe3f5); border-radius: 8px; overflow: hidden; }' +
'  .slot-btn { position: absolute; min-width: 54px; padding: 5px 8px; font-size: 11px; font-weight: 700; color: #222; background: rgba(255,255,255,0.85); border: 1px solid rgba(0,0,0,0.2); border-radius: 6px; text-align: center; }' +
'  .slot-btn:active { background: #fff; }' +
'  .slot-btn.slot-off { color: #777; font-weight: 400; }' +
'  .slot-btn.slot-na { color: #aaa; background: rgba(230,230,230,0.7); font-style: italic; pointer-events: none; }' +
'  .slot-corner-tl { left: 6px; top: 6px; }' +
'  .slot-corner-tr { right: 6px; top: 6px; }' +
'  .slot-corner-bl { left: 6px; bottom: 6px; }' +
'  .slot-corner-br { right: 6px; bottom: 6px; }' +
// In digital mode the bottom-left/-right corners need to sit above
// #slotDiagramClockBar (bottom third of the diagram) instead of at the
// diagram's own bottom edge -- they represent the two corner features,
// which stay in the sky area on the real watch, same reasoning as
// DIGITAL_PANEL_H in features_layer.c. Toggled by the same
// renderSlotPicker() call that shows/hides the clock bar itself.
'  .slot-corner-bl.slot-corner-above-bar, .slot-corner-br.slot-corner-above-bar { bottom: calc(33.33% + 6px); }' +
'  .slot-upper-l1 { left: 50%; top: 34px; transform: translateX(-50%); }' +
'  .slot-upper-l2 { left: 50%; top: 62px; transform: translateX(-50%); }' +
'  .slot-bottom-l1 { left: 50%; bottom: 62px; transform: translateX(-50%); }' +
'  .slot-bottom-l2 { left: 50%; bottom: 34px; transform: translateX(-50%); }' +
'  .slot-middle-left-l1 { left: 6px; top: calc(50% - 15px); transform: translateY(-50%); }' +
'  .slot-middle-left-l2 { left: 6px; top: calc(50% + 15px); transform: translateY(-50%); }' +
'  .slot-middle-right-l1 { right: 6px; top: calc(50% - 15px); transform: translateY(-50%); }' +
'  .slot-middle-right-l2 { right: 6px; top: calc(50% + 15px); transform: translateY(-50%); }' +
// The digital clock's own bottom bar -- matches digital_clock_area()/
// the bottom-third panel on the actual watch (see
// unobstructed_change_handler's full_top=152 on a 228px-tall screen,
// i.e. (228-152)/228 = a bit over a third) -- shown only in digital
// mode (see updateSlotDiagramMode()) so this diagram actually
// represents what bottom_style==1 looks like instead of reusing the
// analog sky backdrop for slots that don't live there at all. Sized
// at 33.33% rather than the true ~33.3% recurring fraction purely so
// it lines up with the row of feature buttons stacked inside it
// without any of them crowding its top edge.
'  #slotDiagramClockBar { position: absolute; left: 0; right: 0; bottom: 0; height: 33.33%; background: #000; border-radius: 0 0 8px 8px; display: none; }' +
'  #slotDiagramClockText { position: absolute; left: 50%; top: 8px; transform: translateX(-50%); color: #fff; font-family: "Courier New", monospace; font-size: 22px; font-weight: 700; letter-spacing: 1px; pointer-events: none; }' +
// 3 rows per side, bottom-anchored within the clock bar (row 1 nearest
// the clock/top of the bar, row 3 nearest the screen's bottom edge --
// same ordering as SLOT_LEFT_L1..UPPER_L1/SLOT_RIGHT_L1..UPPER_L2 in
// features_layer.c), plus the single always-on feature centered
// beneath the clock at the very bottom edge, the same width band the
// clock text itself occupies on the watch (digital_clock_area()).
'  .slot-digital-left1 { left: 4px; bottom: 42px; }' +
'  .slot-digital-left2 { left: 4px; bottom: 21px; }' +
'  .slot-digital-left3 { left: 4px; bottom: 2px; }' +
'  .slot-digital-right1 { right: 4px; bottom: 42px; }' +
'  .slot-digital-right2 { right: 4px; bottom: 21px; }' +
'  .slot-digital-right3 { right: 4px; bottom: 2px; }' +
'  .slot-digital-bottom { left: 50%; bottom: 2px; transform: translateX(-50%); min-width: 90px; }' +
'</style></head>'
  );
};
