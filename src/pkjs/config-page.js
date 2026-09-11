/**
 * config-page.js -- builds the settings webview HTML for the classic
 * Pebble "configurable" capability (no Clay, no build-time
 * dependency). PKJS opens this via a data: URI in index.js.
 *
 * The page hands settings back by navigating to a "return URL" --
 * but that URL is *not* a fixed constant. The runtime that opened the
 * page (the real phone app, `pebble emu-app-config`, CloudPebble's
 * emulator, etc.) appends its own `return_to` query parameter to the
 * page URL, and the page is expected to use that value if present,
 * falling back to the legacy `pebblejs://close#` prefix only when
 * it's absent. This is the standard pattern from Pebble's own "App
 * Configuration (manual setup)" guide -- skipping it is why Save can
 * silently do nothing under some runtimes (there's no handler
 * registered for a URL scheme we made up ourselves).
 *
 * Font/colour "previews" here are necessarily approximations -- the
 * watch's actual system fonts (Leco, Roboto subset, Bitham) and
 * custom resource fonts aren't available as web fonts in a phone
 * browser, so each option gets a CSS style chosen to be visually
 * evocative of the real thing rather than pixel-identical to it.
 * Good enough to tell them apart before committing to one. The color
 * scheme preview, however, is exact -- both this page and the watch
 * derive colors the same way (2-bit-per-channel packed bytes), so
 * what you see here is exactly what you'll get.
 */

var servicelog = require('./servicelog.js');
// See presets-lookups.js's own header comment for what lives there and
// why (font metadata, color scheme presets, the corner/edge feature
// content catalogue, hand-style presets) -- required once up top since
// several of these tables (FONT_LOOKUP/CORNER_CATEGORIES especially)
// are used well before HAND_PRESETS' own spot further down.
var PRESETS_LOOKUPS = require('./presets-lookups');
var FONT_LOOKUP = PRESETS_LOOKUPS.FONT_LOOKUP;
var COLOR_SCHEMES = PRESETS_LOOKUPS.COLOR_SCHEMES;
var CORNER_COLOR_MODE_LABELS = PRESETS_LOOKUPS.CORNER_COLOR_MODE_LABELS;
var ROMAN_INCOMPATIBLE_FONTS = PRESETS_LOOKUPS.ROMAN_INCOMPATIBLE_FONTS;
var CORNER_CATEGORIES = PRESETS_LOOKUPS.CORNER_CATEGORIES;
var HAND_PRESETS = PRESETS_LOOKUPS.HAND_PRESETS;

function esc(str) {
  return String(str == null ? '' : str)
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

// Screen-shaped (200x228 -- the real Emery/Pebble Time 2 display
// resolution, so the icon's own proportions match the watch's rather
// than a generic square) line-icons for the 3 clock-layout buttons
// above the section they belong to. All still stroke="currentColor"
// (inherits the button's own text color -- white when active, no
// separate active-state icon needed), except the bottom info band:
// that's a solid currentColor-filled rect with an SVG <mask> punching
// transparent windows where its content sits (the time/date bars,
// the analog clock's face, the 4 info-line rows) -- a genuinely
// unfilled "hole" through to whatever's behind the icon, rather than
// a hardcoded second color, so it reads correctly in both the active
// (orange background) and inactive (light/dark theme background)
// button states without needing to know which. Each mask needs a
// unique id since all 3 icons render on the same page at once.
var MODE_BTN_ICONS = {
  // Top 2/3 blank with a scatter of dots (the sky view), bottom 1/3 a
  // dark info band with two window-cutouts: a thick bar (the time)
  // above a thinner one (the date) -- matches the DIGITAL bottom
  // style's actual layout.
  digital:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="40" cy="30" r="5" fill="currentColor" stroke="none"/>' +
    '<circle cx="100" cy="50" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="150" cy="35" r="5" fill="currentColor" stroke="none"/>' +
    '<circle cx="60" cy="90" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="140" cy="100" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="95" cy="122" r="5" fill="currentColor" stroke="none"/>' +
    '<mask id="modeIconDigitalMask">' +
    '<rect x="4" y="152" width="192" height="72" rx="8" fill="#fff" stroke="none"/>' +
    '<rect x="38" y="168" width="124" height="22" rx="10" fill="#000" stroke="none"/>' +
    '<rect x="58" y="198" width="84" height="10" rx="5" fill="#000" stroke="none"/>' +
    '</mask>' +
    '<rect x="4" y="152" width="192" height="72" rx="8" fill="currentColor" stroke="none" mask="url(#modeIconDigitalMask)"/>' +
    '</svg>',
  // No separate info band at all -- one big analog clock filling
  // nearly the whole screen, plus a couple of stray sky dots, since
  // ANALOG replaces the digital/edge-content area entirely rather
  // than sharing the screen with it the way DIGITAL does.
  analog:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="100" cy="114" r="88" stroke-width="7"/>' +
    '<line x1="100" y1="114" x2="100" y2="55" stroke-width="9"/>' +
    '<line x1="100" y1="114" x2="138" y2="140" stroke-width="9"/>' +
    '<circle cx="60" cy="60" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="150" cy="170" r="4" fill="currentColor" stroke="none"/>' +
    '</svg>',

  // Sun (top) + a solid cloud silhouette (3 overlapping circles over a
  // rounded base -- the standard "puffy cloud" icon shape) + 3 rain
  // lines at 45deg below it + a half-lit moon (bottom-left, via the
  // same mask-punches-a-hole trick as the DIGITAL icon's own info
  // band above, so the "dark" half reads correctly against whatever
  // color is actually behind the icon in each button state).
  skyWeather:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="100" cy="32" r="18" fill="currentColor" stroke="none"/>' +
    '<circle cx="70" cy="100" r="24" fill="currentColor" stroke="none"/>' +
    '<circle cx="100" cy="88" r="30" fill="currentColor" stroke="none"/>' +
    '<circle cx="132" cy="100" r="22" fill="currentColor" stroke="none"/>' +
    '<rect x="52" y="96" width="96" height="32" rx="16" fill="currentColor" stroke="none"/>' +
    '<line x1="75" y1="134" x2="97" y2="156" stroke-width="8"/>' +
    '<line x1="100" y1="134" x2="122" y2="156" stroke-width="8"/>' +
    '<line x1="125" y1="134" x2="147" y2="156" stroke-width="8"/>' +
    '<mask id="modeIconSkyWeatherMoonMask">' +
    '<rect x="24" y="159" width="16" height="32" fill="#fff"/>' +
    '<rect x="40" y="159" width="16" height="32" fill="#000"/>' +
    '</mask>' +
    '<circle cx="40" cy="175" r="16" fill="currentColor" stroke="none" mask="url(#modeIconSkyWeatherMoonMask)"/>' +
    '</svg>',
  // Identical sun/moon placement to skyWeather above (reads as the
  // same sky with the weather stripped out), just missing the cloud
  // and rain entirely -- see this button\'s own request/comment.
  skyClear:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="100" cy="32" r="18" fill="currentColor" stroke="none"/>' +
    '<mask id="modeIconSkyClearMoonMask">' +
    '<rect x="24" y="159" width="16" height="32" fill="#fff"/>' +
    '<rect x="40" y="159" width="16" height="32" fill="#000"/>' +
    '</mask>' +
    '<circle cx="40" cy="175" r="16" fill="currentColor" stroke="none" mask="url(#modeIconSkyClearMoonMask)"/>' +
    '</svg>',
  // Solid black regardless of theme/active state (this is the one icon
  // that deliberately doesn\'t use currentColor for its background --
  // it\'s depicting an actual night sky, not a themeable shape), with a
  // scatter of white dots in a few different sizes for stars. Still
  // gets a currentColor border stroke on top of the black fill so the
  // icon\'s own edge stays visible even against a similarly-dark
  // button background in dark mode.
  skySpace:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20" fill="#000"/>' +
    '<circle cx="30" cy="30" r="3" fill="#fff" stroke="none"/>' +
    '<circle cx="70" cy="24" r="4" fill="#fff" stroke="none"/>' +
    '<circle cx="118" cy="18" r="2" fill="#fff" stroke="none"/>' +
    '<circle cx="160" cy="36" r="5" fill="#fff" stroke="none"/>' +
    '<circle cx="45" cy="70" r="2" fill="#fff" stroke="none"/>' +
    '<circle cx="90" cy="55" r="3" fill="#fff" stroke="none"/>' +
    '<circle cx="150" cy="82" r="4" fill="#fff" stroke="none"/>' +
    '<circle cx="30" cy="120" r="3" fill="#fff" stroke="none"/>' +
    '<circle cx="172" cy="130" r="2" fill="#fff" stroke="none"/>' +
    '<circle cx="60" cy="160" r="4" fill="#fff" stroke="none"/>' +
    '<circle cx="130" cy="175" r="3" fill="#fff" stroke="none"/>' +
    '<circle cx="100" cy="202" r="2" fill="#fff" stroke="none"/>' +
    '<circle cx="168" cy="196" r="3" fill="#fff" stroke="none"/>' +
    '</svg>',

  // Plain sun/moon circles (no half-mask -- this one\'s purely about
  // relative size, not phase) at 4 fixed radii scaled from this app\'s
  // own real on-watch base sizes (SUN_R_NORMAL=20, MOON_R_NORMAL=16 in
  // background_layer.c, times this option\'s own pct, times a constant
  // icon-scale factor of 1.4 to make the smallest size still legible
  // at icon scale) -- same fixed screen-shaped box and fixed sun/moon
  // centers across all 4, so only the circles\' own size changes
  // between buttons, giving a direct size-to-size comparison exactly
  // like the request asks for.
  sunMoon100:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="68" cy="114" r="28" fill="currentColor" stroke="none"/>' +
    '<circle cx="136" cy="114" r="22" fill="currentColor" stroke="none"/>' +
    '</svg>',
  sunMoon75:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="68" cy="114" r="21" fill="currentColor" stroke="none"/>' +
    '<circle cx="136" cy="114" r="17" fill="currentColor" stroke="none"/>' +
    '</svg>',
  sunMoon50:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="68" cy="114" r="14" fill="currentColor" stroke="none"/>' +
    '<circle cx="136" cy="114" r="11" fill="currentColor" stroke="none"/>' +
    '</svg>',
  sunMoon25:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="68" cy="114" r="7" fill="currentColor" stroke="none"/>' +
    '<circle cx="136" cy="114" r="6" fill="currentColor" stroke="none"/>' +
    '</svg>',

  // Same word ("Sun") rendered the same 3 ways label_style actually
  // draws it on the watch (see draw_label() in background_layer.c):
  // Boxed is an opaque box behind the text -- drawn here as a solid
  // currentColor box with the word itself cut out via a mask (the
  // DIGITAL icon\'s own "punch a hole" trick again), since there\'s no
  // one fixed color available to draw literal white text with that
  // would still read correctly on an orange active button. Outlined
  // is heavier/bolder text (a small self-stroke, standing in for a
  // real contrasting outline for the same reason). Soft is thin,
  // reduced-opacity text with neither.
  labelBoxed:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<mask id="modeIconLabelBoxedMask">' +
    '<rect x="50" y="94" width="100" height="40" rx="10" fill="#fff"/>' +
    '<text x="100" y="123" font-size="34" font-weight="700" font-family="sans-serif" text-anchor="middle" fill="#000">Sun</text>' +
    '</mask>' +
    '<rect x="50" y="94" width="100" height="40" rx="10" fill="currentColor" stroke="none" mask="url(#modeIconLabelBoxedMask)"/>' +
    '</svg>',
  labelOutlined:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<text x="100" y="128" font-size="44" font-weight="700" font-family="sans-serif" text-anchor="middle" fill="currentColor" stroke="currentColor" stroke-width="3" paint-order="stroke">Sun</text>' +
    '</svg>',
  labelSoft:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<text x="100" y="128" font-size="38" font-weight="400" font-family="sans-serif" text-anchor="middle" fill="currentColor" opacity="0.5">Sun</text>' +
    '</svg>',
};

// iOS-style "app icon" for each collapsible settings section: a
// rounded-square colour swatch (SECTION_META's own `color`) with a
// small white glyph centered in it (see .section-icon/.section-icon
// svg below), shown to the left of that section's title + one-line
// summary sub-header (see buildSectionLegendHtml() and the
// compute*Subheader() family of functions in the runtime script
// further down -- this constant only supplies the icon, not the sub-
// header text). Keyed by the same short id toggleSection()/the
// section-legend's own onclick already use (examples/style/colors/
// corners/.../testing), not the longer human label, so a header's
// three pieces (legend markup, category-icon-style glyph, sub-header
// updater) never drift out of sync with each other. `weather` and
// `astronomy` deliberately reuse the EXACT same glyph CORNER_CATEGORIES
// (presets-lookups.js) already has for its own "weather"/"astro"
// category buttons (the Features section's own per-category icons)
// rather than new art, so a person already recognizes them by the
// time they reach this section -- read directly off that array below
// instead of a second hand-typed copy, so the two can\'t drift apart.
// `animated: true` (Animation's own entry) spins its glyph forever via
// the .section-icon-animation svg CSS rule -- the one icon here that's
// actually in motion, fittingly.
function sectionCategoryIcon(categoryId) {
  for (var i = 0; i < CORNER_CATEGORIES.length; i++) {
    if (CORNER_CATEGORIES[i].id === categoryId) return CORNER_CATEGORIES[i].icon;
  }
  return '';
}
var SECTION_META = {
  examples:  { color: '#af52de', icon:
    '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 2c.4 3.2 1 4.8 2.2 6S17 9.6 20 10c-3.2.4-4.8 1-6 2.2S12.4 14.8 12 18c-.4-3.2-1-4.8-2.2-6S6.8 10.4 4 10c3.2-.4 4.8-1 6-2.2S11.6 5.2 12 2z"/><path d="M18.5 14.5c.2 1.1.4 1.6.8 2 .4.4.9.6 2 .8-1.1.2-1.6.4-2 .8-.4.4-.6.9-.8 2-.2-1.1-.4-1.6-.8-2-.4-.4-.9-.6-2-.8 1.1-.2 1.6-.4 2-.8.4-.4.6-.9.8-2z"/></svg>' },
  style:     { color: '#5e5ce6', icon:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M19.4 3.6c1 1 1 2.6 0 3.5L11 15.5l-3.2-3.2L16.2 4c1-1 2.5-1 3.5-.4z"/><path d="M9.3 13.7L5.8 17.2c-.7.7-1 1.6-1 2.6 0 .5-.3 1-.9 1.1-.4.1-.8-.2-.7-.6.1-1 .5-1.6 1.1-2.2.8-.8 1.3-1.8 1.4-2.9l.1-1"/></svg>' },
  colors:    { color: '#ff375f', icon:
    '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 2S5.5 11 5.5 15.2A6.5 6.5 0 0 0 12 21.7a6.5 6.5 0 0 0 6.5-6.5C18.5 11 12 2 12 2z"/></svg>' },
  corners:   { color: '#ff9500', icon:
    '<svg viewBox="0 0 24 24" fill="currentColor"><rect x="3" y="3" width="8" height="8" rx="2.2"/><rect x="13" y="3" width="8" height="8" rx="2.2"/><rect x="3" y="13" width="8" height="8" rx="2.2"/><rect x="13" y="13" width="8" height="8" rx="2.2"/></svg>' },
  animation: { color: '#34c759', animated: true, icon:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"><path d="M12 3a9 9 0 1 1 -6.36 2.64"/></svg>' },
  presets:   { color: '#b8860b', icon:
    '<svg viewBox="0 0 24 24" fill="currentColor"><path d="M6.5 2h11a1 1 0 0 1 1 1v18.2c0 .8-.9 1.3-1.6.9L12 18.3l-4.9 3.8c-.7.4-1.6-.1-1.6-.9V3a1 1 0 0 1 1-1z"/></svg>' },
  weather:   { color: '#0a84ff', icon: sectionCategoryIcon('weather') },
  astronomy: { color: '#1c1c3a', icon: sectionCategoryIcon('astro') },
  location:  { color: '#ff3b30', icon:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"><path d="M12 21s7-7.7 7-13a7 7 0 1 0-14 0c0 5.3 7 13 7 13z"/><circle cx="12" cy="8" r="2.6"/></svg>' },
  updates:   { color: '#00c7be', icon:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4.5 12a7.5 7.5 0 0 1 13-5.1M19.5 12a7.5 7.5 0 0 1-13 5.1"/><path d="M17.3 3.8v3.4h-3.4M6.7 20.2v-3.4h3.4"/></svg>' },
  other:     { color: '#5856d6', icon:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M18 16v-5a6 6 0 1 0-12 0v5l-1.8 2.4A1 1 0 0 0 5 20h14a1 1 0 0 0 .8-1.6L18 16z"/><path d="M10 20a2 2 0 0 0 4 0"/></svg>' },
  testing:   { color: '#8e8e93', icon:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><ellipse cx="12" cy="14" rx="5" ry="6" fill="currentColor" stroke="none"/><line x1="12" y1="8" x2="12" y2="20"/><line x1="4.5" y1="11" x2="7.5" y2="10"/><line x1="4.5" y1="14" x2="7.5" y2="14"/><line x1="4.5" y1="18" x2="7.5" y2="17.3"/><line x1="19.5" y1="11" x2="16.5" y2="10"/><line x1="19.5" y1="14" x2="16.5" y2="14"/><line x1="19.5" y1="18" x2="16.5" y2="17.3"/><path d="M9 5.5L10.5 8M15 5.5L13.5 8"/><circle cx="12" cy="5.5" r="1.2" fill="currentColor" stroke="none"/></svg>' }
};

// Builds one section-legend header: the rounded-square icon (from
// SECTION_META), a title + (initially empty, filled in live by
// refreshAllSectionSubheaders() at the bottom of the runtime script)
// one-line summary sub-header stacked underneath it, and the existing
// expand/collapse chevron -- replaces what used to be a single plain-
// text line for every fieldset's own <div class="section-legend">.
// `sectionId` must match both a SECTION_META key and the id
// toggleSection(id)/chev-<id>/section-<id> already use for that
// fieldset, so all the pieces stay wired to the same section.
function sectionLegendHtml(sectionId, title) {
  var meta = SECTION_META[sectionId] || {};
  return (
'    <div class="section-legend" onclick="toggleSection(\'' + sectionId + '\')">' +
'      <span class="section-icon section-icon-' + sectionId + (meta.animated ? ' section-icon-animated' : '') + '" style="background:' + (meta.color || '#999') + ';">' + (meta.icon || '') + '</span>' +
'      <span class="section-legend-text">' +
'        <span class="section-legend-title">' + esc(title) + '</span>' +
'        <span class="section-legend-sub" id="subhead-' + sectionId + '"></span>' +
'      </span>' +
'      <span class="chevron" id="chev-' + sectionId + '">&#9656;</span>' +
'    </div>'
  );
}

// Nested, one-level-down variant of sectionLegendHtml() above -- see
// .subsection-legend's own comment for why this exists separately
// rather than just registering these in SECTION_META. `id` gets
// wired to subsec-<id>/subsecchev-<id>/subsubhead-<id>/subsecpreview-<id>
// (toggleSubsection() below), all independent of the outer Style
// section's own toggleSection('style'); collapsing/expanding one
// doesn't touch the other. previewHtml is whatever the caller wants
// shown in the small thumbnail slot at render time (a static "?" is
// fine here -- refreshHandsIndicesPreviews() below fills in the real
// current-preset thumbnail live, the same "computed after render,
// kept live by the same delegated listeners every other sub-header
// already uses" pattern as refreshAllSectionSubheaders()).
function subsectionLegendHtml(id, title, previewHtml) {
  return (
'    <div class="subsection-legend" onclick="toggleSubsection(\'' + id + '\')">' +
'      <span class="subsection-legend-preview" id="subsecpreview-' + id + '">' + (previewHtml || '') + '</span>' +
'      <span class="subsection-legend-text">' +
'        <span class="subsection-legend-title">' + esc(title) + '</span>' +
'        <span class="subsection-legend-sub" id="subsubhead-' + id + '"></span>' +
'      </span>' +
'      <span class="chevron" id="subsecchev-' + id + '">&#9656;</span>' +
'    </div>'
  );
}

// Base64 data: URIs for each bitmap marker style's preview image,
// generated at build time from the same resource PNGs used on the
// watch itself (resources/images/<name>_background.png -- see
// scripts/generate-marker-previews.js -- this file itself has no way
// to load an external image at runtime, since the whole settings
// page ends up as one self-contained data: URI with no server behind
// it). Missing entries (a style with no PNG provided yet) are handled
// gracefully wherever this is used below, not treated as an error.
var MARKER_PREVIEW_IMAGES = require('./marker-preview-images');

// Base64 data: URIs for the hand style picker popup's 9 buttons
// (resources/infographics/hands<n>.png -- see
// scripts/generate-infographics.js) and the marker style picker
// popup's 4 procedural-preset buttons (marker_preset_<name>.png,
// same script) -- same "generated at build time, embedded gracefully
// missing-or-not" story as MARKER_PREVIEW_IMAGES/EXAMPLE_STYLE_IMAGES
// above and below.
var HAND_STYLE_IMAGES = require('./hand-style-images');
var MARKER_PRESET_IMAGES = require('./marker-preset-images');

// Base64 data: URIs for the hand editor popup's own full-width
// explainer diagram, one per HandConfig.style value (see
// scripts/generate-infographics.js and its own comment) -- distinct
// from HAND_STYLE_IMAGES above, which is the style PICKER popup's
// thumbnail grid, not the editor's in-popup diagram.
var HAND_STYLE_DIAGRAM_IMAGES = require('./hand-style-diagram-images');
// Small unlabeled silhouette icons for the hand-style PICKER's own
// button list (see scripts/generate_hand_style_icons.py and
// scripts/generate-hand-style-icons.js) -- a different image set from
// HAND_STYLE_DIAGRAM_IMAGES just above, which is the full-width
// labeled explainer diagram at the top of the editor popup instead.
// Both are keyed the same way: HandConfig.style id as a string.
var HAND_STYLE_ICON_IMAGES = require('./hand-style-icon-images');
// One representative ("partly cloudy") icon per weather_icon_style
// value, nearest-neighbor upscaled to stay crisp -- see
// scripts/generate-weather-icon-previews.js for how these get made.
var WEATHER_ICON_STYLE_PREVIEWS = require('./weather-icon-style-previews');

// "Example styles" grid (first section on the settings page) -- each
// numbered slot pairs a screenshot (resources/example-styles/<n>.png,
// embedded at build time -- see generate-example-style-previews.js)
// with a hand-authored preset (src/pkjs/example-style-presets.js) in
// exactly the same shape the "Style Presets" section's own export/
// import uses. This single constant controls how many slots exist;
// add a 10th by bumping it, adding resources/example-styles/10.png,
// re-running the generator script, and adding a "10" entry to
// example-style-presets.js -- nothing else here needs to change.
var EXAMPLE_STYLE_COUNT = 9;
var EXAMPLE_STYLE_IMAGES = require('./example-style-images');
var EXAMPLE_STYLE_PRESETS = require('./example-style-presets');

// Sun..Sat, matching struct tm's own tm_wday (0=Sunday) so the watch
// can test "is today's bit set" directly against the mask byte with
// no reindexing -- see hourly_vibe_days_mask's own comment in
// eclipse_data.h. Rendered as a flush 7-button row (renderMarkerStyleGrid()'s
// neighbors aside, this is the one other place a whole button row is
// built at HTML-generation time here rather than client-side).
var HOURLY_VIBE_DAY_LABELS = ['S', 'M', 'T', 'W', 'T', 'F', 'S'];
// quiet_time_is_active() is a real, standard Pebble SDK function
// (pebble.h, present on every platform this app targets) -- gates
// whether the "Override quiet time" checkbox is offered at all, so
// this whole feature degrades gracefully (silently always-on, no
// dead checkbox) if that ever stops being true for some future
// platform this app gets ported to.
var QUIET_TIME_API_AVAILABLE = true;

// Base64 data: URIs for the font picker popup's real on-watch
// renderings of its own sample text -- resources/font-previews/
// <fontId>_<role>.png, see scripts/generate-font-previews.js and its
// own top-of-file comment for why this exists (Gothic/Bitham/LECO
// have no free live-webfont equivalent to preview with) and the exact
// filename convention. Same "generated at build time, embedded
// gracefully missing-or-not" story as MARKER_PREVIEW_IMAGES/
// HAND_STYLE_IMAGES above -- a font/role with no PNG yet just falls
// back to the existing CSS-approximation text preview for that one
// button.
var FONT_PREVIEW_IMAGES = require('./font-preview-images');

// FONT_LOOKUP (required up top, alongside PRESETS_LOOKUPS itself) --
// see presets-lookups.js's own header/FONT_LOOKUP comment for what
// every field means. (Still need to bump FONT_MAX_CONTENT_ID in
// index.js when adding an entry there.)

// Builds one combined Google Fonts stylesheet URL covering every
// `google` family (at its own specific weight/italic) FONT_LOOKUP
// actually uses, deduplicated -- so the settings page only ever makes
// one request instead of one per font. Used for the live font-picker
// previews (see fontPickerModal below) -- purely cosmetic, the watch
// itself never touches Google Fonts.
function googleFontsHref() {
  var seen = {};
  var params = [];
  FONT_LOOKUP.forEach(function (f) {
    if (!f.google) return;
    var italAxis = f.italic ? '1' : '0';
    var key = f.google + '|' + italAxis + '|' + f.weight;
    if (seen[key]) return;
    seen[key] = true;
    params.push('family=' + encodeURIComponent(f.google).replace(/%20/g, '+') + ':ital,wght@' + italAxis + ',' + f.weight);
  });
  return 'https://fonts.googleapis.com/css2?' + params.join('&') + '&display=swap';
}

// Same idea as googleFontsHref() above, for the handful of fonts
// (RebelRedux, Digital dream, Fizzy Soda as of this comment -- see
// each entry's own `cdn` field and comment in presets-lookups.js)
// that aren't on Google Fonts at all but ARE hosted as a free webfont
// on cdnfonts.com. Unlike Google Fonts (one combined stylesheet
// request covering every family), cdnfonts.com serves one stylesheet
// per font family, so this returns one <link> per distinct `cdn` slug
// instead of trying to combine them -- still just one request per
// font actually used, not per FONT_LOOKUP entry (Digital Dream Small/
// Digital Dream share the same slug and so the same single link).
function cdnFontLinks() {
  var seen = {};
  var links = [];
  FONT_LOOKUP.forEach(function (f) {
    if (!f.cdn || seen[f.cdn]) return;
    seen[f.cdn] = true;
    links.push('<link rel="stylesheet" href="https://fonts.cdnfonts.com/css/' + encodeURIComponent(f.cdn) + '">');
  });
  return links.join('');
}

// Fastest way to go from an id to its entry -- every font picker
// needs this (rendering the current selection, gating Show Seconds,
// auto-pairing the small companion, etc.).
function fontLookupEntry(id) {
  for (var i = 0; i < FONT_LOOKUP.length; i++) {
    if (FONT_LOOKUP[i].id === id) return FONT_LOOKUP[i];
  }
  return FONT_LOOKUP[0];
}

// Renders <option>s for one of the four font pickers. `onlyMainClock`
// restricts to the Clock font picker's own subset (see FONT_LOOKUP's
// own comment above); the other three pickers (clock's small
// companion, marker text, corner/edge content) get every font.
function fontOptionsHtml(selectedId, onlyMainClock) {
  return FONT_LOOKUP.filter(function (f) {
    return !onlyMainClock || f.mainClock;
  }).map(function (f) {
    // A wide font can't show seconds at all (no room), and a font
    // that's merely marked allowInlineSeconds:false can't either (its
    // own numerals don't read well with one) -- both fold into this
    // same data-seconds flag rather than being two separate checks
    // every consumer of data-seconds would otherwise need to remember.
    var secondsOk = !f.wide && f.allowInlineSeconds !== false;
    return '<option value="' + f.id + '" data-preview="' + esc(f.preview) + '" data-seconds="' +
      (secondsOk ? '1' : '0') + '" data-height="' + f.height + '" data-small="' + (f.small ? '1' : '0') +
      '" data-wide="' + (f.wide ? '1' : '0') + '"' +
      (selectedId === f.id ? ' selected' : '') + '>' + esc(f.label) + '</option>';
  }).join('');
}

// A vertical stack of buttons that behaves like a radio group -- same
// "one active at a time, tap to switch" idea as .mode-btn-group (the
// side-by-side DIGITAL/ANALOG layout buttons), just stacked
// instead of side-by-side, for option lists too long/wordy to fit
// 3+ across (the two Animation section pickers). Backed by one hidden
// input (same "hidden input is the real value, buttons just reflect
// it" pattern selectBottomStyle()/selectSunTimeMode() already use)
// rather than actual radio inputs, so save() and applyStyleCornersJson()
// read it the same simple way as every other hidden-input field.
// Falls back to the first option (Off, for every current caller) when
// currentValue doesn't match any of them -- e.g. a preset saved by an
// older version of this page whose value set has since changed, or
// hand-edited/corrupted persisted settings. Without this, a stray
// value used to leave every button unhighlighted (looking like
// nothing at all was selected) rather than falling back to a sane,
// visibly-selected default. selectVerticalOption() below applies the
// same fallback client-side, for values that arrive after the initial
// render (preset apply, popup pre-fill).
function verticalButtonGroupHtml(groupId, hiddenId, options, currentValue, onclickFn) {
  var validValue = options.some(function (opt) { return String(opt.value) === String(currentValue); })
    ? currentValue : options[0].value;
  var buttons = options.map(function (opt) {
    var active = String(validValue) === String(opt.value);
    var call = onclickFn ?
      onclickFn + '(\'' + esc(opt.value) + '\')' :
      'selectVerticalOption(\'' + groupId + '\', \'' + hiddenId + '\', \'' + esc(opt.value) + '\')';
    return '<button type="button" class="mode-btn-vertical' + (active ? ' active' : '') + '" data-value="' + esc(opt.value) + '" onclick="' + call + '">' + esc(opt.label) + '</button>';
  }).join('');
  return '<div class="mode-btn-group-vertical" id="' + groupId + '">' + buttons + '</div>' +
    '<input type="hidden" id="' + hiddenId + '" value="' + esc(validValue) + '">';
}

// Same idea as verticalButtonGroupHtml() above, but for a short (2-4
// option) side-by-side row in the exact same style the Layout
// section's own DIGITAL/ANALOG buttons use (.mode-btn/.mode-btn-group)
// -- an optional icon (one of MODE_BTN_ICONS, or any other inline SVG
// string) above the label, same as those two already have. `onclickFn`
// lets one caller (the Sky style row) hook its own extra side effect
// (showing/hiding the Weather-drawing-style row underneath) in on top
// of the plain "set the hidden field, toggle .active" behavior every
// other row just uses directly -- see selectSkyMode()'s own comment.
function modeButtonGroupHtml(groupId, hiddenId, options, currentValue, onclickFn) {
  var buttons = options.map(function (opt) {
    var active = String(currentValue) === String(opt.value);
    var call = onclickFn ?
      onclickFn + '(\'' + esc(opt.value) + '\')' :
      'selectModeButton(\'' + groupId + '\', \'' + hiddenId + '\', \'' + esc(opt.value) + '\')';
    return '<button type="button" class="mode-btn' + (active ? ' active' : '') + '" data-value="' + esc(opt.value) + '" onclick="' + call + '">' + (opt.icon || '') + '<span>' + esc(opt.label) + '</span></button>';
  }).join('');
  return '<div class="mode-btn-group" id="' + groupId + '">' + buttons + '</div>' +
    '<input type="hidden" id="' + hiddenId + '" value="' + esc(currentValue) + '">';
}

// The plain 3-role (0=Main/1=Accent/2=Background, optionally 3=None)
// color choice used all over these popups -- schemeColorOptionsHtml()'s
// own <option> list turned into the same button-row shape as everything
// else here instead, rather than the full swatch-preview color picker
// the actual Colors section above uses (that one shows real colors
// because it HAS 64 real ones to choose from; these 3-4 are just roles
// that resolve to whatever the current color scheme says Main/Accent/
// Background actually are, so naming them is all there is to show).
function colorRoleButtonGroupHtml(groupId, hiddenId, currentValue, includeNone) {
  var options = [
    { value: '0', label: 'Main' },
    { value: '1', label: 'Accent' },
    { value: '2', label: 'Background' }
  ];
  if (includeNone) options.push({ value: '3', label: 'None' });
  return modeButtonGroupHtml(groupId, hiddenId, options, currentValue || '0');
}

// Flattened, id-ascending view of CORNER_CATEGORIES (see
// presets-lookups.js's own header comment for why that\'s the single
// source of truth now) -- used to build the flat <option> lists the
// hidden per-slot <select>s need. Category grouping/curated item order
// only matters to the categorized picker itself (see
// categoryItemOptionsHtml() below), not to these hidden stores.
var CORNER_CONTENT_OPTIONS = [];
CORNER_CATEGORIES.forEach(function (cat) {
  cat.items.forEach(function (item) {
    CORNER_CONTENT_OPTIONS.push({ id: item.id, label: item.label });
  });
});
CORNER_CONTENT_OPTIONS.sort(function (a, b) { return a.id - b.id; });
// auroraEnabled omits id 84 entirely (not just hides it) when auroras
// are turned off in the Astronomy section -- see onAuroraEnabledChange()
// for the live version of this same filtering, run client-side when
// the checkbox itself is toggled without a page reload.
function cornerContentOptionsHtml(selected, auroraEnabled) {
  return CORNER_CONTENT_OPTIONS.filter(function (o) {
    return o.id !== 84 || auroraEnabled;
  }).map(function (o) {
    return '<option value="' + o.id + '"' + (String(selected) === String(o.id) ? ' selected' : '') + '>' + esc(o.label) + '</option>';
  }).join('');
}

// Fonts known not to render Roman numerals correctly (missing/wrong
// glyphs for some of the letters int_to_roman() needs) -- the Roman
// numerals checkbox gets disabled (and, if it was checked, force-
// unchecked) whenever one of these is selected for marker text. Only
// verified for these three so far (Leco Medium, Leco Large, Bitham
// Medium 34 in FONT_LOOKUP's own ids) -- add more here as they're
// checked -- see int_to_roman() in background_layer.c for what it
// actually needs (I, V, X, L, C, D, M).

// A 12-button grid for picking which hour numerals (kind='hour', labels
// 12,1..11) or which every-5-second slots (kind='sec', labels 0,5..55)
// should get a numeral -- bit i of the mask corresponds to button i,
// same order marker_layer_draw_text() iterates on-watch.
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
'    <div class="help" id="' + p + 'NoneHelp" style="display:none;">This ring is turned off -- pick any other shape to bring it back with its last settings.</div>' +

'    <div id="' + p + 'GeometryWrap">' +

'    <div class="slider-row">' +
'      <label for="' + p + 'Thickness">Thickness <span class="val" id="' + p + 'ThicknessVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Thickness" min="1" max="' + thicknessMax + '" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help" id="' + p + 'ThicknessHelp">Each mark is drawn directly between its inner and outer border points below -- no separate length setting.</div>' +

'    <div class="slider-row" id="' + p + 'InnerThicknessRow" style="display:none;">' +
'      <label for="' + p + 'InnerThickness">Inner thickness <span class="val" id="' + p + 'InnerThicknessVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerThickness\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'InnerThickness" min="1" max="' + thicknessMax + '" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'InnerThickness\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help" id="' + p + 'InnerThicknessHelp" style="display:none;">Tapered only: this is the width at the ring\'s inner (base) edge -- "Thickness" above becomes the outer (end) edge\'s width instead. Set one of the two to 1 and the other higher for a sharp triangle; equal values look the same as Square.</div>' +

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
'    <div class="help">0 = circle, 100 = a rectangle fitted to the screen edges -- this is what "bends" each mark around corners as it changes.</div>' +

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
'    <div class="help">Outer can\'t go below inner -- it gets pulled up automatically if you drag inner past it.</div>' +

'    <div class="checkbox-row" style="margin-top:12px;">' +
'      <input type="checkbox" id="' + p + 'Translucent">' +
'      <label for="' + p + 'Translucent" style="margin:0;">Semi-transparent</label>' +
'    </div>' +
'    <div class="help">Dithers this ring (independent of the hour/second ring\'s own setting, and of Semi-transparent hands) to ~50% so the sky shows through.</div>' +

'    <label style="margin-top:12px;">Color</label>' +
      colorRoleButtonGroupHtml(p + 'ColorGroup', p + 'Color', '0', false) +
'    <div class="help">Independent of the hour/second ring\'s own color -- pick a different one for each if you want them to stand apart.</div>' +

'    <label style="margin-top:12px;">Presets (translated from the procedural styles)</label>' +
'    <div class="preset-btn-row">' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'minimal\')">Minimal</button>' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'small\')">Small</button>' +
'      <button type="button" onclick="applyMarkerPreset(\'' + kind + '\', \'big\')">Big</button>' +
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
'    <div class="help">Numbers can go on the hour ring or the second ring, not both at once.</div>' +

'    <div id="markerTextOptions" style="' + (current.markerTextTarget && current.markerTextTarget !== '0' ? '' : 'display:none;') + '">' +
'      <label for="markerTextFont" style="margin-top:10px;">Font</label>' +
'      <select id="markerTextFont" onchange="onMarkerTextFontChange()" style="display:none;">' + fontOptionsHtml(parseInt(current.markerTextFont || '0', 10), false) + '</select>' +
'      <button type="button" class="font-picker-btn font-picker-trigger" id="markerTextFontTrigger" onclick="openFontPicker(\'markerTextFont\')">' +
'        <span class="font-picker-preview" id="markerTextFontTriggerPreview"></span>' +
'        <span class="font-picker-name" id="markerTextFontTriggerName"></span>' +
'      </button>' +

'      <div class="checkbox-row" style="margin-top:12px;">' +
'        <input type="checkbox" id="markerTextRoman" onchange="refreshAllFontTriggerLabels()" ' + (current.markerTextRoman === 'true' && !ROMAN_INCOMPATIBLE_FONTS[current.markerTextFont] ? 'checked' : '') + ' ' + (ROMAN_INCOMPATIBLE_FONTS[current.markerTextFont] ? 'disabled' : '') + '>' +
'        <label for="markerTextRoman" style="margin:0;">Roman numerals</label>' +
'      </div>' +
'      <div class="help" id="markerTextRomanHelp">' + (ROMAN_INCOMPATIBLE_FONTS[current.markerTextFont] ? 'Not available with this font -- its glyphs don\'t support Roman numerals correctly.' : 'Shows I, II, III... instead of 1, 2, 3... -- independent of the font above.') + '</div>' +

'      <div class="slider-row">' +
'        <label for="markerTextOffset">Offset from index <span class="val" id="markerTextOffsetVal">' + esc(current.markerTextOffset || '0') + 'px</span></label>' +
'        <div class="slider-with-buttons">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'markerTextOffset\', -1)">&minus;</button>' +
'        <input type="range" id="markerTextOffset" min="-50" max="50" step="1" value="' + esc(current.markerTextOffset || '0') + '" oninput="document.getElementById(\'markerTextOffsetVal\').textContent = this.value + \'px\';">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'markerTextOffset\', 1)">+</button>' +
'        </div>' +
'      </div>' +
'      <div class="help">Positive nudges numbers outward (away from center), negative pulls them inward -- so they don\'t overlap the dot/line/square index.</div>' +

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

function schemeColorOptionsHtml(selected) {
  var sel = selected || '0';
  return (
'<option value="0"' + (sel === '0' ? ' selected' : '') + '>Main color</option>' +
'<option value="1"' + (sel === '1' ? ' selected' : '') + '>Accent color</option>' +
'<option value="2"' + (sel === '2' ? ' selected' : '') + '>Background color</option>'
  );
}

// The 14 fields per hand (hour/min/sec -- 42 total) that get sent to the
// watch, kept as hidden inputs edited via the 3 popups below, same
// pattern as customMarkerHiddenInputsHtml(). Defaults approximate the
// "Pointy" procedural style so hands are visible immediately, rather
// than defaulting to width/length 0.
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


/**
 * @param {object} current  current settings, as plain values:
 *   { autoLoc, lat, lon, owmKey, updateMins,
 *     clockFont, showSeconds, bottomStyle: 'digital'|'analog' (a persisted 'biganalog' from before this app version is treated as 'analog'),
 *     bigAnalogMarkerStyle: '0'-'8' (8=custom -- see customHour.../customSec.../markerText... below), upperMiddleLine1Content/upperMiddleLine2Content: '0'-'12', upperMiddleLine1Color/upperMiddleLine2Color: '0'-'3',
 *     colorScheme: '0'-'9'|'custom', customBg, customText, customAccent (packed byte strings),
 *     nightEnabled, nightScheme, nightCustomBg, nightCustomText, nightCustomAccent,
 *     showSunTime, showIss, sunMoonSize: '25'|'50'|'75'|'100', shakeLabelSeconds, vibrateOnPhaseChange,
 *     tempUnit: 'C'|'F',
 *     cornerTL, cornerTR, cornerBL, cornerBR: '0'-'9', cornerTLColor, cornerTRColor, cornerBLColor, cornerBRColor: '0'-'3', stepGoal,
 *     customHourStyle/customSecStyle: '0'-'2' (dot/line/square), '4' (tapered -- 3 reserved/unused;
 *     also drives customHourInnerThickness (1-20)/customSecInnerThickness (1-10) below, only
 *     meaningful for style 4), customHourThickness (1-20)/
 *     customSecThickness (1-10), customHourInnerEcc/customHourOuterEcc/customSecInnerEcc/
 *     customSecOuterEcc: '0'-'100', customHourInnerBorder/customHourOuterBorder/
 *     customSecInnerBorder/customSecOuterBorder: '0'-'100' (% reach, see marker_reach_px()
 *     in marker_layer.c -- each mark spans directly between its inner/outer border points),
 *     markerTextTarget: '0'(off)|'1'(hour)|'2'(second), markerTextFont: '0'-'35', markerTextOffset: '-50'-'50',
 *     markerTextHourMask/markerTextSecMask: 0-4095 (12-bit),
 *     hourlyVibeMode: '0'(off)|'1'(on full hours)|'2'(every X minutes), hourlyVibeIntervalMin: '1'-'180',
 *     hourlyVibePattern: '0'(short)|'1'(double)|'2'(long), hourlyVibeStartTime/hourlyVibeEndTime: 'HH:MM'
 *     (inclusive; start==end, including the '00:00'/'00:00' default, means all 24 hours),
 *     hourlyVibeDaysMask: 0-127 (bit i = HOURLY_VIBE_DAY_LABELS[i], Sun=bit 0), hourlyVibeOverrideQuiet: boolean,
 *     testMode, testDateTime, fullKeysetJson: pretty-printed JSON string, every current
 *     AppMessage key/value pre-filled for the debug "Full keyset" window (see buildFullKeysetDict() in index.js) }
 */
// One quick-recall Style Presets row (apply button, rename input, save/
// rename icon buttons) plus its two backing hidden inputs -- called for
// n = 1..6 from buildConfigHtml below, same "templated, not copy-pasted
// per slot" pattern as handEditorModalHtml() uses for the 3 hands.
function presetSlotHtml(current, n) {
  var name = current['presetSlot' + n + 'Name'] || ('Preset ' + n);
  var json = current['presetSlot' + n + 'Json'] || '';
  return (
'    <div class="preset-slot-row">' +
'      <button type="button" class="preset-apply-btn" id="presetApplyBtn' + n + '" onclick="applyPresetSlot(' + n + ')" ' + (json ? '' : 'disabled') + '>' + esc(name) + '</button>' +
'      <input type="text" class="preset-name-input" id="presetNameInput' + n + '" style="display:none;" onblur="commitRenamePresetSlot(' + n + ')" onkeydown="if (event.key === \'Enter\') this.blur();">' +
'      <button type="button" class="preset-icon-btn" onclick="savePresetSlot(' + n + ')" title="Save current design here">&#128190;</button>' +
'      <button type="button" class="preset-icon-btn" onclick="startRenamePresetSlot(' + n + ')" title="Rename">&#9998;</button>' +
'    </div>' +
'    <input type="hidden" id="presetSlot' + n + 'Name" value="' + esc(name) + '">' +
'    <input type="hidden" id="presetSlot' + n + 'Json" value="' + esc(json) + '">'
  );
}
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

function buildConfigHtml(current) {
  var autoLocChecked = current.autoLoc ? 'checked' : '';
  var manualDisabled = current.autoLoc ? 'disabled' : '';
  var testModeChecked = current.testMode ? 'checked' : '';
  var testDisabled = current.testMode ? '' : 'disabled';
  // Falls back to the most recently sent raw chunk (see
  // rawMessageLogButtonsHtml() above) rather than the old single
  // LAST_COMPUTED_DICT snapshot -- same as clicking the newest of the
  // buttons below would load, just done automatically on page open.
  var rawLogEntries = current.rawMessageLog || [];
  var mostRecentRawChunk = rawLogEntries.length ? rawLogEntries[rawLogEntries.length - 1].dict : null;
  var debugTextareaInitial = (current.debugOverrideEnabled && current.debugOverrideData)
    ? current.debugOverrideData
    : (mostRecentRawChunk ? JSON.stringify(mostRecentRawChunk, null, 2) : '');
  var bottomStyleVal = (current.bottomStyle === 'analog' || current.bottomStyle === 'biganalog') ? 'analog' : 'digital';
  var isAnalog = bottomStyleVal === 'analog';
  var clockFontId = parseInt(current.clockFont || '8', 10);
  var clockFontIsWide = !!fontLookupEntry(clockFontId).wide;
  // A wide font can't show seconds at all (no room), and a font
  // marked allowInlineSeconds:false can't either (its own numerals
  // clip/read badly with one) -- see fontOptionsHtml()'s own comment
  // on why data-seconds folds both of those together too.
  var secondsUnsupported = (bottomStyleVal === 'digital') && (fontLookupEntry(clockFontId).allowInlineSeconds === false || clockFontIsWide);
  var secondsChecked = (current.showSeconds && !secondsUnsupported) ? 'checked' : '';
  var secondsDisabled = secondsUnsupported ? 'disabled' : '';
  var digitalSidesVal = current.digitalSides || 'none';
  var digitalLeftOn = digitalSidesVal === 'left' || digitalSidesVal === 'both';
  var digitalRightOn = digitalSidesVal === 'right' || digitalSidesVal === 'both';
  var cornerFontId = parseInt(current.cornerFont || '1', 10);

  // Client-side copy of CORNER_CATEGORIES (see presets-lookups.js's
  // own header comment), filtered the same way the old hand-typed
  // version was: id 84 ("Aurora Kp index") only present when auroras
  // are actually on -- onAuroraEnabledChange() handles adding/removing
  // it live client-side if that checkbox changes without a reload.
  var cornerCategoriesForClient = CORNER_CATEGORIES.map(function (cat) {
    if (cat.id !== 'astro' || current.auroraEnabled) return cat;
    return { id: cat.id, label: cat.label, icon: cat.icon, items: cat.items.filter(function (it) { return it.id !== 84; }) };
  });

  // One <button> per example-style slot (see EXAMPLE_STYLE_COUNT's own
  // comment above) -- a screenshot if one's been generated for that
  // slot, otherwise just its number as an empty placeholder tile;
  // disabled (not clickable) until that slot has an actual preset.
  var exampleStylesButtonsHtml = '';
  for (var exStyleI = 1; exStyleI <= EXAMPLE_STYLE_COUNT; exStyleI++) {
    var exStyleImg = EXAMPLE_STYLE_IMAGES[String(exStyleI)];
    var exStyleHasPreset = EXAMPLE_STYLE_PRESETS[String(exStyleI)] != null;
    exampleStylesButtonsHtml +=
      '<button type="button" class="example-style-btn" onclick="openExampleStyleModal(' + exStyleI + ')"' +
      (exStyleHasPreset ? '' : ' disabled') + '>' +
      (exStyleImg
        ? '<img src="' + exStyleImg + '" alt="Example style ' + exStyleI + '">'
        : '<span class="example-style-btn-empty">' + exStyleI + '</span>') +
      '</button>';
  }

  // Which edge-middle slots (upper/bottom/left/right-middle) does the
  // current mode/style support, and are the 4 corners themselves
  // suppressed? Must match computeSlotAvailability()'s client-side
  // logic (and features_recompute_slots's rules in features_layer.c)
  // exactly, or the settings page would show slots as available that
  // the watch itself won't actually draw. (Not currently read by
  // anything below -- computeSlotAvailability() is what actually
  // drives the rendered page -- but kept in sync anyway since this
  // comment already promises it matches, and a future reader/caller
  // shouldn't inherit a silently-stale copy.)
  var markerStyleNum = parseInt(current.bigAnalogMarkerStyle || '0', 10);
  var isBitmapMarkerStyle = markerStyleNum >= 3 && markerStyleNum !== 8 && markerStyleNum !== 9;
  var bitmapCornerOverride = !!current.bitmapCornerOverride;
  var edgeAvail = { upper: false, bottom: false, left: false, right: false, cornersGrayed: false };
  if (isAnalog) {
    if (markerStyleNum < 3 || markerStyleNum === 8 || markerStyleNum === 9) {
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: false };
    } else if (markerStyleNum === 3 || markerStyleNum === 4 || markerStyleNum === 6) {
      edgeAvail = { upper: true, bottom: true, left: bitmapCornerOverride, right: bitmapCornerOverride, cornersGrayed: !bitmapCornerOverride };
    } else if (markerStyleNum === 5) {
      // Tally -- its own mask art leaves all 4 corners clear (unlike
      // every other bitmap style), so it alone keeps them active
      // regardless of the override checkbox.
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: false };
    } else if (markerStyleNum === 7) {
      edgeAvail = { upper: true, bottom: true, left: true, right: true, cornersGrayed: !bitmapCornerOverride };
    } else {
      edgeAvail = { upper: true, bottom: bitmapCornerOverride, left: bitmapCornerOverride, right: bitmapCornerOverride, cornersGrayed: !bitmapCornerOverride };
    }
  }
  var fontOptions = fontOptionsHtml(clockFontId, true);

  function hexFromPackedByte(byte) {
    var b = parseInt(byte, 10);
    if (isNaN(b)) return '#000000';
    var r2 = (b >> 4) & 3, g2 = (b >> 2) & 3, b2 = b & 3;
    function ch(v) { var h = (v * 85).toString(16); return h.length < 2 ? '0' + h : h; }
    return '#' + ch(r2) + ch(g2) + ch(b2);
  }
  // Colors are always three concrete packed bytes now -- there's no
  // "preset vs custom" mode stored anywhere. Picking a preset (see
  // chooseColorPreset() below) just writes its RGB straight into these
  // same three hidden fields, same as tapping each swatch individually
  // would, so the page only ever has one representation of "current
  // colors" to read back on load -- whether that happens to currently
  // match one of COLOR_SCHEMES is worked out fresh each time by
  // matchingPresetId(), not tracked as its own separate state.
  function resolveInitialColors(bgByte, textByte, accentByte) {
    return {
      bg: hexFromPackedByte(bgByte),
      text: hexFromPackedByte(textByte),
      accent: hexFromPackedByte(accentByte)
    };
  }
  var initialColors = resolveInitialColors(current.customBg || '255', current.customText || '192', current.customAccent || '192');
  var initialNightColors = resolveInitialColors(current.nightCustomBg || '192', current.nightCustomText || '255', current.nightCustomAccent || '255');

  return '<!DOCTYPE html>' +
'<html><head><meta charset="utf-8">' +
'<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">' +
'<title>Eclipz Settings</title>' +
'<link rel="preconnect" href="https://fonts.googleapis.com">' +
'<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>' +
'<link rel="stylesheet" href="' + googleFontsHref() + '">' +
cdnFontLinks() +
'<style>' +
'  :root { --page-bg: #f4f4f4; --card-bg: #fff; --text: #222; --text-strong: #333; --text-muted: #666; --text-faint: #888; --text-faint2: #555; --text-disabled: #999; --border: #ccc; --border-light: #eee; --border-lighter: #ddd; --btn-bg: #fafafa; }' +
'  @media (prefers-color-scheme: dark) {' +
'    :root { --page-bg: #1c1c1e; --card-bg: #2c2c2e; --text: #f2f2f2; --text-strong: #e5e5e5; --text-muted: #aaa; --text-faint: #999; --text-faint2: #bbb; --text-disabled: #777; --border: #48484a; --border-light: #3a3a3c; --border-lighter: #545456; --btn-bg: #3a3a3c; }' +
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
// Dark-mode overrides for the two rules just above and .bitmap-marker-img
// further up -- deliberately placed here, AFTER both of those base
// rules, rather than up in the earlier @media (prefers-color-scheme:
// dark) block alongside the --page-bg/etc. custom-property overrides.
// CSS cascade breaks ties between equal-specificity rules by SOURCE
// ORDER regardless of which one sits in a matching @media block --
// so a dark-mode override declared before a later unconditional rule
// for the very same selector loses to it whenever dark mode is
// actually active, which is exactly what silently made both of these
// dead code (bitmap marker previews stayed inverted/dark instead of
// turning white, hand-style icons stayed plain black instead of
// turning white) until this block was moved down here, past both
// selectors\' own base declarations.
'  @media (prefers-color-scheme: dark) {' +
'    .bitmap-marker-img { filter: none; }' +
'    .hand-style-icon-preview img { filter: invert(1); }' +
'  }' +
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
'  .donate-btn { padding: 6px 10px; font-size: 13px; font-weight: 700; color: #fff; background: linear-gradient(135deg, #ffb347, #ff8c00, #ffb347); background-size: 200% 200%; animation: donateGradientShift 4s ease-in-out infinite; border: none; border-radius: 6px; box-shadow: 0 1px 3px rgba(255,140,0,0.5); }' +
'  @keyframes donateGradientShift { 0%, 100% { background-position: 0% 50%; } 50% { background-position: 100% 50%; } }' +
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
// Day-of-week toggle row for Hourly vibrations -- like .mode-btn-group
// but each button toggles independently (any subset can be active at
// once) instead of exactly one, since this picks a SET of days rather
// than a single mode.
'  .day-toggle-group { display: flex; width: 100%; margin-top: 6px; border-radius: 6px; overflow: hidden; border: 1px solid var(--border); box-sizing: border-box; }' +
'  .day-toggle-btn { flex: 1; padding: 10px 0; font-size: 13px; font-weight: 700; color: var(--text-strong); background: var(--btn-bg); border: none; border-right: 1px solid var(--border); }' +
'  .day-toggle-btn:last-child { border-right: none; }' +
'  .day-toggle-btn.active { background: #ff9200; color: #fff; box-shadow: inset 0 2px 4px rgba(0,0,0,0.35); }' +
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
'  .section-legend-sub { font-size: 11.2px; line-height: 1.25; color: var(--text-faint); margin-top: 1px; min-width: 0; white-space: nowrap; overflow: hidden; position: relative; }' +
// A lighter, nested variant of .section-legend/.section-icon above --
// used for Hands style/Indices style, one level down inside the Style
// section rather than a full top-level section of their own (no
// SECTION_META color-coded icon; a small thumbnail of the actual
// currently-selected preset instead -- see subsectionLegendHtml()
// further down for how that gets filled in).
'  .subsection-legend { cursor: pointer; display: flex; align-items: center; gap: 10px; width: 100%; box-sizing: border-box; margin: 12px 0 0; padding: 8px; border: 1px solid var(--border); border-radius: 8px; background: var(--btn-bg); user-select: none; color: var(--text-strong); }' +
'  .subsection-legend-preview { width: 34px; height: 34px; border-radius: 6px; flex: 0 0 auto; display: flex; align-items: center; justify-content: center; overflow: hidden; background: var(--card-bg); border: 1px solid var(--border-light); }' +
'  .subsection-legend-preview img { max-width: 100%; max-height: 100%; display: block; }' +
'  .subsection-legend-text { flex: 1 1 auto; min-width: 0; display: flex; flex-direction: column; justify-content: center; }' +
'  .subsection-legend-title { font-size: 14px; font-weight: 700; line-height: 1.15; }' +
'  .subsection-legend-sub { font-size: 11px; line-height: 1.25; color: var(--text-faint); margin-top: 1px; min-width: 0; white-space: nowrap; overflow: hidden; position: relative; }' +
'  .subsection-body { border: 1px solid var(--border); border-top: none; border-radius: 0 0 8px 8px; padding: 10px 8px 4px; margin-top: -1px; margin-bottom: 4px; }' +
// Sliding-overflow inner span for .section-legend-sub -- see
// applySubheadSlide()/setSubheaderText()/setSubheaderHtml() further
// down for when subhead-sliding actually gets added (only once the
// text is measured as genuinely too wide for the collapsed row, not
// unconditionally). Ease in/out and a long-ish pause at each end
// (15% and 65%) rather than a constant scroll, so it reads as
// "pausing to let you read, then sliding to reveal the rest" instead
// of a distracting nonstop marquee.
'  .subhead-sub-inner { display: inline-block; white-space: nowrap; }' +
'  .subhead-sub-inner.subhead-sliding { animation: subheadSlide 6s ease-in-out infinite; }' +
'  @keyframes subheadSlide { 0%, 15% { transform: translateX(0); } 50%, 65% { transform: translateX(var(--slide-dist, 0)); } 100% { transform: translateX(0); } }' +
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
// Spread across the bar's own ~91px height (274px diagram * 33.33%)
// with a real gap between rows -- a .slot-btn is itself about 25px
// tall (11px font + 10px padding + 2px border), so the previous
// 21px-apart anchors left adjacent rows overlapping by several px
// instead of reading as 3 distinct rows. These sit clear of the
// centered clock digits regardless of how close to the bar's top
// edge they get, since the buttons are pinned to the far left/right
// (left/right: 4px) while the digits sit centered.
'  .slot-digital-left1 { left: 4px; bottom: 60px; }' +
'  .slot-digital-left2 { left: 4px; bottom: 32px; }' +
'  .slot-digital-left3 { left: 4px; bottom: 4px; }' +
'  .slot-digital-right1 { right: 4px; bottom: 60px; }' +
'  .slot-digital-right2 { right: 4px; bottom: 32px; }' +
'  .slot-digital-right3 { right: 4px; bottom: 4px; }' +
'  .slot-digital-bottom { left: 50%; bottom: 2px; transform: translateX(-50%); min-width: 90px; }' +
'</style></head>' +
'<body>' +

'<div id="topBar">' +
'  <div class="top-bar-left">' +
'    <div class="top-bar-actions">' +
'      <button type="button" class="back-btn" onclick="goBack()">&lsaquo; Back</button>' +
'      <button type="button" class="donate-btn" onclick="openDonateModal()">&#9825; Donate</button>' +
'    </div>' +
'    <div class="top-bar-title">Eclipz</div>' +
'    <div class="top-bar-desc">Configure where the eclipse geometry should be calculated for, and (optionally) a second weather source.</div>' +
'  </div>' +
'  <div class="top-bar-preview">' +
'    <canvas id="previewCanvas" width="176" height="201"></canvas>' +
'  </div>' +
'</div>' +

'<div class="modal-overlay" id="slotEditModal" onclick="if (event.target === this) closeSlotEditor();">' +
'  <div class="modal-box">' +
'    <div class="modal-title" id="slotEditTitle">Edit slot</div>' +
'    <label>Category</label>' +
'    <div class="category-btn-group" id="slotEditCategoryGroup"></div>' +
'    <input type="hidden" id="slotEditCategory">' +
'    <label for="slotEditContent" style="margin-top:10px;">Content</label>' +
'    <select id="slotEditContent" onchange="onSlotEditContentChange()">' + cornerContentOptionsHtml(0) + '</select>' +
'    <div class="help" id="slotEditCategoryHelp" style="display:none;">DST is calculated for current-era US and EU rules -- Sydney and Auckland don\'t adjust for DST yet.</div>' +
'    <div class="mode-btn-group" id="slotEditColorGroup" style="margin-top:10px;">' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(0)">MONO</button>' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(1)">ACC</button>' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(2)">PILL</button>' +
'      <button type="button" class="mode-btn" onclick="slotEditorSelectColor(3)">COLOR</button>' +
'    </div>' +
'    <div class="help"><b>MONO</b> = your main color, <b>ACC</b> = accent color, <b>PILL</b> = solid background-color capsule behind main-color content, <b>COLOR</b> = dynamic (changes with the value shown).</div>' +
'    <button type="button" onclick="saveSlotEditor()" style="width:100%; box-sizing:border-box; padding:14px; font-size:16px; font-weight:600; color:#fff; background:#ff9200; border:none; border-radius:8px; margin-top:14px;">OK</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeSlotEditor()">Cancel</button>' +
'  </div>' +
'</div>' +

customMarkerModalHtml('hour', 'Edit hour indices', 20) +
customMarkerModalHtml('sec', 'Edit seconds indices', 10) +
textMarkerModalHtml(current) +

handEditorModalHtml('hour', 'Edit hour hand') +
handEditorModalHtml('min', 'Edit minute hand') +
handEditorModalHtml('sec', 'Edit second hand') +

// Hand style / marker style pickers -- a 3x3 grid of image+title
// buttons (see HAND_PRESETS/MARKER_PRESET_STYLES + MARKER_BITMAP_STYLES
// below) plus one horizontal "Custom" button, same one-tap-applies-
// and-closes interaction for both. Tapping outside the popup (the
// overlay itself, not the box) discards -- nothing is applied.
'<div class="modal-overlay" id="handStyleModal" onclick="if (event.target === this) closeHandStyleModal();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Hand style</div>' +
'    <div class="modal-scroll-body">' +
'      <div class="style-picker-grid" id="handStyleGrid"></div>' +
'      <button type="button" class="style-picker-custom-btn" onclick="chooseHandStyleCustom()">Custom</button>' +
'    </div>' +
'  </div>' +
'</div>' +

'<div class="modal-overlay" id="markerStyleModal" onclick="if (event.target === this) closeMarkerStyleModal();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Hour/seconds indices style</div>' +
'    <div class="modal-scroll-body">' +
'      <div class="style-picker-grid" id="markerStyleGrid"></div>' +
'      <button type="button" class="style-picker-custom-btn" onclick="chooseMarkerStyleCustom()">Custom</button>' +
'      <button type="button" class="style-picker-custom-btn style-picker-none-btn" onclick="chooseMarkerStyle(\'9\')">None</button>' +
'    </div>' +
'  </div>' +
'</div>' +

// Center circle -- same "commits live, no separate draft/Save state"
// shape as the numerals modal above, not the hand editor's own
// draft-then-Save/Cancel one, since there are only 2 fields here and
// they already commit live via their own oninput/onchange handlers.
'<div class="modal-overlay" id="centerCircleModal" onclick="if (event.target === this) closeCenterCircleEditor();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Center circle</div>' +

'    <div class="slider-row">' +
'      <label for="centerCircleRadius">Radius <span class="val" id="centerCircleRadiusVal">' + esc(current.centerCircleRadius || '3') + 'px</span></label>' +
'    <div class="slider-with-buttons">' +
'    <button type="button" class="slider-step-btn" onclick="stepSlider(\'centerCircleRadius\', -1)">&minus;</button>' +
'      <input type="range" id="centerCircleRadius" min="0" max="30" step="1" value="' + esc(current.centerCircleRadius || '3') + '" oninput="document.getElementById(\'centerCircleRadiusVal\').textContent = this.value + \'px\'; refreshEditButtonLabels();">' +
'    <button type="button" class="slider-step-btn" onclick="stepSlider(\'centerCircleRadius\', 1)">+</button>' +
'    </div>' +
'    </div>' +
'    <div class="help">0 = off.</div>' +
'    <label for="centerCircleColor">Color</label>' +
'    <select id="centerCircleColor">' + schemeColorOptionsHtml(current.centerCircleColor) + '</select>' +

'    <button type="button" class="modal-cancel-btn" onclick="closeCenterCircleEditor()" style="margin-top:14px;">Close</button>' +
'  </div>' +
'</div>' +

// Shadow style/angle apply to every hand's shadow at once (preset or
// custom, see their own help text below) -- not hand-specific the way
// the per-hand editors above are, so this is its own standalone modal
// rather than another field inside openHandEditor()'s. Same "changes
// apply live via onchange, no separate save step" behavior these two
// fields already had inline before moving in here -- just decluttering
// the main page, not changing how they work.
'<div class="modal-overlay" id="shadowStyleModal" onclick="if (event.target === this) closeShadowStyleEditor();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Shadow style</div>' +

'    <label>Shadow style</label>' +
      modeButtonGroupHtml('shadowTranslucentGroup', 'shadowTranslucent', [
        { value: 'false', label: 'Solid' },
        { value: 'true', label: 'Translucent' }
      ], current.shadowTranslucent !== 'false' ? 'true' : 'false', 'selectShadowTranslucent') +
'    <div class="help">Applies to every hand\'s shadow, preset or custom -- translucent dithers to ~50% (~25% for a hand that\'s itself semi-transparent), solid is fully opaque black.</div>' +

'    <div class="slider-row">' +
'      <label for="shadowAngle">Shadow angle <span class="val" id="shadowAngleVal">' + (current.shadowAngle || '120') + '&deg;</span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'shadowAngle\', -1)">&minus;</button>' +
'      <input type="range" id="shadowAngle" min="0" max="359" step="1" value="' + (current.shadowAngle || '120') + '" oninput="onShadowAngleInput()">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'shadowAngle\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">One shared light-source direction for every hand\'s shadow, preset or custom -- separate angles per hand would just be confusing since they all come from the same light.</div>' +

'    <button type="button" class="modal-cancel-btn" onclick="closeShadowStyleEditor()" style="margin-top:14px;">Close</button>' +
'  </div>' +
'</div>' +

// Shared by all 4 font pickers (clock, clock\'s small companion,
// corner/edge, marker numerals) -- which one is currently open lives
// in currentFontPickerRole (see openFontPicker() below), so there\'s
// one grid/modal to keep in sync rather than 4 near-identical copies.
// "Show incompatible fonts" (only meaningful for the 2 pickers that
// default to hiding the ~48px-scale display fonts -- see FONT_LOOKUP\'s
// own `small` comment) lives at the bottom here now instead of inline
// on the main page, and is hidden entirely for the other 2 pickers.
'<div class="modal-overlay" id="fontPickerModal" onclick="if (event.target === this) closeFontPicker();">' +
'  <div class="modal-box">' +
'    <div class="modal-title" id="fontPickerTitle">Font</div>' +
'    <div class="modal-scroll-body">' +
'      <div id="fontPickerGrid"></div>' +
'    </div>' +
'    <div class="modal-footer">' +
'      <div class="checkbox-row" id="fontPickerIncompatibleRow" style="margin-top:10px;">' +
'        <input type="checkbox" id="fontPickerShowIncompatible" onchange="renderFontPickerGrid()">' +
'        <label for="fontPickerShowIncompatible" style="margin:0;">Show incompatible fonts</label>' +
'      </div>' +
'      <button type="button" class="modal-cancel-btn" onclick="closeFontPicker()">Cancel</button>' +
'    </div>' +
'  </div>' +
'</div>' +

// Shared by all 3 hand editor popups\' own Shape picker (hour/min/sec)
// -- which popup-field-prefix is currently being edited lives in
// HAND_STYLE_ICON_PREFIX (see openHandStyleIconPicker() below), same
// "one popup, not 3 near-identical copies" shape the color preset
// picker uses for its own day/night pair.
'<div class="modal-overlay" id="handStyleIconPickerModal" onclick="if (event.target === this) closeHandStyleIconPicker();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Shape</div>' +
'    <div class="modal-scroll-body" id="handStyleIconPickerGrid"></div>' +
'  </div>' +
'</div>' +

'<div class="modal-overlay" id="weatherIconStylePickerModal" onclick="if (event.target === this) closeWeatherIconStylePicker();">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Weather icon style</div>' +
'    <div class="modal-scroll-body" id="weatherIconStylePickerGrid"></div>' +
'  </div>' +
'</div>' +

'<div class="modal-overlay" id="donateModal">' +
'  <div class="modal-box">' +
'    <div class="modal-title">Support this project</div>' +
'    <a class="secondary-btn" style="display:block; box-sizing:border-box; text-align:center; text-decoration:none;" href="#" onclick="return false;">Donate via PayPal</a>' +
'    <a class="secondary-btn" style="display:block; box-sizing:border-box; text-align:center; text-decoration:none; margin-top:8px;" href="#" onclick="return false;">Donate Bitcoin</a>' +
'    <div class="help" style="text-align:center; margin-top:10px;">Links coming soon.</div>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeDonateModal()">Close</button>' +
'  </div>' +
'</div>' +

// Shared by anything that needs a plain "are you sure?" step before
// acting -- Style Presets' save/apply buttons below. showConfirm()
// stashes the action to run and shows this; confirmModalYes() runs it
// (once) and closes; canceling (or tapping outside) just closes.
'<div class="modal-overlay" id="confirmModal" onclick="if (event.target === this) closeConfirmModal();">' +
'  <div class="modal-box">' +
'    <div class="modal-title" id="confirmModalTitle"></div>' +
'    <div class="help" id="confirmModalMessage" style="text-align:center;"></div>' +
'    <button type="button" class="modal-confirm-btn" onclick="confirmModalYes()">Confirm</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeConfirmModal()">Cancel</button>' +
'  </div>' +
'</div>' +

// Shared by every service's (i) button in the Updates section -- one
// modal, repopulated per tap by openServiceLog() from that service's
// own slice of serviceLogsJson (see serviceStatusRowsHtml() above).
// Console-style: newest first, one line per attempt with its date,
// time, OK/ERR ###, and (for errors) the message.
'<div class="modal-overlay" id="serviceLogModal" onclick="if (event.target === this) closeServiceLogModal();">' +
'  <div class="modal-box">' +
'    <div class="modal-title" id="serviceLogModalTitle"></div>' +
'    <div class="modal-scroll-body" id="serviceLogModalBody"></div>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeServiceLogModal()">Close</button>' +
'  </div>' +
'</div>' +

// Example styles: tapping a tile opens this instead of applying
// immediately -- image preview, bold title, description, then an
// explicit Apply button, per the same "confirm before it takes
// effect" idea as the plain confirm modal above, just with a richer
// preview since there's a real design to show off first.
'<div class="modal-overlay" id="exampleStyleModal" onclick="if (event.target === this) closeExampleStyleModal();">' +
'  <div class="modal-box">' +
'    <img class="example-style-modal-img" id="exampleStyleModalImg" src="" alt="">' +
'    <div class="example-style-modal-title" id="exampleStyleModalTitle"></div>' +
'    <div class="help" id="exampleStyleModalDesc" style="text-align:center;"></div>' +
'    <button type="button" class="modal-confirm-btn" id="exampleStyleModalApplyBtn" onclick="confirmExampleStyleApply()">Apply this style</button>' +
'    <button type="button" class="modal-cancel-btn" onclick="closeExampleStyleModal()">Cancel</button>' +
'  </div>' +
'</div>' +

'  <fieldset>' +
    sectionLegendHtml('examples', 'Example styles') +
'    <div class="section-body" id="section-examples" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <div class="help">Tap a design below to preview it -- each one sets every Style, Colors, and Features setting to match once you confirm, the same as pasting its JSON into "Style Presets" further down.</div>' +
'    <div class="example-style-grid">' + exampleStylesButtonsHtml + '</div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('style', 'Style') +
'    <div class="section-body" id="section-style" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <label>Layout</label>' +
'    <div class="mode-btn-group" id="bottomStyleGroup">' +
'      <button type="button" class="mode-btn' + (bottomStyleVal === 'digital' ? ' active' : '') + '" onclick="selectBottomStyle(\'digital\')">' + MODE_BTN_ICONS.digital + '<span>DIGITAL</span></button>' +
'      <button type="button" class="mode-btn' + (isAnalog ? ' active' : '') + '" onclick="selectBottomStyle(\'analog\')">' + MODE_BTN_ICONS.analog + '<span>ANALOG</span></button>' +
'    </div>' +
'    <input type="hidden" id="bottomStyleValue" value="' + esc(bottomStyleVal) + '">' +
'    <div class="help">Analog fills the whole screen with fullscreen hands over the sky/eclipse view -- no bottom bar.</div>' +

'    <div id="digitalOnlySettings" class="subsection" style="' + (bottomStyleVal === 'digital' ? '' : 'display:none;') + '">' +
'      <label for="clockFont">Clock font</label>' +
'      <select id="clockFont" onchange="onFontChange()" style="display:none;">' + fontOptions + '</select>' +
'      <button type="button" class="font-picker-btn font-picker-trigger" id="clockFontTrigger" onclick="openFontPicker(\'clock\')">' +
'        <span class="font-picker-preview" id="clockFontTriggerPreview"></span>' +
'        <span class="font-picker-name" id="clockFontTriggerName"></span>' +
'      </button>' +
'    </div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="showSeconds" ' + secondsChecked + ' ' + secondsDisabled + ' onchange="onShowSecondsChange()">' +
'      <label for="showSeconds" style="margin:0;">Show seconds</label>' +
'    </div>' +
'    <div class="help" id="secondsHelp" style="' + (secondsUnsupported ? '' : 'display:none;') + '">This font doesn\'t support showing seconds.</div>' +
'    <div class="help">Used by both layouts -- the digital clock\'s own seconds digits, and whether analog draws a second hand at all (gates the Custom style\'s "Edit second hand" below, too).</div>' +

'    <label style="margin-top:12px;">Label style</label>' +
      modeButtonGroupHtml('labelStyleGroup', 'labelStyle', [
        { value: '0', label: 'BOXED', icon: MODE_BTN_ICONS.labelBoxed },
        { value: '1', label: 'OUTLINED', icon: MODE_BTN_ICONS.labelOutlined },
        { value: '2', label: 'SOFT', icon: MODE_BTN_ICONS.labelSoft }
      ], current.labelStyle || '0') +
'    <div class="help">Boxed is an opaque rounded box with white text (the original look). Outlined uses your main color with a contrasting outline. Soft is plain light-gray text with no background or outline. Used for the shake-to-reveal Sun/Moon/ISS/Aurora name labels, in both layouts.</div>' +

'    <div id="bigAnalogSettings" class="subsection" style="' + (isAnalog ? '' : 'display:none;') + '">' +

      subsectionLegendHtml('hands', 'Hands style') +
'      <div class="subsection-body" id="subsec-hands" style="display:none;">' +
'      <label>Hand style</label>' +
'      <button type="button" class="marker-edit-btn" id="handStyleTriggerBtn" style="margin-top:8px;" onclick="openHandStyleModal()">Hand style: <span id="handStyleTriggerLabel"></span> &rsaquo;</button>' +
'      <div class="help">To show the date behind the hands, pick "Short date" as a line in the Features section below (bottom-middle line 1 does this by default).</div>' +

'      <div id="customHandSection">' +
'        <button type="button" class="marker-edit-btn" onclick="openHandEditor(\'hour\')">Edit hour hand: <span id="handHourStatusLabel"></span> &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" onclick="openHandEditor(\'min\')">Edit minute hand: <span id="handMinStatusLabel"></span> &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" id="editSecHandBtn" ' + (secondsChecked ? '' : 'disabled') + ' onclick="openHandEditor(\'sec\')">Edit second hand: <span id="handSecStatusLabel"></span> &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" onclick="openCenterCircleEditor()">Edit center circle: <span id="centerCircleStatusLabel"></span> &rsaquo;</button>' +
          handHiddenInputsHtml(current) +
'      </div>' +
'      <button type="button" class="marker-edit-btn" id="shadowStyleTriggerBtn" onclick="openShadowStyleEditor()">Edit shadow style: <span id="shadowStyleStatusLabel"></span> &rsaquo;</button>' +
'      </div>' +

      subsectionLegendHtml('indices', 'Indices style') +
'      <div class="subsection-body" id="subsec-indices" style="display:none;">' +
'      <label>Hour/seconds indices style</label>' +
'      <button type="button" class="marker-edit-btn" id="markerStyleTriggerBtn" style="margin-top:8px;" onclick="openMarkerStyleModal()">Indices style: <span id="markerStyleTriggerLabel"></span> &rsaquo;</button>' +
'      <select id="bigAnalogMarkerStyle" style="display:none;" onchange="onMarkerStyleChange()">' +
'        <option value="9"' + (current.bigAnalogMarkerStyle === '9' ? ' selected' : '') + '>None</option>' +
'        <option value="0"' + (current.bigAnalogMarkerStyle === '0' || !current.bigAnalogMarkerStyle ? ' selected' : '') + '>Minimal (thin hour indices only)</option>' +
'        <option value="1"' + (current.bigAnalogMarkerStyle === '1' ? ' selected' : '') + '>Small markers (hour + second)</option>' +
'        <option value="2"' + (current.bigAnalogMarkerStyle === '2' ? ' selected' : '') + '>Big markers (thick hour, thin second)</option>' +
'        <option value="3"' + (current.bigAnalogMarkerStyle === '3' ? ' selected' : '') + '>Modern</option>' +
'        <option value="4"' + (current.bigAnalogMarkerStyle === '4' ? ' selected' : '') + '>Shadow</option>' +
'        <option value="5"' + (current.bigAnalogMarkerStyle === '5' ? ' selected' : '') + '>Tally</option>' +
'        <option value="6"' + (current.bigAnalogMarkerStyle === '6' ? ' selected' : '') + '>Bell</option>' +
'        <option value="7"' + (current.bigAnalogMarkerStyle === '7' ? ' selected' : '') + '>Fancy</option>' +
'        <option value="8"' + (current.bigAnalogMarkerStyle === '8' ? ' selected' : '') + '>Custom</option>' +
'      </select>' +
'      <div class="checkbox-row" id="bitmapMarkerTransparentRow" style="margin-top:12px;' + (isBitmapMarkerStyle ? '' : ' display:none;') + '">' +
'        <input type="checkbox" id="bitmapMarkerTransparent" ' + (current.bitmapMarkerTransparent ? 'checked' : '') + ' onchange="updatePreview()">' +
'        <label for="bitmapMarkerTransparent" style="margin:0;">Semi transparent markers (see the sky through them)</label>' +
'      </div>' +
'      <div class="help">Bitmap styles are tinted with your main color (see the preview above) and their mask art shows behind the hands there once you\'ve added a resource PNG for that style. Which edge-middle info slots they support varies by style -- some are off by default so they don\'t overlap the artwork; see "Incompatible features" in the Features section below if you want them anyway.</div>' +
'      <div class="help">When an eclipse is actually happening, the Sun fills the whole screen as a background behind the hands.</div>' +

'      <div id="customMarkerSection" style="' + (current.bigAnalogMarkerStyle === '8' ? '' : 'display:none;') + '">' +
'        <button type="button" class="marker-edit-btn" onclick="openCustomMarkerEditor(\'hour\')">Edit hour indices: <span id="cmHourStatusLabel"></span> &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" onclick="openCustomMarkerEditor(\'sec\')">Edit seconds indices: <span id="cmSecStatusLabel"></span> &rsaquo;</button>' +
'        <button type="button" class="marker-edit-btn" onclick="openTextMarkerEditor()">Numerals: <span id="numeralsStatusLabel"></span> &rsaquo;</button>' +
          customMarkerHiddenInputsHtml(current) +
'      </div>' +
'      </div>' +
'    </div>' +

'    <label style="margin-top:12px;">Sky style</label>' +
'    <div class="mode-btn-group" id="skyModeGroup">' +
'      <button type="button" class="mode-btn' + ((current.skyMode === '0' || !current.skyMode) ? ' active' : '') + '" data-value="0" onclick="selectSkyMode(\'0\')">' + MODE_BTN_ICONS.skyWeather + '<span>WEATHER</span></button>' +
'      <button type="button" class="mode-btn' + (current.skyMode === '1' ? ' active' : '') + '" data-value="1" onclick="selectSkyMode(\'1\')">' + MODE_BTN_ICONS.skyClear + '<span>CLEAR</span></button>' +
'      <button type="button" class="mode-btn' + (current.skyMode === '2' ? ' active' : '') + '" data-value="2" onclick="selectSkyMode(\'2\')">' + MODE_BTN_ICONS.skySpace + '<span>SPACE</span></button>' +
'    </div>' +
'    <input type="hidden" id="skyMode" value="' + esc(current.skyMode || '0') + '">' +
'    <div class="help">Weather sky shows clouds/rain/snow and the day-night gradient. Clear sky keeps the gradient but never draws weather. Space view drops the gradient entirely for a fixed dark sky, always shows the Sun/Moon/planets when above the horizon regardless of time of day, and adds a field of bright named stars (tap/shake to reveal names).</div>' +

'    <div class="subsection">' +
'      <label>Outline text and icons for contrast</label>' +
      modeButtonGroupHtml('outlineStyleGroup', 'outlineStyle', [
        { value: '0', label: 'None' },
        { value: '1', label: 'Thin' },
        { value: '2', label: 'Thick' }
      ], current.outlineStyle || '1', 'onOutlineStyleChange') +
'    </div>' +
'    <div class="help">Adds an outline (in your color scheme\'s background color) behind corner/edge text and icons, the analog date, and the eclipse phase text -- so they stay readable over any part of the sky. Thick adds a wider 2px cardinal shift plus 1px diagonal shifts on top of Thin\'s own 1px cardinal outline. Icons only get it outside translucent/transparent mode. Hands have their own separate outline setting, per hand, in the Style section.</div>' +

'  </fieldset>' +

'  <fieldset id="cornersFieldset">' +
    sectionLegendHtml('corners', 'Features') +
'    <div class="section-body" id="section-corners" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <div class="help">Features are small info readouts (weather, health, date/time, and more) placed around your watch face. Tap a slot on the diagram below to pick what it shows and how it\'s colored -- grayed-out slots aren\'t available for your current style.</div>' +

'    <div id="slotPickerDiagram">' +
'      <div id="slotDiagramClockBar"><span id="slotDiagramClockText">12:34</span></div>' +
'      <button type="button" class="slot-btn slot-corner-tl" id="slotBtn-cornerTL" onclick="openSlotEditor(\'cornerTL\')"></button>' +
'      <button type="button" class="slot-btn slot-corner-tr" id="slotBtn-cornerTR" onclick="openSlotEditor(\'cornerTR\')"></button>' +
'      <button type="button" class="slot-btn slot-corner-bl" id="slotBtn-cornerBL" onclick="openSlotEditor(\'cornerBL\')"></button>' +
'      <button type="button" class="slot-btn slot-corner-br" id="slotBtn-cornerBR" onclick="openSlotEditor(\'cornerBR\')"></button>' +
'      <button type="button" class="slot-btn slot-upper-l1" id="slotBtn-upperMiddleLine1" onclick="openSlotEditor(\'upperMiddleLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-upper-l2" id="slotBtn-upperMiddleLine2" onclick="openSlotEditor(\'upperMiddleLine2\')"></button>' +
'      <button type="button" class="slot-btn slot-bottom-l1" id="slotBtn-bottomMiddleLine1" onclick="openSlotEditor(\'bottomMiddleLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-bottom-l2" id="slotBtn-bottomMiddleLine2" onclick="openSlotEditor(\'bottomMiddleLine2\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-left-l1" id="slotBtn-middleLeftLine1" onclick="openSlotEditor(\'middleLeftLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-left-l2" id="slotBtn-middleLeftLine2" onclick="openSlotEditor(\'middleLeftLine2\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-right-l1" id="slotBtn-middleRightLine1" onclick="openSlotEditor(\'middleRightLine1\')"></button>' +
'      <button type="button" class="slot-btn slot-middle-right-l2" id="slotBtn-middleRightLine2" onclick="openSlotEditor(\'middleRightLine2\')"></button>' +
'      <button type="button" class="slot-btn slot-digital-left1" id="slotBtn-digitalLeft1" onclick="openSlotEditor(\'digitalLeft1\')"></button>' +
'      <button type="button" class="slot-btn slot-digital-left2" id="slotBtn-digitalLeft2" onclick="openSlotEditor(\'digitalLeft2\')"></button>' +
'      <button type="button" class="slot-btn slot-digital-left3" id="slotBtn-digitalLeft3" onclick="openSlotEditor(\'digitalLeft3\')"></button>' +
'      <button type="button" class="slot-btn slot-digital-right1" id="slotBtn-digitalRight1" onclick="openSlotEditor(\'digitalRight1\')"></button>' +
'      <button type="button" class="slot-btn slot-digital-right2" id="slotBtn-digitalRight2" onclick="openSlotEditor(\'digitalRight2\')"></button>' +
'      <button type="button" class="slot-btn slot-digital-right3" id="slotBtn-digitalRight3" onclick="openSlotEditor(\'digitalRight3\')"></button>' +
'      <button type="button" class="slot-btn slot-digital-bottom" id="slotBtn-digitalBottom" onclick="openSlotEditor(\'digitalBottom\')"></button>' +
'    </div>' +

'    <div class="checkbox-row" id="bitmapCornerOverrideRow" style="margin-top:12px;' + ((isBitmapMarkerStyle && markerStyleNum !== 5) ? '' : ' display:none;') + '">' +
'      <input type="checkbox" id="bitmapCornerOverride" ' + (current.bitmapCornerOverride ? 'checked' : '') + ' onchange="onBitmapCornerOverrideChange()">' +
'      <label for="bitmapCornerOverride" style="margin:0;">Incompatible features (may overlap the design)</label>' +
'    </div>' +
'    <div class="help" id="bitmapCornerOverrideHelp" style="' + ((isBitmapMarkerStyle && markerStyleNum !== 5) ? '' : 'display:none;') + '">Your current bitmap marker style\'s artwork doesn\'t leave room for every feature slot -- corners and side edges are grayed out above by default so they don\'t overlap it. Check this box to enable all of them anyway.</div>' +

'    <div class="subsection" id="digitalSidesSection" style="' + ((bottomStyleVal === 'digital' && !clockFontIsWide) ? '' : 'display:none;') + '">' +
'      <label>Side features</label>' +
'      <div class="mode-btn-group" id="digitalSidesGroup">' +
'        <button type="button" class="mode-btn' + (digitalLeftOn ? ' active' : '') + '" data-side="left" onclick="toggleDigitalSide(\'left\')">LEFT SIDE</button>' +
'        <button type="button" class="mode-btn' + (digitalRightOn ? ' active' : '') + '" data-side="right" onclick="toggleDigitalSide(\'right\')">RIGHT SIDE</button>' +
'      </div>' +
'      <input type="hidden" id="digitalSides" value="' + esc(digitalSidesVal) + '">' +
'      <div class="help">Adds up to 3 short info lines down each side of the digital clock, on the bottom bar -- pick their content on the diagram above. Only offered for narrower clock fonts (this one qualifies); picking both sides assumes the font is already narrow enough to share the bar with them without shrinking the clock any further.</div>' +
'    </div>' +
'    <div class="help" id="digitalSidesWideHelp" style="' + ((bottomStyleVal === 'digital' && clockFontIsWide) ? '' : 'display:none;') + '">This font runs too wide for side features -- pick a narrower one in the Style section to use them.</div>' +

'    <label for="cornerFont">Font</label>' +


'    <select id="cornerFont" onchange="onCornerFontChange()" style="display:none;">' + fontOptionsHtml(cornerFontId, false) + '</select>' +
'    <button type="button" class="font-picker-btn font-picker-trigger" id="cornerFontTrigger" onclick="openFontPicker(\'cornerFont\')">' +
'      <span class="font-picker-preview" id="cornerFontTriggerPreview"></span>' +
'      <span class="font-picker-name" id="cornerFontTriggerName"></span>' +
'    </button>' +
'    <div class="help">Applies to corner/edge feature text and the analog date. Bigger display fonts are hidden by default in the picker -- see "Show incompatible fonts" there.</div>' +

'    <div id="weatherIconStyleRow">' +
'      <label>Weather icon style</label>' +
'      <select id="weatherIconStyle" style="display:none;">' +
'        <option value="0"' + (current.weatherIconStyle === '0' ? ' selected' : '') + '>Simple</option>' +
'        <option value="1"' + (current.weatherIconStyle === '1' || !current.weatherIconStyle ? ' selected' : '') + '>Hollow</option>' +
'        <option value="2"' + (current.weatherIconStyle === '2' ? ' selected' : '') + '>Full color</option>' +
'      </select>' +
'      <button type="button" class="font-picker-btn font-picker-trigger" id="weatherIconStyleTrigger" onclick="openWeatherIconStylePicker()">' +
'        <span class="font-picker-preview weather-icon-style-preview"></span>' +
'        <span class="font-picker-name"></span>' +
'      </button>' +
'      <div class="help">"Simple" is a placeholder for now. Hollow follows the slot\'s own color mode like any other icon; Full color is a genuine multi-color image with its own baked-in colors (see README.md), so it ignores the slot\'s color mode entirely. Applies wherever a Weather icon feature is picked below.</div>' +
'    </div>' +

'    <div style="display:none;" id="slotDataStore">' +
'      <select id="cornerTL">' + cornerContentOptionsHtml(current.cornerTL, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerTLColor" value="' + esc(current.cornerTLColor || '0') + '">' +
'      <select id="cornerTR">' + cornerContentOptionsHtml(current.cornerTR, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerTRColor" value="' + esc(current.cornerTRColor || '0') + '">' +
'      <select id="cornerBL">' + cornerContentOptionsHtml(current.cornerBL, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerBLColor" value="' + esc(current.cornerBLColor || '0') + '">' +
'      <select id="cornerBR">' + cornerContentOptionsHtml(current.cornerBR, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="cornerBRColor" value="' + esc(current.cornerBRColor || '0') + '">' +
'      <select id="upperMiddleLine1Content">' + cornerContentOptionsHtml(current.upperMiddleLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="upperMiddleLine1Color" value="' + esc(current.upperMiddleLine1Color || '0') + '">' +
'      <select id="upperMiddleLine2Content">' + cornerContentOptionsHtml(current.upperMiddleLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="upperMiddleLine2Color" value="' + esc(current.upperMiddleLine2Color || '0') + '">' +
'      <select id="bottomMiddleLine1Content">' + cornerContentOptionsHtml(current.bottomMiddleLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="bottomMiddleLine1Color" value="' + esc(current.bottomMiddleLine1Color || '0') + '">' +
'      <select id="bottomMiddleLine2Content">' + cornerContentOptionsHtml(current.bottomMiddleLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="bottomMiddleLine2Color" value="' + esc(current.bottomMiddleLine2Color || '0') + '">' +
'      <select id="middleLeftLine1Content">' + cornerContentOptionsHtml(current.middleLeftLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleLeftLine1Color" value="' + esc(current.middleLeftLine1Color || '0') + '">' +
'      <select id="middleLeftLine2Content">' + cornerContentOptionsHtml(current.middleLeftLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleLeftLine2Color" value="' + esc(current.middleLeftLine2Color || '0') + '">' +
'      <select id="middleRightLine1Content">' + cornerContentOptionsHtml(current.middleRightLine1Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleRightLine1Color" value="' + esc(current.middleRightLine1Color || '0') + '">' +
'      <select id="middleRightLine2Content">' + cornerContentOptionsHtml(current.middleRightLine2Content, current.auroraEnabled) + '</select>' +
'      <input type="hidden" id="middleRightLine2Color" value="' + esc(current.middleRightLine2Color || '0') + '">' +
'    </div>' +

'    <div class="subsection">' +
'      <label for="stepGoal">Daily step goal (used by "Step goal %")</label>' +
'      <input type="number" id="stepGoal" min="1000" max="60000" step="500" value="' + esc(current.stepGoal || '10000') + '">' +
'      <div class="help">Pebble doesn\'t expose a system step goal, so this app keeps its own -- same as every other Pebble health app.</div>' +
'    </div>' +

'    <div class="checkbox-row subsection" id="drawFeaturesBeneathHandsRow">' +
'      <input type="checkbox" id="drawFeaturesBeneathHands" ' + (current.drawFeaturesBeneathHands ? 'checked' : '') + ' onchange="updatePreview()">' +
'      <label for="drawFeaturesBeneathHands" style="margin:0;">Draw features beneath hands</label>' +
'    </div>' +
'    <div class="help">Big-analog mode only -- corners/edges info here normally draws on top of the hands; enable this to tuck it underneath instead.</div>' +
'    </div>' +
'  </fieldset>' +
'  <fieldset>' +
    sectionLegendHtml('colors', 'Colors') +
'    <div class="section-body" id="section-colors" style="display:none;">' +

'    <div class="subsection" id="daySchemeSubsection">' +
'      <label>Colors<span class="scheme-active-badge" id="daySchemeActiveBadge" style="display:none;">Active now</span></label>' +
'      <div class="color-role-buttons">' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'text\')">' +
'          <span class="color-role-swatch" id="swatchMain" style="background:' + esc(initialColors.text) + ';"></span>' +
'          <span class="color-role-label">Main</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'accent\')">' +
'          <span class="color-role-swatch" id="swatchAccent" style="background:' + esc(initialColors.accent) + ';"></span>' +
'          <span class="color-role-label">Accent</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'bg\')">' +
'          <span class="color-role-swatch" id="swatchBg" style="background:' + esc(initialColors.bg) + ';"></span>' +
'          <span class="color-role-label">Background</span>' +
'        </button>' +
'      </div>' +
'      <label style="margin-top:12px;">Or pick a preset</label>' +
'      <button type="button" class="color-preset-btn color-preset-trigger" id="colorSchemePresetTrigger" onclick="openColorPresetPicker(\'day\')">' +
'        <span class="color-preset-trigger-text">' +
'          <span class="color-preset-main-line"></span>' +
'          <span class="color-preset-accent-line"></span>' +
'        </span>' +
'        <span class="color-preset-chevron">&rsaquo;</span>' +
'      </button>' +
'      <div class="help">Applies that preset\'s three colors immediately -- picking one is the same as tapping each swatch above and choosing that exact color.</div>' +
'      <input type="hidden" id="customBgValue" value="' + esc(current.customBg || '255') + '">' +
'      <input type="hidden" id="customTextValue" value="' + esc(current.customText || '192') + '">' +
'      <input type="hidden" id="customAccentValue" value="' + esc(current.customAccent || '192') + '">' +
'    </div>' +

'    <div class="modal-overlay" id="colorPickerModal">' +
'      <div class="modal-box">' +
'        <div class="modal-title" id="colorPickerTitle">Pick a color</div>' +
'        <div class="hex-grid" id="hexColorGrid"></div>' +
'        <button type="button" class="modal-cancel-btn" onclick="closeColorPicker()">Cancel</button>' +
'      </div>' +
'    </div>' +

// Shared by both the day and night "Or pick a preset" triggers --
// which one is currently open lives in CURRENT_PRESET_SCHEME (see
// openColorPresetPicker() below), same "one popup, not two near-
// identical copies" shape the font picker uses for its own 4 roles.
// "Custom" is just the last button in the same list (see
// renderColorPresetGrid()'s own comment) rather than a separate
// footer action -- tapping it, same as tapping outside the popup,
// closes without changing anything.
'    <div class="modal-overlay" id="colorPresetPickerModal" onclick="if (event.target === this) closeColorPresetPicker();">' +
'      <div class="modal-box">' +
'        <div class="modal-title" id="colorPresetPickerTitle">Color preset</div>' +
'        <div class="modal-scroll-body" id="colorPresetPickerGrid"></div>' +
'      </div>' +
'    </div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="nightEnabled" ' + (current.nightEnabled ? 'checked' : '') + ' onchange="onNightToggle()">' +
'      <label for="nightEnabled" style="margin:0;">Use different colors at night</label>' +
'    </div>' +
'    <div id="nightSchemeSettings" style="' + (current.nightEnabled ? '' : 'display:none;') + '">' +
'      <label>Night colors<span class="scheme-active-badge" id="nightSchemeActiveBadge" style="display:none;">Active now</span></label>' +
'      <div class="color-role-buttons">' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'text\', \'night\')">' +
'          <span class="color-role-swatch" id="swatchNightMain" style="background:' + esc(initialNightColors.text) + ';"></span>' +
'          <span class="color-role-label">Main</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'accent\', \'night\')">' +
'          <span class="color-role-swatch" id="swatchNightAccent" style="background:' + esc(initialNightColors.accent) + ';"></span>' +
'          <span class="color-role-label">Accent</span>' +
'        </button>' +
'        <button type="button" class="color-role-btn" onclick="openColorPicker(\'bg\', \'night\')">' +
'          <span class="color-role-swatch" id="swatchNightBg" style="background:' + esc(initialNightColors.bg) + ';"></span>' +
'          <span class="color-role-label">Background</span>' +
'        </button>' +
'      </div>' +
'      <label style="margin-top:12px;">Or pick a preset</label>' +
'      <button type="button" class="color-preset-btn color-preset-trigger" id="nightSchemePresetTrigger" onclick="openColorPresetPicker(\'night\')">' +
'        <span class="color-preset-trigger-text">' +
'          <span class="color-preset-main-line"></span>' +
'          <span class="color-preset-accent-line"></span>' +
'        </span>' +
'        <span class="color-preset-chevron">&rsaquo;</span>' +
'      </button>' +
'      <div class="help">Applies that preset\'s three colors immediately -- picking one is the same as tapping each swatch above and choosing that exact color.</div>' +
'      <input type="hidden" id="nightCustomBgValue" value="' + esc(current.nightCustomBg || '192') + '">' +
'      <input type="hidden" id="nightCustomTextValue" value="' + esc(current.nightCustomText || '255') + '">' +
'      <input type="hidden" id="nightCustomAccentValue" value="' + esc(current.nightCustomAccent || '255') + '">' +
'    </div>' +
'    <div class="help">Battery and Moon phase are now pickable as Features content below, with their own color style.</div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('animation', 'Animation') +
'    <div class="section-body" id="section-animation" style="display:none;">' +
'    <div class="subsection">' +
'      <label>Startup clock animation</label>' +
      verticalButtonGroupHtml('startupClockAnimModeGroup', 'startupClockAnimMode', [
        { value: '0', label: 'Off' },
        { value: '1', label: 'Animate clock' },
        { value: '2', label: 'Planet sweep time shift' }
      ], current.startupClockAnimMode || '1') +
'    </div>' +
'    <div class="help">On launch, the hands/digits sweep in from a cold-start position up to the current time, under 1.5s, instead of just appearing already showing it. "Planet sweep time shift" plays the same sweep but chases the same couple-hours-ago starting point the "Planets" background animation below is itself sweeping through, so the hands and the sky advance together -- if that background animation isn\'t also set to Planets, this behaves the same as "Animate clock" since there\'s no time shift to follow.</div>' +

'    <div class="subsection">' +
'      <label>Animate background on start</label>' +
      verticalButtonGroupHtml('bgAnimModeGroup', 'bgAnimMode', [
        { value: '0', label: 'Off' },
        { value: '1', label: 'Planets (Sun/Moon/planets + sky sweep in from a couple hours ago)' },
        { value: '2', label: 'Indices (analog hour indices animate in; seconds draw normally)' }
      ], current.bgAnimMode || '0') +
'    </div>' +
'    <div class="help">Off by default: exactly one of the above sweeps into place on launch, under 1.5s.</div>' +

'    <div class="subsection">' +
'      <label>On shake animation</label>' +
      verticalButtonGroupHtml('shakeAnimModeGroup', 'shakeAnimMode', [
        { value: '0', label: 'Off' },
        { value: '1', label: 'Smooth second hand' },
        { value: '2', label: 'Planet seek' },
        { value: '3', label: 'Both' }
      ], current.shakeAnimMode || '0') +
'    </div>' +
'    <div class="help">Off by default: runs for as long as the shake labels stay up (see the duration slider just below). "Planet seek" points the sky view at whichever 90&deg; slice of the horizon your compass currently faces, repositioning the Sun/Moon/planets to match as you turn -- weather is hidden for the duration, and it never runs on a day with an eclipse. "Both" runs Smooth second hand and Planet seek together; picking just one of the two runs only that one.</div>' +

'    <div class="subsection">' +
'      <div class="slider-row">' +
'        <label for="shakeLabelSeconds">Shake animations duration <span class="val" id="shakeLabelSecondsVal">' + esc(current.shakeLabelSeconds || '3') + 's</span></label>' +
'        <div class="slider-with-buttons">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'shakeLabelSeconds\', -1)">&minus;</button>' +
'          <input type="range" id="shakeLabelSeconds" min="0" max="30" step="1" value="' + esc(current.shakeLabelSeconds || '3') + '" oninput="document.getElementById(\'shakeLabelSecondsVal\').textContent = this.value + \'s\';">' +
'        <button type="button" class="slider-step-btn" onclick="stepSlider(\'shakeLabelSeconds\', 1)">+</button>' +
'        </div>' +
'      </div>' +
'    </div>' +
'    <div class="help">How long a shake keeps "Smooth second hand"/"Planet seek" above running, and how long shake-revealed star name labels stay on screen -- both share this one window.</div>' +
'    </div>' +

'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('presets', 'My Style Presets') +
'    <div class="section-body" id="section-presets" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <div class="help">Save up to 6 quick-recall snapshots of your whole Style + Colors + Features design below, or export/import it as JSON to back it up or share it.</div>' +

'    ' + [1, 2, 3, 4, 5, 6].map(function (n) { return presetSlotHtml(current, n); }).join('') +

'    <label for="presetExportBox" style="margin-top:12px;">Export current design</label>' +
'    <button type="button" class="secondary-btn" onclick="exportDesignJson()">Generate JSON</button>' +
'    <button type="button" class="secondary-btn" onclick="copyExportBoxToClipboard()">Copy to clipboard</button>' +
'    <textarea id="presetExportBox" readonly rows="6" style="margin-top:8px; font-family:monospace; font-size:11px;" onclick="this.select();"></textarea>' +
'    <div class="help" id="presetExportStatus">Tap the box above then copy the text -- covers everything in the Style, Colors, and Features sections.</div>' +

'    <label for="presetImportBox" style="margin-top:12px;">Import a design</label>' +
'    <button type="button" class="secondary-btn" onclick="pasteImportBoxFromClipboard()">Paste from clipboard</button>' +
'    <textarea id="presetImportBox" rows="6" placeholder="Paste JSON here" style="margin-top:8px; font-family:monospace; font-size:11px;"></textarea>' +
'    <button type="button" class="secondary-btn" onclick="importDesignJson()">Apply</button>' +
'    <div class="help" id="presetImportStatus"></div>' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('weather', 'Weather') +
'    <div class="section-body" id="section-weather" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <div class="help">Cloud cover is always pulled from Open-Meteo (no signup needed). Optionally add an OpenWeatherMap API key to average in a second forecast.</div>' +
'    <label for="owmKey">OpenWeatherMap API key (optional)</label>' +
'    <input type="text" id="owmKey" placeholder="leave blank to skip" value="' + esc(current.owmKey) + '">' +

'    <label style="margin-top:10px;">Temperature unit</label>' +
      modeButtonGroupHtml('tempUnitGroup', 'tempUnit', [
        { value: 'C', label: '\u00b0C' },
        { value: 'F', label: '\u00b0F' },
        { value: 'K', label: 'K' }
      ], current.tempUnit || 'C') +
'    <div class="help">Used everywhere temperature is shown, including the Features section below.</div>' +

'    <label style="margin-top:10px;">Wind speed unit</label>' +
      modeButtonGroupHtml('windSpeedUnitGroup', 'windSpeedUnit', [
        { value: 'kmh', label: 'KM/H' },
        { value: 'mph', label: 'MPH' },
        { value: 'ms', label: 'M/S' },
        { value: 'kn', label: 'KNOTS' }
      ], current.windSpeedUnit || 'kmh') +
'    <div class="help">Used by the "Wind" corner content.</div>' +

'    <label style="margin-top:10px;">Air quality index scale</label>' +
      modeButtonGroupHtml('aqiUnitGroup', 'aqiUnit', [
        { value: '0', label: 'US AQI' },
        { value: '1', label: 'EUROPEAN' }
      ], current.aqiUnit || '0') +
'    <div class="help">Used by the "Air quality" corner content.</div>' +

'    <label style="margin-top:10px;">Altitude unit</label>' +
      modeButtonGroupHtml('altitudeUnitGroup', 'altitudeUnit', [
        { value: '0', label: 'METERS' },
        { value: '1', label: 'FEET' }
      ], current.altitudeUnit || '0') +
'    <div class="help">Used by the "Altitude" corner content. Comes from GPS, so it needs "Use GPS automatically" turned on in Location below, and not every phone reports it -- shows "N/A" when it\'s not available.</div>' +

'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('astronomy', 'Astronomy') +
'    <div class="section-body" id="section-astronomy" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <label>Sun &amp; Moon size</label>' +
      modeButtonGroupHtml('sunMoonSizeGroup', 'sunMoonSize', [
        { value: '100', label: 'LARGE', icon: MODE_BTN_ICONS.sunMoon100 },
        { value: '75', label: 'MEDIUM', icon: MODE_BTN_ICONS.sunMoon75 },
        { value: '50', label: 'SMALL', icon: MODE_BTN_ICONS.sunMoon50 },
        { value: '25', label: 'X-SMALL', icon: MODE_BTN_ICONS.sunMoon25 }
      ], current.sunMoonSize || '75') +
'    <div class="help">Ignored during an actual eclipse, which sizes the Sun and Moon by their real geometry instead.</div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="showIss" ' + (current.showIss ? 'checked' : '') + '>' +
'      <label for="showIss" style="margin:0;">Show the ISS when overhead (experimental)</label>' +
'    </div>' +
'    <div class="help">Fetches live orbital data each refresh. Position is a snapshot, not continuously tracked, and doesn\'t account for the station being in Earth\'s shadow -- it can occasionally show when it wouldn\'t really be visible.</div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="auroraEnabled" ' + (current.auroraEnabled ? 'checked' : '') + ' onchange="onAuroraEnabledChange()">' +
'      <label for="auroraEnabled" style="margin:0;">Show auroras (experimental)</label>' +
'    </div>' +
'    <div class="help">Fetches NOAA\'s current planetary Kp index each refresh and estimates whether it\'s bright enough to reach your latitude -- a rough approximation (real aurora visibility also depends on local weather/light pollution), not a precise forecast. When on, an "Aurora Kp index" option becomes available in the Features section below, and the sky itself paints a faint aurora glow when conditions and darkness line up. Turning this off removes that option from every feature slot it might currently be set to.</div>' +

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="vibrateOnPhaseChange" ' + (current.vibrateOnPhaseChange ? 'checked' : '') + '>' +
'      <label for="vibrateOnPhaseChange" style="margin:0;">Vibrate when the eclipse reaches its next phase</label>' +
'    </div>' +
'    <div class="help">A brief double buzz right as C1/C2/C3/C4 happens -- not on ordinary day-to-day changes.</div>' +

'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('location', 'Location') +
'    <div class="section-body" id="section-location" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <div class="checkbox-row">' +
'      <input type="checkbox" id="autoLoc" ' + autoLocChecked + ' onchange="toggleManual()">' +
'      <label for="autoLoc" style="margin:0;">Use phone GPS automatically</label>' +
'    </div>' +
'    <label for="locationSearch">Search for a place</label>' +
'    <div style="display:flex; gap:6px;">' +
'      <input type="text" id="locationSearch" style="flex:1;" placeholder="e.g. Innsbruck, Austria" ' + manualDisabled + '>' +
'      <button type="button" id="locationSearchBtn" class="secondary-btn" style="width:auto; margin-top:0; padding:8px 14px;" onclick="searchLocation()" ' + manualDisabled + '>Find</button>' +
'    </div>' +
'    <div class="help" id="locationSearchStatus"></div>' +
'    <label for="lat">Manual latitude (decimal degrees)</label>' +
'    <input type="number" step="any" id="lat" ' + manualDisabled + ' placeholder="e.g. 40.7128" value="' + esc(current.lat) + '" oninput="onManualCoordsInput()" onblur="onManualCoordsBlur()">' +
'    <label for="lon">Manual longitude (decimal degrees)</label>' +
'    <input type="number" step="any" id="lon" ' + manualDisabled + ' placeholder="e.g. -74.0060" value="' + esc(current.lon) + '" oninput="onManualCoordsInput()" onblur="onManualCoordsBlur()">' +
'    <div class="help">Only used when GPS is turned off above.</div>' +
// Cached resolved display name for the coordinates above -- see the
// Location sub-header's own compute function and
// reverseGeocodeAndCacheLocationName() further down for how this gets
// filled in and kept from being re-looked-up on every page load.
'    <input type="hidden" id="locationName" value="' + esc(current.locationName || '') + '">' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('updates', 'Updates') +
'    <div class="section-body" id="section-updates" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <div class="checkbox-row">' +
'      <input type="checkbox" id="batterySaverEnabled" ' + (current.batterySaverEnabled ? 'checked' : '') + '>' +
'      <label for="batterySaverEnabled" style="margin:0;">Preserve battery when watch is not in use</label>' +
'    </div>' +
'    <div class="help">If the watch goes 2 hours without being shaken, it redraws only once a minute and shows "Zzz" where the eclipse status normally sits; after 4 hours that drops further to once every 5 minutes ("Zzzzzzz"), and the phone holds off on its own periodic refresh until the next full hour too. Any shake wakes it back up immediately. Off by default.</div>' +
'    <div class="slider-row">' +
'      <label for="updateMins">Refresh interval <span class="val" id="updateMinsVal">' + esc(current.updateMins) + ' min</span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'updateMins\', -5)">&minus;</button>' +
'        <input type="range" id="updateMins" min="5" max="60" step="5" value="' + esc(current.updateMins) + '" oninput="document.getElementById(\'updateMinsVal\').textContent = this.value + \' min\';">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'updateMins\', 5)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">The watch won\'t re-fetch more often than this unless your location changes by more than ~10km.</div>' +
'    <button type="button" class="secondary-btn" onclick="save(true)">Force refresh now</button>' +
'    <div class="help">Fetches fresh data on the same terms as a normal refresh, bypassing the "don\'t refetch if recent and unmoved" skip. See the Debug section below to inspect exactly what gets sent.</div>' +

'    <div class="help" style="margin-top:14px;">Service status, as of when this page was opened -- gray: never used yet; green: last fetch worked; yellow: last fetch failed but some of the last 10 worked; red: last 10 all failed. Tap the (i) on yellow/red for details.</div>' +
      serviceStatusRowsHtml(current) +
'    <input type="hidden" id="serviceLogsJson" value="' + esc(JSON.stringify(current.serviceLogs || {})) + '">' +
'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('other', 'Other') +
'    <div class="section-body" id="section-other" style="display:none;">' +
'    <div class="subsection"></div>' +

'    <label>Hourly vibrations</label>' +
      verticalButtonGroupHtml('hourlyVibeModeGroup', 'hourlyVibeMode', [
        { value: '0', label: 'Off' },
        { value: '1', label: 'On full hours' },
        { value: '2', label: 'Every X minutes' }
      ], current.hourlyVibeMode || '0', 'onHourlyVibeModeChange') +

'    <div id="hourlyVibeSubOptions" class="' + ((current.hourlyVibeMode || '0') === '0' ? 'grayed-out' : '') + '">' +

'    <div class="slider-row" id="hourlyVibeIntervalRow" style="' + ((current.hourlyVibeMode || '0') === '2' ? '' : 'display:none;') + '">' +
'      <label for="hourlyVibeIntervalMin">Minutes interval <span class="val" id="hourlyVibeIntervalMinVal">' + esc(current.hourlyVibeIntervalMin || '30') + '</span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'hourlyVibeIntervalMin\', -1)">&minus;</button>' +
'      <input type="range" id="hourlyVibeIntervalMin" min="1" max="180" step="1" value="' + esc(current.hourlyVibeIntervalMin || '30') + '" oninput="document.getElementById(\'hourlyVibeIntervalMinVal\').textContent = this.value;">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'hourlyVibeIntervalMin\', 1)">+</button>' +
'      </div>' +
'    </div>' +

'    <label style="margin-top:12px;">Vibration</label>' +
      verticalButtonGroupHtml('hourlyVibePatternGroup', 'hourlyVibePattern', [
        { value: '0', label: 'Short' },
        { value: '1', label: 'Double' },
        { value: '2', label: 'Long' }
      ], current.hourlyVibePattern || '0') +

'    <label style="margin-top:12px;" for="hourlyVibeStartTime">Start time</label>' +
'    <input type="time" id="hourlyVibeStartTime" value="' + esc(current.hourlyVibeStartTime || '00:00') + '" onchange="onHourlyVibeTimeChange(\'hourlyVibeStartTime\')">' +
'    <label for="hourlyVibeEndTime">End time</label>' +
'    <input type="time" id="hourlyVibeEndTime" value="' + esc(current.hourlyVibeEndTime || '00:00') + '" onchange="onHourlyVibeTimeChange(\'hourlyVibeEndTime\')">' +
'    <div class="help">Both ends included -- e.g. 8:00 to 22:00 vibrates at 8:00 and at 22:00, not just in between. Leave both at 00:00 (the default) for all 24 hours. "On full hours" mode rounds these to the nearest whole hour automatically.</div>' +

'    <label style="margin-top:12px;">Active days</label>' +
'    <div class="day-toggle-group" id="hourlyVibeDaysGroup">' +
      HOURLY_VIBE_DAY_LABELS.map(function (label, i) {
        return '<button type="button" class="day-toggle-btn active" data-bit="' + i + '" onclick="toggleHourlyVibeDay(' + i + ')">' + esc(label) + '</button>';
      }).join('') +
'    </div>' +
'    <input type="hidden" id="hourlyVibeDaysMask" value="' + esc(current.hourlyVibeDaysMask == null ? '127' : current.hourlyVibeDaysMask) + '">' +

'    </div>' + // #hourlyVibeSubOptions

(QUIET_TIME_API_AVAILABLE ? (
'    <div class="checkbox-row' + ((current.hourlyVibeMode || '0') === '0' ? ' grayed-out' : '') + '" style="margin-top:12px;" id="hourlyVibeOverrideQuietRow">' +
'      <input type="checkbox" id="hourlyVibeOverrideQuiet" ' + (current.hourlyVibeOverrideQuiet === false ? '' : 'checked') + '>' +
'      <label for="hourlyVibeOverrideQuiet" style="margin:0;">Override quiet time</label>' +
'    </div>' +
'    <div class="help">On (default): hourly vibrations still happen even while your watch\'s Quiet Time is active. Turn off to let Quiet Time suppress them like any other notification.</div>'
    ) : '') +

'    </div>' +
'  </fieldset>' +

'  <fieldset>' +
    sectionLegendHtml('testing', 'Debug') +
'    <div class="section-body" id="section-testing" style="display:none;">' +
'    <div class="subsection"></div>' +
'    <div class="checkbox-row">' +
'      <input type="checkbox" id="testMode" ' + testModeChecked + ' onchange="toggleTestMode()">' +
'      <label for="testMode" style="margin:0;">Use a custom test date/time</label>' +
'    </div>' +
'    <label for="testDateTime">Test date &amp; time</label>' +
'    <input type="datetime-local" id="testDateTime" ' + testDisabled + ' value="' + esc(current.testDateTime) + '">' +
'    <div class="help">Overrides "now" for the eclipse calculation only (e.g. a known historical/future eclipse date), so you can preview the watchface without waiting for one. Set your watch\'s own clock to this same date/time too, so the countdown on-screen lines up with the data sent over.</div>' +
'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="drawDebug" ' + (current.drawDebug ? 'checked' : '') + '>' +
'      <label for="drawDebug" style="margin:0;">Draw debug bounding boxes</label>' +
'    </div>' +
'    <div class="subsection">' +
'      <label>Last 10 raw messages sent to watch</label>' +
'      <div class="help">Chunking (see AppMessage chunking) means one refresh/save now sends several small messages instead of one big one -- these are the individual chunks, most recent first. Tap one to load it below.</div>' +
      rawMessageLogButtonsHtml(current) +
'      <input type="hidden" id="rawMessageLogJson" value="' + esc(JSON.stringify(current.rawMessageLog || [])) + '">' +
'      <label for="debugData" style="margin-top:10px;">Raw data (editable)</label>' +
'      <textarea id="debugData" rows="12" style="width:100%; box-sizing:border-box; font-family:monospace; font-size:11px;">' + esc(debugTextareaInitial) + '</textarea>' +
'      <button type="button" class="secondary-btn" id="copyDebugDataBtn" style="width:auto; margin-top:6px; padding:6px 12px;" onclick="copyTextareaContent(\'debugData\', \'copyDebugDataBtn\')">Copy</button>' +
'      <div class="checkbox-row" style="margin-top:8px;">' +
'        <input type="checkbox" id="debugOverrideEnabled" ' + (current.debugOverrideEnabled ? 'checked' : '') + '>' +
'        <label for="debugOverrideEnabled" style="margin:0;">Override data sent to watch with the text above</label>' +
'      </div>' +
'      <div class="help">Pick a chunk above to load its exact JSON payload here, or edit it freely. Enabling the checkbox sends exactly this text (as one message, unchunked) instead of the normally-computed data on every future refresh, useful for testing specific values without needing real conditions to match. Invalid JSON is ignored and the app falls back to normal data rather than failing to send anything.</div>' +
'    </div>' +

'    <div class="subsection">' +
'      <label>Full keyset (every current value)</label>' +
'      <div class="help">Regenerated fresh every time this page opens: every key the watch could receive, filled in with whatever\'s actually configured right now (settings) plus the last real eclipse/weather/astronomy data that was computed (may be blank/zeroed fields if nothing\'s been fetched yet). Edit anything below, then send it as-is -- this bypasses your other settings and the normal data sources entirely for this one send; nothing here gets saved, and Save above is unaffected by it.</div>' +
'      <textarea id="fullKeysetData" rows="16" style="width:100%; box-sizing:border-box; font-family:monospace; font-size:11px;">' + esc(current.fullKeysetJson || '{}') + '</textarea>' +
'      <button type="button" class="secondary-btn" id="copyFullKeysetBtn" style="width:auto; margin-top:6px; padding:6px 12px;" onclick="copyTextareaContent(\'fullKeysetData\', \'copyFullKeysetBtn\')">Copy</button>' +
'      <button type="button" class="secondary-btn" style="width:auto; margin-top:6px; margin-left:6px; padding:6px 12px;" onclick="sendFullKeysetToWatch()">Send to watch now</button>' +
'    </div>' +
'    </div>' +
'  </fieldset>' +

'  <div class="save-bar"><button onclick="save()">Save</button></div>' +

'<script>' +
'var MARKER_PREVIEW_IMAGES = ' + JSON.stringify(MARKER_PREVIEW_IMAGES) + ';' +
'var HAND_STYLE_IMAGES = ' + JSON.stringify(HAND_STYLE_IMAGES) + ';' +
'var MARKER_PRESET_IMAGES = ' + JSON.stringify(MARKER_PRESET_IMAGES) + ';' +
'var HAND_STYLE_DIAGRAM_IMAGES = ' + JSON.stringify(HAND_STYLE_DIAGRAM_IMAGES) + ';' +
'var HAND_STYLE_ICON_IMAGES = ' + JSON.stringify(HAND_STYLE_ICON_IMAGES) + ';' +
'var WEATHER_ICON_STYLE_PREVIEWS = ' + JSON.stringify(WEATHER_ICON_STYLE_PREVIEWS) + ';' +
// Matches weather_icon_style\'s own <option> list exactly.
'var WEATHER_ICON_STYLE_NAMES = ["Simple", "Hollow", "Full color"];' +
// Matches the Shape <option> list\'s own order/values exactly (see
// handEditorModalHtml() and STYLE_IDS_BY_NAME in
// scripts/generate-hand-style-icons.js).
'var HAND_STYLE_NAMES = ["Baton", "Galba", "Pencil", "Dauphine", "Sword", "Pomme", "Spade", "Arrow", "Leaf", "Syringe", "Serpentine"];' +
// EXAMPLE_STYLE_PRESETS is { title, description, preset } per slot
// (or null for an empty one) -- the popup below reads title/
// description directly, and applies `.preset` (the same shape
// applyStyleCornersJson() everywhere else already expects) only once
// the user actually taps "Apply this style". EXAMPLE_STYLE_IMAGES is
// needed client-side too now, for the popup's own <img> -- each
// button's own <img src> above is a separate, already-baked-in copy.
'var EXAMPLE_STYLE_PRESETS = ' + JSON.stringify(EXAMPLE_STYLE_PRESETS) + ';' +
'var EXAMPLE_STYLE_IMAGES = ' + JSON.stringify(EXAMPLE_STYLE_IMAGES) + ';' +
'var s_exampleStyleModalSlot = null;' +
'function openExampleStyleModal(n) {' +
'  var entry = EXAMPLE_STYLE_PRESETS[String(n)];' +
'  if (!entry) return;' +
'  s_exampleStyleModalSlot = n;' +
'  var img = document.getElementById("exampleStyleModalImg");' +
'  var src = EXAMPLE_STYLE_IMAGES[String(n)];' +
'  img.style.display = src ? "" : "none";' +
'  img.src = src || "";' +
'  document.getElementById("exampleStyleModalTitle").textContent = entry.title || ("Example " + n);' +
'  document.getElementById("exampleStyleModalDesc").textContent = entry.description || "";' +
'  document.getElementById("exampleStyleModal").className = "modal-overlay open";' +
'}' +
'function closeExampleStyleModal() {' +
'  document.getElementById("exampleStyleModal").className = "modal-overlay";' +
'  s_exampleStyleModalSlot = null;' +
'}' +
'function confirmExampleStyleApply() {' +
'  var entry = s_exampleStyleModalSlot != null ? EXAMPLE_STYLE_PRESETS[String(s_exampleStyleModalSlot)] : null;' +
'  closeExampleStyleModal();' +
'  if (entry && entry.preset) applyStyleCornersJson(entry.preset);' +
'}' +
'function toggleManual() {' +
'  var on = !document.getElementById("autoLoc").checked;' +
'  document.getElementById("lat").disabled = !on;' +
'  document.getElementById("lon").disabled = !on;' +
'  document.getElementById("locationSearch").disabled = !on;' +
'  document.getElementById("locationSearchBtn").disabled = !on;' +
'}' +
'function toggleTestMode() {' +
'  document.getElementById("testDateTime").disabled = !document.getElementById("testMode").checked;' +
'}' +
// Loads raw message log entry `i` (index into rawMessageLogJson, same
// oldest-first order rawMessageLogButtonsHtml() built its button ids
// from) into the debugData textarea, and highlights the button that
// was tapped.
'function loadRawMessage(i) {' +
'  var all = [];' +
'  try { all = JSON.parse(document.getElementById("rawMessageLogJson").value || "[]"); } catch (e) {}' +
'  var entry = all[i];' +
'  if (!entry) return;' +
'  document.getElementById("debugData").value = JSON.stringify(entry.dict, null, 2);' +
'  var grid = document.getElementById("rawLogBtnGrid");' +
'  if (grid) {' +
'    var btns = grid.getElementsByClassName("raw-log-btn");' +
'    for (var j = 0; j < btns.length; j++) btns[j].className = "raw-log-btn";' +
'  }' +
'  var btn = document.getElementById("rawLogBtn" + i);' +
'  if (btn) btn.className = "raw-log-btn active";' +
'}' +
'function copyTextareaContent(textareaId, btnId) {' +
'  var ta = document.getElementById(textareaId);' +
'  ta.focus();' +
'  ta.select();' +
'  ta.setSelectionRange(0, 999999);' +
'  var ok = false;' +
'  try { ok = document.execCommand("copy"); } catch (e) {}' +
'  var btn = document.getElementById(btnId);' +
'  if (btn) {' +
'    var original = btn.textContent;' +
'    btn.textContent = ok ? "Copied!" : "Copy failed";' +
'    setTimeout(function () { btn.textContent = original; }, 1500);' +
'  }' +
'}' +
// The "Send full keyset to watch now" button below -- reads whatever
// is CURRENTLY in the fullKeysetData textarea (the auto-generated
// snapshot from page load, or whatever the person edited it to since
// then), closes the settings page carrying it plus a marker flag, and
// returns. index.js's webviewclosed handler checks for that flag
// before anything else and, if set, parses this text and sends it
// chunked to the watch AS-IS -- skipping every normal setSetting()/
// sendFlatDict()/refreshAndSend() call entirely, so nothing else
// changes and nothing here gets persisted. A deliberately different,
// parallel exit from the page than the normal Save button\'s -- Save
// itself never sets this flag, so an ordinary save is completely
// unaffected by this textarea\'s contents.
'function sendFullKeysetToWatch() {' +
'  var text = document.getElementById("fullKeysetData").value;' +
'  try {' +
'    JSON.parse(text);' +
'  } catch (e) {' +
'    alert("Not valid JSON -- fix the text before sending:\\n\\n" + e.message);' +
'    return;' +
'  }' +
'  var returnTo = getQueryParam("return_to", "pebblejs://close#");' +
'  var payload = { CONFIG_SEND_FULL_KEYSET: true, CONFIG_FULL_KEYSET_DATA: text };' +
'  document.location = returnTo + encodeURIComponent(JSON.stringify(payload));' +
'}' +
// Boils a Nominatim `address` object (city/town/village/... + country)
// down to the short "City, Country" form the Location sub-header shows
// (see computeLocationSubheader() below) -- falls back gracefully to
// whichever half is actually present rather than requiring both.
'function shortNameFromAddress(addr) {' +
'  if (!addr) return "";' +
'  var place = addr.city || addr.town || addr.village || addr.municipality || addr.county || addr.state || "";' +
'  var country = addr.country || "";' +
'  if (place && country) return place + ", " + country;' +
'  return place || country;' +
'}' +
// Coordinates just changed by hand -- the cached name (if any) no
// longer necessarily matches them, so clear it rather than let a
// now-stale "Use Innsbruck, Austria" linger over different
// coordinates; onManualCoordsBlur() below re-resolves it once typing
// stops, same "don\'t look it up on every keystroke" reasoning as not
// re-fetching on every settings page load.
'function onManualCoordsInput() {' +
'  var el = document.getElementById("locationName");' +
'  if (el) el.value = "";' +
'}' +
'function onManualCoordsBlur() {' +
'  if (!document.getElementById("autoLoc").checked && !document.getElementById("locationName").value) {' +
'    reverseGeocodeAndCacheLocationName();' +
'  }' +
'}' +
// One-shot reverse geocode of whatever\'s currently in #lat/#lon into
// #locationName (see that hidden field\'s own comment) -- called once
// at page init if a name isn\'t already cached (see the bottom of this
// script) and again from onManualCoordsBlur() above after a manual
// coordinate edit. Silently does nothing on failure/no results,
// leaving the Location sub-header to fall back to raw coordinates --
// this is a "nice to have" label, not something worth surfacing an
// error for.
'function reverseGeocodeAndCacheLocationName() {' +
'  var lat = document.getElementById("lat").value;' +
'  var lon = document.getElementById("lon").value;' +
'  if (!lat || !lon) return;' +
'  var xhr = new XMLHttpRequest();' +
'  xhr.open("GET", "https://nominatim.openstreetmap.org/reverse?format=jsonv2&lat=" + encodeURIComponent(lat) + "&lon=" + encodeURIComponent(lon) + "&zoom=10&addressdetails=1", true);' +
'  xhr.timeout = 8000;' +
'  xhr.onload = function () {' +
'    try {' +
'      var data = JSON.parse(xhr.responseText);' +
'      var name = shortNameFromAddress(data && data.address);' +
'      if (name) {' +
'        document.getElementById("locationName").value = name;' +
'        refreshAllSectionSubheaders();' +
'      }' +
'    } catch (e) {}' +
'  };' +
'  xhr.send();' +
'}' +
'function searchLocation() {' +
'  var query = document.getElementById("locationSearch").value;' +
'  query = query ? query.trim() : "";' +
'  if (!query) return;' +
'  var statusEl = document.getElementById("locationSearchStatus");' +
'  statusEl.textContent = "Searching...";' +
'  var xhr = new XMLHttpRequest();' +
'  xhr.open("GET", "https://nominatim.openstreetmap.org/search?format=json&limit=1&addressdetails=1&q=" + encodeURIComponent(query), true);' +
'  xhr.timeout = 8000;' +
'  xhr.onload = function () {' +
'    try {' +
'      var results = JSON.parse(xhr.responseText);' +
'      if (results && results.length > 0) {' +
'        document.getElementById("lat").value = parseFloat(results[0].lat).toFixed(5);' +
'        document.getElementById("lon").value = parseFloat(results[0].lon).toFixed(5);' +
'        document.getElementById("locationName").value = shortNameFromAddress(results[0].address) || query;' +
'        statusEl.textContent = "Found: " + (results[0].display_name || query);' +
'        refreshAllSectionSubheaders();' +
'      } else {' +
'        statusEl.textContent = "No results found.";' +
'      }' +
'    } catch (e) {' +
'      statusEl.textContent = "Search failed.";' +
'    }' +
'  };' +
'  xhr.onerror = function () { statusEl.textContent = "Network error."; };' +
'  xhr.ontimeout = function () { statusEl.textContent = "Timed out."; };' +
'  xhr.send();' +
'}' +

'function packedByteFor(r2,g2,b2) { return 0xC0 | (r2<<4) | (g2<<2) | b2; }' +
'function chHex(v) { var h = (v*85).toString(16); return h.length<2 ? "0"+h : h; }' +
'function hexFor(r2,g2,b2) { return "#" + chHex(r2) + chHex(g2) + chHex(b2); }' +
'function hexFromByte(byte) { return hexFor((byte>>4)&3, (byte>>2)&3, byte&3); }' +

// Runtime copy of the generator-side COLOR_SCHEMES table -- serialized
// straight from that same array at page-generation time, same "can't
// drift apart" reasoning as FONT_LOOKUP's own runtime copy. Shared by
// findPresetById() below and the color preset picker popup further
// down (matchingPresetId()/renderColorPresetGrid()).
'var COLOR_SCHEMES = ' + JSON.stringify(COLOR_SCHEMES) + ';' +

'function findPresetById(id) {' +
'  for (var i = 0; i < COLOR_SCHEMES.length; i++) {' +
'    if (String(COLOR_SCHEMES[i].id) === String(id)) return COLOR_SCHEMES[i];' +
'  }' +
'  return COLOR_SCHEMES[0];' +
'}' +

'function colorsFor(bgId, textId, accentId) {' +
'  return {' +
'    bg: hexFromByte(parseInt(document.getElementById(bgId).value, 10)),' +
'    text: hexFromByte(parseInt(document.getElementById(textId).value, 10)),' +
'    accent: hexFromByte(parseInt(document.getElementById(accentId).value, 10))' +
'  };' +
'}' +
'function dayColors() {' +
'  return colorsFor("customBgValue", "customTextValue", "customAccentValue");' +
'}' +
'function nightColors() {' +
'  return colorsFor("nightCustomBgValue", "nightCustomTextValue", "nightCustomAccentValue");' +
'}' +
// The color every font-picker preview (both the masked system-font
// images and the live Google-Fonts CSS text) paints itself in. This is
// deliberately NOT derived from the watch's own color scheme -- doing
// that used to make preview text/images render in whatever hue the
// day scheme's text role happened to be, which could turn barely
// legible against the picker's own background. It's the same neutral
// --text-strong the rest of this settings page's UI text already
// uses (see .font-picker-preview/-name's own CSS), so previews stay
// readable and consistent with the page's own light/dark theme
// (prefers-color-scheme) regardless of what the WATCH's day/night
// color scheme happens to be.
'function fontPreviewColor() {' +
'  return "var(--text-strong)";' +
'}' +

'function canvasFontFor(previewCss, px) {' +
'  var familyMatch = /font-family:\\s*([^;]+);?/.exec(previewCss);' +
'  var family = familyMatch ? familyMatch[1] : "sans-serif";' +
'  var weightMatch = /font-weight:\\s*([^;]+);?/.exec(previewCss);' +
'  var weight = weightMatch ? weightMatch[1].trim() : "400";' +
'  var boldPrefix = (parseInt(weight, 10) >= 600 || weight === "bold") ? "bold " : "";' +
'  var italicPrefix = /font-style:\\s*italic/.test(previewCss) ? "italic " : "";' +
'  return italicPrefix + boldPrefix + px + "px " + family;' +
'}' +

// Runtime copy of this file's own top-level esc() -- needed here since
// renderFontPickerGrid() below builds HTML strings client-side, in the
// webview\'s own separate JS context, which can\'t reach that generator-
// side function.
'function esc(str) {' +
'  return String(str == null ? "" : str).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;").replace(/>/g, "&gt;");' +
'}' +

// Runtime copy of the generator-side FONT_LOOKUP table (see its own
// comment there for what each field means) -- serialized straight
// from that same array at page-generation time, not hand-duplicated,
// so the two can never drift apart the way two independently-typed
// copies could.
'var FONT_LOOKUP = ' + JSON.stringify(FONT_LOOKUP) + ';' +
// Runtime copy of the constant controlling how many "Example styles"
// tiles exist -- used only by the Example styles section's own
// sub-header (computeExamplesSubheader() below); that count never
// changes at runtime, so this is just a plain baked-in number, same
// spirit as every other "generator-side data, copied once for the
// browser" constant on this page.
'var EXAMPLE_STYLE_COUNT = ' + EXAMPLE_STYLE_COUNT + ';' +
// Same reasoning as FONT_LOOKUP just above -- serialized straight from
// the generator-side FONT_PREVIEW_IMAGES (itself just require()'d from
// the generated src/pkjs/font-preview-images.js) rather than
// hand-duplicated. Empty ({}) whenever no font-preview PNGs have been
// added yet -- every lookup against it below already handles that
// gracefully.
'var FONT_PREVIEW_IMAGES = ' + JSON.stringify(FONT_PREVIEW_IMAGES) + ';' +
'function fontLookupEntry(id) {' +
'  id = parseInt(id, 10);' +
'  for (var i = 0; i < FONT_LOOKUP.length; i++) {' +
'    if (FONT_LOOKUP[i].id === id) return FONT_LOOKUP[i];' +
'  }' +
'  return FONT_LOOKUP[0];' +
'}' +
// Scales a font's real on-watch bake size (12-48px) down to something
// that reads clearly inside a compact picker-button preview without
// the biggest ones (the 48px-baked main clock faces) overflowing it --
// same relative ordering as the real sizes (a 12px font\'s preview is
// noticeably smaller than a 48px font\'s), just compressed into a
// roughly 11-26px on-screen range.
'function fontPickerPreviewPx(sizePx) {' +
'  return Math.max(11, Math.min(26, Math.round(sizePx * 0.5)));' +
'}' +

// One entry per font-picker "role" -- which underlying <select> and
// trigger button it drives, whether it\'s restricted to FONT_LOOKUP\'s
// own `mainClock` subset (only the Clock font picker is), whether it
// gets the "Show incompatible fonts" checkbox (only the pickers that
// default to hiding ~48px-scale display fonts do -- see FONT_LOOKUP\'s
// own `small` comment), and what sample text its buttons preview --
// value depends on the picker\'s own role per the request: a clock
// reads a time, a corner/edge feature reads a temperature, and the
// numerals picker reads a number (Roman if that checkbox is on).
'var FONT_PICKER_ROLES = {' +
'  clock: { selectId: "clockFont", triggerId: "clockFontTrigger", title: "Clock font", onlyMainClock: true, showIncompatibleToggle: false,' +
'    previewText: function () { return "12:34"; } },' +
'  cornerFont: { selectId: "cornerFont", triggerId: "cornerFontTrigger", title: "Font", onlyMainClock: false, showIncompatibleToggle: true,' +
'    previewText: function () { return "-10\\u00b0C"; } },' +
'  markerTextFont: { selectId: "markerTextFont", triggerId: "markerTextFontTrigger", title: "Font", onlyMainClock: false, showIncompatibleToggle: false,' +
'    previewText: function () { var roman = document.getElementById("markerTextRoman"); return (roman && roman.checked) ? "XII" : "12"; } }' +
'};' +
'var currentFontPickerRole = null;' +

// Builds whatever goes inside a .font-picker-preview cell for one
// (fontId, role) pair -- a real on-watch rendering (see
// FONT_PREVIEW_IMAGES's own comment) when one exists, the existing
// CSS-approximation text otherwise. Shared by updateFontTriggerLabel()
// and renderFontPickerGrid() below so the two can never fall out of
// sync on which fonts actually have a real image.
'function fontPreviewInnerHtml(fontId, role, text) {' +
'  var images = FONT_PREVIEW_IMAGES[fontId];' +
'  var src = images && images[role];' +
'  var color = fontPreviewColor();' +
'  if (src) {' +
'    var maskCss = "-webkit-mask-image:url(\'" + src + "\'); mask-image:url(\'" + src + "\'); background-color:" + color + ";";' +
'    return \'<span class="font-preview-img" role="img" aria-label="\' + esc(text) + \'" style="\' + maskCss + \'"></span>\';' +
'  }' +
'  return \'<span style="color:\' + color + \';">\' + esc(text) + "</span>";' +
'}' +

// Fills one trigger button\'s own preview/name spans from whatever its
// underlying (now display:none) <select> currently holds -- called
// after every pick, plus once at page load, so the collapsed trigger
// always already shows the current font rendered in itself rather
// than plain placeholder text.
'function updateFontTriggerLabel(role) {' +
'  var cfg = FONT_PICKER_ROLES[role];' +
'  if (!cfg) return;' +
'  var sel = document.getElementById(cfg.selectId);' +
'  var trigger = document.getElementById(cfg.triggerId);' +
'  if (!sel || !trigger) return;' +
'  var entry = fontLookupEntry(sel.value);' +
'  var preview = trigger.querySelector(".font-picker-preview");' +
'  var name = trigger.querySelector(".font-picker-name");' +
'  if (preview) {' +
'    preview.setAttribute("style", entry.preview + " font-size:" + fontPickerPreviewPx(entry.sizePx) + "px;");' +
'    preview.innerHTML = fontPreviewInnerHtml(entry.id, role, cfg.previewText());' +
'  }' +
'  if (name) name.textContent = entry.label;' +
'}' +
'function refreshAllFontTriggerLabels() {' +
'  Object.keys(FONT_PICKER_ROLES).forEach(updateFontTriggerLabel);' +
'}' +

'function openFontPicker(role) {' +
'  currentFontPickerRole = role;' +
'  var cfg = FONT_PICKER_ROLES[role];' +
'  document.getElementById("fontPickerTitle").textContent = cfg.title;' +
'  var incompatibleRow = document.getElementById("fontPickerIncompatibleRow");' +
'  incompatibleRow.style.display = cfg.showIncompatibleToggle ? "" : "none";' +
'  if (cfg.showIncompatibleToggle) {' +
'    var currentId = document.getElementById(cfg.selectId).value;' +
'    document.getElementById("fontPickerShowIncompatible").checked = !fontLookupEntry(currentId).small;' +
'  }' +
'  renderFontPickerGrid();' +
'  document.getElementById("fontPickerModal").className = "modal-overlay open";' +
'}' +
'function closeFontPicker() {' +
'  document.getElementById("fontPickerModal").className = "modal-overlay";' +
'}' +
// Rebuilds the grid for whichever role is currently open -- called on
// open and again whenever "Show incompatible fonts" changes, since
// that changes which entries even appear rather than just their
// styling.
'function renderFontPickerGrid() {' +
'  var cfg = FONT_PICKER_ROLES[currentFontPickerRole];' +
'  if (!cfg) return;' +
'  var sel = document.getElementById(cfg.selectId);' +
'  var currentId = parseInt(sel.value, 10);' +
'  var showIncompatible = cfg.showIncompatibleToggle ? document.getElementById("fontPickerShowIncompatible").checked : true;' +
'  var previewText = cfg.previewText();' +
'  var html = "";' +
'  FONT_LOOKUP.forEach(function (f) {' +
'    if (cfg.onlyMainClock && !f.mainClock) return;' +
'    if (!showIncompatible && !f.small && f.id !== currentId) return;' +
'    var previewStyle = f.preview + " font-size:" + fontPickerPreviewPx(f.sizePx) + "px;";' +
'    html += \'<button type="button" class="font-picker-btn\' + (f.id === currentId ? " selected" : "") + \'" onclick="chooseFontOption(\' + f.id + \')">\' +' +
'      \'<span class="font-picker-preview" style="\' + previewStyle + \'">\' + fontPreviewInnerHtml(f.id, currentFontPickerRole, previewText) + "</span>" +' +
'      \'<span class="font-picker-name">\' + esc(f.label) + "</span></button>";' +
'  });' +
'  document.getElementById("fontPickerGrid").innerHTML = html;' +
'}' +
'function chooseFontOption(id) {' +
'  var cfg = FONT_PICKER_ROLES[currentFontPickerRole];' +
'  if (!cfg) return;' +
'  var sel = document.getElementById(cfg.selectId);' +
'  sel.value = id;' +
'  if (typeof Event === "function") sel.dispatchEvent(new Event("change"));' +
'  else { var evt = document.createEvent("HTMLEvents"); evt.initEvent("change", true, true); sel.dispatchEvent(evt); }' +
'  closeFontPicker();' +
'  refreshAllFontTriggerLabels();' +
'}' +

'function drawSkyLayer(ctx, x, y, w, h, skyMode, phase) {' +
'  if (skyMode === "2") {' +
'    ctx.fillStyle = "#000000";' +
'    ctx.fillRect(x, y, w, h);' +
'    drawStarsPreview(ctx, x, y, w, h);' +
'    return;' +
'  }' +
'  var g = SKY_PHASE_COLORS[phase] || SKY_PHASE_COLORS.day;' +
'  var grad = ctx.createLinearGradient(0, y, 0, y + h);' +
'  grad.addColorStop(0, g.top);' +
'  grad.addColorStop(1, g.bottom);' +
'  ctx.fillStyle = grad;' +
'  ctx.fillRect(x, y, w, h);' +
'  if (skyMode !== "1") {' + // Clear sky never draws weather; Weather sky gets a cloud regardless of time of day
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

'function drawCornerSlot(ctx, contentId, colorId, x, y, textAlign, colors) {' +
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
'  ctx.font = "bold 9px sans-serif";' +
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
'  if (markerStyle === 8) {' +
'    var hourCfg = readCustomMarkerConfig("hour");' +
'    pct = hourCfg.innerBorderPct; ecc = hourCfg.innerEccentricity;' +
'  } else if (markerStyle <= 2) {' +
'    var hour = MARKER_STYLE_HOUR_PRESETS[markerStyle], sec = MARKER_STYLE_SECOND_PRESETS[markerStyle];' +
'    if (sec.thickness === 0 || hour.innerBorderPct <= sec.innerBorderPct) { pct = hour.innerBorderPct; ecc = hour.innerEccentricity; }' +
'    else { pct = sec.innerBorderPct; ecc = sec.innerEccentricity; }' +
'  } else { pct = 100; ecc = 0; }' + // 9 ("none") or anything unrecognized -- background_marker_inner_reach()'s own "fully retracted" fallback
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

'function drawCornersAndEdges(ctx, w, h, colors, skyBottom) {' +
'  var bottomY = (typeof skyBottom === "number") ? skyBottom : h;' +
'  var lineH = 14;' +
'  if (slotAvailable("cornerTLWrap")) drawCornerSlot(ctx, "cornerTL", "cornerTLColor", 5, 5, "left", colors);' +
'  if (slotAvailable("cornerTRWrap")) drawCornerSlot(ctx, "cornerTR", "cornerTRColor", w - 5, 5, "right", colors);' +
'  if (slotAvailable("cornerBLWrap")) drawCornerSlot(ctx, "cornerBL", "cornerBLColor", 5, bottomY - 14, "left", colors);' +
'  if (slotAvailable("cornerBRWrap")) drawCornerSlot(ctx, "cornerBR", "cornerBRColor", w - 5, bottomY - 14, "right", colors);' +
'  var needMiddleMargins = slotAvailable("upperMiddleWrap") || slotAvailable("bottomMiddleWrap") || slotAvailable("middleLeftWrap") || slotAvailable("middleRightWrap");' +
'  var mm = needMiddleMargins ? computeMiddleFeatureMargins(w, h) : null;' +
'  if (slotAvailable("upperMiddleWrap")) {' +
'    var upperHasLine2 = hasPreviewContent("upperMiddleLine2Content");' +
'    drawCornerSlot(ctx, "upperMiddleLine1Content", "upperMiddleLine1Color", w / 2, upperHasLine2 ? mm.top : mm.top + lineH / 2, "center", colors);' +
'    if (upperHasLine2) drawCornerSlot(ctx, "upperMiddleLine2Content", "upperMiddleLine2Color", w / 2, mm.top + lineH, "center", colors);' +
'  }' +
'  if (slotAvailable("bottomMiddleWrap")) {' +
'    var bottomHasLine2 = hasPreviewContent("bottomMiddleLine2Content");' +
'    drawCornerSlot(ctx, "bottomMiddleLine1Content", "bottomMiddleLine1Color", w / 2, bottomHasLine2 ? bottomY - mm.bottom - lineH : bottomY - mm.bottom - lineH / 2, "center", colors);' +
'    if (bottomHasLine2) drawCornerSlot(ctx, "bottomMiddleLine2Content", "bottomMiddleLine2Color", w / 2, bottomY - mm.bottom, "center", colors);' +
'  }' +
'  if (slotAvailable("middleLeftWrap")) {' +
'    var midLeftHasLine2 = hasPreviewContent("middleLeftLine2Content");' +
'    drawCornerSlot(ctx, "middleLeftLine1Content", "middleLeftLine1Color", mm.left, midLeftHasLine2 ? h / 2 - 4 - lineH / 2 : h / 2 - 4, "left", colors);' +
'    if (midLeftHasLine2) drawCornerSlot(ctx, "middleLeftLine2Content", "middleLeftLine2Color", mm.left, h / 2 - 4 + lineH / 2, "left", colors);' +
'  }' +
'  if (slotAvailable("middleRightWrap")) {' +
'    var midRightHasLine2 = hasPreviewContent("middleRightLine2Content");' +
'    drawCornerSlot(ctx, "middleRightLine1Content", "middleRightLine1Color", w - mm.right, midRightHasLine2 ? h / 2 - 4 - lineH / 2 : h / 2 - 4, "right", colors);' +
'    if (midRightHasLine2) drawCornerSlot(ctx, "middleRightLine2Content", "middleRightLine2Color", w - mm.right, h / 2 - 4 + lineH / 2, "right", colors);' +
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
'  { style: 1, thickness: 1, innerEccentricity: 0, outerEccentricity: 0, innerBorderPct: 65, outerBorderPct: 85, translucent: false, color: 0 },' +
'  { style: 1, thickness: 1, innerEccentricity: 0, outerEccentricity: 0, innerBorderPct: 60, outerBorderPct: 85, translucent: false, color: 0 },' +
'  { style: 2, thickness: 5, innerEccentricity: 0, outerEccentricity: 0, innerBorderPct: 60, outerBorderPct: 85, translucent: false, color: 0 }' +
'];' +
'var MARKER_STYLE_SECOND_PRESETS = [' +
'  { style: 1, thickness: 0, innerEccentricity: 0, outerEccentricity: 0, innerBorderPct: 65, outerBorderPct: 85, translucent: false, color: 0 },' +
'  { style: 1, thickness: 1, innerEccentricity: 0, outerEccentricity: 0, innerBorderPct: 65, outerBorderPct: 85, translucent: false, color: 0 },' +
'  { style: 1, thickness: 1, innerEccentricity: 0, outerEccentricity: 0, innerBorderPct: 65, outerBorderPct: 85, translucent: false, color: 0 }' +
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
'  ctx.font = canvasFontFor(opt.getAttribute("data-preview") || "", 14 * scale);' +
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

'  if (markerStyle <= 2) {' +
'    var idx = markerStyle;' +
'    drawMarkerRing(ctx, cx, cy, w, h, MARKER_STYLE_SECOND_PRESETS[idx], 60, 5, colors);' +
'    drawMarkerRing(ctx, cx, cy, w, h, MARKER_STYLE_HOUR_PRESETS[idx], 12, 0, colors);' +
'  } else if (markerStyle === 8) {' +
'    var secCfg = readCustomMarkerConfig("sec"), hourCfg = readCustomMarkerConfig("hour");' +
'    drawMarkerRing(ctx, cx, cy, w, h, secCfg, 60, 5, colors);' +
'    drawMarkerRing(ctx, cx, cy, w, h, hourCfg, 12, 0, colors);' +
'    drawTextMarkers(ctx, cx, cy, w, h, colors);' +
'  } else if (markerStyle === 9) {' +
'    /* none -- no marker ring, no placeholder text either */' +
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
'function drawDigitalSideFeatures(ctx, w, h, colors, clockArea) {' +
'  var avail = computeSlotAvailability();' +
'  var scale = w / 200;' +
'  var xInset = 5 * scale;' +
'  var row1Y = 152 * scale, row2Y = 176 * scale, row3Y = 200 * scale;' +
'  if (avail.digitalLeft) {' +
'    drawCornerSlot(ctx, "middleLeftLine1Content", "middleLeftLine1Color", xInset, row1Y, "left", colors);' +
'    drawCornerSlot(ctx, "middleLeftLine2Content", "middleLeftLine2Color", xInset, row2Y, "left", colors);' +
'    drawCornerSlot(ctx, "upperMiddleLine1Content", "upperMiddleLine1Color", xInset, row3Y, "left", colors);' +
'  }' +
'  if (avail.digitalRight) {' +
'    drawCornerSlot(ctx, "middleRightLine1Content", "middleRightLine1Color", w - xInset, row1Y, "right", colors);' +
'    drawCornerSlot(ctx, "middleRightLine2Content", "middleRightLine2Color", w - xInset, row2Y, "right", colors);' +
'    drawCornerSlot(ctx, "upperMiddleLine2Content", "upperMiddleLine2Color", w - xInset, row3Y, "right", colors);' +
'  }' +
'  drawCornerSlot(ctx, "bottomMiddleLine1Content", "bottomMiddleLine1Color", clockArea.x + clockArea.w / 2, row3Y, "center", colors);' +
'}' +

'function drawDigitalPreview(ctx, colors, now, showSeconds, w, panelTop, panelBottom, clockArea) {' +
'  var cx = clockArea ? clockArea.x + clockArea.w / 2 : w / 2;' +
'  var fontSel = document.getElementById("clockFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var hh = now.getHours(), mm = now.getMinutes();' +
'  var txt = (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm;' +
'  if (showSeconds) { var ss = now.getSeconds(); txt += ":" + (ss < 10 ? "0" : "") + ss; }' +
'  var clockY = panelTop + (panelBottom - panelTop) * 0.42;' +
// A real on-watch rendering of this font, when one exists, takes over
// the main preview too -- same FONT_PREVIEW_IMAGES asset the font
// PICKER buttons already use (see fontPreviewInnerHtml()), drawn here
// via the standard canvas "draw image, then clip a color fill to its
// alpha" trick rather than CSS mask-image (this is a <canvas>, not a
// DOM element a mask-image could apply to). Only used when seconds
// aren\'t shown -- the baked image is a fixed "12:34", nothing it could
// show a live seconds count with -- the plain font-approximation text
// path below covers that case instead.
'  var images = FONT_PREVIEW_IMAGES[fontSel.value];' +
'  var clockImgSrc = images && images.clock;' +
'  var drewImage = false;' +
'  if (clockImgSrc && !showSeconds) {' +
'    var img = getCachedImage(clockImgSrc);' +
'    if (img.complete && img.naturalWidth > 0) {' +
'      var targetH = 26, targetW = targetH * (img.naturalWidth / img.naturalHeight);' +
'      ctx.save();' +
'      ctx.drawImage(img, cx - targetW / 2, clockY - targetH / 2, targetW, targetH);' +
'      ctx.globalCompositeOperation = "source-in";' +
'      ctx.fillStyle = colors.text;' +
'      ctx.fillRect(cx - targetW / 2, clockY - targetH / 2, targetW, targetH);' +
'      ctx.restore();' +
'      drewImage = true;' +
'    }' +
'  }' +
'  if (!drewImage) {' +
'    ctx.font = canvasFontFor(opt.getAttribute("data-preview") || "", 26);' +
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

'function onBottomStyleChange() {' +
'  var styleVal = document.getElementById("bottomStyleValue").value;' +
'  var isAnalog = styleVal === "analog";' +
'  document.getElementById("digitalOnlySettings").style.display = (styleVal === "digital") ? "block" : "none";' +
'  document.getElementById("bigAnalogSettings").style.display = isAnalog ? "block" : "none";' +
'  updateDigitalSidesVisibility();' +
'  var secondsBox = document.getElementById("showSeconds");' +
'  var fontSel = document.getElementById("clockFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var fontOk = opt.getAttribute("data-seconds") === "1";' +
'  var secondsUnavailable = !isAnalog && !fontOk;' +
'  secondsBox.disabled = secondsUnavailable;' +
'  if (secondsUnavailable) secondsBox.checked = false;' +
'  document.getElementById("secondsHelp").style.display = secondsUnavailable ? "block" : "none";' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
// A font switch can turn side features on/off (the "too wide" check
// depends on the clock font, not just digital-vs-analog), so both this
// and onBottomStyleChange() above call it. If the newly-selected font
// can't support them, forces digitalSides back to "none" -- same
// reasoning clearGrayedSlotsIfUnavailable() clears stale corner/edge picks
// instead of leaving a hidden section quietly keep sending a value the
// watch would otherwise still draw.
'function updateDigitalSidesVisibility() {' +
'  var styleVal = document.getElementById("bottomStyleValue").value;' +
'  var fontSel = document.getElementById("clockFont");' +
'  var opt = fontSel.options[fontSel.selectedIndex];' +
'  var fontIsWide = opt.getAttribute("data-wide") === "1";' +
'  var isDigital = styleVal === "digital";' +
'  document.getElementById("digitalSidesSection").style.display = (isDigital && !fontIsWide) ? "" : "none";' +
'  document.getElementById("digitalSidesWideHelp").style.display = (isDigital && fontIsWide) ? "" : "none";' +
'  if (!isDigital || fontIsWide) {' +
'    document.getElementById("digitalSides").value = "none";' +
'    var buttons = document.getElementById("digitalSidesGroup").getElementsByClassName("mode-btn");' +
'    for (var i = 0; i < buttons.length; i++) buttons[i].className = "mode-btn";' +
'  }' +
'}' +
'function toggleDigitalSide(side) {' +
'  var hidden = document.getElementById("digitalSides");' +
'  var cur = hidden.value;' +
'  var leftOn = cur === "left" || cur === "both";' +
'  var rightOn = cur === "right" || cur === "both";' +
'  if (side === "left") leftOn = !leftOn; else rightOn = !rightOn;' +
'  var next = leftOn && rightOn ? "both" : (leftOn ? "left" : (rightOn ? "right" : "none"));' +
'  hidden.value = next;' +
'  var buttons = document.getElementById("digitalSidesGroup").getElementsByClassName("mode-btn");' +
'  buttons[0].className = "mode-btn" + (leftOn ? " active" : "");' +
'  buttons[1].className = "mode-btn" + (rightOn ? " active" : "");' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
// Runtime copy of presets-lookups.js's own CORNER_CATEGORIES --
// serialized straight from that same canonical array (already
// filtered for the current auroraEnabled state -- see
// cornerCategoriesForClient above) rather than hand-duplicated, same
// pattern as FONT_LOOKUP/COLOR_SCHEMES above. CORNER_PREVIEW_LABELS
// (used by hasPreviewContent()/drawCornerSlot() below) is derived from
// it too, right after, instead of being its own separately-maintained
// list -- see presets-lookups.js's own header comment for why one
// shared array now covers what used to be 3 independently hand-typed
// ones (a flat id+label list, a category-grouped id+label copy, and a
// separate id->preview-text map) that could, and did, drift apart.
'var CONTENT_SELECT_IDS = ["cornerTL", "cornerTR", "cornerBL", "cornerBR", ' +
'  "upperMiddleLine1Content", "upperMiddleLine2Content", "bottomMiddleLine1Content", "bottomMiddleLine2Content", ' +
'  "middleLeftLine1Content", "middleLeftLine2Content", "middleRightLine1Content", "middleRightLine2Content"];' +
'var CORNER_CATEGORIES = ' + JSON.stringify(cornerCategoriesForClient) + ';' +
'var CORNER_PREVIEW_LABELS = {};' +
'CORNER_CATEGORIES.forEach(function (cat) {' +
'  cat.items.forEach(function (it) {' +
'    if (it.preview) CORNER_PREVIEW_LABELS[it.id] = it.preview;' +
'  });' +
'});' +
'function categoryForContentId(contentId) {' +
'  var idNum = parseInt(contentId, 10);' +
'  for (var i = 0; i < CORNER_CATEGORIES.length; i++) {' +
'    for (var j = 0; j < CORNER_CATEGORIES[i].items.length; j++) {' +
'      if (CORNER_CATEGORIES[i].items[j].id === idNum) return CORNER_CATEGORIES[i].id;' +
'    }' +
'  }' +
'  return "none";' + // unrecognized id -- fall back rather than leave both dropdowns unset
'}' +
'function findCategory(categoryId) {' +
'  for (var i = 0; i < CORNER_CATEGORIES.length; i++) {' +
'    if (CORNER_CATEGORIES[i].id === categoryId) return CORNER_CATEGORIES[i];' +
'  }' +
'  return CORNER_CATEGORIES[0];' +
'}' +
'function categoryItemOptionsHtml(categoryId, selectedContentId) {' +
'  var items = findCategory(categoryId).items;' +
'  var html = "";' +
'  for (var i = 0; i < items.length; i++) {' +
'    html += "<option value=\\"" + items[i].id + "\\"" + (String(selectedContentId) === String(items[i].id) ? " selected" : "") + ">" + items[i].label + "</option>";' +
'  }' +
'  return html;' +
'}' +
'var SLOT_DEFS = {' +
'  cornerTL: { contentId: "cornerTL", colorId: "cornerTLColor", btnId: "slotBtn-cornerTL", label: "Top-left", avail: function (a) { return !a.cornersGrayed; } },' +
'  cornerTR: { contentId: "cornerTR", colorId: "cornerTRColor", btnId: "slotBtn-cornerTR", label: "Top-right", avail: function (a) { return !a.cornersGrayed; } },' +
'  cornerBL: { contentId: "cornerBL", colorId: "cornerBLColor", btnId: "slotBtn-cornerBL", label: "Bottom-left", avail: function (a) { return !a.cornersGrayed; } },' +
'  cornerBR: { contentId: "cornerBR", colorId: "cornerBRColor", btnId: "slotBtn-cornerBR", label: "Bottom-right", avail: function (a) { return !a.cornersGrayed; } },' +
'  upperMiddleLine1: { contentId: "upperMiddleLine1Content", colorId: "upperMiddleLine1Color", btnId: "slotBtn-upperMiddleLine1", label: "Upper-middle, line 1", analogOnly: true, avail: function (a) { return a.upper; } },' +
'  upperMiddleLine2: { contentId: "upperMiddleLine2Content", colorId: "upperMiddleLine2Color", btnId: "slotBtn-upperMiddleLine2", label: "Upper-middle, line 2", analogOnly: true, avail: function (a) { return a.upper; } },' +
'  bottomMiddleLine1: { contentId: "bottomMiddleLine1Content", colorId: "bottomMiddleLine1Color", btnId: "slotBtn-bottomMiddleLine1", label: "Bottom-middle, line 1", analogOnly: true, avail: function (a) { return a.bottom; } },' +
'  bottomMiddleLine2: { contentId: "bottomMiddleLine2Content", colorId: "bottomMiddleLine2Color", btnId: "slotBtn-bottomMiddleLine2", label: "Bottom-middle, line 2", analogOnly: true, avail: function (a) { return a.bottom; } },' +
'  middleLeftLine1: { contentId: "middleLeftLine1Content", colorId: "middleLeftLine1Color", btnId: "slotBtn-middleLeftLine1", label: "Middle-left, line 1", analogOnly: true, avail: function (a) { return a.left; } },' +
'  middleLeftLine2: { contentId: "middleLeftLine2Content", colorId: "middleLeftLine2Color", btnId: "slotBtn-middleLeftLine2", label: "Middle-left, line 2", analogOnly: true, avail: function (a) { return a.left; } },' +
'  middleRightLine1: { contentId: "middleRightLine1Content", colorId: "middleRightLine1Color", btnId: "slotBtn-middleRightLine1", label: "Middle-right, line 1", analogOnly: true, avail: function (a) { return a.right; } },' +
'  middleRightLine2: { contentId: "middleRightLine2Content", colorId: "middleRightLine2Color", btnId: "slotBtn-middleRightLine2", label: "Middle-right, line 2", analogOnly: true, avail: function (a) { return a.right; } },' +
  // digitalLeft/Right/Bottom below deliberately point contentId/colorId
  // at the SAME underlying elements as their analog counterparts
  // (middleLeftLine1/2, middleRightLine1/2, upperMiddleLine1/2,
  // bottomMiddleLine1) rather than a separate set of digital-only ones
  // -- matches eclipse_data.h's own dual-purpose field reuse, and since
  // analogOnly/digitalOnly slots are never both visible at once (see
  // renderSlotPicker()), two SLOT_DEFS entries safely sharing one
  // underlying <select>/<input> pair is no different from any other
  // slot reading/writing its own.
'  digitalLeft1: { contentId: "middleLeftLine1Content", colorId: "middleLeftLine1Color", btnId: "slotBtn-digitalLeft1", label: "Left side, row 1 (top)", digitalOnly: true, avail: function (a) { return a.digitalLeft; } },' +
'  digitalLeft2: { contentId: "middleLeftLine2Content", colorId: "middleLeftLine2Color", btnId: "slotBtn-digitalLeft2", label: "Left side, row 2", digitalOnly: true, avail: function (a) { return a.digitalLeft; } },' +
'  digitalLeft3: { contentId: "upperMiddleLine1Content", colorId: "upperMiddleLine1Color", btnId: "slotBtn-digitalLeft3", label: "Left side, row 3 (bottom)", digitalOnly: true, avail: function (a) { return a.digitalLeft; } },' +
'  digitalRight1: { contentId: "middleRightLine1Content", colorId: "middleRightLine1Color", btnId: "slotBtn-digitalRight1", label: "Right side, row 1 (top)", digitalOnly: true, avail: function (a) { return a.digitalRight; } },' +
'  digitalRight2: { contentId: "middleRightLine2Content", colorId: "middleRightLine2Color", btnId: "slotBtn-digitalRight2", label: "Right side, row 2", digitalOnly: true, avail: function (a) { return a.digitalRight; } },' +
'  digitalRight3: { contentId: "upperMiddleLine2Content", colorId: "upperMiddleLine2Color", btnId: "slotBtn-digitalRight3", label: "Right side, row 3 (bottom)", digitalOnly: true, avail: function (a) { return a.digitalRight; } },' +
'  digitalBottom: { contentId: "bottomMiddleLine1Content", colorId: "bottomMiddleLine1Color", btnId: "slotBtn-digitalBottom", label: "Bottom feature", digitalOnly: true, avail: function () { return true; } }' +
'};' +
'var CURRENT_SLOT_KEY = null;' +
'var SLOT_EDITOR_DRAFT_COLOR = 0;' +
// Procedural marker styles (<3) have all 8 slots and the 4 corners;
// bitmap styles are each limited to whatever their own artwork
// actually has room for and (Tally excepted -- its own mask leaves
// all 4 corners clear) suppress the corners -- and, for the styles
// whose mask only really has room for upper+bottom, the left/right
// edges too -- by default, since the mask fills most of the rest of
// the screen either way. The "Incompatible features" checkbox
// (bitmapCornerOverride) unlocks all of it at once: corners AND
// whichever middle edges that style would otherwise leave off, not
// just the corners.
'function computeSlotAvailability() {' +
'  var styleVal = document.getElementById("bottomStyleValue").value;' +
'  var isAnalog = styleVal === "analog";' +
'  var markerStyle = parseInt(document.getElementById("bigAnalogMarkerStyle").value, 10);' +
'  var override = document.getElementById("bitmapCornerOverride").checked;' +
'  var digitalSidesVal = document.getElementById("digitalSides").value;' +
'  var avail = { upper: false, bottom: false, left: false, right: false, cornersGrayed: false,' +
'    digitalLeft: !isAnalog && (digitalSidesVal === "left" || digitalSidesVal === "both"),' +
'    digitalRight: !isAnalog && (digitalSidesVal === "right" || digitalSidesVal === "both") };' +
'  if (isAnalog) {' +
'    if (markerStyle < 3 || markerStyle === 8 || markerStyle === 9) {' +
'      avail.upper = avail.bottom = avail.left = avail.right = true;' +
'    } else if (markerStyle === 3 || markerStyle === 4 || markerStyle === 6) {' +
'      avail.upper = avail.bottom = true; avail.left = avail.right = override; avail.cornersGrayed = !override;' +
'    } else if (markerStyle === 5) {' +
'      avail.upper = avail.bottom = avail.left = avail.right = true;' +
'    } else if (markerStyle === 7) {' +
'      avail.upper = avail.bottom = avail.left = avail.right = true; avail.cornersGrayed = !override;' +
'    } else {' +
'      avail.upper = true; avail.bottom = avail.left = avail.right = override; avail.cornersGrayed = !override;' +
'    }' +
'  }' +
'  return avail;' +
'}' +
// Updates each slot button\'s label (its example preview value, "OFF",
// or "N/A") and styling to match current settings -- called on init
// and whenever bottom-style/marker-style or a slot\'s own content
// changes.
'function renderSlotPicker() {' +
'  var avail = computeSlotAvailability();' +
'  var isAnalogMode = document.getElementById("bottomStyleValue").value === "analog";' +
'  document.getElementById("slotDiagramClockBar").style.display = isAnalogMode ? "none" : "block";' +
'  for (var key in SLOT_DEFS) {' +
'    var def = SLOT_DEFS[key];' +
'    var btn = document.getElementById(def.btnId);' +
'    var baseClass = btn.getAttribute("data-base-class");' +
'    if (!baseClass) { baseClass = btn.className; btn.setAttribute("data-base-class", baseClass); }' +
    // analogOnly/digitalOnly slots are two different underlying
    // AppMessage fields sharing one settings <select> (see SLOT_DEFS'
    // own comment) but sit at DIFFERENT diagram positions now (the
    // digital ones on the clock bar, the analog ones around the dial)
    // -- only one member of each pair is ever relevant at a time, so
    // the other is fully hidden here rather than shown as a "N/A"
    // placeholder over a spot that mode doesn't even use.
'    if ((def.analogOnly && !isAnalogMode) || (def.digitalOnly && isAnalogMode)) {' +
'      btn.style.display = "none";' +
'      continue;' +
'    }' +
'    btn.style.display = "";' +
'    if (!def.avail(avail)) {' +
'      btn.textContent = "N/A";' +
'      btn.className = baseClass + " slot-na";' +
'      continue;' +
'    }' +
'    var val = parseInt(document.getElementById(def.contentId).value, 10);' +
'    if (!val || !CORNER_PREVIEW_LABELS[val]) {' +
'      btn.textContent = "OFF";' +
'      btn.className = baseClass + " slot-off";' +
'    } else {' +
'      btn.textContent = CORNER_PREVIEW_LABELS[val];' +
'      btn.className = baseClass;' +
'    }' +
'  }' +
// Applied AFTER the loop above, not before it -- these two buttons'
// className just got fully overwritten (baseClass + a mode-dependent
// suffix) by that loop, same as every other slot button's, so toggling
// this class BEFORE the loop (the way an earlier version of this
// function did) meant the loop's own className assignment silently
// wiped it straight back off again on every call after the very first
// -- .slot-corner-above-bar (and so which mode's position CSS,
// .slot-corner-bl vs the "above the digital bar" variant, actually
// applied) would only ever reflect whichever mode the page happened
// to load in, never a mode switched into afterward, until a full page
// reload re-derived everything from scratch. This is exactly why the
// bug this fixes only ever showed up after switching modes, not on a
// fresh load.
'  document.getElementById("slotBtn-cornerBL").classList.toggle("slot-corner-above-bar", !isAnalogMode);' +
'  document.getElementById("slotBtn-cornerBR").classList.toggle("slot-corner-above-bar", !isAnalogMode);' +
'}' +
'function setSlotEditorColorGroupVisibility(contentVal) {' +
'  document.getElementById("slotEditColorGroup").style.display = (contentVal === "0") ? "none" : "flex";' +
'}' +
'function setSlotEditorColorButtons(value) {' +
'  SLOT_EDITOR_DRAFT_COLOR = parseInt(value, 10) || 0;' +
'  var buttons = document.getElementById("slotEditColorGroup").getElementsByClassName("mode-btn");' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    buttons[i].className = "mode-btn" + (i === SLOT_EDITOR_DRAFT_COLOR ? " active" : "");' +
'  }' +
'}' +
'function slotEditorSelectColor(value) {' +
'  setSlotEditorColorButtons(value);' +
'}' +
'function onSlotEditContentChange() {' +
'  setSlotEditorColorGroupVisibility(document.getElementById("slotEditContent").value);' +
'}' +
// Renders the 9 category picker buttons once (their icons/order never
// change at runtime, unlike Content below which is rebuilt per open) --
// called once at page init, see the bottom of this script.
'function renderCategoryButtons() {' +
'  var group = document.getElementById("slotEditCategoryGroup");' +
'  group.innerHTML = CORNER_CATEGORIES.map(function (c) {' +
'    return \'<button type="button" class="category-btn" data-category="\' + c.id + \'" onclick="selectSlotEditCategory(\\\'\' + c.id + \'\\\')" title="\' + c.label + \'">\' + c.icon + \'</button>\';' +
'  }).join("");' +
'}' +
'function updateCategoryButtonActive(categoryId) {' +
'  var buttons = document.getElementById("slotEditCategoryGroup").getElementsByClassName("category-btn");' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    buttons[i].className = "category-btn" + (buttons[i].getAttribute("data-category") === categoryId ? " active" : "");' +
'  }' +
'}' +
'function selectSlotEditCategory(categoryId) {' +
'  document.getElementById("slotEditCategory").value = categoryId;' +
'  updateCategoryButtonActive(categoryId);' +
'  onSlotEditCategoryChange();' +
'}' +
// Repopulates the item dropdown to just the newly-chosen category's
// items whenever the category itself changes -- defaults to that
// category\'s first item, since the previously-selected content id
// (from a different category) is never one of the new options.
// "None" has nothing else to pick, so Content is disabled rather than
// left showing a single always-selected "None" option to interact with.
'function onSlotEditCategoryChange() {' +
'  var categoryId = document.getElementById("slotEditCategory").value;' +
'  var firstItemId = findCategory(categoryId).items[0].id;' +
'  var contentSelect = document.getElementById("slotEditContent");' +
'  contentSelect.innerHTML = categoryItemOptionsHtml(categoryId, firstItemId);' +
'  contentSelect.disabled = (categoryId === "none");' +
'  document.getElementById("slotEditCategoryHelp").style.display = (categoryId === "timezone") ? "" : "none";' +
'  onSlotEditContentChange();' +
'}' +
// Opens the popup pre-filled with this slot\'s current (already-saved)
// content/color -- nothing is written back to the real elements until
// saveSlotEditor() runs, so closing without saving (Cancel, or a tap
// outside the box) leaves the slot exactly as it was.
'function openSlotEditor(slotKey) {' +
'  var def = SLOT_DEFS[slotKey];' +
'  if (!def) return;' +
'  CURRENT_SLOT_KEY = slotKey;' +
'  document.getElementById("slotEditTitle").textContent = def.label;' +
'  var contentVal = document.getElementById(def.contentId).value;' +
'  var colorVal = document.getElementById(def.colorId).value;' +
'  var categoryId = categoryForContentId(contentVal);' +
'  document.getElementById("slotEditCategory").value = categoryId;' +
'  updateCategoryButtonActive(categoryId);' +
'  var contentSelect = document.getElementById("slotEditContent");' +
'  contentSelect.innerHTML = categoryItemOptionsHtml(categoryId, contentVal);' +
'  contentSelect.disabled = (categoryId === "none");' +
'  document.getElementById("slotEditCategoryHelp").style.display = (categoryId === "timezone") ? "" : "none";' +
'  setSlotEditorColorGroupVisibility(contentVal);' +
'  setSlotEditorColorButtons(colorVal);' +
'  document.getElementById("slotEditModal").className = "modal-overlay open";' +
'}' +
'function closeSlotEditor() {' +
'  document.getElementById("slotEditModal").className = "modal-overlay";' +
'  CURRENT_SLOT_KEY = null;' +
'}' +
'function saveSlotEditor() {' +
'  if (!CURRENT_SLOT_KEY) return;' +
'  var def = SLOT_DEFS[CURRENT_SLOT_KEY];' +
'  document.getElementById(def.contentId).value = document.getElementById("slotEditContent").value;' +
'  document.getElementById(def.colorId).value = String(SLOT_EDITOR_DRAFT_COLOR);' +
'  closeSlotEditor();' +
'  renderSlotPicker();' +
'  updateWeatherIconStyleVisibility();' +
'  updatePreview();' +
'}' +
// Weather icon style is now always shown (not gated on any corner/edge
// slot being set to "Weather icon"/"Temp + weather icon" -- previously
// tying visibility to that meant the selector went missing for anyone
// using it on the middle-feature or other content types, or who just
// wanted to preview it in advance). This still refreshes the trigger
// button's own preview/label though -- called from saveSlotEditor() plus
// once at page load, same as before, just no longer touched on slot
// content specifically.
'function updateWeatherIconStyleVisibility() {' +
'  updateWeatherIconStyleTriggerLabel();' +
'}' +

// ---- collapsible sections + slider step buttons ------------------------
'function toggleSection(id) {' +
'  var body = document.getElementById("section-" + id);' +
'  var chev = document.getElementById("chev-" + id);' +
'  var sub = document.getElementById("subhead-" + id);' +
'  if (!body) return;' +
'  var isOpen = body.style.display !== "none";' +
'  body.style.display = isOpen ? "none" : "";' +
'  if (chev) chev.className = isOpen ? "chevron" : "chevron open";' +
// The sub-header is only useful while collapsed (a quick summary of
// what\'s inside); once the section\'s own full controls are visible
// it\'s redundant, so it disappears on expansion per the request --
// its text stays computed underneath (refreshAllSectionSubheaders()
// keeps updating it even while hidden), just not shown.
'  if (sub) sub.style.display = isOpen ? "" : "none";' +
'}' +
// Same idea as toggleSection() above, one level down -- see
// subsectionLegendHtml()\'s own comment for why Hands style/Indices
// style get this instead of a full top-level section registration.
'function toggleSubsection(id) {' +
'  var body = document.getElementById("subsec-" + id);' +
'  var chev = document.getElementById("subsecchev-" + id);' +
'  var sub = document.getElementById("subsubhead-" + id);' +
'  if (!body) return;' +
'  var isOpen = body.style.display !== "none";' +
'  body.style.display = isOpen ? "none" : "";' +
'  if (chev) chev.className = isOpen ? "chevron" : "chevron open";' +
'  if (sub) sub.style.display = isOpen ? "" : "none";' +
'}' +
'function stepSlider(id, delta) {' +
'  var el = document.getElementById(id);' +
'  if (!el) return;' +
'  var min = parseFloat(el.min), max = parseFloat(el.max);' +
'  var v = parseFloat(el.value) + delta;' +
'  if (!isNaN(min) && v < min) v = min;' +
'  if (!isNaN(max) && v > max) v = max;' +
'  el.value = v;' +
'  if (el.oninput) el.oninput();' +
'  else if (el.onchange) el.onchange();' +
'}' +

// Press-and-hold repeat for the +/- slider step buttons. Reads the
// button's own onclick="stepSlider(\'id\', delta)" text to find which
// slider and direction it steps, rather than needing separate
// data attributes on all 20-odd call sites above -- one generic
// listener covers all of them, present and future. A single tap is
// left to the browser's normal click (one call to stepSlider); this
// only starts repeating after a short delay, so a quick tap never
// double-steps.
'var SLIDER_HOLD_TIMER = null;' +
'var SLIDER_HOLD_INTERVAL = null;' +
'function parseStepSliderArgs(btn) {' +
'  var attr = btn.getAttribute("onclick") || "";' +
'  var m = attr.match(/stepSlider\\(\'([^\']+)\',\\s*(-?\\d+)\\)/);' +
'  return m ? { id: m[1], delta: parseInt(m[2], 10) } : null;' +
'}' +
'function stopSliderHold() {' +
'  if (SLIDER_HOLD_TIMER) { clearTimeout(SLIDER_HOLD_TIMER); SLIDER_HOLD_TIMER = null; }' +
'  if (SLIDER_HOLD_INTERVAL) { clearInterval(SLIDER_HOLD_INTERVAL); SLIDER_HOLD_INTERVAL = null; }' +
'}' +
'function startSliderHold(btn) {' +
'  var args = parseStepSliderArgs(btn);' +
'  if (!args) return;' +
'  stopSliderHold();' +
'  SLIDER_HOLD_TIMER = setTimeout(function () {' +
'    SLIDER_HOLD_INTERVAL = setInterval(function () { stepSlider(args.id, args.delta); }, 90);' +
'  }, 400);' +
'}' +
'document.addEventListener("mousedown", function (e) {' +
'  var btn = e.target.closest && e.target.closest(".slider-step-btn");' +
'  if (btn) { startSliderHold(btn); e.preventDefault(); }' + // belt-and-suspenders alongside the CSS
                                                                // user-select:none above -- stops the
                                                                // press-and-hold repeat from also
                                                                // starting a text selection drag
'});' +
'document.addEventListener("touchstart", function (e) {' +
'  var btn = e.target.closest && e.target.closest(".slider-step-btn");' +
'  if (btn) startSliderHold(btn);' +
'}, { passive: true });' +
'["mouseup", "mouseleave", "touchend", "touchcancel"].forEach(function (ev) {' +
'  document.addEventListener(ev, stopSliderHold);' +
'});' +

'function onMarkerStyleChange() {' +
'  var val = document.getElementById("bigAnalogMarkerStyle").value;' +
'  document.getElementById("customMarkerSection").style.display = (val === "8") ? "" : "none";' +
'  var isBitmap = (val === "3" || val === "4" || val === "5" || val === "6" || val === "7");' +
'  document.getElementById("bitmapMarkerTransparentRow").style.display = isBitmap ? "" : "none";' +
'  document.getElementById("bitmapCornerOverrideRow").style.display = (isBitmap && val !== "5") ? "" : "none";' +
'  document.getElementById("bitmapCornerOverrideHelp").style.display = (isBitmap && val !== "5") ? "" : "none";' +
'  updateMarkerStyleButtonLabel();' +
'  clearGrayedSlotsIfUnavailable();' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
// The 4 corner content selections -- and, since the "Incompatible
// features" override now covers them too, left/right middle-edge
// content -- are off by default (and cleared, not just visually
// grayed) for bitmap styles whose artwork doesn't leave room for them.
// Without this, a value picked while a procedural/custom style (or
// the override) was active would silently keep being sent to the
// watch even once its slot button shows "N/A", since features_layer.c
// no longer suppresses any of this itself (see its own note on why
// that moved here).
'function clearGrayedSlotsIfUnavailable() {' +
'  var avail = computeSlotAvailability();' +
'  if (avail.cornersGrayed) {' +
'    var cornerIds = ["cornerTL", "cornerTR", "cornerBL", "cornerBR"];' +
'    for (var i = 0; i < cornerIds.length; i++) { document.getElementById(cornerIds[i]).value = "0"; }' +
'  }' +
  // middleLeft/RightLineContent are dual-purpose (digital mode's own
  // side-feature columns reuse these same fields -- see SLOT_DEFS'
  // own comment), and avail.left/right stay false unconditionally in
  // digital mode (it tracks its own availability via
  // avail.digitalLeft/digitalRight instead) -- so this only clears
  // them in analog mode, where !avail.left/right specifically means
  // "this bitmap style doesn't support it without the override",
  // never "wrong mode".
'  var isAnalog = document.getElementById("bottomStyleValue").value === "analog";' +
'  if (isAnalog && !avail.left) {' +
'    document.getElementById("middleLeftLine1Content").value = "0";' +
'    document.getElementById("middleLeftLine2Content").value = "0";' +
'  }' +
'  if (isAnalog && !avail.right) {' +
'    document.getElementById("middleRightLine1Content").value = "0";' +
'    document.getElementById("middleRightLine2Content").value = "0";' +
'  }' +
'}' +
'function onBitmapCornerOverrideChange() {' +
'  clearGrayedSlotsIfUnavailable();' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +

// ---- custom hour/seconds indices popups --------------------------------
'var CM_FIELDS = ["Style", "Thickness", "InnerThickness", "InnerEcc", "OuterEcc", "InnerBorder", "OuterBorder", "Translucent", "Color"];' +
'var CM_CHECKBOX_FIELDS = ["Translucent"];' +
'function cmHiddenPrefix(kind) { return kind === "hour" ? "customHour" : "customSec"; }' +
'function cmPopupPrefix(kind) { return kind === "hour" ? "cmHour" : "cmSec"; }' +
'var MARKER_PRESETS = {' +
'  hour: {' +
'    minimal: { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "20", OuterBorder: "100" },' +
'    small:   { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "0", OuterBorder: "100" },' +
'    big:     { Style: "2", Thickness: "3", InnerEcc: "0", OuterEcc: "0", InnerBorder: "0", OuterBorder: "100" }' +
'  },' +
'  sec: {' +
'    minimal: { Style: "0", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "85", OuterBorder: "100" },' +
'    small:   { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "60", OuterBorder: "100" },' +
'    big:     { Style: "1", Thickness: "1", InnerEcc: "0", OuterEcc: "0", InnerBorder: "60", OuterBorder: "100" }' +
'  }' +
'};' +
// A rough approximation of the 3 built-in procedural styles, translated
// into border-reach percentages (see marker_reach_px() in
// marker_layer.c) now that a mark's length comes directly from its
// inner/outer border points rather than a separate slider -- a starting
// point to tune from, not an exact match. The "second" ring has no real
// minimal-style equivalent (that style draws no second indices at all),
// so its "minimal" preset is just a short stub near the outer edge.
'function updateCustomMarkerValLabels(kind) {' +
'  var p = cmPopupPrefix(kind);' +
'  ["Thickness", "InnerThickness", "InnerEcc", "OuterEcc", "InnerBorder", "OuterBorder"].forEach(function (f) {' +
'    var el = document.getElementById(p + f);' +
'    var out = document.getElementById(p + f + "Val");' +
'    if (el && out) out.textContent = el.value + ((f === "Thickness" || f === "InnerThickness") ? "px" : "%");' +
'  });' +
'}' +
// Shows/hides the popup rows that only make sense for a particular
// Shape choice: the whole geometry section (everything below Shape)
// when "None" is picked -- there\'s nothing left to configure for a
// ring that draws nothing at all -- and the Inner thickness row,
// which only Tapered (style 4) actually reads (see MarkerRingConfig\'s
// own style/thickness comments in eclipse_data.h). Called from every
// place that can change Style out from under the popup: a direct tap,
// re-opening the popup, applying a preset, or copying the other
// ring\'s config.
'function updateCustomMarkerRowVisibility(kind) {' +
'  var p = cmPopupPrefix(kind);' +
'  var styleEl = document.getElementById(p + "Style");' +
'  if (!styleEl) return;' +
'  var isNone = styleEl.value === "none";' +
'  var isTapered = styleEl.value === "4";' +
'  var geomWrap = document.getElementById(p + "GeometryWrap");' +
'  var noneHelp = document.getElementById(p + "NoneHelp");' +
'  var innerRow = document.getElementById(p + "InnerThicknessRow");' +
'  var innerHelp = document.getElementById(p + "InnerThicknessHelp");' +
'  var thicknessHelp = document.getElementById(p + "ThicknessHelp");' +
'  if (geomWrap) geomWrap.style.display = isNone ? "none" : "";' +
'  if (noneHelp) noneHelp.style.display = isNone ? "" : "none";' +
'  if (innerRow) innerRow.style.display = isTapered ? "" : "none";' +
'  if (innerHelp) innerHelp.style.display = isTapered ? "" : "none";' +
'  if (thicknessHelp) thicknessHelp.style.display = isTapered ? "none" : "";' +
'}' +
'function cmHourStyleChange(val) { onCustomMarkerStyleChange("hour", val); }' +
'function cmSecStyleChange(val) { onCustomMarkerStyleChange("sec", val); }' +
'function onCustomMarkerStyleChange(kind, val) {' +
'  var p = cmPopupPrefix(kind);' +
'  selectModeButton(p + "StyleGroup", p + "Style", val);' +
'  updateCustomMarkerRowVisibility(kind);' +
'}' +
'function onCustomMarkerSliderInput(kind) {' +
'  updateCustomMarkerValLabels(kind);' +
'}' +
'function onCustomMarkerBorderInput(kind, isInner) {' +
'  var p = cmPopupPrefix(kind);' +
'  var innerEl = document.getElementById(p + "InnerBorder"), outerEl = document.getElementById(p + "OuterBorder");' +
'  var innerVal = parseInt(innerEl.value, 10), outerVal = parseInt(outerEl.value, 10);' +
'  outerEl.min = innerVal;' +
'  if (outerVal < innerVal) outerEl.value = innerVal;' +
'  updateCustomMarkerValLabels(kind);' +
'}' +
// Pre-fills the popup from the currently-saved customHour*/customSec*
// hidden inputs -- nothing is written back until saveCustomMarkerEditor()
// runs, so Cancel (or tapping outside) leaves the saved config untouched.
'function openCustomMarkerEditor(kind) {' +
'  var hp = cmHiddenPrefix(kind), p = cmPopupPrefix(kind);' +
'  CM_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    if (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) { popupEl.checked = hidden.value === "true"; } else { popupEl.value = hidden.value; }' +
'  });' +
// A committed thickness of 0 is how "None" is actually represented on
// the wire (see saveCustomMarkerEditor()\'s own comment) -- the
// committed Style value alongside it is meaningless in that case, so
// the draft picker shows None regardless of whatever it happens to be.
'  if (document.getElementById(hp + "Thickness").value === "0") {' +
'    document.getElementById(p + "Style").value = "none";' +
'  }' +
'  refreshModeButtonGroup(p + "StyleGroup", p + "Style");' +
'  refreshModeButtonGroup(p + "ColorGroup", p + "Color");' +
'  document.getElementById(p + "OuterBorder").min = document.getElementById(p + "InnerBorder").value;' +
'  updateCustomMarkerValLabels(kind);' +
'  updateCustomMarkerRowVisibility(kind);' +
'  document.getElementById("customMarkerModal-" + kind).className = "modal-overlay open";' +
'}' +
'function closeCustomMarkerEditor(kind) {' +
'  document.getElementById("customMarkerModal-" + kind).className = "modal-overlay";' +
'}' +
// "None" isn\'t a real MarkerRingConfig.style value the watch knows
// about -- draw_marker_ring() already treats thickness == 0 as "this
// ring draws nothing at all" (see its own comment in background_layer.c),
// so that\'s the actual wire representation: Style commits as plain "0"
// (Dot -- never read once thickness is 0) and Thickness commits as "0",
// regardless of whatever the sliders were last left at, so re-picking
// any real shape later starts from a sensible Dot rather than a
// leftover 0px line.
'function saveCustomMarkerEditor(kind) {' +
'  var hp = cmHiddenPrefix(kind), p = cmPopupPrefix(kind);' +
'  var isNone = document.getElementById(p + "Style").value === "none";' +
'  CM_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    if (isNone && f === "Style") { hidden.value = "0"; return; }' +
'    if (isNone && f === "Thickness") { hidden.value = "0"; return; }' +
'    hidden.value = (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) ? String(popupEl.checked) : popupEl.value;' +
'  });' +
'  closeCustomMarkerEditor(kind);' +
'  refreshEditButtonLabels();' +
'  updatePreview();' +
'}' +
'function applyMarkerPreset(kind, name) {' +
'  var p = cmPopupPrefix(kind);' +
'  var preset = MARKER_PRESETS[kind][name];' +
'  if (!preset) return;' +
'  CM_FIELDS.forEach(function (f) {' +
'    var el = document.getElementById(p + f);' +
'    if (!el || preset[f] === undefined) return;' +
'    if (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) { el.checked = preset[f] === true || preset[f] === "true"; } else { el.value = preset[f]; }' +
'  });' +
'  refreshModeButtonGroup(p + "StyleGroup", p + "Style");' +
'  refreshModeButtonGroup(p + "ColorGroup", p + "Color");' +
'  document.getElementById(p + "OuterBorder").min = document.getElementById(p + "InnerBorder").value;' +
'  updateCustomMarkerValLabels(kind);' +
'  updateCustomMarkerRowVisibility(kind);' +
'}' +
// Copies the OTHER ring\'s last-saved (not currently-open-popup-draft)
// config into this popup\'s controls -- text-marker settings are never
// touched here, since hour/second numbers already exclude each other.
'function copyMarkerConfig(kind) {' +
'  var otherKind = kind === "hour" ? "sec" : "hour";' +
'  var otherHiddenPrefix = cmHiddenPrefix(otherKind), p = cmPopupPrefix(kind);' +
'  CM_FIELDS.forEach(function (f) {' +
'    var src = document.getElementById(otherHiddenPrefix + f);' +
'    var dst = document.getElementById(p + f);' +
'    if (!src || !dst) return;' +
'    if (CM_CHECKBOX_FIELDS.indexOf(f) !== -1) { dst.checked = src.value === "true"; } else { dst.value = src.value; }' +
'  });' +
'  if (document.getElementById(otherHiddenPrefix + "Thickness").value === "0") {' +
'    document.getElementById(p + "Style").value = "none";' +
'  }' +
'  refreshModeButtonGroup(p + "StyleGroup", p + "Style");' +
'  document.getElementById(p + "OuterBorder").min = document.getElementById(p + "InnerBorder").value;' +
'  updateCustomMarkerValLabels(kind);' +
'  updateCustomMarkerRowVisibility(kind);' +
'}' +
'function selectMarkerTextTarget(val) {' +
'  selectModeButton("markerTextTargetGroup", "markerTextTarget", val);' +
'  onMarkerTextTargetChange();' +
'  refreshEditButtonLabels();' +
'}' +
'function onMarkerTextTargetChange() {' +
'  var val = document.getElementById("markerTextTarget").value;' +
'  document.getElementById("markerTextOptions").style.display = (val === "0") ? "none" : "";' +
'  document.getElementById("markerTextHourGrid").style.display = (val === "1") ? "" : "none";' +
'  document.getElementById("markerTextSecGrid").style.display = (val === "2") ? "" : "none";' +
'  updatePreview();' +
'}' +
'function onMarkerTextFontChange() {' +
'  var font = document.getElementById("markerTextFont").value;' +
'  var romanBox = document.getElementById("markerTextRoman");' +
'  var romanHelp = document.getElementById("markerTextRomanHelp");' +
'  var incompatible = !!ROMAN_INCOMPATIBLE_FONTS[font];' +
'  romanBox.disabled = incompatible;' +
'  if (incompatible) romanBox.checked = false;' +
'  if (romanHelp) romanHelp.textContent = incompatible ?' +
'    "Not available with this font -- its glyphs don\'t support Roman numerals correctly." :' +
'    "Shows I, II, III... instead of 1, 2, 3... -- independent of the font above.";' +
'  updatePreview();' +
'}' +
'function openTextMarkerEditor() {' +
'  document.getElementById("textMarkerModal").className = "modal-overlay open";' +
'}' +
'function closeTextMarkerEditor() {' +
'  document.getElementById("textMarkerModal").className = "modal-overlay";' +
'}' +
'function openCenterCircleEditor() {' +
'  document.getElementById("centerCircleModal").className = "modal-overlay open";' +
'}' +
'function closeCenterCircleEditor() {' +
'  document.getElementById("centerCircleModal").className = "modal-overlay";' +
'}' +
'function openShadowStyleEditor() {' +
'  document.getElementById("shadowStyleModal").className = "modal-overlay open";' +
'}' +
'function closeShadowStyleEditor() {' +
'  document.getElementById("shadowStyleModal").className = "modal-overlay";' +
'}' +
'function toggleMarkBtn(kind, i) {' +
'  var hiddenId = kind === "hour" ? "markerTextHourMask" : "markerTextSecMask";' +
'  var hidden = document.getElementById(hiddenId);' +
'  var mask = parseInt(hidden.value, 10) || 0;' +
'  var btn = document.getElementById("markBtn-" + kind + "-" + i);' +
'  var bit = 1 << i;' +
'  if (mask & bit) { mask &= ~bit; btn.className = "mark-btn"; } else { mask |= bit; btn.className = "mark-btn active"; }' +
'  hidden.value = String(mask);' +
'}' +

// ---- custom hour/minute/second hand popups ----------------------------
'var HE_FIELDS = ["Style", "Width", "Length", "BackOffset", "MiddleOffset", "SecondaryWidth", "Color", "OutlineEnabled", "OutlineColor", "Translucent", "ShadowEnabled", "ShadowDistance", "Hollow", "HollowThickness"];' +
'var HE_CHECKBOX_FIELDS = ["OutlineEnabled", "Translucent", "ShadowEnabled", "Hollow"];' +
// Which HandConfig.style values actually use MiddleOffset -- 0-2
// (dot/triangle/square) ignore it, same as the C side; every other
// style (3-10) uses it. SecondaryWidth is used by the same set MINUS
// Leaf (8), which only has a single Width slider (its own peak is
// always exactly `width` wide -- see hand_layer.c's own comment).
// Drives updateHandFieldVisibility() below.
'var HAND_STYLES_WITH_MIDDLE = ["3", "4", "5", "6", "7", "8", "9", "10"];' +
'var HAND_STYLES_WITH_SECONDARY = ["3", "4", "5", "6", "7", "9", "10"];' +
'var HAND_COPY_SOURCE = { hour: "min", min: "hour", sec: "min" };' +
'function heHiddenPrefix(kind) { return kind === "hour" ? "handHour" : (kind === "min" ? "handMin" : "handSec"); }' +
'function hePopupPrefix(kind) { return "he" + kind.charAt(0).toUpperCase() + kind.slice(1); }' +
// Runtime copy of presets-lookups.js's own HAND_PRESETS -- serialized
// straight from that same canonical object (see resources/infographics/
// hands<n>.png, embedded via scripts/generate-infographics.js as
// HAND_STYLE_IMAGES, for the matching preview images) rather than
// hand-duplicated, same pattern as FONT_LOOKUP/COLOR_SCHEMES above.
// Keyed "1".."9" to match those filenames, each with a display title
// plus hour/min/sec field sets in the same shape applyHandPresetToKind()
// below writes into the hidden custom-hand inputs, PLUS a centerCircle
// and shadow field set applied the same way to the standalone Center
// circle/Shadow style settings (see applyHandPresetExtras() below) --
// those two aren't per-hand, but still look like part of "the hand
// style" to anyone picking a preset, so a preset now sets them too
// instead of leaving whatever was there before. Picking a preset is a
// one-shot copy into those same fields the manual "Edit ... hand" /
// Center circle / Shadow style editors use -- nothing about it is
// remembered as a distinct "preset" afterward, on this page or on the
// watch (see hand_layer.h's own comment for why the watch doesn't need
// to know).
'var HAND_PRESETS = ' + JSON.stringify(HAND_PRESETS) + ';' +
'function applyHandPresetToKind(kind, preset) {' +
'  var hp = heHiddenPrefix(kind);' +
'  for (var f in preset) {' +
'    var hidden = document.getElementById(hp + f);' +
'    if (hidden) hidden.value = preset[f];' +
'  }' +
'}' +
// Center circle/Shadow style aren't per-hand (no heHiddenPrefix() to
// go through), and 2 of the 4 fields are sliders whose own displayed
// "Val" span text (see centerCircleModal/shadowStyleModal\'s own HTML)
// only updates from their oninput handler -- setting .value alone from
// here wouldn\'t touch that span, so this sets both by hand for those.
'function applyHandPresetExtras(entry) {' +
'  if (entry.centerCircle) {' +
'    if (entry.centerCircle.Radius !== undefined) {' +
'      document.getElementById("centerCircleRadius").value = entry.centerCircle.Radius;' +
'      var radiusVal = document.getElementById("centerCircleRadiusVal");' +
'      if (radiusVal) radiusVal.textContent = entry.centerCircle.Radius + "px";' +
'    }' +
'    if (entry.centerCircle.Color !== undefined) document.getElementById("centerCircleColor").value = entry.centerCircle.Color;' +
'  }' +
'  if (entry.shadow) {' +
'    if (entry.shadow.Translucent !== undefined) {' +
'      document.getElementById("shadowTranslucent").value = entry.shadow.Translucent;' +
'      refreshModeButtonGroup("shadowTranslucentGroup", "shadowTranslucent");' +
'    }' +
'    if (entry.shadow.Angle !== undefined) {' +
'      document.getElementById("shadowAngle").value = entry.shadow.Angle;' +
'      var angleVal = document.getElementById("shadowAngleVal");' +
'      if (angleVal) angleVal.textContent = entry.shadow.Angle + "\\u00b0";' +
'    }' +
'  }' +
'}' +
// Populates #handStyleGrid with one button per HAND_PRESETS entry --
// called once up front (see the bottom of this script) rather than
// inline in the HTML template, since the button list is entirely
// data-driven off HAND_PRESETS/HAND_STYLE_IMAGES.
'function renderHandStyleGrid() {' +
'  var grid = document.getElementById("handStyleGrid");' +
'  if (!grid) return;' +
'  var html = "";' +
'  for (var n = 1; n <= 9; n++) {' +
'    var entry = HAND_PRESETS[String(n)];' +
'    if (!entry) continue;' +
'    var img = HAND_STYLE_IMAGES[String(n)];' +
'    html += \'<button type="button" class="style-picker-btn" onclick="chooseHandStyle(\' + n + \')">\' +' +
'      (img ? \'<img src="\' + img + \'" alt="\' + entry.title + \'">\' : \'<span class="style-picker-btn-empty">\' + entry.title + "</span>") +' +
'      \'<span class="style-picker-btn-cap-top">\' + entry.title + "</span></button>";' +
'  }' +
'  grid.innerHTML = html;' +
'}' +
'function openHandStyleModal() {' +
'  document.getElementById("handStyleModal").className = "modal-overlay open";' +
'}' +
'function closeHandStyleModal() {' +
'  document.getElementById("handStyleModal").className = "modal-overlay";' +
'}' +
'function chooseHandStyle(n) {' +
'  var entry = HAND_PRESETS[String(n)];' +
'  if (!entry) return;' +
'  applyHandPresetToKind("hour", entry.hour);' +
'  applyHandPresetToKind("min", entry.min);' +
'  applyHandPresetToKind("sec", entry.sec);' +
'  applyHandPresetExtras(entry);' +
'  closeHandStyleModal();' +
'  updateHandStyleButtonLabel();' +
'  refreshEditButtonLabels();' +
'  updatePreview();' +
'}' +
// "Custom" -- same meaning the old dropdown\'s "Custom" option had:
// leaves the hand fields exactly as they are (the 3 "Edit ... hand"
// buttons are always available below regardless), just closes the
// popup without applying any preset. Center circle/shadow are left
// alone too, for the same reason.
'function chooseHandStyleCustom() {' +
'  closeHandStyleModal();' +
'  updateHandStyleButtonLabel();' +
'}' +
// Whether the trigger button reads a preset\'s own name or "Custom" is
// entirely DERIVED from the 3 hands\' current field values, not a
// separate remembered flag -- so it stays correct regardless of HOW
// those values got there (a preset button, a manual "Edit ... hand"
// change, an imported style JSON) without needing every one of those
// call sites to separately remember to update/clear some "current
// preset" state by hand. Deliberately checks only the fields a given
// preset actually specifies (Style/Width/Length/BackOffset/
// MiddleOffset/SecondaryWidth/Color -- see HAND_PRESETS\' own entries)
// rather than every HE_FIELDS entry: those are the fields that define
// the STYLE itself, whereas Translucent/Outline/Shadow/Hollow are
// independent finish options layered on top of it, not part of
// telling one named style apart from another.
'function handKindMatchesPresetFields(kind, presetFields) {' +
'  var hp = heHiddenPrefix(kind);' +
'  for (var f in presetFields) {' +
'    var el = document.getElementById(hp + f);' +
'    if (!el || el.value !== String(presetFields[f])) return false;' +
'  }' +
'  return true;' +
'}' +
'function computeHandStylePresetNumber() {' +
'  for (var n = 1; n <= 9; n++) {' +
'    var entry = HAND_PRESETS[String(n)];' +
'    if (!entry) continue;' +
'    if (handKindMatchesPresetFields("hour", entry.hour) && handKindMatchesPresetFields("min", entry.min) &&' +
'        handKindMatchesPresetFields("sec", entry.sec)) {' +
'      return n;' +
'    }' +
'  }' +
'  return null;' +
'}' +
'function computeHandStyleLabel() {' +
'  var n = computeHandStylePresetNumber();' +
'  return n ? HAND_PRESETS[String(n)].title : "Custom";' +
'}' +
'function updateHandStyleButtonLabel() {' +
'  var span = document.getElementById("handStyleTriggerLabel");' +
'  if (span) span.textContent = computeHandStyleLabel();' +
'}' +
// Only touches the DOM when the actual HTML differs (same "don't
// restart CSS state that doesn't need restarting" reasoning as
// setSubheaderTextById() -- an <img> has nothing to animate, but
// there's no reason to force a decode/repaint of the same image once
// a second either).
'function setPreviewThumbnail(elId, html) {' +
'  var el = document.getElementById(elId);' +
'  if (!el || el.innerHTML === html) return;' +
'  el.innerHTML = html;' +
'}' +
// Hands style subsection\'s own collapsed-state thumbnail + sub-header
// -- the thumbnail is whichever HAND_STYLE_IMAGES preset picture
// currently matches (the same "derived from the live field values,
// not a remembered flag" source computeHandStyleLabel() already
// uses), blank for Custom (no single static picture could represent
// an arbitrary custom combination). The sub-header adds whether any
// hand currently has its shadow on, since that's real information the
// bare style name alone wouldn\'t show.
'function refreshHandsSubsectionPreview() {' +
'  var n = computeHandStylePresetNumber();' +
'  var img = n ? HAND_STYLE_IMAGES[String(n)] : null;' +
'  setPreviewThumbnail("subsecpreview-hands", img ? (\'<img src="\' + img + \'" alt="">\') : "");' +
'  var title = n ? HAND_PRESETS[String(n)].title : "Custom";' +
'  var anyShadow = ["hour", "min", "sec"].some(function (kind) {' +
'    var el = document.getElementById(heHiddenPrefix(kind) + "ShadowEnabled");' +
'    return el && el.value === "true";' +
'  });' +
'  setSubsubheaderText("hands", title + " hands" + (anyShadow ? ", shadow on" : ""));' +
'}' +
// Same idea for Indices style -- the thumbnail is whichever bitmap or
// procedural-preset picture MARKER_STYLE_TITLES/updateMarkerStyleButtonLabel()
// already resolve bigAnalogMarkerStyle\'s current value to, blank for
// Custom (8) and None (9) alike (neither has one static picture to
// show -- Custom is user-defined per-ring geometry, None draws
// nothing at all).
'function refreshIndicesSubsectionPreview() {' +
'  var val = document.getElementById("bigAnalogMarkerStyle").value;' +
'  var bmp = MARKER_BITMAP_STYLES.filter(function (s) { return s.value === val; })[0];' +
'  var img = null, isBitmap = false;' +
'  if (bmp) { img = MARKER_PREVIEW_IMAGES[val]; isBitmap = true; }' +
'  else {' +
'    var preset = MARKER_PRESET_STYLES.filter(function (s) { return s.value === val; })[0];' +
'    if (preset) img = MARKER_PRESET_IMAGES[preset.image];' +
'  }' +
'  setPreviewThumbnail("subsecpreview-indices", img ? (\'<img class="\' + (isBitmap ? "bitmap-marker-img" : "") + \'" src="\' + img + \'" alt="">\') : "");' +
'  var title = MARKER_STYLE_TITLES.hasOwnProperty(val) ? MARKER_STYLE_TITLES[val] : "Custom";' +
'  setSubsubheaderText("indices", title + " indices");' +
'}' +

// ---- marker style picker popup -----------------------------------------
// Unlike hands, markers keep a real on-watch "which style" field
// (bigAnalogMarkerStyle, still 0-9 -- see eclipse_data.h) -- this
// popup is just a friendlier picker for that same hidden <select>,
// not a replacement for it. MARKER_BITMAP_STYLES covers the 5
// existing bitmap styles (their own thumbnails already exist as
// MARKER_PREVIEW_IMAGES, generated from the actual watch resource
// PNGs -- see generate-marker-previews.js); MARKER_PRESET_STYLES
// covers the 4 procedural ring styles, thumbnails from
// MARKER_PRESET_IMAGES (generate-infographics.js).
'var MARKER_BITMAP_STYLES = [' +
'  { value: "3", title: "Modern" },' +
'  { value: "4", title: "Shadow" },' +
'  { value: "5", title: "Tally" },' +
'  { value: "6", title: "Bell" },' +
'  { value: "7", title: "Fancy" }' +
'];' +
'var MARKER_PRESET_STYLES = [' +
'  { value: "9", title: "None", image: "none" },' +
'  { value: "0", title: "Minimal", image: "minimal" },' +
'  { value: "1", title: "Small", image: "small" },' +
'  { value: "2", title: "Big", image: "big" }' +
'];' +
// Trigger-button label lookup -- every MARKER_BITMAP_STYLES/
// MARKER_PRESET_STYLES entry\'s own title, plus "8" (Custom, the only
// value neither array carries since it has no picker button of its
// own -- see chooseMarkerStyleCustom()).
'var MARKER_STYLE_TITLES = { "8": "Custom" };' +
'MARKER_BITMAP_STYLES.concat(MARKER_PRESET_STYLES).forEach(function (s) { MARKER_STYLE_TITLES[s.value] = s.title; });' +
'function updateMarkerStyleButtonLabel() {' +
'  var span = document.getElementById("markerStyleTriggerLabel");' +
'  if (!span) return;' +
'  var val = document.getElementById("bigAnalogMarkerStyle").value;' +
'  span.textContent = MARKER_STYLE_TITLES.hasOwnProperty(val) ? MARKER_STYLE_TITLES[val] : "Custom";' +
'}' +
'function renderMarkerStyleGrid() {' +
'  var grid = document.getElementById("markerStyleGrid");' +
'  if (!grid) return;' +
'  var html = "";' +
'  MARKER_BITMAP_STYLES.forEach(function (s) {' +
'    var img = MARKER_PREVIEW_IMAGES[s.value];' +
'    html += \'<button type="button" class="style-picker-btn" onclick="chooseMarkerStyle(\\\'\' + s.value + \'\\\')">\' +' +
'      (img ? \'<img class="bitmap-marker-img" src="\' + img + \'" alt="\' + s.title + \'">\' : \'<span class="style-picker-btn-empty">\' + s.title + "</span>") +' +
'      \'<span class="style-picker-btn-cap">\' + s.title + "</span></button>";' +
'  });' +
'  MARKER_PRESET_STYLES.forEach(function (s) {' +
'    var img = MARKER_PRESET_IMAGES[s.image];' +
'    html += \'<button type="button" class="style-picker-btn" onclick="chooseMarkerStyle(\\\'\' + s.value + \'\\\')">\' +' +
'      (img ? \'<img src="\' + img + \'" alt="\' + s.title + \'">\' : \'<span class="style-picker-btn-empty">\' + s.title + "</span>") +' +
'      \'<span class="style-picker-btn-cap">\' + s.title + "</span></button>";' +
'  });' +
'  grid.innerHTML = html;' +
'}' +
'function openMarkerStyleModal() {' +
'  document.getElementById("markerStyleModal").className = "modal-overlay open";' +
'}' +
'function closeMarkerStyleModal() {' +
'  document.getElementById("markerStyleModal").className = "modal-overlay";' +
'}' +
'function chooseMarkerStyle(value) {' +
'  document.getElementById("bigAnalogMarkerStyle").value = value;' +
'  onMarkerStyleChange();' +
'  closeMarkerStyleModal();' +
'}' +
// "Custom" -- sets the underlying select to the real custom value (8)
// so customMarkerSection actually shows, then closes -- same meaning
// the old dropdown\'s "Custom" option had.
'function chooseMarkerStyleCustom() {' +
'  document.getElementById("bigAnalogMarkerStyle").value = "8";' +
'  onMarkerStyleChange();' +
'  closeMarkerStyleModal();' +
'}' +
'function onShadowAngleInput() {' +
'  var el = document.getElementById("shadowAngle");' +
'  var out = document.getElementById("shadowAngleVal");' +
'  if (el && out) out.textContent = el.value + "\u00b0";' +
'  updatePreview();' +
'}' +
// ---- Style Presets: export/import + 3 quick-recall slots -----------
// Scoped to exactly the DOM containers the design covers (Style,
// Colors, Features/\'corners\', Animation) by walking every input/
// select/textarea with an id inside them -- deliberately NOT a hand-
// maintained field list, so this never goes stale as fields get added
// to any of them.
'var PRESET_SCOPE_IDS = ["section-style", "section-colors", "section-corners", "section-animation"];' +
'function collectStyleCornersJson() {' +
'  var obj = {};' +
'  PRESET_SCOPE_IDS.forEach(function (containerId) {' +
'    var container = document.getElementById(containerId);' +
'    if (!container) return;' +
'    var els = container.querySelectorAll("input[id], select[id], textarea[id]");' +
'    els.forEach(function (el) {' +
'      if (el.type === "radio") {' +
'        if (el.checked) obj[el.name] = el.value;' + // one entry per group (its own name, not each option's own id), only for whichever option is actually checked
'        return;' +
'      }' +
'      obj[el.id] = (el.type === "checkbox") ? el.checked : el.value;' +
'    });' +
'  });' +
'  return obj;' +
'}' +
// Sets every id->value pair from a previously-exported (or a preset
// slot\'s saved) object, then re-runs the same cascade of dependent-UI
// handlers each field\'s own onchange would have triggered by hand --
// show/hide rows, slot picker labels, the preview canvas.
'function applyStyleCornersJson(obj) {' +
'  Object.keys(obj).forEach(function (id) {' +
'    var el = document.getElementById(id);' +
'    if (!el) {' +
'      var radio = document.querySelector(\'input[name="\' + id + \'"][value="\' + obj[id] + \'"]\');' + // no plain element has this id -- try it as a radio group name instead
'      if (radio) radio.checked = true;' +
'      return;' +
'    }' +
'    if (el.type === "checkbox") el.checked = !!obj[id]; else el.value = obj[id];' +
'  });' +
'  selectVerticalOption("bgAnimModeGroup", "bgAnimMode", document.getElementById("bgAnimMode").value);' +
'  selectVerticalOption("shakeAnimModeGroup", "shakeAnimMode", document.getElementById("shakeAnimMode").value);' +
'  onBottomStyleChange();' +
'  onMarkerStyleChange();' +
'  updateHandStyleButtonLabel();' +
'  onCornerFontChange();' +
'  onSkyModeChange();' +
'  onShowSecondsChange();' +
'  updateColorRoleButtons();' +
'  updateColorRoleButtons("night");' +
'  renderSlotPicker();' +
'  refreshAllFontTriggerLabels();' +
'  refreshEditButtonLabels();' +
'  updatePreview();' +
'}' +
'function exportDesignJson() {' +
'  document.getElementById("presetExportBox").value = JSON.stringify(collectStyleCornersJson(), null, 2);' +
'}' +
// navigator.clipboard needs a secure context and isn't guaranteed to
// exist in every webview Pebble's settings page might run inside --
// falls back to just selecting the text (same as tapping the box
// itself already does) so the user can still copy it via the
// device's own selection menu.
'function copyExportBoxToClipboard() {' +
'  var box = document.getElementById("presetExportBox");' +
'  var status = document.getElementById("presetExportStatus");' +
'  if (!box.value) exportDesignJson();' +
'  box.select();' +
'  if (navigator.clipboard && navigator.clipboard.writeText) {' +
'    navigator.clipboard.writeText(box.value).then(function () {' +
'      if (status) status.textContent = "Copied to clipboard.";' +
'    }, function () {' +
'      if (status) status.textContent = "Couldn\'t copy automatically -- text is selected, copy it from there.";' +
'    });' +
'  } else if (status) {' +
'    status.textContent = "Couldn\'t copy automatically -- text is selected, copy it from there.";' +
'  }' +
'}' +
'function pasteImportBoxFromClipboard() {' +
'  var box = document.getElementById("presetImportBox");' +
'  var status = document.getElementById("presetImportStatus");' +
'  if (navigator.clipboard && navigator.clipboard.readText) {' +
'    navigator.clipboard.readText().then(function (text) {' +
'      box.value = text;' +
'      if (status) status.textContent = "Pasted -- tap Apply to use it.";' +
'    }, function () {' +
'      if (status) status.textContent = "Couldn\'t read the clipboard automatically -- paste into the box by hand instead.";' +
'    });' +
'  } else if (status) {' +
'    status.textContent = "Clipboard access isn\'t available here -- paste into the box by hand instead.";' +
'  }' +
'}' +
'function importDesignJson() {' +
'  var status = document.getElementById("presetImportStatus");' +
'  var raw = document.getElementById("presetImportBox").value;' +
'  var obj;' +
'  try { obj = JSON.parse(raw); } catch (e) {' +
'    if (status) status.textContent = "Couldn\'t parse that as JSON.";' +
'    return;' +
'  }' +
'  applyStyleCornersJson(obj);' +
'  if (status) status.textContent = "Applied.";' +
'}' +
// Shared "are you sure?" step -- stashes the action to run and shows
// the confirm modal; confirmModalYes() runs it (once) and closes;
// canceling (or tapping outside the box) just closes without running
// anything.
'var s_pendingConfirmAction = null;' +
'function showConfirm(title, message, onConfirm) {' +
'  document.getElementById("confirmModalTitle").textContent = title;' +
'  document.getElementById("confirmModalMessage").textContent = message;' +
'  s_pendingConfirmAction = onConfirm;' +
'  document.getElementById("confirmModal").className = "modal-overlay open";' +
'}' +
'function closeConfirmModal() {' +
'  document.getElementById("confirmModal").className = "modal-overlay";' +
'  s_pendingConfirmAction = null;' +
'}' +
'function confirmModalYes() {' +
'  var action = s_pendingConfirmAction;' +
'  closeConfirmModal();' +
'  if (action) action();' +
'}' +
// One console-style line per logged attempt (newest first), reading
// straight from serviceLogsJson's snapshot of that service's own
// entries -- see servicelog.js's recordAttempt() on the PKJS side for
// exactly what {t, ok, code, label} holds.
'function openServiceLog(service, label) {' +
'  var all = {};' +
'  try { all = JSON.parse(document.getElementById("serviceLogsJson").value || "{}"); } catch (e) {}' +
'  document.getElementById("serviceLogModalTitle").textContent = label + " \\u2014 last attempts";' +
'  var entries = (all[service] && all[service].log) || [];' +
'  var body = document.getElementById("serviceLogModalBody");' +
'  body.innerHTML = "";' +
'  if (entries.length === 0) {' +
'    var empty = document.createElement("div");' +
'    empty.className = "help";' +
'    empty.textContent = "No attempts logged yet.";' +
'    body.appendChild(empty);' +
'  } else {' +
'    entries.slice().reverse().forEach(function (e) {' +
'      var d = new Date(e.t);' +
'      var line = document.createElement("div");' +
'      line.className = "service-log-line";' +
'      var status = e.ok ? "OK" : ("ERR " + e.code + " - " + e.label);' +
'      line.textContent = d.toLocaleDateString() + " " + d.toLocaleTimeString() + "  " + label + "  " + status;' +
'      body.appendChild(line);' +
'    });' +
'  }' +
'  document.getElementById("serviceLogModal").className = "modal-overlay open";' +
'}' +
'function closeServiceLogModal() {' +
'  document.getElementById("serviceLogModal").className = "modal-overlay";' +
'}' +
'function applyPresetSlot(n) {' +
'  var jsonEl = document.getElementById("presetSlot" + n + "Json");' +
'  if (!jsonEl || !jsonEl.value) return;' +
'  var obj;' +
'  try { obj = JSON.parse(jsonEl.value); } catch (e) { return; }' +
'  var name = document.getElementById("presetSlot" + n + "Name").value || ("Preset " + n);' +
'  showConfirm("Apply preset", \'Apply "\' + name + \'"? This replaces your current Style, Colors, and Features settings.\', function () {' +
'    applyStyleCornersJson(obj);' +
'  });' +
'}' +
'function savePresetSlot(n) {' +
'  var jsonEl = document.getElementById("presetSlot" + n + "Json");' +
'  var btn = document.getElementById("presetApplyBtn" + n);' +
'  if (!jsonEl) return;' +
'  var name = document.getElementById("presetSlot" + n + "Name").value || ("Preset " + n);' +
'  var hadPreset = !!jsonEl.value;' +
'  showConfirm("Save preset", (hadPreset ? \'Overwrite "\' : \'Save your current design into "\') + name + \'"?\' + (hadPreset ? \' This replaces what was saved there.\' : \'\'), function () {' +
'    jsonEl.value = JSON.stringify(collectStyleCornersJson());' +
'    if (btn) btn.disabled = false;' +
'  });' +
'}' +
'function startRenamePresetSlot(n) {' +
'  var btn = document.getElementById("presetApplyBtn" + n);' +
'  var input = document.getElementById("presetNameInput" + n);' +
'  var nameEl = document.getElementById("presetSlot" + n + "Name");' +
'  if (!input || !btn) return;' +
'  input.value = nameEl ? nameEl.value : ("Preset " + n);' +
'  btn.style.display = "none";' +
'  input.style.display = "";' +
'  input.focus();' +
'  input.select();' +
'}' +
'function commitRenamePresetSlot(n) {' +
'  var btn = document.getElementById("presetApplyBtn" + n);' +
'  var input = document.getElementById("presetNameInput" + n);' +
'  var nameEl = document.getElementById("presetSlot" + n + "Name");' +
'  if (!input || !btn) return;' +
'  var newName = input.value.replace(/^\\s+|\\s+$/g, "") || ("Preset " + n);' +
'  if (nameEl) nameEl.value = newName;' +
'  btn.textContent = newName;' +
'  input.style.display = "none";' +
'  btn.style.display = "";' +
'}' +
'function onAuroraEnabledChange() {' +
'  var enabled = document.getElementById("auroraEnabled").checked;' +
'  var astro = findCategory("astro");' +
'  var hasIt = astro.items.some(function (it) { return it.id === 84; });' +
'  if (enabled && !hasIt) astro.items.push({ id: 84, label: "Aurora Kp index" });' +
'  if (!enabled && hasIt) astro.items = astro.items.filter(function (it) { return it.id !== 84; });' +
'  CONTENT_SELECT_IDS.forEach(function (id) {' +
'    var sel = document.getElementById(id);' +
'    if (!sel) return;' +
'    var opt = sel.querySelector(\'option[value="84"]\');' +
'    if (enabled && !opt) {' +
'      opt = document.createElement("option");' +
'      opt.value = "84";' +
'      opt.textContent = "Aurora Kp index";' +
'      sel.appendChild(opt);' +
'    } else if (!enabled && opt) {' +
'      if (sel.value === "84") sel.value = "0";' + // dangling selection on a now-hidden option -- fall back to None
'      opt.remove();' +
'    }' +
'  });' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
'function onShowSecondsChange() {' +
'  var box = document.getElementById("showSeconds");' +
'  var btn = document.getElementById("editSecHandBtn");' +
'  if (btn) btn.disabled = !(box.checked && !box.disabled);' +
'  updatePreview();' +
'}' +
'function onSkyModeChange() {' +
'  updatePreview();' +
'}' +
'function updateHandValLabels(kind) {' +
'  var p = hePopupPrefix(kind);' +
'  ["Width", "Length", "BackOffset", "MiddleOffset", "SecondaryWidth", "ShadowDistance", "HollowThickness"].forEach(function (f) {' +
'    var el = document.getElementById(p + f);' +
'    var out = document.getElementById(p + f + "Val");' +
'    if (el && out) out.textContent = el.value + "px";' +
'  });' +
'}' +
// Shows Middle offset for the 8 styles that use it and Secondary
// width for the 7 that do (see HAND_STYLES_WITH_MIDDLE/
// HAND_STYLES_WITH_SECONDARY) -- independently, since Leaf uses only
// the former. The shared help text tags along with Middle offset
// (the superset of the two).
'function updateHandFieldVisibility(kind) {' +
'  var p = hePopupPrefix(kind);' +
'  var styleEl = document.getElementById(p + "Style");' +
'  if (!styleEl) return;' +
'  var showMiddle = HAND_STYLES_WITH_MIDDLE.indexOf(styleEl.value) !== -1;' +
'  var showSecondary = HAND_STYLES_WITH_SECONDARY.indexOf(styleEl.value) !== -1;' +
'  ["MiddleOffsetRow", "MiddleSecondaryHelp"].forEach(function (id) {' +
'    var el = document.getElementById(p + id);' +
'    if (el) el.style.display = showMiddle ? "" : "none";' +
'  });' +
'  var secEl = document.getElementById(p + "SecondaryWidthRow");' +
'  if (secEl) secEl.style.display = showSecondary ? "" : "none";' +
'  var diagramImg = document.getElementById(p + "Diagram");' +
'  if (diagramImg) {' +
'    var src = HAND_STYLE_DIAGRAM_IMAGES[styleEl.value];' +
'    diagramImg.style.display = src ? "" : "none";' +
'    diagramImg.src = src || "";' +
'  }' +
'  updateHandStyleIconTriggerLabel(p);' +
'}' +
// Fills one editor popup\'s own Shape trigger button from whatever its
// underlying (now display:none) <select> currently holds -- same
// "trigger button mirrors a hidden real control" shape
// updateFontTriggerLabel()/updateColorPresetTriggerLabel() already use
// for their own pickers. Called from updateHandFieldVisibility() above
// so it never needs its own separate call site.
// Fills in the "current status" shown right on each Edit .../Numerals
// trigger button (see this file's own request/comment) -- one shared
// function covering all 7 rather than 7 separate single-purpose ones,
// called after anything that can actually change one of these values:
// saving a hand/indices editor, applying a hand or marker preset,
// tapping a Shadow style/Numerals button, dragging the center circle
// slider, or loading/importing a whole style. A little more than any
// one call site strictly needs, but safer than trying to track exactly
// which of the 7 a given change could touch.
'function customMarkerStyleLabel(val) {' +
'  return ["Dot", "Line", "Square", "", "Tapered"][parseInt(val, 10)] || "Dot";' +
'}' +
'function markerTextTargetLabel(val) {' +
'  if (val === "1") return "On hours";' +
'  if (val === "2") return "Every 5s";' +
'  return "Off";' +
'}' +
'function refreshEditButtonLabels() {' +
'  ["hour", "min", "sec"].forEach(function (kind) {' +
'    var el = document.getElementById(heHiddenPrefix(kind) + "Style");' +
'    var span = document.getElementById("hand" + kind.charAt(0).toUpperCase() + kind.slice(1) + "StatusLabel");' +
'    if (el && span) span.textContent = HAND_STYLE_NAMES[parseInt(el.value, 10)] || "Baton";' +
'  });' +
'  var ccSpan = document.getElementById("centerCircleStatusLabel");' +
'  var ccEl = document.getElementById("centerCircleRadius");' +
'  if (ccSpan && ccEl) {' +
'    var ccVal = parseInt(ccEl.value, 10) || 0;' +
'    ccSpan.textContent = ccVal > 0 ? (ccVal + "px") : "Off";' +
'  }' +
'  var shSpan = document.getElementById("shadowStyleStatusLabel");' +
'  var shEl = document.getElementById("shadowTranslucent");' +
'  if (shSpan && shEl) shSpan.textContent = shEl.value === "false" ? "Solid" : "Translucent";' +
'  var anyHandShadow = ["hour", "min", "sec"].some(function (kind) {' +
'    var el = document.getElementById(heHiddenPrefix(kind) + "ShadowEnabled");' +
'    return el && el.value === "true";' +
'  });' +
'  var shBtn = document.getElementById("shadowStyleTriggerBtn");' +
'  if (shBtn) shBtn.disabled = !anyHandShadow;' +
'  ["Hour", "Sec"].forEach(function (kindCap) {' +
'    var span = document.getElementById("cm" + kindCap + "StatusLabel");' +
'    var el = document.getElementById("custom" + kindCap + "Style");' +
'    var thickEl = document.getElementById("custom" + kindCap + "Thickness");' +
'    if (el && span) span.textContent = (thickEl && thickEl.value === "0") ? "Off" : customMarkerStyleLabel(el.value);' +
'  });' +
'  var numSpan = document.getElementById("numeralsStatusLabel");' +
'  var numEl = document.getElementById("markerTextTarget");' +
'  if (numSpan && numEl) numSpan.textContent = markerTextTargetLabel(numEl.value);' +
'}' +
'function updateHandStyleIconTriggerLabel(p) {' +
'  var sel = document.getElementById(p + "Style");' +
'  var trigger = document.getElementById(p + "StyleTrigger");' +
'  if (!sel || !trigger) return;' +
'  var preview = trigger.querySelector(".hand-style-icon-preview");' +
'  var name = trigger.querySelector(".font-picker-name");' +
'  var src = HAND_STYLE_ICON_IMAGES[sel.value];' +
'  if (preview) preview.innerHTML = src ? \'<img src="\' + src + \'" alt="">\' : "";' +
'  if (name) name.textContent = HAND_STYLE_NAMES[parseInt(sel.value, 10)] || "";' +
'}' +
'var HAND_STYLE_ICON_PREFIX = null;' +
'var HAND_STYLE_ICON_KIND = null;' +
'function openHandStyleIconPicker(p, kind) {' +
'  HAND_STYLE_ICON_PREFIX = p;' +
'  HAND_STYLE_ICON_KIND = kind;' +
'  renderHandStyleIconGrid();' +
'  document.getElementById("handStyleIconPickerModal").className = "modal-overlay open";' +
'}' +
'function closeHandStyleIconPicker() {' +
'  document.getElementById("handStyleIconPickerModal").className = "modal-overlay";' +
'}' +
'function renderHandStyleIconGrid() {' +
'  var sel = document.getElementById(HAND_STYLE_ICON_PREFIX + "Style");' +
'  var currentVal = sel ? sel.value : "0";' +
'  var html = "";' +
'  for (var i = 0; i < HAND_STYLE_NAMES.length; i++) {' +
'    var src = HAND_STYLE_ICON_IMAGES[String(i)];' +
'    var selected = String(i) === String(currentVal);' +
'    html += \'<button type="button" class="font-picker-btn\' + (selected ? " selected" : "") + \'" onclick="chooseHandStyleIcon(\' + i + \')">\' +' +
'      \'<span class="font-picker-preview hand-style-icon-preview">\' + (src ? \'<img src="\' + src + \'" alt="">\' : "") + "</span>" +' +
'      \'<span class="font-picker-name">\' + esc(HAND_STYLE_NAMES[i]) + "</span></button>";' +
'  }' +
'  document.getElementById("handStyleIconPickerGrid").innerHTML = html;' +
'}' +
'function chooseHandStyleIcon(value) {' +
'  var sel = document.getElementById(HAND_STYLE_ICON_PREFIX + "Style");' +
'  if (sel) sel.value = value;' +
'  closeHandStyleIconPicker();' +
'  onCustomHandStyleChange(HAND_STYLE_ICON_KIND);' +
'}' +

// Same shape as the hand-style icon picker just above, for
// weather_icon_style\'s own 3 options instead of HandConfig.style\'s 11.
'function updateWeatherIconStyleTriggerLabel() {' +
'  var sel = document.getElementById("weatherIconStyle");' +
'  var trigger = document.getElementById("weatherIconStyleTrigger");' +
'  if (!sel || !trigger) return;' +
'  var preview = trigger.querySelector(".weather-icon-style-preview");' +
'  var name = trigger.querySelector(".font-picker-name");' +
'  var src = WEATHER_ICON_STYLE_PREVIEWS[sel.value];' +
'  if (preview) preview.innerHTML = src ? \'<img src="\' + src + \'" alt="">\' : "";' +
'  if (name) name.textContent = WEATHER_ICON_STYLE_NAMES[parseInt(sel.value, 10)] || "";' +
'}' +
'function openWeatherIconStylePicker() {' +
'  renderWeatherIconStylePickerGrid();' +
'  document.getElementById("weatherIconStylePickerModal").className = "modal-overlay open";' +
'}' +
'function closeWeatherIconStylePicker() {' +
'  document.getElementById("weatherIconStylePickerModal").className = "modal-overlay";' +
'}' +
'function renderWeatherIconStylePickerGrid() {' +
'  var sel = document.getElementById("weatherIconStyle");' +
'  var currentVal = sel ? sel.value : "1";' +
'  var html = "";' +
'  for (var i = 0; i < WEATHER_ICON_STYLE_NAMES.length; i++) {' +
'    var src = WEATHER_ICON_STYLE_PREVIEWS[String(i)];' +
'    var selected = String(i) === String(currentVal);' +
'    html += \'<button type="button" class="font-picker-btn\' + (selected ? " selected" : "") + \'" onclick="chooseWeatherIconStyle(\' + i + \')">\' +' +
'      \'<span class="font-picker-preview weather-icon-style-preview">\' + (src ? \'<img src="\' + src + \'" alt="">\' : "") + "</span>" +' +
'      \'<span class="font-picker-name">\' + esc(WEATHER_ICON_STYLE_NAMES[i]) + "</span></button>";' +
'  }' +
'  document.getElementById("weatherIconStylePickerGrid").innerHTML = html;' +
'}' +
'function chooseWeatherIconStyle(value) {' +
'  var sel = document.getElementById("weatherIconStyle");' +
'  if (sel) sel.value = value;' +
'  closeWeatherIconStylePicker();' +
'  updateWeatherIconStyleTriggerLabel();' +
'}' +
// Shows/hides the Hollow thickness slider based on the Hollow
// checkbox -- called both on the checkbox's own onchange and once up
// front when the popup opens/gets prefilled.
'function updateHandHollowVisibility(kind) {' +
'  var p = hePopupPrefix(kind);' +
'  var box = document.getElementById(p + "Hollow");' +
'  var row = document.getElementById(p + "HollowThicknessRow");' +
'  if (row) row.style.display = (box && box.checked) ? "" : "none";' +
'}' +
'function onHandHollowChange(kind) {' +
'  updateHandHollowVisibility(kind);' +
'  updatePreview();' +
'}' +
'function onCustomHandStyleChange(kind) {' +
'  updateHandFieldVisibility(kind);' +
'  updatePreview();' +
'}' +
// Which hand editor popup (if any) is currently open -- null when
// closed. updatePreview()'s readHandConfig() checks this: for the hand
// currently being edited, it reads the popup's own draft fields
// (hePopupPrefix) so the preview reflects every slider drag/checkbox
// toggle live, per the request; for every other hand (and once this
// one closes, saved or cancelled), it reads the committed handHour*/
// handMin*/handSec* hidden fields instead -- which is also exactly
// what makes Cancel work correctly: an untouched committed value is
// what the preview falls back to the moment the popup closes without
// a Save.
'var s_openHandEditorKind = null;' +
'function onHandSliderInput(kind) {' +
'  updateHandValLabels(kind);' +
'  updatePreview();' +
'}' +
// Pre-fills the popup from the currently-saved handHour*/handMin*/
// handSec* hidden inputs -- nothing is written back until
// saveHandEditor() runs.
'function openHandEditor(kind) {' +
'  s_openHandEditorKind = kind;' +
'  var hp = heHiddenPrefix(kind), p = hePopupPrefix(kind);' +
'  HE_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    if (HE_CHECKBOX_FIELDS.indexOf(f) !== -1) { popupEl.checked = hidden.value === "true"; } else { popupEl.value = hidden.value; }' +
'  });' +
'  updateHandValLabels(kind);' +
'  updateHandFieldVisibility(kind);' +
'  updateHandHollowVisibility(kind);' +
'  document.getElementById("handEditorModal-" + kind).className = "modal-overlay open";' +
'}' +
'function closeHandEditor(kind) {' +
'  s_openHandEditorKind = null;' +
'  document.getElementById("handEditorModal-" + kind).className = "modal-overlay";' +
'  updatePreview();' +
'}' +
'function saveHandEditor(kind) {' +
'  s_openHandEditorKind = null;' +
'  var hp = heHiddenPrefix(kind), p = hePopupPrefix(kind);' +
'  HE_FIELDS.forEach(function (f) {' +
'    var hidden = document.getElementById(hp + f);' +
'    var popupEl = document.getElementById(p + f);' +
'    if (!hidden || !popupEl) return;' +
'    hidden.value = (HE_CHECKBOX_FIELDS.indexOf(f) !== -1) ? String(popupEl.checked) : popupEl.value;' +
'  });' +
'  closeHandEditor(kind);' +
'  updateHandStyleButtonLabel();' +
'  refreshEditButtonLabels();' +
'  updatePreview();' +
'}' +
// Copies the OTHER hand's last-saved settings into this popup's draft
// controls (not committed until OK) -- direction is fixed per hand, see
// HAND_COPY_SOURCE: hour<-minute, minute<-hour, second<-minute.
'function copyHandConfig(kind) {' +
'  var srcHp = heHiddenPrefix(HAND_COPY_SOURCE[kind]);' +
'  var p = hePopupPrefix(kind);' +
'  HE_FIELDS.forEach(function (f) {' +
'    var src = document.getElementById(srcHp + f);' +
'    var dst = document.getElementById(p + f);' +
'    if (!src || !dst) return;' +
'    if (HE_CHECKBOX_FIELDS.indexOf(f) !== -1) { dst.checked = src.value === "true"; } else { dst.value = src.value; }' +
'  });' +
'  updateHandValLabels(kind);' +
'  updateHandFieldVisibility(kind);' +
'  updateHandHollowVisibility(kind);' +
'}' +

'function onCornerFontChange() {' +
'  renderSlotPicker();' +
'  updatePreview();' +
'}' +
'function selectBottomStyle(val) {' +
'  document.getElementById("bottomStyleValue").value = val;' +
'  var buttons = document.getElementById("bottomStyleGroup").getElementsByClassName("mode-btn");' +
'  var order = ["digital", "analog"];' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    buttons[i].className = "mode-btn" + (order[i] === val ? " active" : "");' +
'  }' +
'  onBottomStyleChange();' +
'}' +
// Falls back to the group's first button (Off, for every current
// caller) when val doesn't match any button's own data-value -- same
// reasoning as verticalButtonGroupHtml()'s own comment, but for
// values arriving after the initial render (a preset being applied
// via applyStyleCornersJson(), which sets the hidden field\'s value
// straight from possibly-stale saved JSON before calling this). Also
// corrects the hidden field itself to the fallback value, not just
// the highlighted button, so a subsequent Save persists the sane
// fallback rather than the original invalid string.
'function selectVerticalOption(groupId, hiddenId, val) {' +
'  var buttons = document.getElementById(groupId).getElementsByClassName("mode-btn-vertical");' +
'  var matched = false;' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    if (buttons[i].getAttribute("data-value") === val) { matched = true; break; }' +
'  }' +
'  if (!matched && buttons.length) val = buttons[0].getAttribute("data-value");' +
'  document.getElementById(hiddenId).value = val;' +
'  for (var j = 0; j < buttons.length; j++) {' +
'    buttons[j].className = "mode-btn-vertical" + (buttons[j].getAttribute("data-value") === val ? " active" : "");' +
'  }' +
'  updatePreview();' +
'}' +
// Same "hidden field is the real value, buttons just reflect it"
// mechanics as selectVerticalOption() above, for a modeButtonGroupHtml()
// row instead of a verticalButtonGroupHtml() one -- shared by every
// converted dropdown below except Sky style, which needs its own
// extra step (see selectSkyMode() right after this).
'function selectModeButton(groupId, hiddenId, val) {' +
'  document.getElementById(hiddenId).value = val;' +
'  var buttons = document.getElementById(groupId).getElementsByClassName("mode-btn");' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    buttons[i].className = "mode-btn" + (buttons[i].getAttribute("data-value") === val ? " active" : "");' +
'  }' +
'}' +
// For the few places something OTHER than a direct button tap changes
// one of these hidden fields\' value -- a preset being applied, or a
// popup being pre-filled from already-saved state on open -- and the
// button group needs to catch up to reflect it, since setting
// .value on the hidden input alone (the way any of those already did
// before the field in question was a plain <select>) doesn\'t touch
// the buttons\' own "active" class the way a real tap does via
// selectModeButton() above.
'function refreshModeButtonGroup(groupId, hiddenId) {' +
'  var group = document.getElementById(groupId);' +
'  var hidden = document.getElementById(hiddenId);' +
'  if (!group || !hidden) return;' +
'  var buttons = group.getElementsByClassName("mode-btn");' +
'  for (var i = 0; i < buttons.length; i++) {' +
'    buttons[i].className = "mode-btn" + (buttons[i].getAttribute("data-value") === hidden.value ? " active" : "");' +
'  }' +
'}' +
'function selectSkyMode(val) {' +
'  selectModeButton("skyModeGroup", "skyMode", val);' +
'  onSkyModeChange();' +
'}' +
'function onOutlineStyleChange(val) {' +
'  selectModeButton("outlineStyleGroup", "outlineStyle", val);' +
'  updatePreview();' +
'}' +
// Rounds an <input type="time"> element\'s "HH:MM" value to the
// nearest whole hour (23:xx rounds up to 00:00 the same as any other
// hour, matching a plain clock-face reading of "closer to midnight
// than to 11pm"). Used both right when "On full hours" is picked
// (so a previously-set 8:15 becomes 8:00 immediately) and on every
// live edit of either time field while that mode stays selected --
// see onHourlyVibeTimeChange() below.
'function roundTimeInputToHour(el) {' +
'  if (!el || !el.value) return;' +
'  var parts = el.value.split(":");' +
'  var h = parseInt(parts[0], 10) || 0, m = parseInt(parts[1], 10) || 0;' +
'  if (m >= 30) h = (h + 1) % 24;' +
'  el.value = (h < 10 ? "0" : "") + h + ":00";' +
'}' +
// Shows the Minutes interval slider only for "Every X minutes",
// grays out (and, via pointer-events:none from .grayed-out, disables)
// every sub-option -- pattern, time range, days, override-quiet --
// when Hourly vibrations is "Off" outright, since none of them mean
// anything without vibrations happening at all, and snaps both time
// fields to a whole hour the instant "On full hours" is chosen.
'function updateHourlyVibeVisibility() {' +
'  var mode = document.getElementById("hourlyVibeMode").value;' +
'  var intervalRow = document.getElementById("hourlyVibeIntervalRow");' +
'  var subOptions = document.getElementById("hourlyVibeSubOptions");' +
'  var quietRow = document.getElementById("hourlyVibeOverrideQuietRow");' +
'  if (intervalRow) intervalRow.style.display = (mode === "2") ? "" : "none";' +
'  if (subOptions) subOptions.className = (mode === "0") ? "grayed-out" : "";' +
'  if (quietRow) quietRow.className = "checkbox-row" + (mode === "0" ? " grayed-out" : "");' +
'  if (mode === "1") {' +
'    roundTimeInputToHour(document.getElementById("hourlyVibeStartTime"));' +
'    roundTimeInputToHour(document.getElementById("hourlyVibeEndTime"));' +
'  }' +
'}' +
'function onHourlyVibeModeChange(val) {' +
'  selectVerticalOption("hourlyVibeModeGroup", "hourlyVibeMode", val);' +
'  updateHourlyVibeVisibility();' +
'}' +
'function onHourlyVibeTimeChange(id) {' +
'  if (document.getElementById("hourlyVibeMode").value === "1") {' +
'    roundTimeInputToHour(document.getElementById(id));' +
'  }' +
'  updatePreview();' +
'}' +
// hourlyVibeDaysMask: one bit per day, bit i = HOURLY_VIBE_DAY_LABELS[i]
// (Sun=bit 0 .. Sat=bit 6), matching struct tm\'s own tm_wday directly
// -- see that array\'s own comment. Independent toggle, not a "pick
// one" group, so this flips a single bit rather than calling
// selectModeButton()/selectVerticalOption().
'function toggleHourlyVibeDay(bit) {' +
'  var hidden = document.getElementById("hourlyVibeDaysMask");' +
'  var mask = parseInt(hidden.value, 10) || 0;' +
'  mask ^= (1 << bit);' +
'  hidden.value = String(mask);' +
'  var btn = document.querySelector(\'.day-toggle-btn[data-bit="\' + bit + \'"]\');' +
'  if (btn) btn.className = "day-toggle-btn" + ((mask & (1 << bit)) ? " active" : "");' +
'  updatePreview();' +
'}' +
'function selectShadowTranslucent(val) {' +
'  selectModeButton("shadowTranslucentGroup", "shadowTranslucent", val);' +
'  refreshEditButtonLabels();' +
'  updatePreview();' +
'}' +
'function onFontChange() {' +
'  refreshAllFontTriggerLabels();' +
'  onBottomStyleChange();' +
'}' +
'function onAnalogStyleChange() { updatePreview(); }' +

'function updateColorRoleButtons(scheme) {' +
'  var colors = scheme === "night" ? nightColors() : dayColors();' +
'  var prefix = scheme === "night" ? "swatchNight" : "swatch";' +
'  document.getElementById(prefix + "Main").style.background = colors.text;' +
'  document.getElementById(prefix + "Accent").style.background = colors.accent;' +
'  document.getElementById(prefix + "Bg").style.background = colors.bg;' +
'  updateColorPresetTriggerLabel(scheme);' +
'}' +

'function hexToByte(hex) {' +
'  var r = parseInt(hex.substr(1, 2), 16), g = parseInt(hex.substr(3, 2), 16), b = parseInt(hex.substr(5, 2), 16);' +
'  function to2bit(v) { return Math.round(v / 85); }' +
'  return packedByteFor(to2bit(r), to2bit(g), to2bit(b));' +
'}' +

// ---- Color preset picker -------------------------------------------
// A preset\'s own `label` is already written as "<Main> on <Background>"
// (optionally followed by ", <Accent> accent" when the accent differs
// from the main color -- see COLOR_SCHEMES\' own comment) -- these two
// helpers split that one string back into the two separate pieces this
// popup shows on its own two lines, rather than duplicating the accent
// name a second time by showing the label\'s full text verbatim on line
// 1 as well as again on line 2.
'function mainColorPhrase(label) {' +
'  var idx = label.indexOf(",");' +
'  return idx === -1 ? label : label.substring(0, idx);' +
'}' +
'function accentColorName(label) {' +
'  var m = /,\\s*(.+?)\\s+accent$/.exec(label);' +
'  if (m) return m[1];' +
'  var m2 = /^(.+?)\\s+on\\s+/.exec(label);' +
'  return m2 ? m2[1] : label;' +
'}' +
// The one preset (if any) whose 3 colors exactly match the current
// ones -- null means none do, i.e. the current colors are genuinely
// custom. Used both to mark that one button "selected" in the popup
// and to decide what the trigger button itself should say.
'function matchingPresetId(colors) {' +
'  for (var i = 0; i < COLOR_SCHEMES.length; i++) {' +
'    var s = COLOR_SCHEMES[i];' +
'    if (s.bg.toLowerCase() === colors.bg.toLowerCase() && s.text.toLowerCase() === colors.text.toLowerCase() && s.accent.toLowerCase() === colors.accent.toLowerCase()) {' +
'      return s.id;' +
'    }' +
'  }' +
'  return null;' +
'}' +
// Mirrors whatever the popup itself would show for the currently
// active entry (a matching preset\'s own two lines, or the "Custom: ..."
// pair when nothing matches) -- called from updateColorRoleButtons()
// above, so it\'s always in sync with the swatches without needing its
// own separate call site at every place colors can change.
'function updateColorPresetTriggerLabel(scheme) {' +
'  var colors = scheme === "night" ? nightColors() : dayColors();' +
'  var trigger = document.getElementById(scheme === "night" ? "nightSchemePresetTrigger" : "colorSchemePresetTrigger");' +
'  if (!trigger) return;' +
'  var mainLine = trigger.querySelector(".color-preset-main-line");' +
'  var accentLine = trigger.querySelector(".color-preset-accent-line");' +
'  trigger.style.background = colors.bg;' +
'  var matchId = matchingPresetId(colors);' +
'  if (matchId !== null) {' +
'    var preset = findPresetById(matchId);' +
'    if (mainLine) { mainLine.textContent = mainColorPhrase(preset.label); mainLine.style.color = preset.text; }' +
'    if (accentLine) { accentLine.textContent = accentColorName(preset.label) + " accent color"; accentLine.style.color = preset.accent; }' +
'  } else {' +
'    if (mainLine) { mainLine.textContent = "Custom: Main color on Background color"; mainLine.style.color = colors.text; }' +
'    if (accentLine) { accentLine.textContent = "Accent color"; accentLine.style.color = colors.accent; }' +
'  }' +
'}' +
'var CURRENT_PRESET_SCHEME = "day";' +
'function openColorPresetPicker(scheme) {' +
'  CURRENT_PRESET_SCHEME = scheme || "day";' +
'  document.getElementById("colorPresetPickerTitle").textContent = (scheme === "night") ? "Night color preset" : "Color preset";' +
'  renderColorPresetGrid();' +
'  document.getElementById("colorPresetPickerModal").className = "modal-overlay open";' +
'}' +
'function closeColorPresetPicker() {' +
'  document.getElementById("colorPresetPickerModal").className = "modal-overlay";' +
'}' +
// One button per COLOR_SCHEMES entry, colored in that preset\'s own
// background, plus one final "Custom" button (see this file\'s own
// request/comment above) showing whatever the current colors actually
// are -- not a separate footer action, just the last item in the same
// list. Re-run whenever the popup opens and whenever it matters which
// entry counts as "selected" -- which is only ever "which one, if any,
// matches the current colors", so there\'s nothing else to invalidate
// this on.
'function renderColorPresetGrid() {' +
'  var scheme = CURRENT_PRESET_SCHEME;' +
'  var colors = scheme === "night" ? nightColors() : dayColors();' +
'  var matchId = matchingPresetId(colors);' +
'  var html = "";' +
'  COLOR_SCHEMES.forEach(function (s) {' +
'    var selected = matchId !== null && String(matchId) === String(s.id);' +
'    html += \'<button type="button" class="color-preset-btn\' + (selected ? " selected" : "") + \'" style="background:\' + s.bg + \';" onclick="chooseColorPreset(\' + s.id + \')">\' +' +
'      \'<span class="color-preset-main-line" style="color:\' + s.text + \';">\' + esc(mainColorPhrase(s.label)) + "</span>" +' +
'      \'<span class="color-preset-accent-line" style="color:\' + s.accent + \';">\' + esc(accentColorName(s.label) + " accent color") + "</span></button>";' +
'  });' +
'  var customSelected = matchId === null;' +
'  html += \'<button type="button" class="color-preset-btn\' + (customSelected ? " selected" : "") + \'" style="background:\' + colors.bg + \';" onclick="chooseColorPreset(null)">\' +' +
'    \'<span class="color-preset-main-line" style="color:\' + colors.text + \';">Custom: Main color on Background color</span>\' +' +
'    \'<span class="color-preset-accent-line" style="color:\' + colors.accent + \';">Accent color</span></button>\';' +
'  document.getElementById("colorPresetPickerGrid").innerHTML = html;' +
'}' +
// null id means "Custom" was tapped -- same as tapping outside the
// popup, that just closes it without touching any colors (there\'s
// nothing to "apply" -- the current colors already are what they are).
'function chooseColorPreset(id) {' +
'  var scheme = CURRENT_PRESET_SCHEME;' +
'  closeColorPresetPicker();' +
'  if (id === null || id === undefined) return;' +
'  var preset = findPresetById(id);' +
'  document.getElementById(customHiddenIdFor("text", scheme)).value = hexToByte(preset.text);' +
'  document.getElementById(customHiddenIdFor("accent", scheme)).value = hexToByte(preset.accent);' +
'  document.getElementById(customHiddenIdFor("bg", scheme)).value = hexToByte(preset.bg);' +
'  updateColorRoleButtons(scheme);' +
'  if (scheme !== "night") updatePreview();' +
'}' +

'var CURRENT_PICKER_ROLE = null;' +
'var CURRENT_PICKER_SCHEME = "day";' +
'function openColorPicker(role, scheme) {' +
'  CURRENT_PICKER_ROLE = role;' +
'  CURRENT_PICKER_SCHEME = scheme || "day";' +
'  var titles = { text: "Pick main color", accent: "Pick accent color", bg: "Pick background color" };' +
'  document.getElementById("colorPickerTitle").textContent = titles[role] || "Pick a color";' +
'  renderHexColorGrid();' +
'  document.getElementById("colorPickerModal").className = "modal-overlay open";' +
'}' +
'function closeColorPicker() {' +
'  document.getElementById("colorPickerModal").className = "modal-overlay";' +
'  CURRENT_PICKER_ROLE = null;' +
'}' +
'function goBack() {' +
'  document.location = getQueryParam("return_to", "pebblejs://close#");' +
'}' +
'function openDonateModal() {' +
'  document.getElementById("donateModal").className = "modal-overlay open";' +
'}' +
'function closeDonateModal() {' +
'  document.getElementById("donateModal").className = "modal-overlay";' +
'}' +
// The bar's own height varies by device (font scaling, safe-area
// insets) and is capped at 25vh by CSS, so this measures it after
// layout rather than assuming a fixed value, and pushes the
// scrollable content down by exactly that much so nothing starts out
// hidden underneath it.
'function adjustTopBarSpacing() {' +
'  var bar = document.getElementById("topBar");' +
'  if (!bar) return;' +
'  document.body.style.paddingTop = bar.offsetHeight + "px";' +
'}' +
'window.addEventListener("load", adjustTopBarSpacing);' +
'window.addEventListener("resize", adjustTopBarSpacing);' +
'function customHiddenIdFor(role, scheme) {' +
'  var prefix = scheme === "night" ? "night" : "";' +
'  if (role === "text") return prefix ? "nightCustomTextValue" : "customTextValue";' +
'  if (role === "accent") return prefix ? "nightCustomAccentValue" : "customAccentValue";' +
'  return prefix ? "nightCustomBgValue" : "customBgValue";' +
'}' +

// Converts 0-255 RGB to HSL (h in degrees 0-360, s/l 0-1) -- used to
// arrange the wheel by hue, the way a real color wheel reads.
// Exact layout of Pebble's real color-picker tool (developer.rebble.io/
// guides/tools-and-resources/color-picker/), extracted directly from
// its SVG: each hexagon's pixel center was converted to axial (q, r)
// hex-grid coordinates (pointy-top orientation -- flat left/right
// sides, pointed top/bottom vertices, unlike the flat-top approximation
// used before), and matched to its packed color byte. The shape isn't
// a simple symmetric hexagon; flood-filling from outside the shape\'s
// bounding box found exactly 5 cells fully enclosed by colored
// neighbors on all sides but not themselves colored -- those are the
// genuine hollow gaps, listed separately below.
'var PEBBLE_WHEEL_POSITIONS = [' +
'{q:-5,r:2,b:239},{q:-4,r:1,b:223},{q:-3,r:-1,b:222},{q:-3,r:0,b:206},{q:-3,r:1,b:207},{q:-3,r:3,b:219},{q:-3,r:4,b:199},{q:-2,r:-4,b:238},{q:-2,r:-3,b:221},{q:-2,r:-2,b:205},{q:-2,r:-1,b:201},{q:-2,r:0,b:202},{q:-2,r:2,b:203},{q:-2,r:3,b:195},{q:-2,r:4,b:215},{q:-2,r:5,b:235},{q:-1,r:-3,b:204},{q:-1,r:-2,b:200},{q:-1,r:-1,b:217},{q:-1,r:0,b:218},{q:-1,r:1,b:198},{q:-1,r:2,b:194},{q:-1,r:3,b:211},{q:-1,r:4,b:214},{q:0,r:-4,b:220},{q:0,r:-2,b:216},{q:0,r:-1,b:196},{q:0,r:0,b:197},{q:0,r:2,b:193},{q:0,r:3,b:210},{q:0,r:4,b:227},{q:0,r:5,b:231},{q:1,r:-5,b:237},{q:1,r:-4,b:236},{q:1,r:-2,b:233},{q:1,r:-1,b:212},{q:1,r:2,b:209},{q:1,r:3,b:226},{q:1,r:4,b:230},{q:2,r:-2,b:232},{q:2,r:1,b:208},{q:2,r:2,b:225},{q:2,r:3,b:243},{q:2,r:4,b:247},{q:3,r:-3,b:252},{q:3,r:-2,b:248},{q:3,r:-1,b:228},{q:3,r:0,b:229},{q:3,r:1,b:224},{q:3,r:2,b:242},{q:3,r:3,b:246},{q:3,r:4,b:251},{q:4,r:-5,b:254},{q:4,r:-4,b:253},{q:4,r:-3,b:249},{q:4,r:-2,b:244},{q:4,r:-1,b:240},{q:4,r:0,b:241},{q:5,r:-1,b:245},{q:6,r:-5,b:234},{q:6,r:-4,b:192},{q:6,r:-2,b:250},{q:7,r:-5,b:255},{q:7,r:-4,b:213}' +
'];' +
'var PEBBLE_WHEEL_HOLLOW = [{q:0,r:1},{q:1,r:0},{q:1,r:1},{q:2,r:-1},{q:2,r:0}];' +

'function renderHexColorGrid() {' +
'  var container = document.getElementById("hexColorGrid");' +
'  container.innerHTML = "";' +
'  var hiddenId = customHiddenIdFor(CURRENT_PICKER_ROLE, CURRENT_PICKER_SCHEME);' +
'  var selected = parseInt(document.getElementById(hiddenId).value, 10);' +
'  var size = 15;' +
'  var centerX = 130, centerY = 127.5;' + // must match .hex-grid's fixed CSS width/height
'  function place(q, r) {' +
'    return {' +
'      x: centerX + size * Math.sqrt(3) * (q + r / 2),' +
'      y: centerY + size * 1.5 * r' +
'    };' +
'  }' +
'  for (var i = 0; i < PEBBLE_WHEEL_POSITIONS.length; i++) {' +
'    var pos = PEBBLE_WHEEL_POSITIONS[i];' +
'    var pt = place(pos.q, pos.r);' +
'    var sw = document.createElement("div");' +
'    sw.style.left = pt.x + "px";' +
'    sw.style.top = pt.y + "px";' +
'    sw.className = "hex-swatch" + (pos.b === selected ? " selected" : "");' +
'    sw.style.background = hexFromByte(pos.b);' +
'    sw.onclick = (function (byte) { return function () { pickColor(byte); }; })(pos.b);' +
'    container.appendChild(sw);' +
'  }' +
'  for (var j = 0; j < PEBBLE_WHEEL_HOLLOW.length; j++) {' +
'    var hp = PEBBLE_WHEEL_HOLLOW[j];' +
'    var hpt = place(hp.q, hp.r);' +
'    var hsw = document.createElement("div");' +
'    hsw.style.left = hpt.x + "px";' +
'    hsw.style.top = hpt.y + "px";' +
'    hsw.className = "hex-swatch hollow";' +
'    container.appendChild(hsw);' +
'  }' +
'}' +
'function pickColor(byte) {' +
'  if (!CURRENT_PICKER_ROLE) return;' +
'  var scheme = CURRENT_PICKER_SCHEME;' +
'  document.getElementById(customHiddenIdFor(CURRENT_PICKER_ROLE, scheme)).value = byte;' +
'  closeColorPicker();' +
'  updateColorRoleButtons(scheme);' +
'  if (scheme !== "night") updatePreview();' +
'}' +
'function onNightToggle() {' +
'  document.getElementById("nightSchemeSettings").style.display = document.getElementById("nightEnabled").checked ? "block" : "none";' +
'  updateSchemeActiveHighlight();' +
'}' +

// Rough "is it night" approximation for the day/night color highlight
// below -- a plain local-clock-hours check, not the watch\'s own real
// sun-altitude/civil-twilight test (get_active_color_scheme() in
// pebble-eclipse-watch.c), since that needs a location and a real
// astronomy calculation this page has no reason to duplicate just for
// a visual hint. Good enough to avoid the actual confusion this
// feature is for (editing colors that silently do nothing because
// they\'re not the active set right now) without claiming to be exact.
'function isNightNowApprox() {' +
'  var h = new Date().getHours();' +
'  return h < 6 || h >= 20;' +
'}' +
// Marks both day and night color subsections with which one is
// actually in effect right now (see .scheme-active-badge\'s own
// comment -- purely informational, nothing here is disabled): green
// "Active now" on the one in effect, red "Not active now" on the
// other. Only meaningful once night colors are actually turned on --
// with only one set of colors there\'s nothing to disambiguate, so
// both badges stay hidden.
'function updateSchemeActiveHighlight() {' +
'  var dayBadge = document.getElementById("daySchemeActiveBadge");' +
'  var nightBadge = document.getElementById("nightSchemeActiveBadge");' +
'  if (!document.getElementById("nightEnabled").checked) {' +
'    dayBadge.style.display = "none";' +
'    nightBadge.style.display = "none";' +
'    return;' +
'  }' +
'  var isNight = isNightNowApprox();' +
'  dayBadge.style.display = "";' +
'  nightBadge.style.display = "";' +
'  dayBadge.textContent = isNight ? "Not active now" : "Active now";' +
'  dayBadge.classList.toggle("inactive", isNight);' +
'  nightBadge.textContent = isNight ? "Active now" : "Not active now";' +
'  nightBadge.classList.toggle("inactive", !isNight);' +
'}' +

'function save(forceRefresh) {' +
'  var mins = parseInt(document.getElementById("updateMins").value, 10);' +
'  if (isNaN(mins) || mins < 5) mins = 20;' +
'  var bottomStyleVal = document.getElementById("bottomStyleValue").value;' +
// Belt-and-suspenders re-check, independent of whatever the (possibly
// stale, possibly never-recomputed-since-load) Show Seconds checkbox
// itself currently holds -- onBottomStyleChange()/onFontChange()
// already gray it out and uncheck it live whenever the selected clock
// font can't support seconds, but this is the actual value that goes
// to the watch, so it\'s re-derived here from the current clock font
// selection directly rather than trusted to already be correct.
'  var clockFontSel = document.getElementById("clockFont");' +
'  var clockFontOpt = clockFontSel.options[clockFontSel.selectedIndex];' +
'  var secondsOverriddenOff = bottomStyleVal === "digital" && clockFontOpt.getAttribute("data-seconds") === "0";' +
'  var showSecondsVal = !secondsOverriddenOff && document.getElementById("showSeconds").checked;' +
// Same "re-derive at save time rather than trust the DOM already
// reflects it" belt-and-suspenders principle as showSecondsVal above,
// now for which of the 8 edge-line content fields actually apply --
// onMarkerStyleChange()/onBottomStyleChange() already clear these live
// as the user changes styles, but this is the actual data that goes to
// the watch, so it\'s re-checked here against the CURRENT marker/bottom
// style regardless of whether an earlier UI event already handled it.
// features_layer.c no longer has its own copy of "which marker/
// bottom_style supports which edge slots" at all -- it just draws
// whatever content it\'s given, trusting a content of 0 to mean "off"
// -- so this is the one and only place that decision gets made.
'  var avail = computeSlotAvailability();' +
'  var isAnalogNow = bottomStyleVal === "analog";' +
'  function edgeVal(id, analogFlag, digitalFlag) {' +
'    var usable = isAnalogNow ? analogFlag : digitalFlag;' +
'    return usable ? document.getElementById(id).value : "0";' +
'  }' +
'  var settings = {' +
'    CONFIG_AUTO_LOC: document.getElementById("autoLoc").checked,' +
'    CONFIG_LAT: document.getElementById("lat").value,' +
'    CONFIG_LON: document.getElementById("lon").value,' +
'    CONFIG_LOCATION_NAME: document.getElementById("locationName").value,' +
'    CONFIG_OWM_KEY: document.getElementById("owmKey").value,' +
'    CONFIG_UPDATE_MINS: mins,' +
'    CONFIG_CLOCK_FONT: document.getElementById("clockFont").value,' +
'    CONFIG_TEMP_UNIT: document.getElementById("tempUnit").value,' +
'    CONFIG_WIND_SPEED_UNIT: document.getElementById("windSpeedUnit").value,' +
'    CONFIG_AQI_UNIT: document.getElementById("aqiUnit").value,' +
'    CONFIG_ALTITUDE_UNIT: document.getElementById("altitudeUnit").value,' +
'    CONFIG_SKY_MODE: document.getElementById("skyMode").value,' +
'    CONFIG_WEATHER_ICON_STYLE: document.getElementById("weatherIconStyle").value,' +
'    CONFIG_SHOW_SECONDS: showSecondsVal,' +
'    CONFIG_CUSTOM_BG: document.getElementById("customBgValue").value,' +
'    CONFIG_CUSTOM_TEXT: document.getElementById("customTextValue").value,' +
'    CONFIG_CUSTOM_ACCENT: document.getElementById("customAccentValue").value,' +
'    CONFIG_NIGHT_ENABLED: document.getElementById("nightEnabled").checked,' +
'    CONFIG_NIGHT_CUSTOM_BG: document.getElementById("nightCustomBgValue").value,' +
'    CONFIG_NIGHT_CUSTOM_TEXT: document.getElementById("nightCustomTextValue").value,' +
'    CONFIG_NIGHT_CUSTOM_ACCENT: document.getElementById("nightCustomAccentValue").value,' +
'    CONFIG_BOTTOM_STYLE: bottomStyleVal || "digital",' +
'    CONFIG_SHADOW_TRANSLUCENT: document.getElementById("shadowTranslucent").value,' +
'    CONFIG_SHADOW_ANGLE: document.getElementById("shadowAngle").value,' +
'    CONFIG_BIG_ANALOG_MARKER_STYLE: document.getElementById("bigAnalogMarkerStyle").value,' +
'    CONFIG_BITMAP_MARKER_TRANSPARENT: document.getElementById("bitmapMarkerTransparent").checked,' +
'    CONFIG_BITMAP_CORNER_OVERRIDE: document.getElementById("bitmapCornerOverride").checked,' +
'    CONFIG_DRAW_FEATURES_BENEATH_HANDS: document.getElementById("drawFeaturesBeneathHands").checked,' +
'    CONFIG_UPPER_MIDDLE_LINE1_CONTENT: edgeVal("upperMiddleLine1Content", avail.upper, avail.digitalLeft),' +
'    CONFIG_UPPER_MIDDLE_LINE1_COLOR: document.getElementById("upperMiddleLine1Color").value,' +
'    CONFIG_UPPER_MIDDLE_LINE2_CONTENT: edgeVal("upperMiddleLine2Content", avail.upper, avail.digitalRight),' +
'    CONFIG_UPPER_MIDDLE_LINE2_COLOR: document.getElementById("upperMiddleLine2Color").value,' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_CONTENT: edgeVal("bottomMiddleLine1Content", avail.bottom, true),' +
'    CONFIG_BOTTOM_MIDDLE_LINE1_COLOR: document.getElementById("bottomMiddleLine1Color").value,' +
'    CONFIG_BOTTOM_MIDDLE_LINE2_CONTENT: edgeVal("bottomMiddleLine2Content", avail.bottom, false),' +
'    CONFIG_BOTTOM_MIDDLE_LINE2_COLOR: document.getElementById("bottomMiddleLine2Color").value,' +
'    CONFIG_MIDDLE_LEFT_LINE1_CONTENT: edgeVal("middleLeftLine1Content", avail.left, avail.digitalLeft),' +
'    CONFIG_MIDDLE_LEFT_LINE1_COLOR: document.getElementById("middleLeftLine1Color").value,' +
'    CONFIG_MIDDLE_LEFT_LINE2_CONTENT: edgeVal("middleLeftLine2Content", avail.left, avail.digitalLeft),' +
'    CONFIG_MIDDLE_LEFT_LINE2_COLOR: document.getElementById("middleLeftLine2Color").value,' +
'    CONFIG_MIDDLE_RIGHT_LINE1_CONTENT: edgeVal("middleRightLine1Content", avail.right, avail.digitalRight),' +
'    CONFIG_MIDDLE_RIGHT_LINE1_COLOR: document.getElementById("middleRightLine1Color").value,' +
'    CONFIG_MIDDLE_RIGHT_LINE2_CONTENT: edgeVal("middleRightLine2Content", avail.right, avail.digitalRight),' +
'    CONFIG_MIDDLE_RIGHT_LINE2_COLOR: document.getElementById("middleRightLine2Color").value,' +
'    CONFIG_DIGITAL_SIDES: document.getElementById("digitalSides").value,' +
'    CONFIG_SHOW_ISS: document.getElementById("showIss").checked,' +
'    CONFIG_AURORA_ENABLED: document.getElementById("auroraEnabled").checked,' +
'    CONFIG_VIBRATE_ON_PHASE_CHANGE: document.getElementById("vibrateOnPhaseChange").checked,' +
'    CONFIG_STARTUP_CLOCK_ANIM_MODE: document.getElementById("startupClockAnimMode").value,' +
'    CONFIG_BG_ANIM_MODE: document.getElementById("bgAnimMode").value,' +
'    CONFIG_SHAKE_ANIM_MODE: document.getElementById("shakeAnimMode").value,' +
'    CONFIG_OUTLINE_ENABLED: document.getElementById("outlineStyle").value,' +
'    CONFIG_BATTERY_SAVER_ENABLED: document.getElementById("batterySaverEnabled").checked,' +
'    CONFIG_CORNER_FONT: document.getElementById("cornerFont").value,' +
'    CONFIG_CORNER_TL: avail.cornersGrayed ? "0" : document.getElementById("cornerTL").value,' +
'    CONFIG_CORNER_TR: avail.cornersGrayed ? "0" : document.getElementById("cornerTR").value,' +
'    CONFIG_CORNER_BL: avail.cornersGrayed ? "0" : document.getElementById("cornerBL").value,' +
'    CONFIG_CORNER_BR: avail.cornersGrayed ? "0" : document.getElementById("cornerBR").value,' +
'    CONFIG_CORNER_TL_COLOR: document.getElementById("cornerTLColor").value,' +
'    CONFIG_CORNER_TR_COLOR: document.getElementById("cornerTRColor").value,' +
'    CONFIG_CORNER_BL_COLOR: document.getElementById("cornerBLColor").value,' +
'    CONFIG_CORNER_BR_COLOR: document.getElementById("cornerBRColor").value,' +
'    CONFIG_STEP_GOAL: document.getElementById("stepGoal").value,' +
'    CONFIG_SUN_MOON_SIZE: document.getElementById("sunMoonSize").value,' +
'    CONFIG_SHAKE_LABEL_SECONDS: document.getElementById("shakeLabelSeconds").value,' +
'    CONFIG_LABEL_STYLE: document.getElementById("labelStyle").value,' +
'    CONFIG_TEST_MODE: document.getElementById("testMode").checked,' +
'    CONFIG_TEST_DATETIME: document.getElementById("testDateTime").value,' +
'    CONFIG_DEBUG_OVERRIDE_ENABLED: document.getElementById("debugOverrideEnabled").checked,' +
'    CONFIG_CUSTOM_HOUR_STYLE: document.getElementById("customHourStyle").value,' +
'    CONFIG_CUSTOM_HOUR_THICKNESS: document.getElementById("customHourThickness").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_THICKNESS: document.getElementById("customHourInnerThickness").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_ECC: document.getElementById("customHourInnerEcc").value,' +
'    CONFIG_CUSTOM_HOUR_OUTER_ECC: document.getElementById("customHourOuterEcc").value,' +
'    CONFIG_CUSTOM_HOUR_INNER_BORDER: document.getElementById("customHourInnerBorder").value,' +
'    CONFIG_CUSTOM_HOUR_OUTER_BORDER: document.getElementById("customHourOuterBorder").value,' +
'    CONFIG_CUSTOM_HOUR_TRANSLUCENT: document.getElementById("customHourTranslucent").value === "true",' +
'    CONFIG_CUSTOM_HOUR_COLOR: document.getElementById("customHourColor").value,' +
'    CONFIG_CUSTOM_SEC_STYLE: document.getElementById("customSecStyle").value,' +
'    CONFIG_CUSTOM_SEC_THICKNESS: document.getElementById("customSecThickness").value,' +
'    CONFIG_CUSTOM_SEC_INNER_THICKNESS: document.getElementById("customSecInnerThickness").value,' +
'    CONFIG_CUSTOM_SEC_INNER_ECC: document.getElementById("customSecInnerEcc").value,' +
'    CONFIG_CUSTOM_SEC_OUTER_ECC: document.getElementById("customSecOuterEcc").value,' +
'    CONFIG_CUSTOM_SEC_INNER_BORDER: document.getElementById("customSecInnerBorder").value,' +
'    CONFIG_CUSTOM_SEC_OUTER_BORDER: document.getElementById("customSecOuterBorder").value,' +
'    CONFIG_CUSTOM_SEC_TRANSLUCENT: document.getElementById("customSecTranslucent").value === "true",' +
'    CONFIG_CUSTOM_SEC_COLOR: document.getElementById("customSecColor").value,' +
'    CONFIG_MARKER_TEXT_TARGET: document.getElementById("markerTextTarget").value,' +
'    CONFIG_MARKER_TEXT_FONT: document.getElementById("markerTextFont").value,' +
'    CONFIG_MARKER_TEXT_OFFSET: document.getElementById("markerTextOffset").value,' +
'    CONFIG_MARKER_TEXT_HOUR_MASK: document.getElementById("markerTextHourMask").value,' +
'    CONFIG_MARKER_TEXT_SEC_MASK: document.getElementById("markerTextSecMask").value,' +
'    CONFIG_MARKER_TEXT_ROMAN: document.getElementById("markerTextRoman").checked,' +
'    CONFIG_HAND_HOUR_STYLE: document.getElementById("handHourStyle").value,' +
'    CONFIG_HAND_HOUR_WIDTH: document.getElementById("handHourWidth").value,' +
'    CONFIG_HAND_HOUR_LENGTH: document.getElementById("handHourLength").value,' +
'    CONFIG_HAND_HOUR_BACK_OFFSET: document.getElementById("handHourBackOffset").value,' +
'    CONFIG_HAND_HOUR_MIDDLE_OFFSET: document.getElementById("handHourMiddleOffset").value,' +
'    CONFIG_HAND_HOUR_SECONDARY_WIDTH: document.getElementById("handHourSecondaryWidth").value,' +
'    CONFIG_HAND_HOUR_COLOR: document.getElementById("handHourColor").value,' +
'    CONFIG_HAND_HOUR_OUTLINE_ENABLED: document.getElementById("handHourOutlineEnabled").value,' +
'    CONFIG_HAND_HOUR_OUTLINE_COLOR: document.getElementById("handHourOutlineColor").value,' +
'    CONFIG_HAND_HOUR_TRANSLUCENT: document.getElementById("handHourTranslucent").value === "true",' +
'    CONFIG_HAND_HOUR_SHADOW_ENABLED: document.getElementById("handHourShadowEnabled").value === "true",' +
'    CONFIG_HAND_HOUR_SHADOW_DISTANCE: document.getElementById("handHourShadowDistance").value,' +
'    CONFIG_HAND_HOUR_HOLLOW: document.getElementById("handHourHollow").value === "true",' +
'    CONFIG_HAND_HOUR_HOLLOW_THICKNESS: document.getElementById("handHourHollowThickness").value,' +
'    CONFIG_HAND_MIN_STYLE: document.getElementById("handMinStyle").value,' +
'    CONFIG_HAND_MIN_WIDTH: document.getElementById("handMinWidth").value,' +
'    CONFIG_HAND_MIN_LENGTH: document.getElementById("handMinLength").value,' +
'    CONFIG_HAND_MIN_BACK_OFFSET: document.getElementById("handMinBackOffset").value,' +
'    CONFIG_HAND_MIN_MIDDLE_OFFSET: document.getElementById("handMinMiddleOffset").value,' +
'    CONFIG_HAND_MIN_SECONDARY_WIDTH: document.getElementById("handMinSecondaryWidth").value,' +
'    CONFIG_HAND_MIN_COLOR: document.getElementById("handMinColor").value,' +
'    CONFIG_HAND_MIN_OUTLINE_ENABLED: document.getElementById("handMinOutlineEnabled").value,' +
'    CONFIG_HAND_MIN_OUTLINE_COLOR: document.getElementById("handMinOutlineColor").value,' +
'    CONFIG_HAND_MIN_TRANSLUCENT: document.getElementById("handMinTranslucent").value === "true",' +
'    CONFIG_HAND_MIN_SHADOW_ENABLED: document.getElementById("handMinShadowEnabled").value === "true",' +
'    CONFIG_HAND_MIN_SHADOW_DISTANCE: document.getElementById("handMinShadowDistance").value,' +
'    CONFIG_HAND_MIN_HOLLOW: document.getElementById("handMinHollow").value === "true",' +
'    CONFIG_HAND_MIN_HOLLOW_THICKNESS: document.getElementById("handMinHollowThickness").value,' +
'    CONFIG_HAND_SEC_STYLE: document.getElementById("handSecStyle").value,' +
'    CONFIG_HAND_SEC_WIDTH: document.getElementById("handSecWidth").value,' +
'    CONFIG_HAND_SEC_LENGTH: document.getElementById("handSecLength").value,' +
'    CONFIG_HAND_SEC_BACK_OFFSET: document.getElementById("handSecBackOffset").value,' +
'    CONFIG_HAND_SEC_MIDDLE_OFFSET: document.getElementById("handSecMiddleOffset").value,' +
'    CONFIG_HAND_SEC_SECONDARY_WIDTH: document.getElementById("handSecSecondaryWidth").value,' +
'    CONFIG_HAND_SEC_COLOR: document.getElementById("handSecColor").value,' +
'    CONFIG_HAND_SEC_OUTLINE_ENABLED: document.getElementById("handSecOutlineEnabled").value,' +
'    CONFIG_HAND_SEC_OUTLINE_COLOR: document.getElementById("handSecOutlineColor").value,' +
'    CONFIG_HAND_SEC_TRANSLUCENT: document.getElementById("handSecTranslucent").value === "true",' +
'    CONFIG_HAND_SEC_SHADOW_ENABLED: document.getElementById("handSecShadowEnabled").value === "true",' +
'    CONFIG_HAND_SEC_SHADOW_DISTANCE: document.getElementById("handSecShadowDistance").value,' +
'    CONFIG_HAND_SEC_HOLLOW: document.getElementById("handSecHollow").value === "true",' +
'    CONFIG_HAND_SEC_HOLLOW_THICKNESS: document.getElementById("handSecHollowThickness").value,' +
'    CONFIG_CENTER_CIRCLE_RADIUS: document.getElementById("centerCircleRadius").value,' +
'    CONFIG_CENTER_CIRCLE_COLOR: document.getElementById("centerCircleColor").value,' +
'    CONFIG_DEBUG_OVERRIDE_DATA: document.getElementById("debugData").value,' +
'    CONFIG_PRESET_1_NAME: document.getElementById("presetSlot1Name").value,' +
'    CONFIG_PRESET_1_JSON: document.getElementById("presetSlot1Json").value,' +
'    CONFIG_PRESET_2_NAME: document.getElementById("presetSlot2Name").value,' +
'    CONFIG_PRESET_2_JSON: document.getElementById("presetSlot2Json").value,' +
'    CONFIG_PRESET_3_NAME: document.getElementById("presetSlot3Name").value,' +
'    CONFIG_PRESET_3_JSON: document.getElementById("presetSlot3Json").value,' +
'    CONFIG_PRESET_4_NAME: document.getElementById("presetSlot4Name").value,' +
'    CONFIG_PRESET_4_JSON: document.getElementById("presetSlot4Json").value,' +
'    CONFIG_PRESET_5_NAME: document.getElementById("presetSlot5Name").value,' +
'    CONFIG_PRESET_5_JSON: document.getElementById("presetSlot5Json").value,' +
'    CONFIG_PRESET_6_NAME: document.getElementById("presetSlot6Name").value,' +
'    CONFIG_PRESET_6_JSON: document.getElementById("presetSlot6Json").value,' +
'    CONFIG_DRAW_DEBUG: document.getElementById("drawDebug").checked,' +
'    CONFIG_HOURLY_VIBE_MODE: document.getElementById("hourlyVibeMode").value,' +
'    CONFIG_HOURLY_VIBE_INTERVAL_MIN: document.getElementById("hourlyVibeIntervalMin").value,' +
'    CONFIG_HOURLY_VIBE_PATTERN: document.getElementById("hourlyVibePattern").value,' +
'    CONFIG_HOURLY_VIBE_START_TIME: document.getElementById("hourlyVibeStartTime").value,' +
'    CONFIG_HOURLY_VIBE_END_TIME: document.getElementById("hourlyVibeEndTime").value,' +
'    CONFIG_HOURLY_VIBE_DAYS_MASK: document.getElementById("hourlyVibeDaysMask").value,' +
'    CONFIG_HOURLY_VIBE_OVERRIDE_QUIET: (function () { var el = document.getElementById("hourlyVibeOverrideQuiet"); return el ? el.checked : true; })()' +
'  };' +
// Transient, one-shot -- read once by index.js's webviewclosed
// handler to decide whether this save should force an immediate
// network refetch ("Force refresh now") or just apply cosmetic
// settings and let the normal refresh cadence pick up anything that
// actually needs new data -- never itself persisted via setSetting.
'  settings.CONFIG_FORCE_REFRESH = !!forceRefresh;' +
'  var returnTo = getQueryParam("return_to", "pebblejs://close#");' +
'  document.location = returnTo + encodeURIComponent(JSON.stringify(settings));' +
'}' +
'function getQueryParam(name, defaultValue) {' +
'  var query = location.search.substring(1);' +
'  var vars = query.split("&");' +
'  for (var i = 0; i < vars.length; i++) {' +
'    var pair = vars[i].split("=");' +
'    if (pair[0] === name) return decodeURIComponent(pair[1] || "");' +
'  }' +
'  return defaultValue;' +
'}' +

// ---- section sub-headers -------------------------------------------
// One compute*Subheader() function per collapsible section, each
// returning the one-line summary text (or, for Colors, an HTML
// fragment with its own 3 color dots) shown under that section's
// title while it's collapsed -- see sectionLegendHtml() near the top
// of this file for the markup itself and toggleSection() above for
// why it disappears on expansion. setSubheaderText()/Html() below
// just push a function's return value into that section's own
// #subhead-<id> span; refreshAllSectionSubheaders() calls the whole
// set together. None of these mutate anything -- purely read the same
// DOM fields every other part of this page already treats as the
// source of truth (hidden inputs, checkboxes, selects), so a
// sub-header can never show something Save wouldn\'t actually send.
'function applySubheadSlide(outerEl) {' +
'  var inner = outerEl.firstElementChild;' +
'  if (!inner) return;' +
'  inner.classList.remove("subhead-sliding");' +
'  inner.style.removeProperty("--slide-dist");' +
'  var overflow = inner.scrollWidth - outerEl.clientWidth;' +
'  if (overflow > 2) {' +
'    inner.style.setProperty("--slide-dist", (-overflow - 6) + "px");' +
'    inner.classList.add("subhead-sliding");' +
'  }' +
'}' +
// elId-based core, shared by setSubheaderText()/setSubheaderHtml()
// (section sub-headers, id="subhead-<x>") and setSubsubheaderText()
// (Hands style/Indices style\'s own nested sub-headers, id="subsubhead-<x>")
// below -- same element shape/behavior either way, just a different
// id prefix depending which one\'s calling in.
'function setSubheaderTextById(elId, text) {' +
'  var el = document.getElementById(elId);' +
'  if (!el) return;' +
'  var inner = el.firstElementChild;' +
// Unchanged from last time -- leave the DOM (and any already-running
// slide animation) alone. refreshAllSectionSubheaders() recomputes
// every section's text once a second regardless of whether anything
// actually changed; without this check, applySubheadSlide() below
// would strip and re-add .subhead-sliding on every single call, which
// restarts a CSS animation from its 0% keyframe the same as recreating
// the element would -- with a 6s animation and a 1s refresh interval,
// that reset was firing 6x faster than the animation could ever
// progress, so it only ever showed the first ~1px of its own slide
// before jumping back to the start again.
'  if (inner && inner.textContent === text) return;' +
'  el.innerHTML = "";' +
'  inner = document.createElement("span");' +
'  inner.className = "subhead-sub-inner";' +
'  inner.textContent = text;' +
'  el.appendChild(inner);' +
'  applySubheadSlide(el);' +
'}' +
'function setSubheaderHtmlById(elId, html) {' +
'  var el = document.getElementById(elId);' +
'  if (!el) return;' +
'  var inner = el.firstElementChild;' +
'  if (inner && inner.innerHTML === html) return;' + // see setSubheaderTextById()'s own comment on why this guard exists
'  el.innerHTML = \'<span class="subhead-sub-inner">\' + html + "</span>";' +
'  applySubheadSlide(el);' +
'}' +
'function setSubheaderText(id, text) { setSubheaderTextById("subhead-" + id, text); }' +
'function setSubheaderHtml(id, html) { setSubheaderHtmlById("subhead-" + id, html); }' +
'function setSubsubheaderText(id, text) { setSubheaderTextById("subsubhead-" + id, text); }' +
'function computeStyleSubheader() {' +
'  var bottomStyleVal = document.getElementById("bottomStyleValue").value;' +
'  var clockPart;' +
'  if (bottomStyleVal === "analog") {' +
'    clockPart = "analog clock";' +
'  } else {' +
'    var fontId = parseInt(document.getElementById("clockFont").value, 10);' +
'    clockPart = fontLookupEntry(fontId).label + " digital clock";' +
'  }' +
'  var skyVal = document.getElementById("skyMode").value || "0";' +
'  var skyPart = skyVal === "1" ? "clear sky" : skyVal === "2" ? "space sky" : "weather sky";' +
'  var outlineVal = document.getElementById("outlineStyle").value || "1";' +
'  var outlinePart = outlineVal === "0" ? "no outline" : outlineVal === "2" ? "thick outline" : "thin outline";' +
'  return clockPart + " + " + skyPart + " + " + outlinePart;' +
'}' +
// The only sub-header rendered as HTML rather than plain text -- the
// 3 small color dots (see .subhead-dot) need real background-color
// styling, not just words. dayColors()/nightColors()/isNightNowApprox()
// are all existing helpers this page already uses for the color-role
// swatches and the "Active now" badge above.
'function computeColorsSubheaderHtml() {' +
'  var nightEnabled = document.getElementById("nightEnabled").checked;' +
'  var label, colors;' +
'  if (!nightEnabled) {' +
'    label = "one set: ";' +
'    colors = dayColors();' +
'  } else if (isNightNowApprox()) {' +
'    label = "night mode: ";' +
'    colors = nightColors();' +
'  } else {' +
'    label = "day mode: ";' +
'    colors = dayColors();' +
'  }' +
'  function dot(hex) { return \'<span class="subhead-dot" style="background:\' + hex + \';"></span>\'; }' +
'  return esc(label) + dot(colors.text) + dot(colors.accent) + dot(colors.bg);' +
'}' +
// Mirrors save()\'s own edgeVal()/avail logic exactly (same
// computeSlotAvailability() call, same per-slot usable checks) so the
// count here can never disagree with what actually reaches the watch.
'function countActiveFeatures() {' +
'  var avail = computeSlotAvailability();' +
'  var isAnalogNow = document.getElementById("bottomStyleValue").value === "analog";' +
'  function val(id) { return parseInt(document.getElementById(id).value, 10) || 0; }' +
'  function edgeActive(id, analogFlag, digitalFlag) {' +
'    var usable = isAnalogNow ? analogFlag : digitalFlag;' +
'    return usable && val(id) !== 0;' +
'  }' +
'  var count = 0;' +
'  if (!avail.cornersGrayed) {' +
'    ["cornerTL", "cornerTR", "cornerBL", "cornerBR"].forEach(function (id) { if (val(id) !== 0) count++; });' +
'  }' +
'  if (edgeActive("upperMiddleLine1Content", avail.upper, avail.digitalLeft)) count++;' +
'  if (edgeActive("upperMiddleLine2Content", avail.upper, avail.digitalRight)) count++;' +
'  if (edgeActive("bottomMiddleLine1Content", avail.bottom, true)) count++;' +
'  if (edgeActive("bottomMiddleLine2Content", avail.bottom, false)) count++;' +
'  if (edgeActive("middleLeftLine1Content", avail.left, avail.digitalLeft)) count++;' +
'  if (edgeActive("middleLeftLine2Content", avail.left, avail.digitalLeft)) count++;' +
'  if (edgeActive("middleRightLine1Content", avail.right, avail.digitalRight)) count++;' +
'  if (edgeActive("middleRightLine2Content", avail.right, avail.digitalRight)) count++;' +
'  return count;' +
'}' +
'function computeFeaturesSubheader() {' +
'  var count = countActiveFeatures();' +
'  if (count === 0) return "No features active";' +
'  var fontId = parseInt(document.getElementById("cornerFont").value, 10);' +
'  var fontName = fontLookupEntry(fontId).label;' +
'  return count + " feature" + (count === 1 ? "" : "s") + " active in " + fontName;' +
'}' +
// "Planets"/"second hand" here refer to the ON-SHAKE animations
// (Planet seek/Smooth second hand -- see the "On shake animation"
// picker\'s own options), not the separate "Animate background on
// start" picker, per the request\'s own example.
'function computeAnimationSubheader() {' +
'  var parts = [];' +
'  var startup = document.getElementById("startupClockAnimMode").value;' +
'  if (startup === "2") parts.push("planet sweep time shift on start");' +
'  else if (startup === "1") parts.push("animated clock on start");' +
'  var bg = document.getElementById("bgAnimMode").value;' +
'  if (bg === "1") parts.push("planets on start");' +
'  else if (bg === "2") parts.push("indices on start");' +
'  var shake = document.getElementById("shakeAnimMode").value;' +
'  var shakeParts = [];' +
'  if (shake === "2" || shake === "3") shakeParts.push("planets");' +
'  if (shake === "1" || shake === "3") shakeParts.push("second hand");' +
'  if (shakeParts.length) parts.push(shakeParts.join(" and ") + " on shake");' +
'  if (!parts.length) return "No animations active";' +
'  var s = parts.join(", ");' +
'  return s.charAt(0).toUpperCase() + s.slice(1);' +
'}' +
'function computePresetsSubheader() {' +
'  var used = 0;' +
'  for (var n = 1; n <= 6; n++) {' +
'    var el = document.getElementById("presetSlot" + n + "Json");' +
'    if (el && el.value) used++;' +
'  }' +
'  return used + " out of 6 presets used";' +
'}' +
'function computeWeatherSubheader() {' +
'  var tempUnit = document.getElementById("tempUnit").value;' +
'  var tempLabel = tempUnit === "F" ? "Fahrenheit" : tempUnit === "K" ? "Kelvin" : "Celsius";' +
'  var wind = document.getElementById("windSpeedUnit").value;' +
'  var windLabel = wind === "mph" ? "imperial" : wind === "kn" ? "nautical" : "metric";' +
'  var aqi = document.getElementById("aqiUnit").value;' +
'  var aqiLabel = aqi === "1" ? "European" : "US";' +
'  return tempLabel + " temp. units with " + windLabel + " and " + aqiLabel + " air quality";' +
'}' +
'function computeAstronomySubheader() {' +
'  var sizeVal = document.getElementById("sunMoonSize").value;' +
'  var sizeLabel = sizeVal === "100" ? "Large" : sizeVal === "50" ? "Small" : sizeVal === "25" ? "X-small" : "Medium";' +
'  var parts = [sizeLabel + " sun&moon"];' +
'  parts.push("ISS " + (document.getElementById("showIss").checked ? "on" : "off"));' +
'  parts.push("auroras " + (document.getElementById("auroraEnabled").checked ? "on" : "off"));' +
'  if (document.getElementById("vibrateOnPhaseChange").checked) parts.push("vibrate on eclipse phases");' +
'  return parts.join(", ");' +
'}' +
'function computeLocationSubheader() {' +
'  if (document.getElementById("autoLoc").checked) return "Use phone location";' +
'  var nameEl = document.getElementById("locationName");' +
'  var name = nameEl ? nameEl.value : "";' +
'  if (name) return "Use " + name;' +
'  var lat = document.getElementById("lat").value;' +
'  var lon = document.getElementById("lon").value;' +
'  if (!lat && !lon) return "Use phone location";' +
'  return "Use lat: " + lat + ", long: " + lon;' +
'}' +
'function computeOtherSubheader() {' +
'  var mode = document.getElementById("hourlyVibeMode").value;' +
'  if (mode === "0") return "Hourly vibrations off";' +
'  var when = mode === "1" ? "on full hours" : (document.getElementById("hourlyVibeIntervalMin").value + " min interval");' +
'  return "Hourly vibrations: " + when;' +
'}' +
'function computeUpdatesSubheader() {' +
'  var mins = document.getElementById("updateMins").value;' +
'  var saver = document.getElementById("batterySaverEnabled").checked;' +
'  return mins + " minute interval, battery preserver " + (saver ? "on" : "off");' +
'}' +
'function formatTestDateTime(val) {' +
'  if (!val) return null;' +
'  var parts = val.split("T");' +
'  if (parts.length < 2) return val;' +
'  var dateParts = parts[0].split("-");' +
'  if (dateParts.length !== 3) return val;' +
'  var months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];' +
'  var y = dateParts[0], m = parseInt(dateParts[1], 10) - 1, d = parseInt(dateParts[2], 10);' +
'  return d + " " + (months[m] || "") + " " + y + " " + parts[1];' +
'}' +
'function computeDebugSubheader() {' +
'  var testOn = document.getElementById("testMode").checked;' +
'  var overridePart = testOn ? ("Override: " + (formatTestDateTime(document.getElementById("testDateTime").value) || "not set")) : "Override: off";' +
'  var lastPart = "last: none yet";' +
'  try {' +
'    var log = JSON.parse(document.getElementById("rawMessageLogJson").value || "[]");' +
'    if (log.length) {' +
'      var last = log[log.length - 1];' +
'      var d = new Date(last.t);' +
'      function pad(n, len) { var s = String(n); while (s.length < (len || 2)) s = "0" + s; return s; }' +
'      lastPart = "last " + pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds()) + "." + pad(d.getMilliseconds(), 3);' +
'    }' +
'  } catch (e) {}' +
'  return overridePart + ", " + lastPart;' +
'}' +
// Called on init, after every click/change/input anywhere on the page
// (see the delegated listeners below -- cheaper and far less fragile
// than hunting down every individual onclick/onchange handler that
// could affect one of these), and once a second alongside the
// existing preview/scheme-highlight timer (for the Colors day/night
// split and the Debug section\'s own last-message clock, both of which
// can go stale purely from time passing, not from any click).
// Each section wrapped in its own try/catch so one section\'s bug
// (or a not-yet-rendered field, e.g. during the very first call before
// every element exists) can\'t blank out every other section\'s
// sub-header.
'function refreshAllSectionSubheaders() {' +
'  try { setSubheaderText("examples", EXAMPLE_STYLE_COUNT + " styles available"); } catch (e) {}' +
'  try { setSubheaderText("style", computeStyleSubheader()); } catch (e) {}' +
'  try { refreshHandsSubsectionPreview(); } catch (e) {}' +
'  try { refreshIndicesSubsectionPreview(); } catch (e) {}' +
'  try { setSubheaderHtml("colors", computeColorsSubheaderHtml()); } catch (e) {}' +
'  try { setSubheaderText("corners", computeFeaturesSubheader()); } catch (e) {}' +
'  try { setSubheaderText("animation", computeAnimationSubheader()); } catch (e) {}' +
'  try { setSubheaderText("presets", computePresetsSubheader()); } catch (e) {}' +
'  try { setSubheaderText("weather", computeWeatherSubheader()); } catch (e) {}' +
'  try { setSubheaderText("astronomy", computeAstronomySubheader()); } catch (e) {}' +
'  try { setSubheaderText("location", computeLocationSubheader()); } catch (e) {}' +
'  try { setSubheaderText("updates", computeUpdatesSubheader()); } catch (e) {}' +
'  try { setSubheaderText("other", computeOtherSubheader()); } catch (e) {}' +
'  try { setSubheaderText("testing", computeDebugSubheader()); } catch (e) {}' +
'}' +
// A single delegated hook per event type instead of threading
// refreshAllSectionSubheaders() into every existing onclick/onchange/
// oninput handler on the page (mode-btn taps, slider drags, modal
// confirms, preset apply, example-style apply, etc. -- there are
// dozens). Bubbling means each of these fires AFTER whatever inline
// handler the actual target element already has, so the DOM is
// already up to date by the time this runs.
'document.addEventListener("click", refreshAllSectionSubheaders);' +
'document.addEventListener("change", refreshAllSectionSubheaders);' +
'document.addEventListener("input", refreshAllSectionSubheaders);' +

'updateColorRoleButtons("day");' +
'updateColorRoleButtons("night");' +
'onBottomStyleChange();' +
'onMarkerStyleChange();' +
'updateHourlyVibeVisibility();' +
'updateHandStyleButtonLabel();' +
'refreshAllFontTriggerLabels();' +
'refreshEditButtonLabels();' +
'renderHandStyleGrid();' +
'renderMarkerStyleGrid();' +
'renderCategoryButtons();' +
'updateWeatherIconStyleVisibility();' +
'adjustTopBarSpacing();' +
'if (document.fonts && document.fonts.ready) { document.fonts.ready.then(updatePreview); }' +
'updateSchemeActiveHighlight();' +
'refreshAllSectionSubheaders();' +
// One-shot only -- if manual location is already on and coordinates
// are present but nothing's cached yet (e.g. an existing install
// updating into this feature for the first time, or lat/lon set some
// other way than the Search box/blur handler above), resolve a name
// once now rather than leaving the Location sub-header stuck showing
// raw coordinates forever.
'if (!document.getElementById("autoLoc").checked && document.getElementById("lat").value && document.getElementById("lon").value && !document.getElementById("locationName").value) {' +
'  reverseGeocodeAndCacheLocationName();' +
'}' +
'setInterval(function () { updatePreview(); updateSchemeActiveHighlight(); refreshAllSectionSubheaders(); }, 1000);' +
'</script>' +
'</body></html>';
}

module.exports = { buildConfigHtml: buildConfigHtml };
