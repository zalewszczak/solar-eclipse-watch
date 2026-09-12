#include <pebble.h>
#include <stddef.h>
#include <string.h>
#include "comms.h"
#include "message_key_index.h"

#define STARTUP_REQUEST_DELAY_MS 3000

static EclipseData *s_data;
static CommsDataAppliedHandler s_data_applied;
static void *s_data_context;
static AppTimer *s_retry_timer = NULL;
static AppTimer *s_startup_timer = NULL;
static uint16_t s_retry_delay_s = 8;

bool comms_send_battery_saver_phase(uint8_t phase) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return false;
  dict_write_uint8(iter, MESSAGE_KEY_BATTERY_SAVER_PHASE, phase);
  return app_message_outbox_send() == APP_MSG_OK;
}

static void request_update(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
  app_message_outbox_send();
}

static void request_retry_callback(void *context) {
  (void)context;
  s_retry_timer = NULL;
  if (!s_data || s_data->valid) return;
  request_update();
  s_retry_delay_s = (s_retry_delay_s < 60) ? s_retry_delay_s * 2 : 60;
  s_retry_timer = app_timer_register((uint32_t)s_retry_delay_s * 1000, request_retry_callback, NULL);
}

static void startup_request_delay_callback(void *context) {
  (void)context;
  s_startup_timer = NULL;
  request_update();
  s_retry_timer = app_timer_register((uint32_t)s_retry_delay_s * 1000, request_retry_callback, NULL);
}

// ---- Table-driven plain-copy message fields ---------------------------
// After C's message-key consolidation (86 of the old individual keys ->
// 5 grouped ones -- HANDS/MARKER_RINGS/EDGE_LINES/MARKER_TEXT/COLORS,
// handled by apply_consolidated_fields() below instead), this app has
// ~113 AppMessage keys; roughly half resolve to nothing more than "copy
// this value into this EclipseData field, with this one conversion" --
// no clamping, no derived state, no layer redraw to trigger. Those are
// handled once, generically, via this table + apply_simple_fields()
// below, instead of each getting its own hand-written
// `if ((t = dict_find(iter, KEY))) d->field = ...;` block.
// Everything else (validation/clamping, derived fields like has_eclipse,
// byte-blob arrays, the 5 consolidated groups, and anything that needs
// to mark a layer dirty or call apply_layout()/apply_clock_font()) keeps
// its own explicit code below, unchanged -- forcing those into this same
// table would need a per-field side-effect callback, which is most of
// this table's own complexity right back again for comparatively
// little further size win. Order doesn't matter: every field here is
// independent of every other one (and of the explicit fields below) --
// nothing here reads any s_data field, so there's no way processing
// them via one generic loop instead of scattered inline can observe a
// different result than the original hand-written order did. Run
// first, before any of the explicit blocks below, purely so that
// something like corner-content's own features_layer_set_data() call
// (further down) always sees this message's freshly-applied values
// rather than last message's -- moving a field out of this table
// always needs the same "does anything downstream read it" check.
typedef enum {
  F_U8,             // d->FIELD = t->value->uint8;
  F_BOOL,           // d->FIELD = t->value->uint8 != 0;
  F_I16,            // d->FIELD = t->value->int16;
  F_I8_FROM_I16,    // d->FIELD = (int8_t)t->value->int16;
  F_U16,            // d->FIELD = t->value->uint16;
  F_U32,            // d->FIELD = t->value->uint32;
  F_TIME,           // d->FIELD = (time_t)t->value->int32;
} SimpleFieldType;

typedef struct {
  uint8_t  key_index; // position in package.json's messageKeys array (see message_key_index.h) --
                       // MESSAGE_KEY_MESSAGE_TYPE + key_index recovers the real MESSAGE_KEY_* value
  SimpleFieldType type;
  uint16_t offset;    // offsetof(EclipseData, field) -- supports dotted paths (hand_hour.style
                       // etc.) same as any other offsetof use. EclipseData is ~1.2 KB, fits u16.
} SimpleFieldMapping; // 4 bytes, was 12 (uint32_t message_key + enum + size_t offset)

// Now a plain compile-time-constant initializer list -- see message_key_index.h's
// own comment for why MK_* (an array index) is a compile-time constant where
// MESSAGE_KEY_* (the real, link-time-assigned key) isn't. This table lives in
// .rodata instead of being populated into .bss by a runtime init function.
#define SIMPLE_FIELD_MAP_COUNT 74
static const SimpleFieldMapping SIMPLE_FIELD_MAP[SIMPLE_FIELD_MAP_COUNT] = {
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
};

static void apply_simple_fields(DictionaryIterator *iter, EclipseData *d) {
  const uint32_t base = MESSAGE_KEY_MESSAGE_TYPE; // one GOT load, once per call, instead of per-row
  for (size_t i = 0; i < SIMPLE_FIELD_MAP_COUNT; i++) {
    Tuple *st = dict_find(iter, base + SIMPLE_FIELD_MAP[i].key_index);
    if (!st) continue;
    uint8_t *dst = (uint8_t *)d + SIMPLE_FIELD_MAP[i].offset;
    switch (SIMPLE_FIELD_MAP[i].type) {
      case F_U8:          *dst = st->value->uint8; break;
      case F_BOOL:        *(bool *)dst = st->value->uint8 != 0; break;
      case F_I16:         *(int16_t *)dst = st->value->int16; break;
      case F_I8_FROM_I16:  *(int8_t *)dst = (int8_t)st->value->int16; break;
      case F_U16:         *(uint16_t *)dst = st->value->uint16; break;
      case F_U32:         *(uint32_t *)dst = st->value->uint32; break;
      case F_TIME:        *(time_t *)dst = (time_t)st->value->int32; break;
    }
  }
}

// ---- C: consolidated settings fields -----------------------------------
// 5 grouped AppMessage keys replacing 86 individual ones:
//   HAND_HOUR_*/HAND_MIN_*/HAND_SEC_*                -> HANDS         (42 B: 3x HandConfig)
//   CUSTOM_HOUR_*/CUSTOM_SEC_*                        -> MARKER_RINGS  (16 B: 2x MarkerRingConfig)
//   the 16 edge-line content/color-mode keys          -> EDGE_LINES    (16 B: 16x uint8_t)
//   MARKER_TEXT_*                                     -> MARKER_TEXT   (8 B: packed, see below)
//   CUSTOM_BG/TEXT/ACCENT + NIGHT_CUSTOM_BG/TEXT/ACCENT -> COLORS      (6 B: packed, see below)
//
// Four of the five are handled here; MARKER_TEXT and COLORS get an
// explicit byte-by-byte unpack instead of a memcpy:
//  - HandConfig/MarkerRingConfig and the 16 edge-line fields are flat,
//    contiguous, no-padding blocks in EclipseData -- see the
//    _Static_assert()s below, which exist so a struct-layout change
//    that would silently break the wire format fails the build instead
//    of silently scrambling everyone's saved hands/markers.
//  - MarkerTextConfig has two uint16_t masks with padding around them
//    (hour_mask/second_mask aren't at the packed-wire offsets a memcpy
//    would assume), so MARKER_TEXT is sent as 8 packed bytes and
//    unpacked by hand instead.
//  - COLORS is 6 explicit bytes rather than a memcpy of a 7-byte
//    contiguous run, specifically to avoid folding in
//    night_scheme_enabled (the bool that happens to sit between
//    custom_accent and night_custom_bg in the struct) -- that key stays
//    separate on the wire since it has its own layer_mark_dirty()
//    side effect below that this function doesn't want to duplicate or
//    silently drop.
_Static_assert(sizeof(HandConfig) == 14, "HANDS wire format assumes a 14-byte HandConfig");
_Static_assert(sizeof(MarkerRingConfig) == 8, "MARKER_RINGS wire format assumes an 8-byte MarkerRingConfig");

static void apply_consolidated_fields(DictionaryIterator *iter, EclipseData *d) {
  Tuple *t;

  // HANDS: 3x HandConfig, in hour/minute/second order, each in the
  // exact field order HandConfig itself declares them (see hand_layer.h)
  // -- src/pkjs/index.js's handsBytes() must build the array in that
  // same order for this memcpy to land correctly.
  if ((t = dict_find(iter, MESSAGE_KEY_HANDS)) && t->length >= 3 * sizeof(HandConfig)) {
    memcpy(&d->hand_hour, t->value->data, 3 * sizeof(HandConfig));
  }

  // MARKER_RINGS: 2x MarkerRingConfig, hour ring then second ring, each
  // in MarkerRingConfig's own declared field order (see eclipse_data.h).
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_RINGS)) && t->length >= 2 * sizeof(MarkerRingConfig)) {
    memcpy(&d->custom_hour_marker, t->value->data, 2 * sizeof(MarkerRingConfig));
  }

  // EDGE_LINES: the 16 upper/bottom/middle_left/middle_right
  // line1/line2 content+color_mode uint8_t fields, in the exact order
  // eclipse_data.h declares them.
  if ((t = dict_find(iter, MESSAGE_KEY_EDGE_LINES)) && t->length >= 16) {
    memcpy(&d->upper_middle_line1_content, t->value->data, 16);
  }

  // MARKER_TEXT: target, font_choice, offset_px (signed byte), hour_mask
  // (u16 little-endian), second_mask (u16 little-endian), roman_numerals.
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT)) && t->length >= 8) {
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
  if ((t = dict_find(iter, MESSAGE_KEY_COLORS)) && t->length >= 6) {
    uint8_t *b = t->value->data;
    d->custom_bg = b[0];
    d->custom_text = b[1];
    d->custom_accent = b[2];
    d->night_custom_bg = b[3];
    d->night_custom_text = b[4];
    d->night_custom_accent = b[5];
  }
}


static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  (void)context;
  EclipseData *d = s_data;
  uint32_t changes = COMMS_CHANGE_NONE;
  Tuple *t;

  apply_simple_fields(iter, d); // see its own comment -- every plain-copy field, in one pass, before anything below can read one
  apply_consolidated_fields(iter, d); // see its own comment -- the 5 grouped settings keys (HANDS/MARKER_RINGS/EDGE_LINES/MARKER_TEXT/COLORS)

  // Purely diagnostic -- every field below is still applied via its
  // own dict_find(), which already tolerates a partial dictionary, so
  // parsing doesn't branch on this. It just labels which chunk (see
  // MsgType in eclipse_data.h) this inbox message was, for APP_LOG.
  if ((t = dict_find(iter, MESSAGE_KEY_MESSAGE_TYPE))) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "inbox: chunk type %d", (int)t->value->uint8);
  }

  if ((t = dict_find(iter, MESSAGE_KEY_DATA_VALID))) {
    d->valid = t->value->uint8 != 0;
    if (d->valid && s_retry_timer) {
      // Real data made it through -- no need to keep pinging PKJS
      // for it anymore (see request_retry_callback's own comment).
      app_timer_cancel(s_retry_timer);
      s_retry_timer = NULL;
    }
  }
  // Parsed (and applied) before the early-return below so a
  // font-only settings update still takes effect even if the watch
  // hasn't received a valid eclipse payload yet.
  if ((t = dict_find(iter, MESSAGE_KEY_CLOCK_FONT))) {
    d->clock_font = t->value->uint8;
    changes |= COMMS_CHANGE_CLOCK_FONT;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_SECONDS))) {
    d->show_seconds = t->value->uint8 != 0;
    changes |= COMMS_CHANGE_HANDS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_STYLE))) {
    d->bottom_style = t->value->uint8;
    changes |= COMMS_CHANGE_LAYOUT;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_MOON_SIZE_PCT))) {
    d->sun_moon_size_pct = t->value->uint8;
    changes |= COMMS_CHANGE_CANVAS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_MODE))) {
    d->sky_mode = t->value->uint8;
    changes |= COMMS_CHANGE_CANVAS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_LABEL_STYLE))) {
    d->label_style = t->value->uint8;
    changes |= COMMS_CHANGE_CANVAS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BG_ANIM_MODE))) {
    uint8_t v = t->value->uint8;
    d->bg_anim_mode = (v <= 2) ? v : 0; // clamped -- used as a raw array-free switch/compare, but still worth guarding against a stray out-of-range byte
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHAKE_ANIM_MODE))) {
    uint8_t v = t->value->uint8;
    d->shake_anim_mode = (v <= 3) ? v : 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHADOW_TRANSLUCENT))) {
    d->shadow_translucent = t->value->uint8 != 0;
    changes |= COMMS_CHANGE_HANDS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHADOW_ANGLE))) {
    d->shadow_angle_deg = t->value->uint16;
    changes |= COMMS_CHANGE_HANDS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DRAW_FEATURES_BENEATH_HANDS))) {
    d->draw_features_beneath_hands = t->value->uint8 != 0;
    changes |= COMMS_CHANGE_LAYOUT;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_MARKER_STYLE))) {
    d->big_analog_marker_style = t->value->uint8;
    changes |= COMMS_CHANGE_HANDS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_SUN_TIME))) {
    d->show_sun_time = t->value->uint8 != 0;
    changes |= COMMS_CHANGE_PANEL;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_ISS))) {
    d->show_iss = t->value->uint8 != 0;
    changes |= COMMS_CHANGE_CANVAS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_AURORA_ENABLED))) {
    d->aurora_enabled = t->value->uint8 != 0;
    changes |= COMMS_CHANGE_CANVAS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_CONTENT))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) d->corner_content[i] = raw[i];
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_COLOR_MODE))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) d->corner_color_mode[i] = raw[i];
  }
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_SCHEME_ENABLED))) {
    d->night_scheme_enabled = t->value->uint8 != 0;
    changes |= COMMS_CHANGE_PANEL;
  }

  // Every field parsed above this point that affects the features
  // overlay's layout (style/marker-style/bottom-info-bar-mode, and all
  // of the corner/edge content+color fields) has now been applied to
  // s_data -- one recompute here replaces the many individual
  // layer_mark_dirty(s_features_layer) calls the old per-field handling
  // used, since features_layer_set_data() recomputes every slot's
  // layout AND value in one pass and marks the layer dirty. Content-
  // only fields parsed further below (weather icon style, AQI/altitude
  // units, ...) don't affect layout, so they instead call
  // features_layer_refresh_values() themselves right after landing in
  // s_data, to re-resolve just the VALUE half.
    changes |= COMMS_CHANGE_FEATURES;

  if (!d->valid) {
    if (s_data_applied) s_data_applied(changes, s_data_context);
    return;
  }
  d->error_code = 0;

  if ((t = dict_find(iter, MESSAGE_KEY_ECLIPSE_TYPE))) {
    d->type = t->value->uint8;
    d->has_eclipse = d->type != ECLIPSE_TYPE_NONE;
  }

  if ((t = dict_find(iter, MESSAGE_KEY_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    d->sample_count = count > MAX_SEP_SAMPLES ? MAX_SEP_SAMPLES : count;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SEP_SAMPLES))) {
    // Sent as a byte blob of uint16 (little-endian) values.
    uint16_t *samples = (uint16_t *)t->value->data;
    int n = t->length / sizeof(uint16_t);
    if (n > MAX_SEP_SAMPLES) n = MAX_SEP_SAMPLES;
    for (int i = 0; i < n; i++) {
      d->sep_samples_centideg[i] = samples[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MAG_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > MAX_SEP_SAMPLES) n = MAX_SEP_SAMPLES;
    for (int i = 0; i < n; i++) {
      d->mag_pct_samples[i] = raw[i];
    }
  }

  // "Weather in N hours" (87-92 in features_layer.c) -- forecast_temp_c
  // is sent as (celsius + 50) per byte, 255 meaning "not available",
  // since a plain byte array can't carry a signed value; decoded back
  // to a real (possibly negative) Celsius reading here, with -128 as
  // the sentinel d->forecast_temp_c itself uses for "not available"
  // (see its own comment in eclipse_data.h).
  if ((t = dict_find(iter, MESSAGE_KEY_FORECAST_TEMP_C))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 6) n = 6;
    for (int i = 0; i < n; i++) {
      d->forecast_temp_c[i] = (raw[i] == 255) ? -128 : ((int16_t)raw[i] - 50);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_FORECAST_CONDITION))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 6) n = 6;
    for (int i = 0; i < n; i++) {
      d->forecast_condition[i] = raw[i];
    }
    changes |= COMMS_CHANGE_FEATURE_VALUES;
  }

  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_ERROR_CODE))) {
    d->weather_error_code = t->value->uint8;
    if (d->weather_error_code == 0) {
      d->weather_error_streak = 0;
      d->weather_ever_valid = true;
    } else if (d->weather_error_streak < 250) { // saturate well clear of overflow -- only ">= 10" is ever checked
      d->weather_error_streak++;
    }
    changes |= COMMS_CHANGE_FEATURE_VALUES;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_ICON_STYLE))) {
    d->weather_icon_style = t->value->uint8;
    changes |= COMMS_CHANGE_FEATURE_VALUES;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_AQI_UNIT))) {
    d->aqi_unit = t->value->uint8;
    changes |= COMMS_CHANGE_FEATURE_VALUES;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_AURORA_VISIBILITY_PCT))) {
    d->aurora_visibility_pct = t->value->uint8;
    changes |= COMMS_CHANGE_CANVAS;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ALTITUDE_UNIT))) {
    d->altitude_unit = t->value->uint8;
    changes |= COMMS_CHANGE_FEATURE_VALUES;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_LOCATION_NAME))) {
    strncpy(d->location_name, t->value->cstring, sizeof(d->location_name) - 1);
    d->location_name[sizeof(d->location_name) - 1] = '\0';
  }

  // Full-day sky background: sun altitude + cloud cover samples,
  // used to render the gradient and dithered clouds behind the sun.
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    d->sky_sample_count = count > MAX_SKY_SAMPLES ? MAX_SKY_SAMPLES : count;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_ALT_SAMPLES))) {
    // Byte blob of int16 (little-endian) tenths-of-a-degree values.
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      uint16_t u = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
      d->sun_alt_decideg[i] = (int16_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_AZ_SAMPLES))) {
    // Same byte-blob shape as SUN_ALT_SAMPLES, but the field itself is
    // an unsigned uint16 (azimuth is always 0-359.9deg, never
    // negative) -- see sun_az_decideg's own eclipse_data.h comment.
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      d->sun_az_decideg[i] = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      d->cloud_pct_samples[i] = raw[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_ALT_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      uint16_t u = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
      d->moon_alt_decideg[i] = (int16_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_AZ_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      d->moon_az_decideg[i] = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
    }
  }
  // Packed as PLANET_COUNT rows of int16 (little-endian) samples,
  // concatenated in PlanetId order (see eclipse_data.h) -- one
  // message key instead of five near-duplicate ones.
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_ALT_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int total_int16 = t->length / 2;
    int per_planet = total_int16 / PLANET_COUNT;
    if (per_planet > MAX_SKY_SAMPLES) per_planet = MAX_SKY_SAMPLES;
    for (int p = 0; p < PLANET_COUNT; p++) {
      for (int i = 0; i < per_planet; i++) {
        int src_idx = p * (total_int16 / PLANET_COUNT) + i;
        uint16_t u = (uint16_t)raw[src_idx * 2] | ((uint16_t)raw[src_idx * 2 + 1] << 8);
        d->planet_alt_decideg[p][i] = (int16_t)u;
      }
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_AZ_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int total_int16 = t->length / 2;
    int per_planet = total_int16 / PLANET_COUNT;
    if (per_planet > MAX_SKY_SAMPLES) per_planet = MAX_SKY_SAMPLES;
    for (int p = 0; p < PLANET_COUNT; p++) {
      for (int i = 0; i < per_planet; i++) {
        int src_idx = p * (total_int16 / PLANET_COUNT) + i;
        d->planet_az_decideg[p][i] = (uint16_t)raw[src_idx * 2] | ((uint16_t)raw[src_idx * 2 + 1] << 8);
      }
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_RISE))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 4;
    if (n > PLANET_COUNT) n = PLANET_COUNT;
    for (int p = 0; p < n; p++) {
      uint32_t u = (uint32_t)raw[p * 4] | ((uint32_t)raw[p * 4 + 1] << 8) |
                   ((uint32_t)raw[p * 4 + 2] << 16) | ((uint32_t)raw[p * 4 + 3] << 24);
      d->planet_rise[p] = (time_t)(int32_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_SET))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 4;
    if (n > PLANET_COUNT) n = PLANET_COUNT;
    for (int p = 0; p < n; p++) {
      uint32_t u = (uint32_t)raw[p * 4] | ((uint32_t)raw[p * 4 + 1] << 8) |
                   ((uint32_t)raw[p * 4 + 2] << 16) | ((uint32_t)raw[p * 4 + 3] << 24);
      d->planet_set[p] = (time_t)(int32_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_STAR_ALT_SAMPLES))) {
    // Byte blob of int16 (little-endian) tenths-of-a-degree values,
    // same packing as SUN_ALT_SAMPLES above -- see star_alt_decideg's
    // own comment in eclipse_data.h for why this is a flat current-
    // snapshot array, not a full-day grid like that one.
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > STAR_COUNT) n = STAR_COUNT;
    for (int i = 0; i < n; i++) {
      uint16_t u = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
      d->star_alt_decideg[i] = (int16_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_STAR_AZ_SAMPLES))) {
    // Same packing, but always non-negative (0-3600 = 0-360deg x10).
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > STAR_COUNT) n = STAR_COUNT;
    for (int i = 0; i < n; i++) {
      d->star_az_decideg[i] = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_METEOR_SHOWER_NAME))) {
    strncpy(d->meteor_shower_name, t->value->cstring, sizeof(d->meteor_shower_name) - 1);
    d->meteor_shower_name[sizeof(d->meteor_shower_name) - 1] = '\0';
  }

  // Re-evaluate right away -- most relevantly, catches
  // battery_saver_enabled just having been turned off (or on, while
  // already past 2h/4h idle) from the settings page, rather than
  // leaving the watch in a stale phase until the next tick.
  if (s_data_applied) s_data_applied(changes, s_data_context);
}


static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

void comms_init(EclipseData *data, CommsDataAppliedHandler handler, void *context) {
  s_data = data;
  s_data_applied = handler;
  s_data_context = context;
  s_retry_delay_s = 8;
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(APPMSG_INBOX_SIZE, APPMSG_OUTBOX_SIZE);
  s_startup_timer = app_timer_register(STARTUP_REQUEST_DELAY_MS, startup_request_delay_callback, NULL);
}

void comms_deinit(void) {
  if (s_startup_timer) {
    app_timer_cancel(s_startup_timer);
    s_startup_timer = NULL;
  }
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  s_data = NULL;
  s_data_applied = NULL;
  s_data_context = NULL;
}
