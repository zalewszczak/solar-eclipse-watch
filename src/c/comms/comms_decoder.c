#include <pebble.h>
#include <stddef.h>
#include <string.h>
#include "./comms_decoder.h"
#include "../generated/message_key_index.h"
#include "../data/hand_types.h"
#include "../data/marker_types.h"

// ---- Why (almost) everything in here is table-driven -------------------
// This app has ~127 AppMessage keys. Written out longhand, each one costs
// roughly 26 bytes of Thumb-2 (`ldr` the key's address out of the literal
// pool, `ldr` the key, `bl dict_find`, test, load, store) PLUS a 4-byte
// `MESSAGE_KEY_*` word in .data, PLUS a relocation entry in the .pbw for
// the literal-pool pointer to it -- call it ~34 bytes of binary per key
// before any actual field logic runs.
//
// So the rule in this file is: a key gets hand-written code only when it
// needs something a table genuinely cannot express (a value transform, a
// derived field, a saturating clamp against a large limit). Everything
// else is a row and costs 4 or 6 bytes of .rodata.
//
// The tables store an *index* (MK_*, from the generated header) rather
// than the key itself, because MESSAGE_KEY_* are extern variables
// assigned at link time and so can't appear in a `static const`
// initializer, while MK_* is a plain integer literal. The real key is
// MESSAGE_KEY_MESSAGE_TYPE + MK_*, recovered at runtime -- one GOT load
// for the whole decode instead of one per key. See message_key_index.h,
// and the generator script, which asserts messageKeys[0] ==
// "MESSAGE_TYPE" at generation time so this arithmetic cannot silently
// drift (it used to be re-checked at every app launch, which cost more
// binary than the bug it guarded against ever could).

#define NELEM(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

// ---- Table 1: plain-copy fields ---------------------------------------
// "Copy this value into this EclipseData field, with this one
// conversion, and optionally raise one change flag." No derived state,
// no bookkeeping beyond the flag.
//
// Low nibble of `type_flag` is the SimpleFieldType; the high nibble is
// the change flag, stored as (bit index + 1) so that 0 means "no flag"
// and the flag can be recovered branchlessly as (1u << nibble) >> 1.

typedef enum {
  F_U8 = 0,     // d->FIELD = t->value->uint8;
  F_BOOL,       // d->FIELD = t->value->uint8 != 0;
  F_I16,        // d->FIELD = t->value->int16;
  F_U16,        // d->FIELD = t->value->uint16;
  F_U32,        // d->FIELD = t->value->uint32;
  F_TIME,       // d->FIELD = (time_t)t->value->int32;
  // Radio-style settings: any byte past the last valid option falls back
  // to 0 ("off"). The limit is baked into the type so a row stays 4
  // bytes; only these two limits exist today, add another value here if
  // a third ever shows up. Keep these last -- the switch below treats
  // everything from F_U8_MAX2 on as "clamped uint8".
  F_U8_MAX2,    // d->FIELD = (v <= 2) ? v : 0;
  F_U8_MAX3,    // d->FIELD = (v <= 3) ? v : 0;
} SimpleFieldType;

// Change flags, pre-shifted into the high nibble: FL_x == (bit index + 1) << 4.
#define FL_NONE   (0u << 4)
#define FL_FONT   (1u << 4)
#define FL_LAYOUT (2u << 4)
#define FL_HANDS  (3u << 4)
#define FL_CANVAS (4u << 4)
#define FL_PANEL  (5u << 4)

#define FLAG_BITS(tf) ((uint32_t)(1u << ((tf) >> 4)) >> 1)

_Static_assert(FLAG_BITS(FL_NONE)   == 0,                       "FL_NONE must raise nothing");
_Static_assert(FLAG_BITS(FL_FONT)   == COMMS_CHANGE_CLOCK_FONT, "FL_FONT out of sync with comms.h");
_Static_assert(FLAG_BITS(FL_LAYOUT) == COMMS_CHANGE_LAYOUT,     "FL_LAYOUT out of sync with comms.h");
_Static_assert(FLAG_BITS(FL_HANDS)  == COMMS_CHANGE_HANDS,      "FL_HANDS out of sync with comms.h");
_Static_assert(FLAG_BITS(FL_CANVAS) == COMMS_CHANGE_CANVAS,     "FL_CANVAS out of sync with comms.h");
_Static_assert(FLAG_BITS(FL_PANEL)  == COMMS_CHANGE_PANEL,      "FL_PANEL out of sync with comms.h");

typedef struct {
  uint8_t  key_index; // MESSAGE_KEY_MESSAGE_TYPE + key_index == the real key
  uint8_t  type_flag; // SimpleFieldType | FL_*
  uint16_t offset;    // offsetof(EclipseData, field); EclipseData is ~1.2 KB, fits u16
} SimpleFieldMapping; // 4 bytes

// Applied on every message, valid payload or not: settings have to land
// even before the watch has ever received eclipse data.
static const SimpleFieldMapping SIMPLE_FIELD_MAP[] = {
  { MK_DATA_VALID, F_BOOL, offsetof(EclipseData, valid) },
  { MK_ERROR_CODE, F_U8, offsetof(EclipseData, error_code) },
  { MK_TEMP_UNIT, F_U8, offsetof(EclipseData, temp_unit) },
  { MK_WIND_SPEED_UNIT, F_U8, offsetof(EclipseData, wind_speed_unit) },
  { MK_SHAKE_LABEL_SECONDS, F_U8, offsetof(EclipseData, shake_label_seconds) },
  { MK_VIBRATE_ON_PHASE_CHANGE, F_BOOL, offsetof(EclipseData, vibrate_on_phase_change) },
  { MK_STARTUP_CLOCK_ANIM_MODE, F_U8, offsetof(EclipseData, startup_clock_anim_mode) },
  { MK_OUTLINE_ENABLED, F_U8, offsetof(EclipseData, outline_style) },
  { MK_BATTERY_SAVER_ENABLED, F_BOOL, offsetof(EclipseData, battery_saver_enabled) },
  { MK_CORNER_FONT, F_U8, offsetof(EclipseData, corner_font) },
  { MK_CENTER_CIRCLE_RADIUS, F_U8, offsetof(EclipseData, center_circle_radius) },
  { MK_CENTER_CIRCLE_COLOR, F_U8, offsetof(EclipseData, center_circle_color) },
  { MK_BITMAP_MARKER_TRANSPARENT, F_BOOL, offsetof(EclipseData, bitmap_marker_transparent) },
  { MK_DAILY_STEP_GOAL, F_U16, offsetof(EclipseData, daily_step_goal) },
  { MK_C1_TIME, F_TIME, offsetof(EclipseData, c1) },
  { MK_C2_TIME, F_TIME, offsetof(EclipseData, c2) },
  { MK_MAX_TIME, F_TIME, offsetof(EclipseData, max_t) },
  { MK_C3_TIME, F_TIME, offsetof(EclipseData, c3) },
  { MK_C4_TIME, F_TIME, offsetof(EclipseData, c4) },
  { MK_SUNSET_TIME, F_TIME, offsetof(EclipseData, sunset) },
  { MK_MAGNITUDE, F_U8, offsetof(EclipseData, magnitude_pct) },
  { MK_POS_ANGLE, F_I16, offsetof(EclipseData, pos_angle_deg) },
  { MK_SAMPLE_START, F_TIME, offsetof(EclipseData, sample_start) },
  { MK_SAMPLE_INTERVAL, F_U32, offsetof(EclipseData, sample_interval_s) },
  { MK_RADIUS_RATIO_PCT, F_U8, offsetof(EclipseData, radius_ratio_pct) },
  { MK_CLOUD_COVER, F_U8, offsetof(EclipseData, cloud_cover_pct) },
  { MK_VIS_SCORE, F_U8, offsetof(EclipseData, vis_score_pct) },
  { MK_WEATHER_SOURCES, F_U8, offsetof(EclipseData, weather_sources) },
  { MK_WEATHER_CONDITION, F_U8, offsetof(EclipseData, weather_condition) },
  { MK_WIND_DIR_DEG, F_I16, offsetof(EclipseData, wind_dir_deg) },
  { MK_DEW_POINT_C, F_I16, offsetof(EclipseData, dew_point_c) },
  { MK_PRESSURE_HPA, F_I16, offsetof(EclipseData, pressure_hpa) },
  { MK_PRESSURE_TREND, F_U8, offsetof(EclipseData, pressure_trend) },
  { MK_AQI_US, F_U16, offsetof(EclipseData, aqi_us) },
  { MK_AQI_EU, F_U16, offsetof(EclipseData, aqi_eu) },
  { MK_ALTITUDE_M, F_I16, offsetof(EclipseData, altitude_m) },
  { MK_AURORA_KP_X10, F_U8, offsetof(EclipseData, aurora_kp_x10) },
  { MK_AURORA_ERROR_CODE, F_U8, offsetof(EclipseData, aurora_error_code) },
  { MK_WEATHER_TEMP_C, F_I16, offsetof(EclipseData, weather_temp_c) },
  { MK_WEATHER_TEMP_HIGH_C, F_I16, offsetof(EclipseData, temp_high_c) },
  { MK_WEATHER_TEMP_LOW_C, F_I16, offsetof(EclipseData, temp_low_c) },
  { MK_UV_INDEX_X10, F_U8, offsetof(EclipseData, uv_index_x10) },
  { MK_UV_INDEX_CURRENT_X10, F_U8, offsetof(EclipseData, uv_index_current_x10) },
  { MK_RAIN_CHANCE_PCT, F_U8, offsetof(EclipseData, rain_chance_pct) },
  { MK_HUMIDITY_PCT, F_U8, offsetof(EclipseData, humidity_pct) },
  { MK_WIND_SPEED_KMH, F_I16, offsetof(EclipseData, wind_speed_kmh) },
  { MK_SKY_SAMPLE_START, F_TIME, offsetof(EclipseData, sky_sample_start) },
  { MK_SKY_SAMPLE_INTERVAL, F_U32, offsetof(EclipseData, sky_sample_interval_s) },
  { MK_CLOUD_ALTITUDE_PCT, F_U8, offsetof(EclipseData, cloud_altitude_pct) },
  { MK_SATURN_RING_OPEN_PCT, F_U8, offsetof(EclipseData, saturn_ring_open_pct) },
  { MK_SKY_SCALE_MAX_ALT, F_I16, offsetof(EclipseData, sky_scale_max_alt_decideg) },
  { MK_MOON_PHASE_PCT, F_U8, offsetof(EclipseData, moon_phase_pct) },
  { MK_MOON_WAXING, F_BOOL, offsetof(EclipseData, moon_waxing) },
  { MK_SUN_RISE, F_TIME, offsetof(EclipseData, sun_rise) },
  { MK_SUN_SET, F_TIME, offsetof(EclipseData, sun_set) },
  { MK_SUN_RISE_TOMORROW, F_TIME, offsetof(EclipseData, sun_rise_tomorrow) },
  { MK_MOON_RISE, F_TIME, offsetof(EclipseData, moon_rise) },
  { MK_MOON_SET, F_TIME, offsetof(EclipseData, moon_set) },
  { MK_METEOR_INTENSITY, F_U8, offsetof(EclipseData, meteor_intensity) },
  { MK_ISS_ALT, F_I16, offsetof(EclipseData, iss_alt_deg) },
  { MK_ISS_AZ, F_U16, offsetof(EclipseData, iss_az_deg) },
  { MK_ISS_COMPUTED_AT, F_TIME, offsetof(EclipseData, iss_computed_at) },
  { MK_ISS_NEXT_PASS, F_TIME, offsetof(EclipseData, iss_next_pass) },
  { MK_ISS_ERROR_CODE, F_U8, offsetof(EclipseData, iss_error_code) },
  { MK_WEATHER_LAST_UPDATE, F_TIME, offsetof(EclipseData, weather_last_update) },
  { MK_DRAW_DEBUG, F_BOOL, offsetof(EclipseData, draw_debug) },
  // Deliberately separate simple fields, NOT part of the MARKER_RINGS
  // blob -- see custom_hour_marker_inner_thickness's own comment in
  // eclipse_data.h for why (MarkerRingConfig's size is wire-load-
  // bearing; these two exist for the "tapered" ring style, added well
  // after that blob's format was fixed).
  { MK_CUSTOM_HOUR_INNER_THICKNESS, F_U8, offsetof(EclipseData, custom_hour_marker_inner_thickness) },
  { MK_CUSTOM_SEC_INNER_THICKNESS, F_U8, offsetof(EclipseData, custom_second_marker_inner_thickness) },
  { MK_HOURLY_VIBE_MODE, F_U8, offsetof(EclipseData, hourly_vibe_mode) },
  { MK_HOURLY_VIBE_INTERVAL_MIN, F_U8, offsetof(EclipseData, hourly_vibe_interval_min) },
  { MK_HOURLY_VIBE_PATTERN, F_U8, offsetof(EclipseData, hourly_vibe_pattern) },
  { MK_HOURLY_VIBE_START_MIN, F_U16, offsetof(EclipseData, hourly_vibe_start_min) },
  { MK_HOURLY_VIBE_END_MIN, F_U16, offsetof(EclipseData, hourly_vibe_end_min) },
  { MK_HOURLY_VIBE_DAYS_MASK, F_U8, offsetof(EclipseData, hourly_vibe_days_mask) },
  { MK_HOURLY_VIBE_OVERRIDE_QUIET, F_BOOL, offsetof(EclipseData, hourly_vibe_override_quiet) },
  // Settings that additionally need a redraw or a relayout. These were
  // hand-written `if (dict_find(...))` blocks purely because of the
  // trailing `changes |= ...`, which the high nibble now carries.
  { MK_CLOCK_FONT, F_U8 | FL_FONT, offsetof(EclipseData, clock_font) },
  { MK_SHOW_SECONDS, F_BOOL | FL_HANDS, offsetof(EclipseData, show_seconds) },
  { MK_BOTTOM_STYLE, F_U8 | FL_LAYOUT, offsetof(EclipseData, bottom_style) },
  { MK_SUN_MOON_SIZE_PCT, F_U8 | FL_CANVAS, offsetof(EclipseData, sun_moon_size_pct) },
  { MK_SKY_MODE, F_U8 | FL_CANVAS, offsetof(EclipseData, sky_mode) },
  { MK_LABEL_STYLE, F_U8 | FL_CANVAS, offsetof(EclipseData, label_style) },
  { MK_BG_ANIM_MODE, F_U8_MAX2, offsetof(EclipseData, bg_anim_mode) },
  { MK_SHAKE_ANIM_MODE, F_U8_MAX3, offsetof(EclipseData, shake_anim_mode) },
  { MK_SHADOW_TRANSLUCENT, F_BOOL | FL_HANDS, offsetof(EclipseData, shadow_translucent) },
  { MK_SHADOW_ANGLE, F_U16 | FL_HANDS, offsetof(EclipseData, shadow_angle_deg) },
  { MK_DRAW_FEATURES_BENEATH_HANDS, F_BOOL | FL_LAYOUT, offsetof(EclipseData, draw_features_beneath_hands) },
  { MK_BIG_ANALOG_MARKER_STYLE, F_U8 | FL_HANDS, offsetof(EclipseData, big_analog_marker_style) },
  { MK_SHOW_SUN_TIME, F_BOOL | FL_PANEL, offsetof(EclipseData, show_sun_time) },
  { MK_SHOW_ISS, F_BOOL | FL_CANVAS, offsetof(EclipseData, show_iss) },
  { MK_SHOW_MAJOR_STARS, F_BOOL | FL_CANVAS, offsetof(EclipseData, show_major_stars) },
  { MK_AURORA_ENABLED, F_BOOL | FL_CANVAS, offsetof(EclipseData, aurora_enabled) },
  { MK_NIGHT_SCHEME_ENABLED, F_BOOL | FL_PANEL, offsetof(EclipseData, night_scheme_enabled) },
};

// Same shape, but applied only once a valid payload is confirmed --
// exactly where these four sat in the hand-written version.
static const SimpleFieldMapping SIMPLE_FIELD_MAP_VALID[] = {
  { MK_WEATHER_ICON_STYLE, F_U8, offsetof(EclipseData, weather_icon_style) },
  { MK_AQI_UNIT, F_U8, offsetof(EclipseData, aqi_unit) },
  { MK_AURORA_VISIBILITY_PCT, F_U8 | FL_CANVAS, offsetof(EclipseData, aurora_visibility_pct) },
  { MK_ALTITUDE_UNIT, F_U8, offsetof(EclipseData, altitude_unit) },
};

static uint32_t apply_simple_fields(DictionaryIterator *iter, EclipseData *d,
                                    const SimpleFieldMapping *map, uint8_t count) {
  const uint32_t base = MESSAGE_KEY_MESSAGE_TYPE; // one GOT load, once, instead of per-row
  uint32_t changes = 0;
  for (uint8_t i = 0; i < count; i++) {
    const SimpleFieldMapping *m = &map[i];
    Tuple *st = dict_find(iter, base + m->key_index);
    if (!st) continue;
    uint8_t *dst = (uint8_t *)d + m->offset;
    uint8_t tf = m->type_flag;
    uint8_t type = tf & 0x0F;
    switch (type) {
      case F_U8:   *dst = st->value->uint8; break;
      case F_BOOL: *(bool *)dst = st->value->uint8 != 0; break;
      case F_I16:  *(int16_t *)dst = st->value->int16; break;
      case F_U16:  *(uint16_t *)dst = st->value->uint16; break;
      case F_U32:  *(uint32_t *)dst = st->value->uint32; break;
      case F_TIME: *(time_t *)dst = (time_t)st->value->int32; break;
      default: { // F_U8_MAX2 / F_U8_MAX3
        uint8_t v = st->value->uint8;
        uint8_t limit = (uint8_t)(type - F_U8_MAX2 + 2);
        *dst = (v <= limit) ? v : 0;
        break;
      }
    }
    changes |= FLAG_BITS(tf); // 0 when the high nibble is FL_NONE
  }
  return changes;
}

// ---- Table 2: byte-blob array fields ----------------------------------
// Every one of these used to be its own hand-rolled loop, several of
// them reassembling little-endian uint16/uint32 values a byte at a time
// -- which, on a little-endian Cortex-M reading a little-endian wire
// format, is precisely what memcpy already does, for a fraction of the
// code. All that differs between them is destination, element size and
// cap, so they are rows now.
//
// Wire contract (unchanged): PKJS sends each of these as a packed
// little-endian byte blob; anything past the destination array's
// capacity is dropped, and a trailing partial element is ignored.

typedef struct {
  uint8_t  key_index;
  uint8_t  elem_size; // 1, 2 or 4 -- must be a power of two, see the mask below
  uint16_t offset;    // offsetof(EclipseData, array)
  uint16_t max_bytes; // sizeof that array
} BlobFieldMapping;   // 6 bytes

_Static_assert(sizeof(time_t) == 4, "PLANET_RISE/SET wire format assumes a 4-byte time_t");

// Applied on every message, like SIMPLE_FIELD_MAP above.
static const BlobFieldMapping BLOB_FIELD_MAP[] = {
  { MK_CORNER_CONTENT, 1, offsetof(EclipseData, corner_content), 4 },
  { MK_CORNER_COLOR_MODE, 1, offsetof(EclipseData, corner_color_mode), 4 },
};

// Applied only for valid payloads.
static const BlobFieldMapping BLOB_FIELD_MAP_VALID[] = {
  { MK_SEP_SAMPLES, 2, offsetof(EclipseData, sep_samples_centideg), MAX_SEP_SAMPLES * 2 },
  { MK_MAG_SAMPLES, 1, offsetof(EclipseData, mag_pct_samples), MAX_SEP_SAMPLES },
  { MK_FORECAST_CONDITION, 1, offsetof(EclipseData, forecast_condition), 6 },
  { MK_SUN_ALT_SAMPLES, 2, offsetof(EclipseData, sun_alt_decideg), MAX_SKY_SAMPLES * 2 },
  { MK_SUN_AZ_SAMPLES, 2, offsetof(EclipseData, sun_az_decideg), MAX_SKY_SAMPLES * 2 },
  { MK_CLOUD_SAMPLES, 1, offsetof(EclipseData, cloud_pct_samples), MAX_SKY_SAMPLES },
  { MK_MOON_ALT_SAMPLES, 2, offsetof(EclipseData, moon_alt_decideg), MAX_SKY_SAMPLES * 2 },
  { MK_MOON_AZ_SAMPLES, 2, offsetof(EclipseData, moon_az_decideg), MAX_SKY_SAMPLES * 2 },
  { MK_STAR_ALT_SAMPLES, 2, offsetof(EclipseData, star_alt_decideg), STAR_COUNT * 2 },
  { MK_STAR_AZ_SAMPLES, 2, offsetof(EclipseData, star_az_decideg), STAR_COUNT * 2 },
  { MK_PLANET_RISE, 4, offsetof(EclipseData, planet_rise), PLANET_COUNT * 4 },
  { MK_PLANET_SET, 4, offsetof(EclipseData, planet_set), PLANET_COUNT * 4 },
};

static void apply_blob_fields(DictionaryIterator *iter, EclipseData *d,
                              const BlobFieldMapping *map, uint8_t count) {
  const uint32_t base = MESSAGE_KEY_MESSAGE_TYPE;
  for (uint8_t i = 0; i < count; i++) {
    const BlobFieldMapping *m = &map[i];
    Tuple *t = dict_find(iter, base + m->key_index);
    if (!t) continue;
    uint16_t len = t->length;
    if (len > m->max_bytes) len = m->max_bytes;
    len &= (uint16_t)~(uint16_t)(m->elem_size - 1); // whole elements only
    memcpy((uint8_t *)d + m->offset, t->value->data, len);
  }
}

// PLANET_ALT_SAMPLES / PLANET_AZ_SAMPLES: PLANET_COUNT rows of
// little-endian int16 samples concatenated in PlanetId order (see
// data/eclipse_data.h) -- one message key instead of five near-duplicate
// ones. Not a plain blob row because the wire rows are packed tight
// while the destination rows are MAX_SKY_SAMPLES wide, so it needs a
// strided copy. The two keys differ only in signedness, which memcpy
// doesn't care about, so one helper serves both.
static void apply_planet_grid(DictionaryIterator *iter, uint8_t key_index, void *dst_rows) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_MESSAGE_TYPE + key_index);
  if (!t) return;
  int stride = (t->length / 2) / PLANET_COUNT;   // samples per planet on the wire
  int n = (stride > MAX_SKY_SAMPLES) ? MAX_SKY_SAMPLES : stride;
  const uint8_t *raw = t->value->data;
  uint8_t *dst = (uint8_t *)dst_rows;
  for (int p = 0; p < PLANET_COUNT; p++) {
    memcpy(dst + p * (MAX_SKY_SAMPLES * 2), raw + p * stride * 2, (size_t)n * 2);
  }
}

// LOCATION_NAME / METEOR_SHOWER_NAME -- same shape, so one helper.
static void apply_cstring(DictionaryIterator *iter, uint8_t key_index, char *dst, uint16_t cap) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_MESSAGE_TYPE + key_index);
  if (!t) return;
  strncpy(dst, t->value->cstring, cap - 1);
  dst[cap - 1] = '\0';
}

// ---- Consolidated settings fields --------------------------------------
// 5 grouped AppMessage keys replacing 86 individual ones:
//   HAND_HOUR_*/HAND_MIN_*/HAND_SEC_*                   -> HANDS        (42 B: 3x HandConfig)
//   CUSTOM_HOUR_*/CUSTOM_SEC_*                           -> MARKER_RINGS (16 B: 2x MarkerRingConfig)
//   the 16 edge-line content/color-mode keys             -> EDGE_LINES   (16 B: 16x uint8_t)
//   MARKER_TEXT_*                                        -> MARKER_TEXT  (8 B: packed, see below)
//   CUSTOM_BG/TEXT/ACCENT + NIGHT_CUSTOM_BG/TEXT/ACCENT  -> COLORS       (6 B: packed, see below)
//
// Three of the five are a straight memcpy; MARKER_TEXT and COLORS get an
// explicit byte-by-byte unpack instead:
//  - HandConfig/MarkerRingConfig and the 16 edge-line fields are flat,
//    contiguous, no-padding blocks in EclipseData -- see the
//    _Static_assert()s below, which exist so a struct-layout change that
//    would silently break the wire format fails the build instead of
//    silently scrambling everyone's saved hands/markers.
//  - MarkerTextConfig has two uint16_t masks with padding around them
//    (hour_mask/second_mask aren't at the packed-wire offsets a memcpy
//    would assume), so MARKER_TEXT is sent as 8 packed bytes and
//    unpacked by hand instead.
//  - COLORS is 6 explicit bytes rather than a memcpy of a 7-byte
//    contiguous run, specifically to avoid folding in
//    night_scheme_enabled (the bool that happens to sit between
//    custom_accent and night_custom_bg in the struct) -- that key stays
//    separate on the wire since it carries its own redraw side effect
//    (FL_PANEL in SIMPLE_FIELD_MAP) that this function doesn't want to
//    duplicate or silently drop.
_Static_assert(sizeof(HandConfig) == 14, "HANDS wire format assumes a 14-byte HandConfig");
_Static_assert(sizeof(MarkerRingConfig) == 8, "MARKER_RINGS wire format assumes an 8-byte MarkerRingConfig");

static void apply_consolidated_fields(DictionaryIterator *iter, EclipseData *d) {
  const uint32_t base = MESSAGE_KEY_MESSAGE_TYPE;
  Tuple *t;

  // HANDS: 3x HandConfig, in hour/minute/second order, each in the exact
  // field order HandConfig itself declares them (see hand_layer.h) --
  // src/pkjs/index.js's handsBytes() must build the array in that same
  // order for this memcpy to land correctly.
  if ((t = dict_find(iter, base + MK_HANDS)) && t->length >= 3 * sizeof(HandConfig)) {
    memcpy(&d->hand_hour, t->value->data, 3 * sizeof(HandConfig));
  }

  // MARKER_RINGS: 2x MarkerRingConfig, hour ring then second ring, each
  // in MarkerRingConfig's own declared field order (see data/eclipse_data.h).
  if ((t = dict_find(iter, base + MK_MARKER_RINGS)) && t->length >= 2 * sizeof(MarkerRingConfig)) {
    memcpy(&d->custom_hour_marker, t->value->data, 2 * sizeof(MarkerRingConfig));
  }

  // EDGE_LINES: the 16 upper/bottom/middle_left/middle_right line1/line2
  // content+color_mode uint8_t fields, in the exact order
  // eclipse_data.h declares them.
  if ((t = dict_find(iter, base + MK_EDGE_LINES)) && t->length >= 16) {
    memcpy(&d->upper_middle_line1_content, t->value->data, 16);
  }

  // MARKER_TEXT: target, font_choice, offset_px (signed byte), hour_mask
  // (u16 little-endian), second_mask (u16 little-endian), roman_numerals.
  if ((t = dict_find(iter, base + MK_MARKER_TEXT)) && t->length >= 8) {
    uint8_t *b = t->value->data;
    d->marker_text.target = b[0];
    d->marker_text.font_choice = b[1];
    d->marker_text.offset_px = (int8_t)b[2];
    d->marker_text.hour_mask = (uint16_t)(b[3] | (b[4] << 8));
    d->marker_text.second_mask = (uint16_t)(b[5] | (b[6] << 8));
    d->marker_text.roman_numerals = b[7] != 0;
  }

  // COLORS: custom_bg, custom_text, custom_accent, night_custom_bg,
  // night_custom_text, night_custom_accent -- night_scheme_enabled is
  // NOT included here, see this section's own comment above.
  if ((t = dict_find(iter, base + MK_COLORS)) && t->length >= 6) {
    uint8_t *b = t->value->data;
    d->custom_bg = b[0];
    d->custom_text = b[1];
    d->custom_accent = b[2];
    d->night_custom_bg = b[3];
    d->night_custom_text = b[4];
    d->night_custom_accent = b[5];
  }
}


CommsChangeFlags comms_decoder_apply(DictionaryIterator *iter, EclipseData *data) {
  EclipseData *d = data;
  const uint32_t base = MESSAGE_KEY_MESSAGE_TYPE;
  Tuple *t;

  // Every plain-copy setting, then the 5 grouped settings keys, then the
  // two unconditional byte arrays. Order between them doesn't matter --
  // no field here reads another -- but they all run before the explicit
  // blocks below so anything downstream sees this message's freshly
  // applied values rather than the previous message's.
  uint32_t changes = apply_simple_fields(iter, d, SIMPLE_FIELD_MAP, NELEM(SIMPLE_FIELD_MAP));
  apply_consolidated_fields(iter, d);
  apply_blob_fields(iter, d, BLOB_FIELD_MAP, NELEM(BLOB_FIELD_MAP));

  // features_layer_set_data() recomputes every slot's layout AND value
  // in one pass and marks the layer dirty, so the features overlay is
  // flagged once here rather than by each field that feeds it.
  changes |= COMMS_CHANGE_FEATURES;

  if (!d->valid) return changes;
  d->error_code = 0;

  if ((t = dict_find(iter, base + MK_ECLIPSE_TYPE))) {
    d->type = t->value->uint8;
    d->has_eclipse = d->type != ECLIPSE_TYPE_NONE;
  }
  // Saturating clamps against limits too large for the table's own
  // small-limit types, so these two keep their own blocks.
  if ((t = dict_find(iter, base + MK_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    d->sample_count = count > MAX_SEP_SAMPLES ? MAX_SEP_SAMPLES : count;
  }
  if ((t = dict_find(iter, base + MK_SKY_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    d->sky_sample_count = count > MAX_SKY_SAMPLES ? MAX_SKY_SAMPLES : count;
  }

  changes |= apply_simple_fields(iter, d, SIMPLE_FIELD_MAP_VALID, NELEM(SIMPLE_FIELD_MAP_VALID));

  if ((t = dict_find(iter, base + MK_WEATHER_ERROR_CODE))) {
    d->weather_error_code = t->value->uint8;
    if (d->weather_error_code == 0) {
      d->weather_error_streak = 0;
      d->weather_ever_valid = true;
    } else if (d->weather_error_streak < 250) { // saturate well clear of overflow -- only ">= 10" is ever checked
      d->weather_error_streak++;
    }
  }

  // "Weather in N hours" (87-92 in features_layer.c) -- forecast_temp_c
  // is sent as (celsius + 50) per byte, 255 meaning "not available",
  // since a plain byte array can't carry a signed value; decoded back to
  // a real (possibly negative) Celsius reading here, with -128 as the
  // sentinel d->forecast_temp_c itself uses for "not available" (see its
  // own comment in data/eclipse_data.h). The one array in the protocol
  // that isn't a straight copy, hence its own block.
  if ((t = dict_find(iter, base + MK_FORECAST_TEMP_C))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 6) n = 6;
    for (int i = 0; i < n; i++) {
      d->forecast_temp_c[i] = (raw[i] == 255) ? -128 : ((int16_t)raw[i] - 50);
    }
  }

  apply_blob_fields(iter, d, BLOB_FIELD_MAP_VALID, NELEM(BLOB_FIELD_MAP_VALID));
  apply_planet_grid(iter, MK_PLANET_ALT_SAMPLES, d->planet_alt_decideg);
  apply_planet_grid(iter, MK_PLANET_AZ_SAMPLES, d->planet_az_decideg);
  apply_cstring(iter, MK_LOCATION_NAME, d->location_name, sizeof(d->location_name));
  apply_cstring(iter, MK_METEOR_SHOWER_NAME, d->meteor_shower_name, sizeof(d->meteor_shower_name));

  return changes;
}
