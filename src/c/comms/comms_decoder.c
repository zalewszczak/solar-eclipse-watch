#include <pebble.h>
#include <stddef.h>
#include <string.h>
#include "./comms_decoder.h"
#include "../generated/message_key_index.h"
#include "../data/hand_types.h"
#include "../data/marker_types.h"

// Decoder tables keep common fields compact; special transforms remain explicit.
// Wire keys are stored as MK_* offsets from MESSAGE_KEY_MESSAGE_TYPE.

#define NELEM(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

// Plain-copy fields: low nibble is the value type, high nibble the change flag.

typedef enum {
  F_U8 = 0,     // d->FIELD = t->value->uint8;
  F_BOOL,       // d->FIELD = t->value->uint8 != 0;
  F_I16,        // d->FIELD = t->value->int16;
  F_U16,        // d->FIELD = t->value->uint16;
  F_U32,        // d->FIELD = t->value->uint32;
  F_TIME,       // d->FIELD = (time_t)t->value->int32;
  // Invalid values fall back to 0 for bounded byte settings.
  F_U8_MAX2,    // d->FIELD = (v <= 2) ? v : 0;
  F_U8_MAX3,    // d->FIELD = (v <= 3) ? v : 0;
} SimpleFieldType;

// Change flags are stored as (bit index + 1) in the high nibble.
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
  uint8_t  key_index; // MESSAGE_KEY_MESSAGE_TYPE + key_index.
  uint8_t  type_flag; // SimpleFieldType | FL_*.
  uint16_t offset;    // offsetof(EclipseData, field).
} SimpleFieldMapping; // 4 bytes.

// Applied to every message, including invalid payloads.
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
  { MK_SHOW_FLIGHTS, F_BOOL, offsetof(EclipseData, show_flights) },
  { MK_OVERHEAD_OBJECT_COUNT, F_U8, offsetof(EclipseData, overhead_object_count) },
  { MK_OVERHEAD_OBJECTS_COMPUTED_AT, F_TIME, offsetof(EclipseData, overhead_objects_computed_at) },
  // Kept separate from MARKER_RINGS because they are independent fields.
  { MK_CUSTOM_HOUR_INNER_THICKNESS, F_U8, offsetof(EclipseData, custom_hour_marker_inner_thickness) },
  { MK_CUSTOM_SEC_INNER_THICKNESS, F_U8, offsetof(EclipseData, custom_second_marker_inner_thickness) },
  { MK_HOURLY_VIBE_MODE, F_U8, offsetof(EclipseData, hourly_vibe_mode) },
  { MK_HOURLY_VIBE_INTERVAL_MIN, F_U8, offsetof(EclipseData, hourly_vibe_interval_min) },
  { MK_HOURLY_VIBE_PATTERN, F_U8, offsetof(EclipseData, hourly_vibe_pattern) },
  { MK_HOURLY_VIBE_START_MIN, F_U16, offsetof(EclipseData, hourly_vibe_start_min) },
  { MK_HOURLY_VIBE_END_MIN, F_U16, offsetof(EclipseData, hourly_vibe_end_min) },
  { MK_HOURLY_VIBE_DAYS_MASK, F_U8, offsetof(EclipseData, hourly_vibe_days_mask) },
  { MK_HOURLY_VIBE_OVERRIDE_QUIET, F_BOOL, offsetof(EclipseData, hourly_vibe_override_quiet) },
  // Settings that also raise a presentation change flag.
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

// Additional fields applied only to valid payloads.
static const SimpleFieldMapping SIMPLE_FIELD_MAP_VALID[] = {
  { MK_WEATHER_ICON_STYLE, F_U8, offsetof(EclipseData, icon_style) },
  { MK_AQI_UNIT, F_U8, offsetof(EclipseData, aqi_unit) },
  { MK_AURORA_VISIBILITY_PCT, F_U8 | FL_CANVAS, offsetof(EclipseData, aurora_visibility_pct) },
  { MK_ALTITUDE_UNIT, F_U8, offsetof(EclipseData, altitude_unit) },
};

static uint32_t apply_simple_fields(DictionaryIterator *iter, EclipseData *d,
                                    const SimpleFieldMapping *map, uint8_t count) {
  const uint32_t base = MESSAGE_KEY_MESSAGE_TYPE; // Shared wire-key base.
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

// Byte-array fields use packed wire data; excess bytes and partial elements are ignored.

typedef struct {
  uint8_t  key_index;
  uint8_t  elem_size; // Element size in bytes: 1, 2, or 4.
  uint16_t offset;    // offsetof(EclipseData, array).
  uint16_t max_bytes; // Destination capacity in bytes.
} BlobFieldMapping;   // 6 bytes.

_Static_assert(sizeof(time_t) == 4, "PLANET_RISE/SET wire format assumes a 4-byte time_t");

// Applied to every message, including invalid payloads.
static const BlobFieldMapping BLOB_FIELD_MAP[] = {
  { MK_CORNER_CONTENT, 1, offsetof(EclipseData, corner_content), 4 },
  { MK_CORNER_COLOR_MODE, 1, offsetof(EclipseData, corner_color_mode), 4 },
  { MK_OVERHEAD_OBJECTS, 8, offsetof(EclipseData, overhead_objects), MAX_OVERHEAD_OBJECTS * 8 },
};

_Static_assert(sizeof(OverheadObject) == 8, "OVERHEAD_OBJECTS wire format assumes an 8-byte OverheadObject");

// Applied only for valid payloads.
static const BlobFieldMapping BLOB_FIELD_MAP_VALID[] = {
  { MK_SEP_SAMPLES, 2, offsetof(EclipseData, sep_samples_centideg), MAX_SEP_SAMPLES * 2 },
  { MK_MAG_SAMPLES, 1, offsetof(EclipseData, mag_pct_samples), MAX_SEP_SAMPLES },
  { MK_FORECAST_CONDITION, 1, offsetof(EclipseData, forecast_condition), 9 },
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
    len &= (uint16_t)~(uint16_t)(m->elem_size - 1); // Whole elements only.
    memcpy((uint8_t *)d + m->offset, t->value->data, len);
  }
}

// Planet sample rows are packed on the wire and expanded to fixed-width destination rows.
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

// Copy a bounded C-string field.
static void apply_cstring(DictionaryIterator *iter, uint8_t key_index, char *dst, uint16_t cap) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_MESSAGE_TYPE + key_index);
  if (!t) return;
  strncpy(dst, t->value->cstring, cap - 1);
  dst[cap - 1] = '\0';
}

// Consolidated settings fields use fixed wire layouts.
// HANDS, MARKER_RINGS, and EDGE_LINES can be copied directly; MARKER_TEXT and COLORS are packed.
_Static_assert(sizeof(HandConfig) == 14, "HANDS wire format assumes a 14-byte HandConfig");
_Static_assert(sizeof(MarkerRingConfig) == 8, "MARKER_RINGS wire format assumes an 8-byte MarkerRingConfig");

static void apply_consolidated_fields(DictionaryIterator *iter, EclipseData *d) {
  const uint32_t base = MESSAGE_KEY_MESSAGE_TYPE;
  Tuple *t;

  // HANDS: hour, minute, second HandConfig values in declaration order.
  if ((t = dict_find(iter, base + MK_HANDS)) && t->length >= 3 * sizeof(HandConfig)) {
    memcpy(&d->hand_hour, t->value->data, 3 * sizeof(HandConfig));
  }

  // MARKER_RINGS: hour ring followed by second ring.
  if ((t = dict_find(iter, base + MK_MARKER_RINGS)) && t->length >= 2 * sizeof(MarkerRingConfig)) {
    memcpy(&d->custom_hour_marker, t->value->data, 2 * sizeof(MarkerRingConfig));
  }

  // EDGE_LINES: 16 content/color-mode bytes in EclipseData declaration order.
  if ((t = dict_find(iter, base + MK_EDGE_LINES)) && t->length >= 16) {
    memcpy(&d->upper_middle_line1_content, t->value->data, 16);
  }

  // MARKER_TEXT: target, font, signed offset, two little-endian masks, Roman-numeral flag.
  if ((t = dict_find(iter, base + MK_MARKER_TEXT)) && t->length >= 8) {
    uint8_t *b = t->value->data;
    d->marker_text.target = b[0];
    d->marker_text.font_choice = b[1];
    d->marker_text.offset_px = (int8_t)b[2];
    d->marker_text.hour_mask = (uint16_t)(b[3] | (b[4] << 8));
    d->marker_text.second_mask = (uint16_t)(b[5] | (b[6] << 8));
    d->marker_text.roman_numerals = b[7] != 0;
  }

  // COLORS: six custom colors; night_scheme_enabled is sent separately.
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

  // Apply common settings and grouped fields before valid-payload fields.
  uint32_t changes = apply_simple_fields(iter, d, SIMPLE_FIELD_MAP, NELEM(SIMPLE_FIELD_MAP));
  apply_consolidated_fields(iter, d);
  apply_blob_fields(iter, d, BLOB_FIELD_MAP, NELEM(BLOB_FIELD_MAP));

  // Any response to REQUEST_FLIGHTS (success or failure) carries a fresh
  // OVERHEAD_OBJECTS_COMPUTED_AT -- PKJS sends it either way, so its mere
  // presence is enough to clear the watch's own "still waiting" flag,
  // whether or not the list itself is empty.
  if (dict_find(iter, base + MK_OVERHEAD_OBJECTS_COMPUTED_AT)) {
    d->overhead_objects_loading = false;
  }

  // Feature layout and values are recomputed for every decoded message.
  changes |= COMMS_CHANGE_FEATURES;

  if (!d->valid) return changes;
  d->error_code = 0;

  if ((t = dict_find(iter, base + MK_ECLIPSE_TYPE))) {
    d->type = t->value->uint8;
    d->has_eclipse = d->type != ECLIPSE_TYPE_NONE;
  }
  // Sample counts use their full destination limits.
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
    } else if (d->weather_error_streak < 250) { // Saturate the streak.
      d->weather_error_streak++;
    }
  }

  // Forecast temperatures use a +50 byte offset; 255 means unavailable.
  // 9 entries: 0-5 are the next 1-6 hours, 6-8 the next 1-3 days.
  if ((t = dict_find(iter, base + MK_FORECAST_TEMP_C))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 9) n = 9;
    for (int i = 0; i < n; i++) {
      d->forecast_temp_c[i] = (raw[i] == 255) ? -128 : ((int16_t)raw[i] - 50);
    }
  }

  apply_blob_fields(iter, d, BLOB_FIELD_MAP_VALID, NELEM(BLOB_FIELD_MAP_VALID));
  apply_planet_grid(iter, MK_PLANET_ALT_SAMPLES, d->planet_alt_decideg);
  apply_planet_grid(iter, MK_PLANET_AZ_SAMPLES, d->planet_az_decideg);
  apply_cstring(iter, MK_METEOR_SHOWER_NAME, d->meteor_shower_name, sizeof(d->meteor_shower_name));

  return changes;
}
