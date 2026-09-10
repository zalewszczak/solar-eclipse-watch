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
var buildPageCss = require('./config/page-css');
var buildRuntime = require('./config/runtime');
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
function verticalButtonGroupHtml(groupId, hiddenId, options, currentValue) {
  var validValue = options.some(function (opt) { return String(opt.value) === String(currentValue); })
    ? currentValue : options[0].value;
  var buttons = options.map(function (opt) {
    var active = String(validValue) === String(opt.value);
    return '<button type="button" class="mode-btn-vertical' + (active ? ' active' : '') + '" data-value="' + esc(opt.value) + '" onclick="selectVerticalOption(\'' + groupId + '\', \'' + hiddenId + '\', \'' + esc(opt.value) + '\')">' + esc(opt.label) + '</button>';
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
    customHourStyle: '0', customHourThickness: '3',
    customHourInnerEcc: '0', customHourOuterEcc: '0', customHourInnerBorder: '20', customHourOuterBorder: '100',
    customHourTranslucent: 'false', customHourColor: '0',
    customSecStyle: '0', customSecThickness: '1',
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
  return (
'<div class="modal-overlay" id="customMarkerModal-' + kind + '" onclick="if (event.target === this) closeCustomMarkerEditor(\'' + kind + '\');">' +
'  <div class="modal-box">' +
'    <div class="modal-title">' + esc(title) + '</div>' +
'    <div class="modal-scroll-body">' +

'    <label>Shape</label>' +
      modeButtonGroupHtml(p + 'StyleGroup', p + 'Style', [
        { value: '0', label: 'Dot' },
        { value: '1', label: 'Line' },
        { value: '2', label: 'Square' }
      ], '0') +

'    <div class="slider-row">' +
'      <label for="' + p + 'Thickness">Thickness <span class="val" id="' + p + 'ThicknessVal"></span></label>' +
'      <div class="slider-with-buttons">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', -1)">&minus;</button>' +
'      <input type="range" id="' + p + 'Thickness" min="1" max="' + thicknessMax + '" step="1" oninput="onCustomMarkerSliderInput(\'' + kind + '\')">' +
'      <button type="button" class="slider-step-btn" onclick="stepSlider(\'' + p + 'Thickness\', 1)">+</button>' +
'      </div>' +
'    </div>' +
'    <div class="help">Each mark is drawn directly between its inner and outer border points below -- no separate length setting.</div>' +

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
 *     customHourStyle/customSecStyle: '0'-'2' (dot/line/square), customHourThickness (1-20)/
 *     customSecThickness (1-10), customHourInnerEcc/customHourOuterEcc/customSecInnerEcc/
 *     customSecOuterEcc: '0'-'100', customHourInnerBorder/customHourOuterBorder/
 *     customSecInnerBorder/customSecOuterBorder: '0'-'100' (% reach, see marker_reach_px()
 *     in marker_layer.c -- each mark spans directly between its inner/outer border points),
 *     markerTextTarget: '0'(off)|'1'(hour)|'2'(second), markerTextFont: '0'-'35', markerTextOffset: '-50'-'50',
 *     markerTextHourMask/markerTextSecMask: 0-4095 (12-bit),
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
buildPageCss() +
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

'    <div class="checkbox-row subsection">' +
'      <input type="checkbox" id="showSeconds" ' + secondsChecked + ' ' + secondsDisabled + ' onchange="onShowSecondsChange()">' +
'      <label for="showSeconds" style="margin:0;">Show seconds</label>' +
'    </div>' +
'    <div class="help" id="secondsHelp" style="' + (secondsUnsupported ? '' : 'display:none;') + '">This font doesn\'t support showing seconds.</div>' +
'    <div class="help">Used by both layouts -- the digital clock\'s own seconds digits, and whether analog draws a second hand at all (gates the Custom style\'s "Edit second hand" below, too).</div>' +

'    <div id="digitalOnlySettings" class="subsection" style="' + (bottomStyleVal === 'digital' ? '' : 'display:none;') + '">' +
'      <label for="clockFont">Clock font</label>' +
'      <select id="clockFont" onchange="onFontChange()" style="display:none;">' + fontOptions + '</select>' +
'      <button type="button" class="font-picker-btn font-picker-trigger" id="clockFontTrigger" onclick="openFontPicker(\'clock\')">' +
'        <span class="font-picker-preview" id="clockFontTriggerPreview"></span>' +
'        <span class="font-picker-name" id="clockFontTriggerName"></span>' +
'      </button>' +
'    </div>' +

'    <div id="bigAnalogSettings" class="subsection" style="' + (isAnalog ? '' : 'display:none;') + '">' +
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

'      <label style="margin-top:12px;">Hour/seconds indices style</label>' +
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

'    <label>Label style</label>' +
      modeButtonGroupHtml('labelStyleGroup', 'labelStyle', [
        { value: '0', label: 'BOXED', icon: MODE_BTN_ICONS.labelBoxed },
        { value: '1', label: 'OUTLINED', icon: MODE_BTN_ICONS.labelOutlined },
        { value: '2', label: 'SOFT', icon: MODE_BTN_ICONS.labelSoft }
      ], current.labelStyle || '0') +
'    <div class="help">Boxed is an opaque rounded box with white text (the original look). Outlined uses your main color with a contrasting outline. Soft is plain light-gray text with no background or outline.</div>' +

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
buildRuntime() +
'</script>' +
'</body></html>';
}

module.exports = { buildConfigHtml: buildConfigHtml };
