/**
 * presets-lookups.js -- every hand-curated, largely-static lookup table
 * config-page.js needs: font metadata, color scheme presets, the
 * corner/edge feature content catalogue (grouped into categories for
 * the picker, complete with each item's own live-preview text),
 * and the starter hand-style presets. Pulled out into its own plain
 * JS module -- real object/array literals, no string-concatenation
 * escaping -- specifically so these are easy to read and hand-edit
 * (add a font, retune a color preset, add a new feature id) without
 * hunting through buildConfigHtml()'s own giant HTML-template string.
 *
 * CORNER_CATEGORIES is the single source of truth for every corner/
 * edge feature content id: config-page.js derives both the flat,
 * id-ordered options list the hidden per-slot <select>s use (see
 * cornerContentOptionsHtml()) AND the categorized picker's own
 * grouped view from this one array, and pulls each item's live-
 * preview text from the same place its label lives. Previously these
 * were 3 independently hand-maintained lists (a flat id+label array,
 * a separate category-grouped id+label copy, and a separate id-
 * >preview-text map) that could -- and did -- drift apart: id 104
 * ("Current UV index") had a category entry and a real on-watch
 * renderer but no preview text, so it silently failed to draw in the
 * live preview/slot buttons until re-picked; a few other ids had
 * subtly different wording (even a plain typo, "Tinute") between the
 * flat and categorized copies. One array per id now makes that whole
 * class of bug structurally impossible: there's nowhere left for a
 * new id to be added to one list and forgotten in another.
 *
 * Every content id features_layer.c actually implements (0-115,
 * minus the retired id 33) must appear in exactly one category's
 * items list, with an id matching its case in draw_corner_item()'s
 * switch exactly.
 */

// One canonical font table, id-for-id identical to font_lookup.c's
// FONT_TABLE on the watch -- every font this app uses anywhere,
// custom-resource or system, all four font pickers (main clock,
// clock's small companion readout, marker text, corner/edge content)
// draw their <option>s from this SAME list now, so picking (for
// example) "Bebas" in any of them always means the same id, the same
// resource, everywhere -- see font_lookup.c's own top comment for the
// three separate, uncoordinated numbering schemes this replaced.
//
// `mainClock: true` marks the subset offered in the Clock font picker
// specifically (the "big", ~48px-scale fonts a full clock display
// actually reads well in) -- the smaller companion variants (Digital
// Dream Small, Minecrafter Small, etc.) exist for marker text/corner
// content, not as a serious main-clock choice, so they're left out of
// that one dropdown. Every id is still selectable in the other two
// pickers regardless.
// `small: true` marks a font compact enough to read well at the
// small sizes the corner/edge Font picker uses it at (height <= 30px)
// -- that picker hides `small: false` fonts by default (its own "Show
// incompatible fonts" checkbox reveals them), since most of the
// ~48px-scale mainClock fonts read poorly or clip at that small
// size. Doesn't affect the marker text font picker (all fonts always
// shown there) or the main Clock font picker (mainClock already
// filters that one to just the big fonts).
// `sidesWithSeconds` is the single source of truth for whether "Show
// seconds" can be offered at all, and if so, how much side-feature
// company it can keep -- ONLY ever appears on entries that also have
// `mainClock: true` (exactly like `sidesAllowed`; Show Seconds is a
// main-clock-only setting). Four values: -1 means seconds can never
// be shown with this font, full stop, regardless of digital side
// columns -- used by the same width-constrained fonts `sidesAllowed:
// 0` already flags: there's no room for a side column OR a running
// seconds counter next to the minutes, so unlike every value below,
// -1 doesn't even grant the "no sides active" case. 0 means seconds
// is available with NO side column active but drops out the instant
// either one turns on (left, right, or both all block it alike) --
// this is every ordinary baseline-eligible mainClock font's default,
// and matches this app's original (pre-sidesWithSeconds) behavior for
// them exactly: side features and Show Seconds have simply always
// been mutually exclusive for most fonts. 1 means seconds can coexist
// with at most ONE active side column (left or right, not both) --
// previously tracked separately in the now-retired
// SECONDS_WITH_ONE_SIDE_FONTS table (5 fonts: Leco Small/Medium/Large,
// Bitham Light, Bebas). 2 means seconds stays available no matter
// what -- even with both side columns on at once. That's a genuinely
// new tier with no prior equivalent (the old table only ever granted
// "works with exactly one side"; every font, no exceptions, lost
// seconds outright the moment BOTH columns were on).
// No font below claims 2 yet -- it's new headroom for a font
// specifically verified to have room for the clock, both side
// columns, AND a seconds counter all at once, not a default anyone
// should reach for without having actually seen it on a watch.
// Defaults to 0 wherever a lookup finds no explicit value (missing
// field, or a stale/hand-edited font id) -- matching the tier nearly
// every real mainClock font actually carries.
// `sizePx` is the REAL on-watch bake size -- copied straight from each
// custom font's own package.json resource name (the trailing _NN is
// the actual point size Pebble's font tool renders that .ttf/.otf at,
// not just a naming convention -- see package.json's "media" list).
// System fonts (ids 0-15) aren't custom resources at all (no
// package.json entry -- they're built into the firmware, referenced
// via FONT_KEY_* constants), so there's nothing to copy for those;
// `sizePx` falls back to `height`'s own already-approximate estimate.
//
// `google`/`weight`/`italic` identify the actual Google Font used for
// config-page.js's own live font-picker previews (see its
// googleFontsHref() and the font-picker popup) -- verified against
// fonts.google.com, not guessed. `cdn` is the same idea for a font
// hosted on cdnfonts.com instead (its own value is that site's own
// URL slug, e.g. cdnfonts.com/rebelredux.font -> cdn: 'rebelredux' --
// see cdnFontLinks() in config-page.js for how these get loaded
// alongside the one combined Google Fonts request). `approx: true`
// marks a substitute rather than the real face: the on-watch font
// itself isn't (and, as far as could be verified, has never been)
// published on Google Fonts or cdnfonts under its own name, so the
// picker shows the closest visual analogue instead of the exact
// glyphs -- each one says why in its own comment. Every non-approx
// entry below (whether `google` or `cdn`) is the literal same family
// as what's baked into the watch resource.
//
// `categories` drives the font picker's category filter row (see
// FONT_CATEGORIES just below, and buildFontCategoryFilterUi()/
// fontMatchesCategoryFilters() in config-page.js) -- any combination
// of FONT_CATEGORIES ids other than 'all', hand-curated per font by
// look/purpose. 'all' itself is never listed here -- it's a special
// picker-only selection meaning "ignore every category filter",
// handled entirely in config-page.js.
// `sidesAllowed` replaces the old plain `wide: true` flag, and ONLY
// ever appears on entries that also have `mainClock: true` (it gates
// the Style section's digital-mode "Side features" row, which only
// that picker's fonts feed into): 0 means this font is too wide for
// EITHER side column (side features hidden entirely for it, exactly
// like the old `wide: true` did); 1 means it
// can fit exactly ONE side column but not both at once (picking one
// unpicks the other, with a warning tooltip explaining why); 2 means
// any combination (including both at once) fits fine, the old
// plain "not wide" behavior. The 7 fonts that carried `wide: true`
// before are 0 here unchanged; every other mainClock font defaults to
// 2 (no prior behavior change) except a handful of borderline-wide
// display faces called out as 1 below -- those are a best-effort
// design-time judgment call (no real device to measure actual glyph
// widths against), so treat them as a starting point to retune once
// they've actually been seen on a watch.
// `resourceName`/`fileName` mirror FontLookupEntry.resource_name/file_name
// in font_lookup.c exactly (id-for-id, same source data -- see that
// struct's own comment): `resourceName` is the FONT_KEY_* constant name
// for a system font or the package.json media "name" for a custom one;
// `fileName` is that custom font's package.json media "file" (e.g.
// "fonts/DIGITALDREAM.ttf"), null for system fonts (no project-local
// file, built into firmware). Nothing in config-page.js reads these yet
// -- they're here so the phone-side table has the same per-font
// provenance the watch app's own table does, ready for whatever needs
// it next (a debug/about view, a font-file cross-check, etc.) without
// having to add a third lookup later.
var FONT_LOOKUP = [
  { id: 0 , label: 'Gothic X-Small'        , height: 14, preview: 'font-family: Arial, sans-serif;'                                                                         , small: true , google: null                   , sizePx: 14, approx: true, categories: ['pebbleos', 'modern', 'tiny']                              , sidesAllowed: null, sidesWithSeconds: null , romanOk: true , resourceName: "FONT_KEY_GOTHIC_14", fileName: null },                                // Pebble's built-in "Gothic" system font -- no Google Fonts equivalent by name; Arial/Helvetica is the closest common grotesque.
  { id: 1 , label: 'Gothic Small'          , height: 18, preview: 'font-family: Arial, sans-serif; font-weight: 700;'                                                       , small: true , google: null                   , sizePx: 14, approx: true, categories: ['pebbleos', 'modern', 'bold', 'small']                     , sidesAllowed: null, sidesWithSeconds: null , romanOk: true , resourceName: "FONT_KEY_GOTHIC_14_BOLD", fileName: null },                                // see id 0
  { id: 2 , label: 'Gothic Medium'         , height: 24, preview: 'font-family: Arial, sans-serif; font-weight: 700;'                                                       , small: true , google: null                   , sizePx: 17, approx: true, categories: ['pebbleos', 'modern', 'bold', 'medium']                    , sidesAllowed: null, sidesWithSeconds: null , romanOk: true , resourceName: "FONT_KEY_GOTHIC_18_BOLD", fileName: null },                                // see id 0
  { id: 3 , label: 'Gothic Large'          , height: 32, preview: 'font-family: Arial, sans-serif; font-weight: 700;'                                                       , small: true , google: null                   , sizePx: 24, approx: true, categories: ['pebbleos', 'modern', 'bold', 'large']                     , sidesAllowed: null, sidesWithSeconds: null , romanOk: true , resourceName: "FONT_KEY_GOTHIC_24_BOLD", fileName: null },                                // see id 0
  { id: 4 , label: 'Gothic X-Large'        , height: 36, preview: 'font-family: Arial, sans-serif; font-weight: 700;'                                                       , small: true , google: null                   , sizePx: 28, approx: true, categories: ['pebbleos', 'modern', 'bold', 'large']                     , sidesAllowed: null, sidesWithSeconds: null , romanOk: true , resourceName: "FONT_KEY_GOTHIC_28_BOLD", fileName: null },                                // see id 0
  { id: 5 , label: 'Leco Small'            , height: 17, preview: 'font-family: Arial, sans-serif; font-weight: 300;'                                                       , small: false, google: null                   , sizePx: 28, approx: true, categories: ['pebbleos', 'digital', 'thin', 'small']                    , sidesAllowed: '2' , sidesWithSeconds: '2' , mainClock: true  , romanOk: false , resourceName: "FONT_KEY_LECO_28_LIGHT_NUMBERS", fileName: null },              // Pebble's built-in rounded numerals font -- no Google Fonts equivalent; no substitute attempted beyond a plain sans, since Leco's own rounded-digit character is hard to approximate with a generic family.
  { id: 6 , label: 'Leco Medium'           , height: 20, preview: 'font-family: Arial, sans-serif; font-weight: 700;'                                                       , small: false, google: null                   , sizePx: 32, approx: true, categories: ['pebbleos', 'digital', 'bold', 'small']                    , sidesAllowed: 2   , sidesWithSeconds: '2' , mainClock: true  , romanOk: false , resourceName: "FONT_KEY_LECO_32_BOLD_NUMBERS", fileName: null },              // see id 5
  { id: 7 , label: 'Leco Large'            , height: 23, preview: 'font-family: Arial, sans-serif; font-weight: 700;'                                                       , small: false, google: null                   , sizePx: 36, approx: true, categories: ['pebbleos', 'digital', 'bold', 'medium']                   , sidesAllowed: 2   , sidesWithSeconds: '1' , mainClock: true  , romanOk: false , resourceName: "FONT_KEY_LECO_36_BOLD_NUMBERS", fileName: null },              // see id 5
  { id: 8 , label: 'Leco XL'               , height: 26, preview: 'font-family: Arial, sans-serif; font-weight: 700;'                                                       , small: false, google: null                   , sizePx: 42, approx: true, categories: ['pebbleos', 'digital', 'bold', 'medium']                   , sidesAllowed: 2   , sidesWithSeconds: '0' , mainClock: true  , romanOk: false , resourceName: "FONT_KEY_LECO_42_NUMBERS", fileName: null },              // see id 5
  { id: 9 , label: 'Droid Serif'           , height: 17, preview: 'font-family: \'Droid Serif\', Georgia, serif; font-weight: 700;'                                         , small: true , google: 'Droid Serif'          , sizePx: 28, categories: ['pebbleos', 'serif', 'bold', 'small']                      , sidesAllowed: null, sidesWithSeconds: null, weight: 700 , romanOk: true , resourceName: "FONT_KEY_DROID_SERIF_28_BOLD", fileName: null },                                 // still genuinely on Google Fonts (legacy listing, but live)
  { id: 10, label: 'Roboto Condensed'      , height: 15, preview: 'font-family: \'Roboto Condensed\', Arial, sans-serif;'                                                   , small: true , google: 'Roboto Condensed'     , sizePx: 21, categories: ['pebbleos', 'modern', 'narrow', 'small']                   , sidesAllowed: null, sidesWithSeconds: null, weight: 400 , romanOk: true , resourceName: "FONT_KEY_ROBOTO_CONDENSED_21", fileName: null },
  { id: 11, label: 'Roboto Bold'           , height: 30, preview: 'font-family: \'Roboto\', Arial, sans-serif; font-weight: 700;'                                           , small: false, google: 'Roboto'               , sizePx: 49, categories: ['pebbleos', 'modern', 'bold', 'medium']                    , sidesAllowed: '1' , sidesWithSeconds: '0' , mainClock: true , weight: 700 , romanOk: false , resourceName: "FONT_KEY_ROBOTO_BOLD_SUBSET_49", fileName: null },
  { id: 12, label: 'Bitham Bold 30'        , height: 19, preview: 'font-family: \'Futura\', \'Century Gothic\', sans-serif; font-weight: 700;'                              , small: false, google: null                   , sizePx: 30, approx: true, categories: ['pebbleos', 'modern', 'bold', 'small']                     , sidesAllowed: 2   , sidesWithSeconds: '1' , mainClock: true  , romanOk: true , resourceName: "FONT_KEY_BITHAM_30_BLACK", fileName: null },              // Pebble's built-in Bitham -- no Google Fonts equivalent; Futura/Century Gothic (neither actually Google Fonts either) are the closest geometric-sans stand-ins available without downloading anything.
  { id: 13, label: 'Bitham Medium 34'      , height: 21, preview: 'font-family: \'Futura\', \'Century Gothic\', sans-serif; font-weight: 500;'                              , small: false, google: null                   , sizePx: 34, approx: true, categories: ['pebbleos', 'modern', 'medium']                            , sidesAllowed: 2   , sidesWithSeconds: '1' , mainClock: true  , romanOk: false , resourceName: "FONT_KEY_BITHAM_34_MEDIUM_NUMBERS", fileName: null },              // see id 12
  { id: 14, label: 'Bitham Light'          , height: 26, preview: 'font-family: \'Futura\', \'Century Gothic\', sans-serif; font-weight: 300; letter-spacing: 1px;'         , small: false, google: null                   , sizePx: 42, approx: true, categories: ['pebbleos', 'modern', 'thin', 'medium']                    , sidesAllowed: 2   , sidesWithSeconds: '1' , mainClock: true  , romanOk: true , resourceName: "FONT_KEY_BITHAM_42_LIGHT", fileName: null },              // see id 12
  { id: 15, label: 'Bitham Bold'           , height: 26, preview: 'font-family: \'Futura\', \'Century Gothic\', sans-serif; font-weight: 700; letter-spacing: 1px;'         , small: false, google: null                   , sizePx: 42, approx: true, categories: ['pebbleos', 'modern', 'bold', 'medium']                    , sidesAllowed: 2   , sidesWithSeconds: '0' , mainClock: true  , romanOk: true , resourceName: "FONT_KEY_BITHAM_42_BOLD", fileName: null },              // see id 12
  { id: 16, label: 'Digital Dream Small'   , height: 12, preview: 'font-family: \'Digital dream\', \'Courier New\', monospace; letter-spacing: 1px;'                        , small: true , sizePx: 12, categories: ['digital', 'narrow', 'tiny']                               , sidesAllowed: null, sidesWithSeconds: null, cdn: 'digital-dream' , romanOk: true , resourceName: "DIGITALDREAM_FONT_12", fileName: "fonts/DIGITALDREAM.ttf" },                                                         // Digital Dream (Pizzadude, dafont-only) isn't on Google Fonts, but the exact same font is hosted on cdnfonts.com as a webfont -- see cdnFontLinks() in config-page.js for how the extra <link> gets added.
  { id: 17, label: 'Digital Dream'         , height: 40, preview: 'font-family: \'Digital dream\', \'Courier New\', monospace; letter-spacing: 2px; font-style: italic;'    , small: false, sizePx: 48, categories: ['digital', 'large', 'wide', 'thin', 'italic']              , sidesAllowed: '0' , sidesWithSeconds: '-1', mainClock: true , cdn: 'digital-dream', italic: true , romanOk: false , resourceName: "DIGITALDREAM_FONT_48", fileName: "fonts/DIGITALDREAM.ttf" },                         // see id 16
  { id: 18, label: 'Minecrafter Small'     , height: 12, preview: 'font-family: \'Fizzy Soda\', \'Courier New\', monospace;'                                                , small: true , sizePx: 12, approx: true, categories: ['pixelated', 'funky', 'tiny']                              , sidesAllowed: null, sidesWithSeconds: null, cdn: 'fizzy-soda'    , romanOk: true , resourceName: "MINECRAFTER_FONT_12", fileName: "fonts/Minecrafter.ttf" },                                           // Minecrafter (dafont-only) itself isn't on cdnfonts, but Fizzy Soda (also on cdnfonts) is an almost identical blocky pixel-game face -- much closer than Google Fonts' own Press Start 2P was.
  { id: 19, label: 'Minecrafter'           , height: 40, preview: 'font-family: \'Fizzy Soda\', \'Courier New\', monospace;'                                                , small: false, sizePx: 48, approx: true, categories: ['pixelated', 'funky', 'wide', 'large']                     , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , cdn: 'fizzy-soda'    , romanOk: false , resourceName: "MINECRAFTER_FONT_48", fileName: "fonts/Minecrafter.ttf" },                         // see id 18
  { id: 20, label: 'SF Pixelate'           , height: 40, preview: 'font-family: \'DotGothic16\', \'Courier New\', monospace;'                                               , small: false, google: 'DotGothic16'          , sizePx: 48, approx: true, categories: ['pixelated', 'digital', 'wide', 'large']                   , sidesAllowed: '1' , sidesWithSeconds: '0' , mainClock: true , weight: 400 , romanOk: false , resourceName: "SFPIXELATE_FONT_48", fileName: "fonts/SFPixelate.ttf" }, // see id 20
  { id: 21, label: 'Alagard Small'         , height: 19, preview: 'font-family: \'Pixelify Sans\', \'Century Gothic\', sans-serif; font-weight: 600;'                       , small: true , google: 'Pixelify Sans'        , sizePx: 19, approx: true, categories: ['pixelated', 'stylish', 'small']                           , sidesAllowed: null, sidesWithSeconds: null, weight: 600 , romanOk: true , resourceName: "ALAGARD_FONT_19", fileName: "fonts/alagard.ttf" },                   // Alagard (dafont-only, Hewett Tsoi's 16px fantasy bitmap face) isn't on Google Fonts -- Pixelify Sans's blocky pixel-game look is the closest available match.
  { id: 22, label: 'Alagard'               , height: 40, preview: 'font-family: \'Pixelify Sans\', \'Century Gothic\', sans-serif; font-weight: 600;'                       , small: false, google: 'Pixelify Sans'        , sizePx: 48, categories: ['pixelated', 'stylish', 'large', 'funky']                  , sidesAllowed: 2   , sidesWithSeconds: '1' , mainClock: true , weight: 600 , romanOk: false , resourceName: "ALAGARD_FONT_48", fileName: "fonts/alagard.ttf" },               // see id 22
  { id: 23, label: 'Bebas Small'           , height: 20, preview: 'font-family: \'Bebas Neue\', \'Century Gothic\', sans-serif; letter-spacing: 1px;'                       , small: true , google: 'Bebas Neue'           , sizePx: 20, categories: ['stylish', 'modern', 'narrow', 'small']                    , sidesAllowed: null, sidesWithSeconds: null, weight: 400 , romanOk: true , resourceName: "BEBAS_FONT_20", fileName: "fonts/BebasNeueBold.otf" },
  { id: 24, label: 'Bebas'                 , height: 40, preview: 'font-family: \'Bebas Neue\', \'Century Gothic\', sans-serif; letter-spacing: 1px;'                       , small: false, google: 'Bebas Neue'           , sizePx: 48, categories: ['stylish', 'modern', 'narrow', 'large']                    , sidesAllowed: 2   , sidesWithSeconds: '1' , mainClock: true , weight: 400 , romanOk: false , resourceName: "BEBAS_FONT_48", fileName: "fonts/BebasNeueBold.otf" },
  { id: 25, label: 'Amita'                 , height: 40, preview: 'font-family: \'Amita\', Impact, sans-serif; font-weight: 700;'                                           , small: false, google: 'Amita'                , sizePx: 48, categories: ['stylish', 'handwritten', 'large', 'thin', 'wide', 'serif'], sidesAllowed: 2   , sidesWithSeconds: '0' , mainClock: true , weight: 700 , romanOk: false , resourceName: "AMITA_FONT_48", fileName: "fonts/Amita-Bold.ttf" },
  { id: 26, label: 'Averia'                , height: 40, preview: 'font-family: \'Averia Serif Libre\', \'Courier New\', serif; font-weight: 700; font-style: italic;'      , small: false, google: 'Averia Serif Libre'   , sizePx: 48, categories: ['serif', 'italic', 'bold', 'wide', 'large']                , sidesAllowed: '2' , sidesWithSeconds: '0' , mainClock: true , weight: 700, italic: true , romanOk: false , resourceName: "AVERIA_FONT_48", fileName: "fonts/AveriaSerifLibre-BoldItalic.ttf" },
  { id: 27, label: 'Bagel'                 , height: 40, preview: 'font-family: \'Bagel Fat One\', \'Courier New\', monospace;'                                             , small: false, google: 'Bagel Fat One'        , sizePx: 48, categories: ['funky', 'bold', 'wide', 'large']                          , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "BAGEL_FONT_48", fileName: "fonts/BagelFatOne-Regular.ttf" },
  { id: 28, label: 'Bricolage Grotesque'   , height: 40, preview: 'font-family: \'Bricolage Grotesque\', Impact, \'Arial Narrow\', sans-serif; font-weight: 700;'           , small: false, google: 'Bricolage Grotesque'  , sizePx: 48, categories: ['modern', 'bold', 'wide', 'large']                         , sidesAllowed: '2' , sidesWithSeconds: '0' , mainClock: true , weight: 700 , romanOk: false , resourceName: "BRICOLAGE_FONT_48", fileName: "fonts/BricolageGrotesque.ttf" },
  { id: 29, label: 'Chango'                , height: 40, preview: 'font-family: \'Chango\', \'Courier New\', monospace;'                                                    , small: false, google: 'Chango'               , sizePx: 48, categories: ['funky', 'stylish', 'bold', 'large', 'wide']               , sidesAllowed: '0' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "CHANGO_FONT_48", fileName: "fonts/Chango-Regular.ttf" },
  { id: 30, label: 'EmblemaOne'            , height: 40, preview: 'font-family: \'Emblema One\', \'Arial Narrow\', sans-serif; letter-spacing: 1px;'                        , small: false, google: 'Emblema One'          , sizePx: 48, categories: ['stylish', 'bold', 'large', 'wide', 'italic']              , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "EMBLEMA_FONT_48", fileName: "fonts/EmblemaOne-Regular.ttf" },
  { id: 31, label: 'Fraunces'              , height: 40, preview: 'font-family: \'Fraunces\', Georgia, serif; font-weight: 700;'                                            , small: false, google: 'Fraunces'             , sizePx: 48, categories: ['serif', 'stylish', 'bold', 'large', 'funky']              , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 700 , romanOk: false , resourceName: "FRAUNCES_FONT_48", fileName: "fonts/Fraunces.ttf" },
  { id: 32, label: 'Geostar Fill'          , height: 40, preview: 'font-family: \'Geostar Fill\', Impact, sans-serif;'                                                      , small: false, google: 'Geostar Fill'         , sizePx: 48, categories: ['stylish', 'funky', 'modern', 'large', 'thin', 'wide']     , sidesAllowed: '0' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "GEOSTAR_FONT_48", fileName: "fonts/GeostarFill-Regular.ttf" },
  { id: 33, label: 'Michroma'              , height: 40, preview: 'font-family: \'Michroma\', \'Arial Black\', sans-serif; letter-spacing: 1px;'                            , small: false, google: 'Michroma'             , sizePx: 48, categories: ['modern', 'wide', 'large', 'thin', 'stylish']              , sidesAllowed: '0' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "MICHROMA_FONT_48", fileName: "fonts/Michroma-Regular.ttf" },
  { id: 34, label: 'National Park'         , height: 40, preview: 'font-family: \'National Park\', Verdana, sans-serif; font-weight: 700;'                                  , small: false, google: 'National Park'        , sizePx: 48, categories: ['modern', 'stylish', 'bold', 'large']                      , sidesAllowed: '1' , sidesWithSeconds: '0' , mainClock: true , weight: 700 , romanOk: false , resourceName: "NATIONALPARK_FONT_48", fileName: "fonts/NationalPark-VariableFont_wght.ttf" },
  { id: 35, label: 'Komika'                , height: 40, preview: 'font-family: \'Bangers\', \'Comic Sans MS\', cursive;'                                                   , small: false, google: 'Bangers'              , sizePx: 48, approx: true, categories: ['funky', 'bold', 'wide', 'large', 'handwritten', 'italic'] , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "KOMIKAHB_FONT_48", fileName: "fonts/KOMIKAHB.ttf" }, // Komika Hand (Apostrophic Labs, dafont-only) isn't on Google Fonts -- Bangers is the closest bold comic-lettering face Google Fonts actually has.
  { id: 36, label: 'Quantico'              , height: 40, preview: 'font-family: \'Quantico\', Impact, \'Arial Narrow\', sans-serif; font-weight: 700; font-style: italic;'  , small: false, google: 'Quantico'             , sizePx: 48, categories: ['modern', 'italic', 'bold', 'large', 'stylish']            , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 700, italic: true , romanOk: false , resourceName: "QUANTICO_FONT_48", fileName: "fonts/Quantico-BoldItalic.ttf" },
  { id: 37, label: 'Silkscreen'            , height: 40, preview: 'font-family: \'Silkscreen\', Impact, \'Arial Narrow\', sans-serif;'                                      , small: false, google: 'Silkscreen'           , sizePx: 48, categories: ['pixelated', 'digital', 'large']                           , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "SILKSCREEN_FONT_48", fileName: "fonts/Silkscreen-Regular.ttf" },
  { id: 38, label: 'StackSansHeadline'     , height: 40, preview: 'font-family: \'Anton\', \'Arial Narrow\', sans-serif;'                                                   , small: false, google: 'Anton'                , sizePx: 48, approx: true, categories: ['modern', 'narrow', 'large', 'thin', 'stylish']            , sidesAllowed: '1' , sidesWithSeconds: '0' , mainClock: true , weight: 400 , romanOk: false , resourceName: "STACKSANSHEADLINE_FONT_48", fileName: "fonts/StackSansHeadline.ttf" }, // Stack Sans Headline isn't on Google Fonts (independent foundry release) -- Anton's ultra-bold condensed headline shape is the closest match.
  { id: 39, label: 'Unbounded'             , height: 40, preview: 'font-family: \'Unbounded\', Impact, \'Arial Narrow\', sans-serif; font-weight: 500;'                     , small: false, google: 'Unbounded'            , sizePx: 48, categories: ['modern', 'stylish', 'bold', 'large', 'wide']              , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 500 , romanOk: false , resourceName: "UNBOUNDED_FONT_48", fileName: "fonts/Unbounded-Medium.ttf" },
  { id: 40, label: 'Wallpoet'              , height: 40, preview: 'font-family: \'Wallpoet\', Impact, \'Arial Narrow\', sans-serif;'                                        , small: false, google: 'Wallpoet'             , sizePx: 48, categories: ['stylish', 'funky', 'modern', 'large', 'bold', 'wide']     , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: false , resourceName: "WALLPOET_FONT_48", fileName: "fonts/Wallpoet-Regular.ttf" },
  { id: 41, label: 'ZalandoSans'           , height: 40, preview: 'font-family: \'Zalando Sans Expanded\', Impact, \'Arial Narrow\', sans-serif; font-weight: 500;'         , small: false, google: 'Zalando Sans Expanded', sizePx: 48, categories: ['modern', 'wide', 'bold', 'large', 'stylish']              , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 500 , romanOk: false , resourceName: "ZALANDOSANS_FONT_48", fileName: "fonts/ZalandoSansExpanded-Medium.ttf" },
  { id: 42, label: 'Bytesized'             , height: 16, preview: 'font-family: \'Bytesized\', Impact, \'Arial Narrow\', sans-serif; font-weight: 400;'                     , small: true , google: 'Bytesized'            , sizePx: 16, categories: ['pixelated', 'small']                                      , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, weight: 400 , romanOk: true , resourceName: "BYTESIZED_FONT_16", fileName: "fonts/Bytesized-Regular.ttf" },
  { id: 43, label: 'M Plus 1C'             , height: 16, preview: 'font-family: \'M PLUS 1 Code\', Impact, \'Arial Narrow\', sans-serif; font-weight: 700;'                 , small: true , google: 'M PLUS 1 Code'        , sizePx: 16, categories: ['modern', 'bold', 'small']                                 , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, weight: 700 , romanOk: true , resourceName: "MPLUSBLACK_FONT_16", fileName: "fonts/MPLUSRounded1c-Black.ttf" },
  { id: 44, label: 'Noto Serif'            , height: 18, preview: 'font-family: \'Noto Serif\', Impact, \'Arial Narrow\', sans-serif; font-weight: 500; font-style: italic;', small: true , google: 'Noto Serif'           , sizePx: 18, categories: ['serif', 'italic', 'small']                                , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, weight: 500, italic: true , romanOk: true , resourceName: "NOTOITALIC_FONT_18", fileName: "fonts/NotoSerif-MediumItalic.ttf" },
  { id: 45, label: 'M Plus 1C'             , height: 40, preview: 'font-family: \'M PLUS 1 Code\', Impact, \'Arial Narrow\', sans-serif; font-weight: 700;'                 , small: false, google: 'M PLUS 1 Code'        , sizePx: 48, categories: ['modern', 'bold', 'large', 'wide']                         , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 700 , romanOk: false , resourceName: "MPLUSBLACK_FONT_48", fileName: "fonts/MPLUSRounded1c-Black.ttf" },
  { id: 46, label: 'Reddit Sans'           , height: 40, preview: 'font-family: \'Reddit Sans\', Impact, \'Arial Narrow\', sans-serif; font-weight: 700;'                   , small: false, google: 'Reddit Sans'          , sizePx: 48, categories: ['modern', 'bold', 'large']                                 , sidesAllowed: '2' , sidesWithSeconds: '-1', mainClock: true , weight: 700 , romanOk: false , resourceName: "REDITBOLD_FONT_48", fileName: "fonts/RedditSans-Bold.ttf" },
  { id: 47, label: 'Arcade'                , height: 18, preview: 'font-family: \'ArcadeClassic\', \'Courier New\', monospace;'                                             , small: true , sizePx: 18, categories: ['digital', 'pixelated', 'small']                           , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, cdn: 'arcadeclassic' , romanOk: true , resourceName: "ARCADE_FONT_18", fileName: "fonts/ArcadeClassic.ttf" },                                       // ArcadeClassic (Pizzadude, dafont-only) is hosted on cdnfonts.com as a webfont under its own name -- see cdnFontLinks() below.
  { id: 48, label: 'DS Digital Bold'       , height: 20, preview: 'font-family: \'DS-Digital\', \'Courier New\', monospace; font-weight: 700;'                              , small: true , sizePx: 20, categories: ['digital', 'bold', 'small']                                , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, cdn: 'ds-digital'    , romanOk: true , resourceName: "DSDIGIB_FONT_20", fileName: "fonts/DS-DIGIB.TTF" },                                       // DS-Digital is hosted on cdnfonts.com as a webfont -- see cdnFontLinks() below.
  { id: 49, label: 'DS Digital Bold Italic', height: 20, preview: 'font-family: \'DS-Digital\', \'Courier New\', monospace; font-weight: 700; font-style: italic;'          , small: true , sizePx: 20, categories: ['digital', 'bold', 'italic', 'small']                      , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, cdn: 'ds-digital'    , romanOk: true , resourceName: "DSDIGIT_FONT_20", fileName: "fonts/DS-DIGIT.TTF" },                                       // see id 48
  { id: 50, label: 'LCD'                   , height: 18, preview: 'font-family: \'Press Start 2P\', \'Courier New\', monospace;'                                            , small: true , google: 'Press Start 2P'       , sizePx: 18, approx: true, categories: ['digital', 'pixelated', 'small']                           , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, weight: 400 , romanOk: true , resourceName: "LCDSOLID_FONT_18", fileName: "fonts/LCD_Solid.ttf" }, // LCD Solid (dafont-only) doesn't appear to be hosted on cdnfonts.com or Google Fonts under its own name -- Press Start 2P remains the closest available blocky-digital match found so far.
  { id: 51, label: 'Radioland'             , height: 16, preview: 'font-family: \'Radioland\', \'Courier New\', monospace;'                                                 , small: true , sizePx: 16, categories: ['digital', 'small']                                        , sidesAllowed: null, sidesWithSeconds: null, mainClock: false, cdn: 'radioland'     , romanOk: true , resourceName: "RADIOLAND_FONT_16", fileName: "fonts/RADIOLAND.ttf" },                                       // Radioland (Pizzadude, dafont-only) is hosted on cdnfonts.com as a webfont under its own name -- see cdnFontLinks() below.
  { id: 52, label: 'DS Digital Bold'       , height: 48, preview: 'font-family: \'DS-Digital\', \'Courier New\', monospace; font-weight: 700;'                              , small: false, sizePx: 48, categories: ['digital', 'bold', 'large', 'italic']                      , sidesAllowed: 2   , sidesWithSeconds: '0' , mainClock: true , cdn: 'ds-digital'    , romanOk: true , resourceName: "DSDIGIB_FONT_48", fileName: "fonts/DS-DIGIB.TTF" },                                       // see id 48
  { id: 53, label: 'DS Digital Bold Italic', height: 48, preview: 'font-family: \'DS-Digital\', \'Courier New\', monospace; font-weight: 700; font-style: italic;'          , small: false, sizePx: 48, categories: ['digital', 'bold', 'italic', 'large']                      , sidesAllowed: 2   , sidesWithSeconds: '0' , mainClock: true , cdn: 'ds-digital'    , romanOk: true , resourceName: "DSDIGIT_FONT_48", fileName: "fonts/DS-DIGIT.TTF" },                                       // see id 48
  { id: 54, label: 'LCD'                   , height: 48, preview: 'font-family: \'Press Start 2P\', \'Courier New\', monospace;'                                            , small: false, google: 'Press Start 2P'       , sizePx: 48, approx: true, categories: ['digital', 'pixelated', 'large', 'bold']                   , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , weight: 400 , romanOk: true , resourceName: "LCDSOLID_FONT_48", fileName: "fonts/LCD_Solid.ttf" }, // see id 50
  { id: 55, label: 'Rebel Redux'           , height: 48, preview: 'font-family: \'RebelRedux\', \'Arial Narrow\', \'Impact\', sans-serif; font-weight: 500;'                , small: false, sizePx: 48, categories: ['stylish', 'funky', 'modern', 'large', 'thin', 'wide']     , sidesAllowed: '1' , sidesWithSeconds: '-1', mainClock: true , cdn: 'rebelredux'    , romanOk: true , resourceName: "REBELREDUX_FONT_48", fileName: "fonts/RebelRedux.ttf" },                                       // RebelRedux (dafont-only) is hosted on cdnfonts.com as a webfont -- see cdnFontLinks() in config-page.js. (Pizzadude, dafont-only) is hosted on cdnfonts.com as a webfont under its own name -- see cdnFontLinks() below.
  { id: 56, label: '00TT'       , height: 48, preview: 'font-family: \'Press Start 2P\', \'Courier New\', monospace;'                              , small: false, google: 'Press Start 2P', sizePx: 48, approx: true, categories: ['digital', 'bold', 'large']                      , sidesAllowed: 2   , sidesWithSeconds: '0' , mainClock: true , weight: 400 , romanOk: true , resourceName: "ZEROZERO_FONT_48", fileName: "fonts/00TT.TTF" },                                       // see id 48
]; // FONT_MAX_CONTENT_ID below is derived from this array's own ids --
   // no more "remember to bump" comment needed.

// The highest FONT_LOOKUP id currently in use -- used wherever a font
// choice (clock font, corner/edge font, marker text font) gets
// validated/clamped, e.g. clampFontId() in index.js. Computed from
// FONT_LOOKUP itself rather than hand-maintained as a separate number,
// so adding a new font entry above can never leave this stale the way
// a hardcoded copy repeatedly has.
var FONT_MAX_CONTENT_ID = FONT_LOOKUP.reduce(function (max, f) { return Math.max(max, f.id); }, 0);

// Font picker category filter row -- 'all' is a special case handled
// entirely client-side (see fontMatchesCategoryFilters() in
// config-page.js): it's never listed in any font's own `categories`
// array, always shows every font (still subject to the "Show
// incompatible fonts" checkbox, same as every other filter
// combination), and picking it clears/replaces whatever combination
// of the other categories was active. Order here is the order the
// picker's horizontal scroll row shows them in.
var FONT_CATEGORIES = [
  { id: 'all', label: 'All' },
  { id: 'digital', label: 'Digital' },
  { id: 'pixelated', label: 'Pixelated' },
  { id: 'stylish', label: 'Stylish' },
  { id: 'modern', label: 'Modern' },
  { id: 'funky', label: 'Funky' },
  { id: 'serif', label: 'Serif' },
  { id: 'italic', label: 'Italic' },
  { id: 'bold', label: 'Bold' },
  { id: 'thin', label: 'Thin' },
  { id: 'narrow', label: 'Narrow' },
  { id: 'wide', label: 'Wide' },
  { id: 'pebbleos', label: 'PebbleOS' },
  { id: 'large', label: 'Large' },
  { id: 'medium', label: 'Medium' },
  { id: 'small', label: 'Small' },
  { id: 'tiny', label: 'Tiny' },
  { id: 'handwritten', label: 'Handwritten' }
];

// Must match get_color_scheme() in pebble-eclipse-watch.c exactly --
// same order, same id, same colors.
var COLOR_SCHEMES = [
  { id: 0, label: 'Black on White', bg: '#ffffff', text: '#000000', accent: '#000000' },
  { id: 1, label: 'White on Black', bg: '#000000', text: '#ffffff', accent: '#ffffff' },
  { id: 2, label: 'Red on Black', bg: '#000000', text: '#ff0000', accent: '#ff0000' },
  { id: 3, label: 'White on Dark Blue', bg: '#00003c', text: '#ffffff', accent: '#ffffff' },
  { id: 4, label: 'Yellow on Dark Blue', bg: '#00003c', text: '#ffff00', accent: '#ffff00' },
  { id: 5, label: 'White on Black, Red accent', bg: '#000000', text: '#ffffff', accent: '#ff0000' },
  { id: 6, label: 'Black on White, Dark Red accent', bg: '#ffffff', text: '#000000', accent: '#8b0000' },
  { id: 7, label: 'Black on White, Dark Blue accent', bg: '#ffffff', text: '#000000', accent: '#00008b' },
  { id: 8, label: 'Red on Black, White accent', bg: '#000000', text: '#ff0000', accent: '#ffffff' },
  { id: 9, label: 'Red on White, Orange accent', bg: '#ffffff', text: '#ff0000', accent: '#ff8c00' },
  { id: 11, label: 'Brown on Green, Orange accent', bg: '#228b22', text: '#8b4513', accent: '#ff8c00' }
];

// Roman-numeral compatibility lives on FONT_LOOKUP itself now (each
// entry's own `romanOk` field, right alongside every other font
// capability flag) rather than as a second id-keyed table a caller had
// to cross-reference against FONT_LOOKUP by hand -- see fontLookupEntry()
// in config-fonts.js for the normal way to read it. This is the phone-
// side copy of the same data font_lookup.c tracks per-font as
// FontLookupEntry.roman_ok (JS and the watch app are separate runtimes
// that can't share a table, so keep the two in sync by hand) -- see its
// own comment there for exactly how each value was determined: official
// Pebble system-font documentation for ids 0-15, and each custom font's
// own package.json characterRegex for ids 16+. int_to_roman() in
// marker_text.c only ever needs I, V, X, L (it covers 0-55: the hour
// ring's 1-12 and the second/minute ring's 0/5/.../55), never C, D or M.

// Must match draw_corner_item()'s color_mode switch exactly.
var CORNER_COLOR_MODE_LABELS = ['MONO', 'ACC', 'PILL', 'COLOR'];

// The corner/edge feature content catalogue -- see this file's own
// header comment for why this is the one place all of id, label,
// category and live-preview text live together. `icon` is the SVG
// glyph shown on that category's own picker button; `preview` is
// the illustrative (not live) sample text shown in the live preview
// canvas and the slot-picker diagram's own buttons for that item --
// omitted for id 0 ("None"), which never draws anything. Item order
// within a category is deliberately curated (most useful/common
// first), not id-ascending.
var CORNER_CATEGORIES = [
  {
    "id": "none",
    "label": "None",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\"><path d=\"M12 3 L12 11\"/><path d=\"M7 6.5 A8 8 0 1 0 17 6.5\"/></svg>",
    "items": [
      {
        "id": 0,
        "label": "None"
      }
    ]
  },
  {
    "id": "utilities",
    "label": "Utilities",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M17.7 3.3a4.5 4.5 0 0 0-6 5.7L4 16.7a1.8 1.8 0 0 0 2.5 2.5L14.2 12a4.5 4.5 0 0 0 5.7-6l-2.8 2.8-2.1-2.1 2.7-2.7z\"/></svg>",
    "items": [
      {
        "id": 10,
        "label": "Battery",
        "preview": "82%"
      },
      {
        "id": 13,
        "label": "Location",
        "preview": "Innsbruck"
      },
      {
        "id": 17,
        "label": "Pebble logo /w battery bar",
        "preview": "LOGO"
      },
      {
        "id": 20,
        "label": "Bluetooth connection",
        "preview": "Connected"
      },
      {
        "id": 78,
        "label": "Bluetooth status (icon only)",
        "preview": "(bt)"
      },
      {
        "id": 38,
        "label": "Altitude",
        "preview": "380m"
      },
      {
        "id": 85,
        "label": "Compass",
        "preview": "NNW"
      },
      {
        "id": 102,
        "label": "Battery + Bluetooth (icons only)",
        "preview": "(batt)(bt)"
      },
      {
        "id": 103,
        "label": "Battery % + Bluetooth",
        "preview": "68% (bt)"
      },
      {
        "id": 108,
        "label": "Quiet Time (icon only)",
        "preview": "(quiet)"
      },
      {
        "id": 109,
        "label": "Quiet Time (icon + ON/OFF)",
        "preview": "(spkr) OFF"
      },
      {
        "id": 112,
        "label": "Battery + Bluetooth + Quiet Time (icons only)",
        "preview": "(batt)(bt)(quiet)"
      },
      {
        "id": 113,
        "label": "Battery % + Quiet Time + Bluetooth (icon + ON/OFF each)",
        "preview": "82% (spkr)OFF (bt)ON"
      }
    ]
  },
  {
    "id": "health",
    "label": "Health",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 20.8c-.3 0-.6-.1-.8-.3C6.5 16.6 3 13.3 3 9.6 3 6.9 5.1 5 7.7 5c1.6 0 3.1.8 4.3 2.2C13.2 5.8 14.7 5 16.3 5 18.9 5 21 6.9 21 9.6c0 3.7-3.5 7-8.2 10.9-.2.2-.5.3-.8.3z\"/></svg>",
    "items": [
      {
        "id": 1,
        "label": "Heart rate",
        "preview": "72"
      },
      {
        "id": 2,
        "label": "Steps today",
        "preview": "5234"
      },
      {
        "id": 3,
        "label": "Step goal %",
        "preview": "68%"
      },
      {
        "id": 39,
        "label": "Sleep duration",
        "preview": "7h 32m"
      },
      {
        "id": 40,
        "label": "Restful sleep duration",
        "preview": "2h 15m"
      },
      {
        "id": 41,
        "label": "Sleep quality %",
        "preview": "42%"
      },
      {
        "id": 42,
        "label": "Bed time",
        "preview": "23:45"
      },
      {
        "id": 43,
        "label": "Wake time",
        "preview": "07:20"
      }
    ]
  },
  {
    "id": "date",
    "label": "Date",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><rect x=\"3\" y=\"5\" width=\"18\" height=\"16\" rx=\"2\"/><line x1=\"3\" y1=\"10\" x2=\"21\" y2=\"10\"/><line x1=\"8\" y1=\"2\" x2=\"8\" y2=\"6\"/><line x1=\"16\" y1=\"2\" x2=\"16\" y2=\"6\"/><rect x=\"7\" y=\"13\" width=\"3\" height=\"3\" fill=\"currentColor\" stroke=\"none\"/></svg>",
    "items": [
      {
        "id": 12,
        "label": "Short date",
        "preview": "Mon 15"
      },
      {
        "id": 19,
        "label": "Week number",
        "preview": "WK 34"
      },
      {
        "id": 21,
        "label": "Month Day (SEP 11)",
        "preview": "SEP 11"
      },
      {
        "id": 22,
        "label": "Day of month (11)",
        "preview": "11"
      },
      {
        "id": 23,
        "label": "Weekday short (MON)",
        "preview": "MON"
      },
      {
        "id": 24,
        "label": "Weekday long (Monday)",
        "preview": "Monday"
      },
      {
        "id": 25,
        "label": "Month short (SEP)",
        "preview": "SEP"
      },
      {
        "id": 26,
        "label": "Month long (September)",
        "preview": "September"
      },
      {
        "id": 27,
        "label": "Day/Month (11/9)",
        "preview": "11/9"
      },
      {
        "id": 28,
        "label": "Month/Day (9/11)",
        "preview": "9/11"
      },
      {
        "id": 29,
        "label": "Full date (24/9/2026)",
        "preview": "24/9/2026"
      },
      {
        "id": 30,
        "label": "Full date, imperial (9/24/26)",
        "preview": "9/24/26"
      },
      {
        "id": 98,
        "label": "Weekday + Day/Month (MON 24/9)",
        "preview": "MON 24/9"
      },
      {
        "id": 99,
        "label": "Weekday + Month/Day (MON 9/24)",
        "preview": "MON 9/24"
      }
    ]
  },
  {
    "id": "time",
    "label": "Time",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"12\" r=\"9\"/><line x1=\"12\" y1=\"12\" x2=\"12\" y2=\"6.5\"/><line x1=\"12\" y1=\"12\" x2=\"16\" y2=\"14\"/></svg>",
    "items": [
      {
        "id": 18,
        "label": "Time",
        "preview": "12:34"
      },
      {
        "id": 63,
        "label": "Time full (H:M:S)",
        "preview": "14:32:07"
      },
      {
        "id": 64,
        "label": "Hour, 24h leading zero (07)",
        "preview": "07"
      },
      {
        "id": 65,
        "label": "Hour, 24h (7)",
        "preview": "7"
      },
      {
        "id": 66,
        "label": "Hour, 12h (7)",
        "preview": "7"
      },
      {
        "id": 67,
        "label": "Minute (5)",
        "preview": "5"
      },
      {
        "id": 68,
        "label": "Minute, leading zero (05)",
        "preview": "05"
      },
      {
        "id": 69,
        "label": "Seconds (8)",
        "preview": "8"
      },
      {
        "id": 70,
        "label": "Second, leading zero (08)",
        "preview": "08"
      },
      {
        "id": 71,
        "label": "Seconds, tens digit",
        "preview": "3"
      },
      {
        "id": 72,
        "label": "Seconds, ones digit",
        "preview": "8"
      },
      {
        "id": 86,
        "label": "AM/PM",
        "preview": "PM"
      }
    ]
  },
  {
    "id": "timezone",
    "label": "Timezone",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.6\" stroke-linecap=\"round\"><circle cx=\"12\" cy=\"12\" r=\"9\"/><ellipse cx=\"12\" cy=\"12\" rx=\"4\" ry=\"9\"/><line x1=\"3\" y1=\"12\" x2=\"21\" y2=\"12\"/><path d=\"M4.5 7 C8 8.5, 16 8.5, 19.5 7\"/><path d=\"M4.5 17 C8 15.5, 16 15.5, 19.5 17\"/></svg>",
    "items": [
      {
        "id": 44,
        "label": "GMT+0 London",
        "preview": "LON 12:34"
      },
      {
        "id": 45,
        "label": "GMT+1 Paris / Berlin / Madrid",
        "preview": "PAR 13:34"
      },
      {
        "id": 46,
        "label": "GMT+2 Cairo",
        "preview": "CAI 14:34"
      },
      {
        "id": 47,
        "label": "GMT+3 Moscow",
        "preview": "MOW 15:34"
      },
      {
        "id": 48,
        "label": "GMT+4 Dubai",
        "preview": "DXB 16:34"
      },
      {
        "id": 49,
        "label": "GMT+5:30 Delhi / Mumbai",
        "preview": "DEL 18:04"
      },
      {
        "id": 50,
        "label": "GMT+6 Dhaka",
        "preview": "DAC 18:34"
      },
      {
        "id": 51,
        "label": "GMT+7 Bangkok / Jakarta",
        "preview": "BKK 19:34"
      },
      {
        "id": 52,
        "label": "GMT+8 Beijing / Shanghai / Singapore",
        "preview": "BJS 20:34"
      },
      {
        "id": 53,
        "label": "GMT+9 Tokyo",
        "preview": "TOK 21:34"
      },
      {
        "id": 54,
        "label": "GMT+10 Sydney",
        "preview": "SYD 22:34"
      },
      {
        "id": 55,
        "label": "GMT+12 Auckland",
        "preview": "AKL 00:34"
      },
      {
        "id": 56,
        "label": "GMT-5 New York",
        "preview": "NYC 07:34"
      },
      {
        "id": 57,
        "label": "GMT-6 Chicago",
        "preview": "CHI 06:34"
      },
      {
        "id": 58,
        "label": "GMT-7 Denver",
        "preview": "DEN 05:34"
      },
      {
        "id": 59,
        "label": "GMT-8 Los Angeles",
        "preview": "LAX 04:34"
      },
      {
        "id": 60,
        "label": "GMT-9 Anchorage",
        "preview": "ANC 03:34"
      },
      {
        "id": 61,
        "label": "GMT-10 Honolulu",
        "preview": "HNL 02:34"
      },
      {
        "id": 62,
        "label": "GMT-3 Sao Paulo",
        "preview": "SAO 09:34"
      }
    ]
  },
  {
    "id": "weather",
    "label": "Weather",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.8\" stroke-linecap=\"round\"><circle cx=\"8\" cy=\"7.5\" r=\"3.2\" fill=\"currentColor\" stroke=\"none\"/><line x1=\"8\" y1=\"1\" x2=\"8\" y2=\"2.5\"/><line x1=\"1.5\" y1=\"7.5\" x2=\"3\" y2=\"7.5\"/><line x1=\"3.3\" y1=\"2.8\" x2=\"4.4\" y2=\"3.9\"/><path d=\"M6 20h11a4 4 0 0 0 .6-7.95A5.5 5.5 0 0 0 7.2 10.8 3.8 3.8 0 0 0 6 20z\" fill=\"currentColor\" stroke=\"none\"/></svg>",
    "items": [
      {
        "id": 4,
        "label": "High / low temperature",
        "preview": "H72 L58"
      },
      {
        "id": 5,
        "label": "Current conditions",
        "preview": "68F Clear"
      },
      {
        "id": 6,
        "label": "UV Index today",
        "preview": "UV5"
      },
      {
        "id": 107,
        "label": "Current UV index",
        "preview": "UV5"
      },
      {
        "id": 7,
        "label": "Rain chance today",
        "preview": "R20%"
      },
      {
        "id": 8,
        "label": "Humidity",
        "preview": "H45%"
      },
      {
        "id": 9,
        "label": "Wind",
        "preview": "W12"
      },
      {
        "id": 14,
        "label": "Visibility",
        "preview": "80%"
      },
      {
        "id": 15,
        "label": "Cloud cover",
        "preview": "45%"
      },
      {
        "id": 31,
        "label": "Weather icon",
        "preview": "(cloud)"
      },
      {
        "id": 32,
        "label": "Temp + weather icon",
        "preview": "20C"
      },
      {
        "id": 34,
        "label": "Pressure",
        "preview": "1013 hPa"
      },
      {
        "id": 35,
        "label": "Wind direction",
        "preview": "NW"
      },
      {
        "id": 36,
        "label": "Air quality",
        "preview": "AQI 42"
      },
      {
        "id": 37,
        "label": "Dew point",
        "preview": "12C"
      },
      {
        "id": 73,
        "label": "Current temp",
        "preview": "22C"
      },
      {
        "id": 74,
        "label": "High temp",
        "preview": "H 28C"
      },
      {
        "id": 75,
        "label": "Low temp",
        "preview": "L 11C"
      },
      {
        "id": 76,
        "label": "Weather icon + all temps",
        "preview": "22 H28 L11C"
      },
      {
        "id": 77,
        "label": "Feels like temp",
        "preview": "FL 20C"
      },
      {
        "id": 87,
        "label": "Weather in 1 hour",
        "preview": "(1h) 24C"
      },
      {
        "id": 88,
        "label": "Weather in 2 hours",
        "preview": "(2h) 23C"
      },
      {
        "id": 89,
        "label": "Weather in 3 hours",
        "preview": "(3h) 22C"
      },
      {
        "id": 90,
        "label": "Weather in 4 hours",
        "preview": "(4h) 21C"
      },
      {
        "id": 91,
        "label": "Weather in 5 hours",
        "preview": "(5h) 20C"
      },
      {
        "id": 92,
        "label": "Weather in 6 hours",
        "preview": "(6h) 19C"
      },
      {
        "id": 93,
        "label": "Weather in 1 days",
        "preview": "(1d) 24C"
      },
      {
        "id": 94,
        "label": "Weather in 2 days",
        "preview": "(2d) 22C"
      },
      {
        "id": 95,
        "label": "Weather in 3 days",
        "preview": "(3d) 20C"
      }
    ]
  },
  {
    "id": "astro",
    "label": "Astronomy",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.3\"><circle cx=\"12\" cy=\"12\" r=\"2.3\" fill=\"currentColor\" stroke=\"none\"/><ellipse cx=\"12\" cy=\"12\" rx=\"10\" ry=\"4\"/><ellipse cx=\"12\" cy=\"12\" rx=\"6.5\" ry=\"9\" transform=\"rotate(60 12 12)\"/><circle cx=\"21.3\" cy=\"12\" r=\"1.3\" fill=\"currentColor\" stroke=\"none\"/><circle cx=\"9.6\" cy=\"4.1\" r=\"1.1\" fill=\"currentColor\" stroke=\"none\"/></svg>",
    "items": [
      {
        "id": 11,
        "label": "Moon phase",
        "preview": "Full"
      },
      {
        "id": 16,
        "label": "Sunrise / sunset",
        "preview": "19:42"
      },
      {
        "id": 79,
        "label": "Planets visible now",
        "preview": "3 planets"
      },
      {
        "id": 80,
        "label": "Meteor shower",
        "preview": "Perseids"
      },
      {
        "id": 81,
        "label": "Saturn ring angle",
        "preview": "Rings 12%"
      },
      {
        "id": 82,
        "label": "Next planet rise",
        "preview": "VEN 18:32"
      },
      {
        "id": 83,
        "label": "Next ISS pass",
        "preview": "22:47"
      },
      {
        "id": 84,
        "label": "Aurora Kp index",
        "preview": "Kp 4.3"
      }
    ]
  },
  {
    "id": "wide",
    "label": "Wide",
    "icon": "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><line x1=\"3\" y1=\"12\" x2=\"21\" y2=\"12\"/><polyline points=\"8,7 3,12 8,17\"/><polyline points=\"16,7 21,12 16,17\"/></svg>",
    "items": [
      {
        "id": 100,
        "label": "Heart rate + steps",
        "preview": "72 5234"
      },
      {
        "id": 101,
        "label": "Bed time + wake time",
        "preview": "23:45 /07:20"
      },
      {
        "id": 104,
        "label": "Sleep times",
        "preview": "7h32m (2h15m) 42%"
      },
      {
        "id": 105,
        "label": "Long date + sunrise/sunset",
        "preview": "Mon 23 Sep 19:42"
      },
      {
        "id": 106,
        "label": "Long date + week number",
        "preview": "Mon 23 Sep WK34"
      },
      {
        "id": 96,
        "label": "Last weather update, long",
        "preview": "Last updated 12:34"
      },
      {
        "id": 97,
        "label": "Last weather update, short",
        "preview": "12:34"
      },
      {
        "id": 118,
        "label": "Battery + Bluetooth + Quiet Time (% for battery only)",
        "preview": "82% (bt)(spkr)"
      }
    ]
  }
];

// The highest corner/edge content id currently in use -- every content
// id features_layer.c implements (see this file's own comment above
// CORNER_CATEGORIES) must appear somewhere in that array, so its
// highest id IS this number; computed here rather than hand-maintained
// as a separate constant, since a stale hardcoded copy (index.js's own
// MAX_FEATURES sat at 104 for a while after content ids up to 115 were
// added elsewhere in this same file, silently rejecting every one of
// them back down to "None" until this was caught) is exactly the
// failure mode FONT_MAX_CONTENT_ID's own comment already flagged for
// FONT_LOOKUP.
var MAX_FEATURES = CORNER_CATEGORIES.reduce(function (max, cat) {
  return cat.items.reduce(function (m, item) { return Math.max(m, item.id); }, max);
}, 0);

// Starter hand-style presets for the Style section's "Hand style"
// picker -- keyed "1"."9" to match HAND_STYLE_IMAGES/
// HAND_STYLE_DIAGRAM_IMAGES's own filenames, each with a display
// title plus hour/min/sec field sets in the same shape
// applyHandPresetToKind() (config-page.js) writes into the hidden
// custom-hand inputs, PLUS a centerCircle and shadow field set
// applied the same way to the standalone Center circle/Shadow style
// settings (see applyHandPresetExtras()) -- those two aren't
// per-hand, but still look like part of "the hand style" to anyone
// picking a preset, so a preset sets them too instead of leaving
// whatever was there before. Picking a preset is a one-shot copy
// into those same fields the manual "Edit ... hand" / Center
// circle/Shadow style editors use -- nothing about it is remembered
// as a distinct "preset" afterward, on this page or on the watch.
// These 9 are placeholders pairing the existing style shapes with
// generic starter numbers -- replace the images and retune the
// field sets here once real example styles are worked out.
var HAND_PRESETS = {
  "1": {
    "title": "Pebble",
    "hour": {
      "Style": "0",
      "Width": "10",
      "Length": "51",
      "BackOffset": "0",
      "Color": "1"
    },
    "min": {
      "Style": "0",
      "Width": "10",
      "Length": "78",
      "BackOffset": "0",
      "Color": "0"
    },
    "sec": {
      "Style": "0",
      "Width": "2",
      "Length": "85",
      "BackOffset": "0",
      "Color": "0"
    },
    "centerCircle": {
      "Radius": "5",
      "Color": "1"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "120"
    }
  },
  "2": {
    "title": "Fruit",
    "hour": {
      "Style": "5",
      "Width": "8",
      "Length": "63",
      "BackOffset": "0",
      "MiddleOffset": "20",
      "SecondaryWidth": "2",
      "Color": "1"
    },
    "min": {
      "Style": "5",
      "Width": "8",
      "Length": "81",
      "BackOffset": "0",
      "MiddleOffset": "20",
      "SecondaryWidth": "2",
      "Color": "0"
    },
    "sec": {
      "Style": "2",
      "Width": "2",
      "Length": "85",
      "BackOffset": "0",
      "Color": "0"
    },
    "centerCircle": {
      "Radius": "2",
      "Color": "0"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "120"
    }
  },
  "3": {
    "title": "Modern",
    "hour": {
      "Style": "2",
      "Width": "6",
      "Length": "51",
      "BackOffset": "10",
      "Color": "0"
    },
    "min": {
      "Style": "2",
      "Width": "6",
      "Length": "78",
      "BackOffset": "10",
      "Color": "0"
    },
    "sec": {
      "Style": "2",
      "Width": "2",
      "Length": "85",
      "BackOffset": "10",
      "Color": "1"
    },
    "centerCircle": {
      "Radius": "4",
      "Color": "1"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "120"
    }
  },
  "4": {
    "title": "Swiss",
    "hour": {
      "Style": "2",
      "Width": "10",
      "Length": "51",
      "BackOffset": "10",
      "Color": "0"
    },
    "min": {
      "Style": "2",
      "Width": "10",
      "Length": "78",
      "BackOffset": "10",
      "Color": "0"
    },
    "sec": {
      "Style": "6",
      "Width": "2",
      "Length": "85",
      "BackOffset": "15",
      "MiddleOffset": "5",
      "SecondaryWidth": "17",
      "Color": "1"
    },
    "centerCircle": {
      "Radius": "3",
      "Color": "1"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "120"
    }
  },
  "5": {
    "title": "Pointy",
    "hour": {
      "Style": "1",
      "Width": "12",
      "Length": "51",
      "BackOffset": "0",
      "Color": "0"
    },
    "min": {
      "Style": "1",
      "Width": "12",
      "Length": "78",
      "BackOffset": "0",
      "Color": "0"
    },
    "sec": {
      "Style": "0",
      "Width": "2",
      "Length": "85",
      "BackOffset": "0",
      "Color": "1"
    },
    "centerCircle": {
      "Radius": "4",
      "Color": "0"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "120"
    }
  },
  "6": {
    "title": "Classy",
    "hour": {
      "Style": "3",
      "Width": "10",
      "Length": "51",
      "BackOffset": "4",
      "MiddleOffset": "10",
      "Color": "0"
    },
    "min": {
      "Style": "3",
      "Width": "8",
      "Length": "78",
      "BackOffset": "4",
      "MiddleOffset": "14",
      "Color": "0"
    },
    "sec": {
      "Style": "1",
      "Width": "2",
      "Length": "85",
      "BackOffset": "6",
      "Color": "1"
    },
    "centerCircle": {
      "Radius": "5",
      "Color": "0"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "135"
    }
  },
  "7": {
    "title": "Bell",
    "hour": {
      "Style": "4",
      "Width": "10",
      "Length": "61",
      "BackOffset": "-20",
      "MiddleOffset": "40",
      "SecondaryWidth": "14",
      "Color": "0"
    },
    "min": {
      "Style": "4",
      "Width": "8",
      "Length": "78",
      "BackOffset": "-21",
      "MiddleOffset": "40",
      "SecondaryWidth": "8",
      "Color": "0"
    },
    "sec": {
      "Style": "7",
      "Width": "7",
      "Length": "85",
      "BackOffset": "0",
      "Color": "1"
    },
    "centerCircle": {
      "Radius": "5",
      "Color": "1"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "135"
    }
  },
  "8": {
    "title": "Leafs",
    "hour": {
      "Style": "8",
      "Width": "10",
      "Length": "51",
      "BackOffset": "0",
      "MiddleOffset": "5",
      "Color": "0"
    },
    "min": {
      "Style": "8",
      "Width": "8",
      "Length": "78",
      "BackOffset": "0",
      "MiddleOffset": "8",
      "Color": "0"
    },
    "sec": {
      "Style": "1",
      "Width": "2",
      "Length": "85",
      "BackOffset": "6",
      "Color": "1"
    },
    "centerCircle": {
      "Radius": "4",
      "Color": "0"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "120"
    }
  },
  "9": {
    "title": "Funky",
    "hour": {
      "Style": "10",
      "Width": "4",
      "Length": "51",
      "BackOffset": "0",
      "MiddleOffset": "8",
      "SecondaryWidth": "12",
      "Color": "0"
    },
    "min": {
      "Style": "10",
      "Width": "3",
      "Length": "78",
      "BackOffset": "0",
      "MiddleOffset": "10",
      "SecondaryWidth": "10",
      "Color": "0"
    },
    "sec": {
      "Style": "10",
      "Width": "2",
      "Length": "85",
      "BackOffset": "6",
      "MiddleOffset": "8",
      "SecondaryWidth": "17",
      "Color": "1"
    },
    "centerCircle": {
      "Radius": "4",
      "Color": "1"
    },
    "shadow": {
      "Translucent": "true",
      "Angle": "150"
    }
  }
};

module.exports = {
  FONT_LOOKUP: FONT_LOOKUP,
  FONT_MAX_CONTENT_ID: FONT_MAX_CONTENT_ID,
  FONT_CATEGORIES: FONT_CATEGORIES,
  COLOR_SCHEMES: COLOR_SCHEMES,
  CORNER_COLOR_MODE_LABELS: CORNER_COLOR_MODE_LABELS,
  CORNER_CATEGORIES: CORNER_CATEGORIES,
  MAX_FEATURES: MAX_FEATURES,
  HAND_PRESETS: HAND_PRESETS
};
