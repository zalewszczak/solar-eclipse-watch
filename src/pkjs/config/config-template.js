// ---- generic settings-page template helpers ------------------------------
//
// Moved out of config-page.js (configuration architecture extraction,
// JS8 first slice). These are the reusable, generic HTML-building
// blocks every domain-specific picker (fonts, hands, markers, ...)
// composes on top of -- escaping, the 3 collapsible-section icon/
// legend helpers, and the 3 button-group renderers (vertical stack,
// horizontal mode row, and the Main/Accent/Background color-role row
// built on top of that). None of these know about fonts, hands,
// markers, or any other specific domain; they only take already-built
// option lists and labels.
//
// This is a first, deliberately narrow slice of config-page.js's
// eventual split (see the refactoring plan's JS8 step) -- the giant
// buildConfigHtml() function itself (the actual page template plus
// its embedded webview <script>) is untouched here; that is a much
// larger, separate piece of work.

var presetsLookups = require('../presets-lookups');
var CORNER_CATEGORIES = presetsLookups.CORNER_CATEGORIES;

var DUAL_CONTEXT_FIELD_LIST = [
  'upperMiddleLine1', 'upperMiddleLine2', 'bottomMiddleLine1',
  'middleLeftLine1', 'middleLeftLine2', 'middleRightLine1', 'middleRightLine2'
];

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
  // Same sky dots as DIGITAL above, just given the room its own bottom
  // info band would otherwise occupy (shifted down/spread out into
  // that freed space, not reused verbatim, so they don't crowd the two
  // line-shapes below), plus two solid currentColor bars near the TOP
  // standing in for the clock/date text -- no enclosing bar shape and
  // no mask-punched cutout the way DIGITAL's own info band needs
  // (there's no separate-colored panel here to punch a hole through --
  // DIGITAL TOP's whole point is a transparent panel over the sky, so
  // the icon draws the two lines directly, solid, exactly like every
  // other plain shape in these icons already does).
  digitalTop:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<rect x="38" y="16" width="124" height="22" rx="10" fill="currentColor" stroke="none"/>' +
    '<rect x="58" y="46" width="84" height="10" rx="5" fill="currentColor" stroke="none"/>' +
    '<circle cx="40" cy="100" r="5" fill="currentColor" stroke="none"/>' +
    '<circle cx="150" cy="95" r="5" fill="currentColor" stroke="none"/>' +
    '<circle cx="100" cy="120" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="60" cy="160" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="140" cy="170" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="95" cy="195" r="5" fill="currentColor" stroke="none"/>' +
    '</svg>',
  // No separate info band at all -- one big analog clock filling
  // nearly the whole screen, plus a couple of stray sky dots, since
  // ANALOG replaces the digital/edge-content area entirely rather
  // than sharing the screen with it the way DIGITAL (bar or top) does.
  analog:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="100" cy="114" r="88" stroke-width="7"/>' +
    '<line x1="100" y1="114" x2="100" y2="55" stroke-width="9"/>' +
    '<line x1="100" y1="114" x2="138" y2="140" stroke-width="9"/>' +
    '<circle cx="60" cy="60" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="150" cy="170" r="4" fill="currentColor" stroke="none"/>' +
    '</svg>',
  // 4 tall solid bars standing in for "12:34" (a colon's 2 dots between
  // the middle pair) over the same sky-dot background ANALOG's icon
  // uses -- Big Digital has a full-screen sky behind its digits too.
  bigDigital:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<circle cx="34" cy="34" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="166" cy="30" r="5" fill="currentColor" stroke="none"/>' +
    '<circle cx="166" cy="198" r="4" fill="currentColor" stroke="none"/>' +
    '<circle cx="34" cy="194" r="5" fill="currentColor" stroke="none"/>' +
    '<rect x="38" y="44" width="22" height="140" rx="8" fill="currentColor" stroke="none"/>' +
    '<rect x="68" y="44" width="22" height="140" rx="8" fill="currentColor" stroke="none"/>' +
    '<circle cx="100" cy="91" r="7" fill="currentColor" stroke="none"/>' +
    '<circle cx="100" cy="137" r="7" fill="currentColor" stroke="none"/>' +
    '<rect x="110" y="44" width="22" height="140" rx="8" fill="currentColor" stroke="none"/>' +
    '<rect x="140" y="44" width="22" height="140" rx="8" fill="currentColor" stroke="none"/>' +
    '</svg>',
  // 4x4 grid of small filled rounded squares, evenly spaced, over the
  // same clear/transparent interior BIG DIGITAL's icon uses for its
  // bars -- GRID's own screen is a literal 4x4 grid of characters, so
  // this is closer to a direct depiction than an abstraction.
  grid:
    '<svg viewBox="0 0 200 228" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round" stroke-linejoin="round">' +
    '<rect x="4" y="4" width="192" height="220" rx="20"/>' +
    '<rect x="34" y="34" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="85" y="34" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="136" y="34" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="34" y="85" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="85" y="85" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="136" y="85" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="34" y="136" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="85" y="136" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
    '<rect x="136" y="136" width="30" height="30" rx="7" fill="currentColor" stroke="none"/>' +
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
  faq:       { color: '#5856d6', icon:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M9.6 9a2.5 2.5 0 1 1 4.1 1.9c-.9.7-1.7 1.2-1.7 2.6"/><circle cx="12" cy="16.7" r=".8" fill="currentColor" stroke="none"/></svg>' },
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

module.exports = {
  DUAL_CONTEXT_FIELD_LIST: DUAL_CONTEXT_FIELD_LIST,
  esc: esc,
  MODE_BTN_ICONS: MODE_BTN_ICONS,
  sectionCategoryIcon: sectionCategoryIcon,
  SECTION_META: SECTION_META,
  sectionLegendHtml: sectionLegendHtml,
  subsectionLegendHtml: subsectionLegendHtml,
  verticalButtonGroupHtml: verticalButtonGroupHtml,
  modeButtonGroupHtml: modeButtonGroupHtml,
  colorRoleButtonGroupHtml: colorRoleButtonGroupHtml
};
