// ---- primitive-strip preview + primitive-sequence transport (Feature ----
// Primitive Refactor, Phases 3 and 9) --------------------------------------
//
// Two related things live in this file:
//
// 1. The **primitive-strip preview** (Phase 9): shows the person picking a
//    content id what it actually decomposes into on the watch, mirroring
//    feature_value_weather.c/feature_value_time.c/feature_value_composite.c's
//    Phase 8 decompositions chip-for-chip. Every ATOMIC_DECOMPOSITIONS entry
//    has a comment pointing at the C case it mirrors, so the two stay in
//    sync by inspection.
//
// 2. **Real primitive-sequence transport** (Phase 3), for corner AND
//    edge/middle slots: PRIMITIVE_TRANSPORT_CONTENT_IDS lists which content
//    ids get their primitive sequence actually *sent* to the watch as
//    numeric primitive IDs (see primitiveIdSequenceForContent() below and
//    comms/message-encoder.js's use of it), rather than the watch deriving
//    primitives locally from a legacy content number (primitive_bridge.c).
//    Content id 105 is the one remaining gap versus ATOMIC_DECOMPOSITIONS
//    above -- see that constant's own comment for why (a resolver gap, not
//    a color-ownership one -- that question was resolved: every primitive
//    resolves its own color, decided by whoever decided its value, "down
//    to primitives" -- see primitive_resolver.c's own header and
//    IMPLEMENTATION_NOTES.md's Phase 3 update for the full principle).
//
// 3. **The composable palette** (Phase 9): PRIMITIVE_PALETTE lists every
//    primitive a person can build a custom sequence from -- deliberately
//    only the ones primitive_resolver.c actually resolves, grouped and
//    labeled for the picker UI (config-page.js).
//
// A chip is {kind, text}. kind is 'label' (a constant glyph like "H"/"WK"),
// 'value' (a data-driven primitive -- the example text is illustrative,
// matching CORNER_CATEGORIES' own "preview" field for that content id),
// 'space' or 'slash' (a universal spacer primitive, Section 4), or 'icon'
// (a static icon primitive; text is a short label, not real icon art).

var presetsLookups = require('../presets-lookups');
var CORNER_CATEGORIES = presetsLookups.CORNER_CATEGORIES;
var PRIM = require('./primitive-ids-generated');

var CONTENT_ITEM_BY_ID = {};
CORNER_CATEGORIES.forEach(function (cat) {
  cat.items.forEach(function (item) { CONTENT_ITEM_BY_ID[item.id] = item; });
});

function label(text) { return { kind: 'label', text: text }; }
function value(text) { return { kind: 'value', text: text }; }
function space() { return { kind: 'space', text: ' ' }; }
function slash() { return { kind: 'slash', text: '/' }; }
function icon(text) { return { kind: 'icon', text: text }; }

// Each entry mirrors one feature_value_*_compute() case exactly, in
// primitive order. Example values come from that content id's own
// CORNER_CATEGORIES "preview" string (see presets-lookups.js), split at
// the same points the C decomposition splits its own composed string.
//
// Content id 105 is decomposed here (for the preview) but still excluded
// from PRIMITIVE_TRANSPORT_CONTENT_IDS below -- not a color-ownership
// issue (that was resolved -- see this file's PRIMITIVE_TRANSPORT_SEQUENCES
// comment), but because its sunrise/sunset half
// (PRIM_SUN_EVENT_ICON/PRIM_SUN_EVENT_TIME) has no resolver at all yet, for
// reasons that have nothing to do with color: see
// astronomy/sky-display.js's header for the time-dependent-selection and
// 12h/24h-formatting reasons that cluster can't be resolved from a
// primitive ID alone.
var ATOMIC_DECOMPOSITIONS = {
  // feature_value_weather.c case 4, plain branch
  4: [label('H'), value('72'), space(), label('L'), value('58')],
  // feature_value_date_compute case 12
  12: [value('Mon'), space(), value('15')],
  // feature_value_date_compute case 19
  19: [label('WK'), space(), value('34')],
  // feature_value_date_compute case 21
  21: [value('SEP'), space(), value('11')],
  // feature_value_date_compute case 27
  27: [value('11'), slash(), value('9')],
  // feature_value_date_compute case 28
  28: [value('9'), slash(), value('11')],
  // feature_value_date_compute case 29
  29: [value('24'), slash(), value('9'), slash(), value('2026')],
  // feature_value_date_compute case 30
  30: [value('9'), slash(), value('24'), slash(), value('26')],
  // feature_value_weather.c case 76
  76: [icon('weather'), value('22'), space(), label('H'), value('28'), space(), label('L'), value('11')],
  // feature_value_date_compute case 98
  98: [value('MON'), space(), value('24'), slash(), value('9')],
  // feature_value_date_compute case 99
  99: [value('MON'), space(), value('9'), slash(), value('24')],
  // feature_value_composite.c case 101 (sleep-data-available branch)
  101: [icon('sleep'), value('23:45'), slash(), value('07:20')],
  // feature_value_composite.c case 105
  105: [value('Mon'), space(), value('23'), space(), value('Sep'), icon('sun'), value('19:42')],
  // feature_value_date_compute case 106
  106: [value('Mon'), space(), value('23'), space(), value('Sep'), space(), label('WK'), value('34')]
};

// Timezone cluster (44-62): every entry's own preview already follows
// "ABBR HH:MM" (see feature_value_timezone_compute's atomic decomposition),
// so this splits generically instead of hand-writing 19 near-identical
// table rows.
function timezoneChips(previewText) {
  var spaceIdx = previewText.indexOf(' ');
  if (spaceIdx < 0) return [value(previewText)];
  return [value(previewText.slice(0, spaceIdx)), space(), value(previewText.slice(spaceIdx + 1))];
}

// Returns the ordered chip list for a content id. Falls back to a single
// 'value' chip (that content's own CORNER_CATEGORIES preview/label) for
// every content id not listed above -- an accurate fallback, not a
// placeholder: those contents really are still one primitive on the watch
// (Section 4's STATIC_TEXT is a legitimate resting state, not a gap) --
// see IMPLEMENTATION_NOTES.md's Update 8-10 for exactly which ids have and
// haven't been decomposed further.
function primitiveChipsForContent(contentId) {
  var id = parseInt(contentId, 10);
  if (id === 0) return [];
  if (ATOMIC_DECOMPOSITIONS[id]) return ATOMIC_DECOMPOSITIONS[id];
  var item = CONTENT_ITEM_BY_ID[id];
  if (id >= 44 && id <= 62) return timezoneChips(item && item.preview ? item.preview : '');
  if (!item) return [];
  return [value(item.preview || item.label)];
}

// ---- Phase 3: real primitive-sequence transport -------------------------
//
// Numeric primitive-ID sequences (see config/primitive-ids-generated.js,
// generated from the canonical C enum) for the content ids the watch's
// primitive_resolver.c actually knows how to resolve every primitive of --
// see that file's own header for exactly which primitive IDs it covers
// and why the set below is smaller than ATOMIC_DECOMPOSITIONS above.
//
// content 4's sequence deliberately always uses the 5-primitive form (with
// an explicit space between the high and low halves) regardless of what
// color_mode the slot currently has -- the legacy watch code used a
// different, space-less 4-primitive form specifically for COLOR mode (see
// feature_value_weather.c's case 4), but a transmitted sequence is fixed
// at content-selection time and can't vary with a setting that changes
// independently later. The watch's feature_value_resolve_flat_color()
// dispatch (MONO/ACC/PILL/COLOR) still applies per primitive either way,
// so this is a cosmetic-only difference (a slightly different gap width),
// not a functional one -- documented in IMPLEMENTATION_NOTES.md's Phase 3
// update too.
var PRIMITIVE_TRANSPORT_SEQUENCES = {
  4: [PRIM.PRIM_LABEL_HIGH_PREFIX, PRIM.PRIM_TEMP_HIGH_TEXT, PRIM.PRIM_SPACE, PRIM.PRIM_LABEL_LOW_PREFIX, PRIM.PRIM_TEMP_LOW_TEXT],
  12: [PRIM.PRIM_WEEKDAY_SHORT_MIXED, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH],
  21: [PRIM.PRIM_MONTH_SHORT_UPPER, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH],
  27: [PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_MONTH_NUMBER],
  28: [PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_DAY_OF_MONTH],
  29: [PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_YEAR_FULL],
  30: [PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_YEAR_SHORT],
  98: [PRIM.PRIM_WEEKDAY_SHORT_UPPER, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SLASH, PRIM.PRIM_MONTH_NUMBER],
  99: [PRIM.PRIM_WEEKDAY_SHORT_UPPER, PRIM.PRIM_SPACE, PRIM.PRIM_MONTH_NUMBER, PRIM.PRIM_SLASH, PRIM.PRIM_DAY_OF_MONTH],

  // Unblocked by the color-ownership principle (see this file's header and
  // IMPLEMENTATION_NOTES.md's Phase 3 update): these all used to need a
  // *different* canonical color for the same primitive ID depending on
  // which content composed it. Now every date-component primitive shares
  // one watch-decided gradient regardless of context, and every weather
  // primitive resolves its own PKJS-decided color regardless of context --
  // so there's no more ambiguity to avoid.
  19: [PRIM.PRIM_LABEL_WK_PREFIX, PRIM.PRIM_SPACE, PRIM.PRIM_WEEK_NUMBER],
  22: [PRIM.PRIM_DAY_OF_MONTH],
  23: [PRIM.PRIM_WEEKDAY_SHORT_UPPER],
  24: [PRIM.PRIM_WEEKDAY_LONG],
  25: [PRIM.PRIM_MONTH_SHORT_UPPER],
  26: [PRIM.PRIM_MONTH_LONG],
  76: [PRIM.PRIM_WEATHER_CONDITION_ICON, PRIM.PRIM_CURRENT_TEMP_TEXT, PRIM.PRIM_SPACE,
       PRIM.PRIM_LABEL_HIGH_PREFIX, PRIM.PRIM_TEMP_HIGH_TEXT, PRIM.PRIM_SPACE,
       PRIM.PRIM_LABEL_LOW_PREFIX, PRIM.PRIM_TEMP_LOW_TEXT],
  106: [PRIM.PRIM_WEEKDAY_SHORT_MIXED, PRIM.PRIM_SPACE, PRIM.PRIM_DAY_OF_MONTH, PRIM.PRIM_SPACE,
        PRIM.PRIM_MONTH_SHORT_MIXED, PRIM.PRIM_SPACE, PRIM.PRIM_LABEL_WK_PREFIX, PRIM.PRIM_WEEK_NUMBER],

  // Health/status cluster (all watch-runtime, no color-ownership conflicts
  // -- see feature_value_health_compute() and primitive_resolver.c's own
  // per-case comments). Each is icon+text (or icon-only/text-only) exactly
  // as feature_value_helpers.c's feature_value_slot_set()/
  // feature_value_set_icon_segment()+feature_value_set_text_segment()
  // pairs already produce, so no further decomposition was needed --
  // these were already atomic in Phase 8's sense.
  1: [PRIM.PRIM_HEART_RATE_ICON, PRIM.PRIM_HEART_RATE_VALUE],
  2: [PRIM.PRIM_STEPS_ICON, PRIM.PRIM_STEPS_VALUE],
  3: [PRIM.PRIM_STEPS_ICON, PRIM.PRIM_STEP_GOAL_PCT],
  10: [PRIM.PRIM_BATTERY_ICON, PRIM.PRIM_BATTERY_PCT_TEXT],
  17: [PRIM.PRIM_BATTERY_LOGO_ICON],
  20: [PRIM.PRIM_BLUETOOTH_ICON, PRIM.PRIM_BLUETOOTH_STATUS_TEXT],
  78: [PRIM.PRIM_BLUETOOTH_ICON],
  108: [PRIM.PRIM_QUIET_TIME_ICON],
  109: [PRIM.PRIM_QUIET_TIME_ICON, PRIM.PRIM_QUIET_TIME_STATUS_TEXT],
  39: [PRIM.PRIM_SLEEP_TOTAL_ICON, PRIM.PRIM_SLEEP_TOTAL_TEXT],
  40: [PRIM.PRIM_SLEEP_RESTFUL_ICON, PRIM.PRIM_SLEEP_RESTFUL_TEXT],
  41: [PRIM.PRIM_SLEEP_QUALITY_ICON, PRIM.PRIM_SLEEP_QUALITY_PCT],
  42: [PRIM.PRIM_BED_TIME_ICON, PRIM.PRIM_BED_TIME_TEXT],
  43: [PRIM.PRIM_WAKE_TIME_ICON, PRIM.PRIM_WAKE_TIME_TEXT],

  // Weather singles (feature_value_weather.c cases 73/74/75/77) -- each a
  // distinct primitive ID from content 4/32/76's, so no conflict with the
  // temperature cluster above.
  73: [PRIM.PRIM_CURRENT_TEMP_ONLY_TEXT],
  74: [PRIM.PRIM_TEMP_HIGH_ONLY_TEXT],
  75: [PRIM.PRIM_TEMP_LOW_ONLY_TEXT],
  77: [PRIM.PRIM_FEELS_LIKE_TEXT]
};

// Timezone cluster (content 44-62): the one case in this table where a
// primitive needs a per-position *parameter*, not just an ID -- which of
// the 19 zones. Every entry uses the identical 5-primitive shape (abbr,
// space, time, space, am/pm); PRIM_TIMEZONE_AMPM resolves to an empty,
// zero-width primitive in 24h clock style rather than being included or
// omitted conditionally, since the sequence itself can't react to a
// runtime setting once sent -- see primitive_resolver.c's own comment.
// Built programmatically (19 near-identical rows) rather than hand-
// written, same reasoning as generate-primitive-ids.js: fewer places for
// the zone-index arithmetic to drift.
for (var tzContentId = 44; tzContentId <= 62; tzContentId++) {
  PRIMITIVE_TRANSPORT_SEQUENCES[tzContentId] = [
    PRIM.PRIM_TIMEZONE_ABBR, PRIM.PRIM_SPACE, PRIM.PRIM_TIMEZONE_TIME, PRIM.PRIM_SPACE, PRIM.PRIM_TIMEZONE_AMPM
  ];
}

// Per-position aux byte for the sequences above -- see
// eclipse_data.h's corner_primitive_aux comment. Sparse: only content ids
// whose primitives need a non-zero aux appear here; every other content
// id (and every position within it) is implicitly all-zero, which is
// exactly right since none of the primitives used elsewhere in this file
// read their aux at all.
var PRIMITIVE_TRANSPORT_AUX = {};
for (var tzAuxContentId = 44; tzAuxContentId <= 62; tzAuxContentId++) {
  var zoneIdx = tzAuxContentId - 44; // matches feature_timezone_get()'s own index space
  PRIMITIVE_TRANSPORT_AUX[tzAuxContentId] = [zoneIdx, 0, zoneIdx, 0, zoneIdx, 0, 0, 0, 0, 0];
}

var PRIMITIVE_SEQUENCE_SLOT_COUNT = 10; // must match PRIMITIVE_FEATURE_MAX (primitives/primitive_storage.h)
var PRIMITIVE_TRANSPORT_CONTENT_IDS = Object.keys(PRIMITIVE_TRANSPORT_SEQUENCES).map(Number);

// Zero-padded to PRIMITIVE_SEQUENCE_SLOT_COUNT bytes (PRIM_ID_NONE == 0 is
// the watch's own "sequence ends here, or was never sent" signal -- see
// eclipse_data.h's corner_primitive_ids and primitive_transport.c). A
// content id not in PRIMITIVE_TRANSPORT_SEQUENCES returns all zeros, which
// tells the watch to fall back to primitive_bridge.c's legacy content-
// based path for that slot, exactly as if this feature didn't exist.
function primitiveIdSequenceForContent(contentId) {
  var seq = PRIMITIVE_TRANSPORT_SEQUENCES[parseInt(contentId, 10)] || [];
  var out = seq.slice(0, PRIMITIVE_SEQUENCE_SLOT_COUNT);
  while (out.length < PRIMITIVE_SEQUENCE_SLOT_COUNT) out.push(0);
  return out;
}

// Companion to primitiveIdSequenceForContent(): the per-position aux byte
// for that same content id (see PRIMITIVE_TRANSPORT_AUX above). All-zero
// for every content id with no entry there, which is correct by
// construction -- those primitives never read their aux.
function primitiveAuxSequenceForContent(contentId) {
  var aux = PRIMITIVE_TRANSPORT_AUX[parseInt(contentId, 10)] || [];
  var out = aux.slice(0, PRIMITIVE_SEQUENCE_SLOT_COUNT);
  while (out.length < PRIMITIVE_SEQUENCE_SLOT_COUNT) out.push(0);
  return out;
}

// Clamps/pads an arbitrary (person-composed) array of numeric primitive
// IDs to exactly PRIMITIVE_SEQUENCE_SLOT_COUNT bytes the same way -- used
// for a custom sequence (Phase 9's composer, config-page.js) instead of
// one derived from a single content id.
function normalizePrimitiveSequence(ids) {
  var out = (ids || []).slice(0, PRIMITIVE_SEQUENCE_SLOT_COUNT);
  while (out.length < PRIMITIVE_SEQUENCE_SLOT_COUNT) out.push(0);
  return out;
}

// ---- Phase 9: composable primitive palette --------------------------
//
// Only primitive IDs primitive_resolver.c actually resolves -- offering
// anything else would let a person build a sequence that silently falls
// back to a blank/legacy-content render on the watch (primitive_transport.c
// bails the whole slot the moment one ID can't be resolved), which would
// look like a bug rather than a real limit. Grouped and labeled for the
// palette UI; kept in the same file as the transport sequences above so
// adding resolver coverage for a new primitive is a one-place update
// (add it here, and to primitive_resolver.c on the watch side).
var PRIMITIVE_PALETTE = [
  { group: 'Spacers', items: [
    { id: PRIM.PRIM_SPACE, label: 'Space' },
    { id: PRIM.PRIM_SLASH, label: 'Slash (/)' }
  ]},
  { group: 'Date & time', items: [
    { id: PRIM.PRIM_WEEKDAY_SHORT_MIXED, label: 'Weekday (Mon)' },
    { id: PRIM.PRIM_WEEKDAY_SHORT_UPPER, label: 'Weekday (MON)' },
    { id: PRIM.PRIM_WEEKDAY_LONG, label: 'Weekday (Monday)' },
    { id: PRIM.PRIM_MONTH_SHORT_UPPER, label: 'Month (SEP)' },
    { id: PRIM.PRIM_MONTH_SHORT_MIXED, label: 'Month (Sep)' },
    { id: PRIM.PRIM_MONTH_LONG, label: 'Month (September)' },
    { id: PRIM.PRIM_DAY_OF_MONTH, label: 'Day of month' },
    { id: PRIM.PRIM_MONTH_NUMBER, label: 'Month number' },
    { id: PRIM.PRIM_YEAR_FULL, label: 'Year (2026)' },
    { id: PRIM.PRIM_YEAR_SHORT, label: 'Year (26)' },
    { id: PRIM.PRIM_LABEL_WK_PREFIX, label: '"WK" label' },
    { id: PRIM.PRIM_WEEK_NUMBER, label: 'Week number' }
  ]},
  { group: 'Weather', items: [
    { id: PRIM.PRIM_WEATHER_CONDITION_ICON, label: 'Weather icon' },
    { id: PRIM.PRIM_CURRENT_TEMP_TEXT, label: 'Current temp (bare)' },
    { id: PRIM.PRIM_LABEL_HIGH_PREFIX, label: '"H" label' },
    { id: PRIM.PRIM_TEMP_HIGH_TEXT, label: 'High temp' },
    { id: PRIM.PRIM_LABEL_LOW_PREFIX, label: '"L" label' },
    { id: PRIM.PRIM_TEMP_LOW_TEXT, label: 'Low temp' },
    { id: PRIM.PRIM_CURRENT_TEMP_ONLY_TEXT, label: 'Current temp' },
    { id: PRIM.PRIM_TEMP_HIGH_ONLY_TEXT, label: 'High temp ("H 72")' },
    { id: PRIM.PRIM_TEMP_LOW_ONLY_TEXT, label: 'Low temp ("L 58")' },
    { id: PRIM.PRIM_FEELS_LIKE_TEXT, label: 'Feels-like temp' }
  ]},
  { group: 'Health & status', items: [
    { id: PRIM.PRIM_HEART_RATE_ICON, label: 'Heart rate icon' },
    { id: PRIM.PRIM_HEART_RATE_VALUE, label: 'Heart rate BPM' },
    { id: PRIM.PRIM_STEPS_ICON, label: 'Steps icon' },
    { id: PRIM.PRIM_STEPS_VALUE, label: 'Steps count' },
    { id: PRIM.PRIM_STEP_GOAL_PCT, label: 'Step goal %' },
    { id: PRIM.PRIM_BATTERY_ICON, label: 'Battery icon' },
    { id: PRIM.PRIM_BATTERY_PCT_TEXT, label: 'Battery %' },
    { id: PRIM.PRIM_BATTERY_LOGO_ICON, label: 'Pebble battery logo' },
    { id: PRIM.PRIM_BLUETOOTH_ICON, label: 'Bluetooth icon' },
    { id: PRIM.PRIM_BLUETOOTH_STATUS_TEXT, label: 'Bluetooth status' },
    { id: PRIM.PRIM_QUIET_TIME_ICON, label: 'Quiet Time icon' },
    { id: PRIM.PRIM_QUIET_TIME_STATUS_TEXT, label: 'Quiet Time ON/OFF' },
    { id: PRIM.PRIM_SLEEP_TOTAL_ICON, label: 'Sleep icon' },
    { id: PRIM.PRIM_SLEEP_TOTAL_TEXT, label: 'Sleep duration' },
    { id: PRIM.PRIM_SLEEP_RESTFUL_ICON, label: 'Restful sleep icon' },
    { id: PRIM.PRIM_SLEEP_RESTFUL_TEXT, label: 'Restful sleep duration' },
    { id: PRIM.PRIM_SLEEP_QUALITY_ICON, label: 'Sleep quality icon' },
    { id: PRIM.PRIM_SLEEP_QUALITY_PCT, label: 'Sleep quality %' },
    { id: PRIM.PRIM_BED_TIME_ICON, label: 'Bed time icon' },
    { id: PRIM.PRIM_BED_TIME_TEXT, label: 'Bed time' },
    { id: PRIM.PRIM_WAKE_TIME_ICON, label: 'Wake time icon' },
    { id: PRIM.PRIM_WAKE_TIME_TEXT, label: 'Wake time' }
  ]}
];
var PRIMITIVE_LABEL_BY_ID = {};
PRIMITIVE_PALETTE.forEach(function (g) {
  g.items.forEach(function (it) { PRIMITIVE_LABEL_BY_ID[it.id] = it.label; });
});

module.exports = {
  primitiveChipsForContent: primitiveChipsForContent,
  ATOMIC_DECOMPOSITIONS: ATOMIC_DECOMPOSITIONS,
  PRIMITIVE_TRANSPORT_CONTENT_IDS: PRIMITIVE_TRANSPORT_CONTENT_IDS,
  PRIMITIVE_TRANSPORT_SEQUENCES: PRIMITIVE_TRANSPORT_SEQUENCES,
  PRIMITIVE_TRANSPORT_AUX: PRIMITIVE_TRANSPORT_AUX,
  primitiveIdSequenceForContent: primitiveIdSequenceForContent,
  primitiveAuxSequenceForContent: primitiveAuxSequenceForContent,
  normalizePrimitiveSequence: normalizePrimitiveSequence,
  PRIMITIVE_PALETTE: PRIMITIVE_PALETTE,
  PRIMITIVE_LABEL_BY_ID: PRIMITIVE_LABEL_BY_ID
};