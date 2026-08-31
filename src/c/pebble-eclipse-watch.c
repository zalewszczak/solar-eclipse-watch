#include <pebble.h>
#include "eclipse_data.h"
#include "background_layer.h"
#include "features_layer.h"

// EclipseData is well past Pebble's 256-byte-per-key persist limit
// (PERSIST_DATA_MAX_LENGTH), so it's split across several keys here
// rather than written as one blob -- see save_data()/load_data().
#define PERSIST_KEY_DATA_BASE 1
#define PERSIST_CHUNK_SIZE 200
#define PERSIST_CHUNK_COUNT 5 // 1000 bytes of capacity; struct size is checked at compile
                                // time below (persist_capacity_check) -- see that comment
                                // if it ever fails to compile after adding fields.

static Window *s_window;
static Layer *s_countdown_layer; // custom-drawn (not TextLayer) so it can draw the 1px outline
static GColor s_countdown_text_color;
static Layer *s_canvas_layer;
static Layer *s_bottom_layer; // digital/analog mode only
static Layer *s_hands_layer;  // big-analogue mode only
static Layer *s_features_layer; // always present -- overlays the sky canvas's own bounds;
                                  // see features_layer.h -- owns its own per-layer state now
static uint8_t s_current_layout_style = 255; // sentinel: forces initial layout setup
static bool s_current_draw_features_beneath_hands = false; // mirrors s_data's own default

// True while the shake-to-reveal ground bar is up (mirrors
// eclipse_layer.c's own private show_labels state, tracked
// separately here since main.c already owns the shake timer and the
// corners layer needs this to shift its bottom corners up out of the
// bar's way).
static bool s_labels_visible = false;

// Defined near window_load below; forward-declared here since
// inbox_received_handler (which needs to call it on a mode switch)
// appears earlier in this file.
static void apply_layout(void);
static void apply_clock_font(void);

static char s_countdown_buf[40];

static EclipseData s_data;

// Declare a file-scope variable
static GFont clock_font;
static bool clock_font_custom = false;
static GFont small_font;
static bool small_font_custom = false;

// static declarations:
static int get_small_font_height_offset(void);
static int get_clock_font_height_offset(void);
static bool use_small_seconds_for_digital_clock(void);

// ---- color schemes ------------------------------------------------------

// Built at runtime via GColorFromRGB rather than named palette
// constants -- guaranteed valid regardless of exact Pebble color name
// availability, same pattern already used safely for the sky
// gradient in eclipse_layer.c.
// A GColor is just a packed byte (2 bits each of alpha/r/g/b) under
// the hood -- reconstructing one from a raw byte the settings page
// sent is exactly how the "pick any of the 64 real display colors"
// picker round-trips: the phone sends back whichever of the 64 the
// user tapped, packed the same way, and this just re-wraps it.
GColor gcolor_from_packed(uint8_t packed) {
  GColor c;
  c.argb = packed;
  return c;
}

// Picks the day or night set of colors based on the Sun's altitude
// (reusing eclipse_sky_is_bright()'s existing civil-twilight threshold
// rather than a second definition of "night") -- falls back to the day
// colors entirely if the user hasn't turned on separate night ones.
// Takes `d` explicitly (rather than reading the global s_data) so
// background_layer.c can call this too, for marker colors, using its
// own `d` (the same EclipseData, via the pointer eclipse_canvas_set_data()
// stored). The watch has no notion of a "preset" here -- every color
// arriving from the phone is already a concrete packed value; picking
// a named preset in the settings page just fills in these same three
// fields before sending, same as manually choosing each color would.
void get_active_color_scheme(const EclipseData *d, time_t now, GColor *bg, GColor *text, GColor *accent) {
  bool night = d->night_scheme_enabled && !eclipse_sky_is_bright(d, now);
  if (night) {
    *bg = gcolor_from_packed(d->night_custom_bg);
    *text = gcolor_from_packed(d->night_custom_text);
    *accent = gcolor_from_packed(d->night_custom_accent);
  } else {
    *bg = gcolor_from_packed(d->custom_bg);
    *text = gcolor_from_packed(d->custom_text);
    *accent = gcolor_from_packed(d->custom_accent);
  }
}

// ---- analog clock styling ------------------------------------------------

// A minimal 3x5-pixel digit font, drawn procedurally rather than
// loaded as a resource -- "super tiny" is the whole point, smaller
// than any real system font renders legibly. Each row is 3 bits
// (left to right); only the digits needed for a 12/3/6/9 dial are
// used, but the full 0-9 set costs nothing extra to keep complete.
static const uint8_t TINY_DIGITS[10][5] = {
  { 0x7, 0x5, 0x5, 0x5, 0x7 }, // 0
  { 0x2, 0x6, 0x2, 0x2, 0x7 }, // 1
  { 0x7, 0x1, 0x7, 0x4, 0x7 }, // 2
  { 0x7, 0x1, 0x7, 0x1, 0x7 }, // 3
  { 0x5, 0x5, 0x7, 0x1, 0x1 }, // 4
  { 0x7, 0x4, 0x7, 0x1, 0x7 }, // 5
  { 0x7, 0x4, 0x7, 0x5, 0x7 }, // 6
  { 0x7, 0x1, 0x1, 0x1, 0x1 }, // 7
  { 0x7, 0x5, 0x7, 0x5, 0x7 }, // 8
  { 0x7, 0x5, 0x7, 0x1, 0x7 }, // 9
};

static void draw_tiny_digit(GContext *ctx, GPoint top_left, int digit, GColor color, int16_t px) {
  if (digit < 0 || digit > 9) return;
  graphics_context_set_fill_color(ctx, color);
  for (int row = 0; row < 5; row++) {
    uint8_t bits = TINY_DIGITS[digit][row];
    for (int col = 0; col < 3; col++) {
      if (bits & (1 << (2 - col))) {
        graphics_fill_rect(ctx, GRect(top_left.x + col * px, top_left.y + row * px, px, px), 0, GCornerNone);
      }
    }
  }
}

// Centers a (1- or 2-digit) number on `center` using the tiny font.
static void draw_tiny_number(GContext *ctx, GPoint center, int number, GColor color, int16_t px) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", number);
  int len = (int)strlen(buf);
  int16_t digit_w = 3 * px + px; // 3px-wide digit + 1px gap
  int16_t total_w = (int16_t)(len * digit_w - px);
  GPoint start = GPoint(center.x - total_w / 2, center.y - (5 * px) / 2);
  for (int i = 0; i < len; i++) {
    draw_tiny_digit(ctx, GPoint(start.x + i * digit_w, start.y), buf[i] - '0', color, px);
  }
}

// Small tick marks at each hour position -- slightly longer at
// 12/3/6/9 for a bit of visual structure without needing numerals.
static void draw_hour_markers(GContext *ctx, GPoint center, int16_t r, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  for (int h = 0; h < 12; h++) {
    int32_t angle = (h * TRIG_MAX_ANGLE) / 12;
    int16_t outer = r;
    int16_t inner = (h % 3 == 0) ? r - 5 : r - 3;
    GPoint p1 = GPoint(center.x + (outer * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y - (outer * cos_lookup(angle)) / TRIG_MAX_RATIO);
    GPoint p2 = GPoint(center.x + (inner * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y - (inner * cos_lookup(angle)) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, p1, p2);
  }
}

// ---- sunrise/sunset readout ------------------------------------------

// Whichever of today's sunrise/sunset is still ahead of `now`. Only
// covers *today's* two contacts (that's all the phone sends), so once
// today's sunset has passed there's nothing valid to show until the
// next refresh rolls the data over to a new day -- callers fall back
// to the week number in that case rather than showing stale or
// invented data.
// Whichever of today's sunrise/sunset is still ahead of `now`, falling
// back to tomorrow's sunrise once both of today's have passed (rather
// than reporting "no event" -- which used to show as "--:--" once
// today's sunset had passed, since only *today's* two contacts used to
// be sent at all).
bool get_next_sun_event(time_t now, time_t sun_rise, time_t sun_set, time_t sun_rise_tomorrow,
                         time_t *event_time, bool *is_sunrise) {
  if (sun_rise != 0 && now < sun_rise) {
    *event_time = sun_rise;
    *is_sunrise = true;
    return true;
  }
  if (sun_set != 0 && now < sun_set) {
    *event_time = sun_set;
    *is_sunrise = false;
    return true;
  }
  if (sun_rise_tomorrow != 0) {
    *event_time = sun_rise_tomorrow;
    *is_sunrise = true;
    return true;
  }
  return false;
}

// A compact "sunrise/sunset" glyph -- an arrow (up for rise, down for
// set) next to a horizon-sun icon (a circle with its bottom half
// covered by the background color, sitting on a short line), both
// built from plain fill primitives rather than a font glyph that may
// not exist in the built-in charset. Returns the total width drawn,
// so the caller can place the time text right after it.
int16_t draw_sun_time_icon(GContext *ctx, GPoint top_left, bool is_sunrise, GColor color, GColor bg) {
  graphics_context_set_fill_color(ctx, color);
  int16_t ax = top_left.x, ay = top_left.y;
  for (int16_t row = 0; row < 5; row++) {
    int16_t width = is_sunrise ? (row + 1) : (5 - row);
    int16_t row_y = is_sunrise ? (ay + (4 - row)) : (ay + row);
    graphics_fill_rect(ctx, GRect(ax + (5 - width) / 2, row_y, width, 1), 0, GCornerNone);
  }

  GPoint sun_center = GPoint(ax + 13, ay + 4);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, sun_center, 4);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, GRect(sun_center.x - 5, sun_center.y, 10, 5), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(sun_center.x - 6, sun_center.y, 12, 1), 0, GCornerNone);

  return 20; // total icon width, arrow + horizon-sun glyph
}

// ---- big-analogue mode: fullscreen hands over the sky layer --------------

// point_in_convex_polygon()/fill_polygon_dithered()/contrasting_outline_color()/
// draw_text_outlined() moved to features_layer.c (also used there), exposed
// via features_layer.h for this file's own countdown-label use.

// ---- big-analogue marker styles (procedural + bitmap) --------------------
// Moved into background_layer.c -- markers now draw as part of the sky
// canvas's own cached redraw (see the design note at the top of that
// file), not from here every tick.

// ---- big-analogue hand-style presets (procedural, styles 0-3) ------------
// Recreates the 4 non-custom big_analog_hand_style options through
// hand_layer.c's shared drawing code, rather than a separate procedural
// drawing function -- same simplification trade-off the recreated
// procedural marker presets in background_layer.c make: these are static
// approximations (~92px reference radius, a 200x228 screen) rather than
// proportional to the actual screen radius the way the original
// formula-driven draw_big_hand() was. outline_enabled/outline_color/
// translucent are left at their zero-value defaults here and filled in
// at the call site instead, from the (still-global, still applicable to
// these 4 styles) outline_enabled and big_analog_hands_transparent
// settings -- see hands_layer_update_proc.
static const HandConfig HAND_STYLE_HOUR_PRESETS[4] = {
  { .style = 1, .width = 12, .length = 51, .back_offset = 6, .color = 0 },                     // 0: pointy
  { .style = 2, .width = 8,  .length = 51, .back_offset = 6, .color = 0 },                     // 1: square
  { .style = 2, .width = 6,  .length = 51, .back_offset = 8, .color = 0, .hollow = true },      // 2: modern
  { .style = 0, .width = 6,  .length = 51, .back_offset = 0, .color = 1 },                     // 3: rounded/classic (accent hour hand)
};
static const HandConfig HAND_STYLE_MIN_PRESETS[4] = {
  { .style = 1, .width = 12, .length = 78, .back_offset = 6, .color = 0 },
  { .style = 2, .width = 8, .length = 78, .back_offset = 6, .color = 0 },
  { .style = 2, .width = 6,  .length = 78, .back_offset = 8, .color = 0, .hollow = true },
  { .style = 0, .width = 6, .length = 78, .back_offset = 0, .color = 0 },
};
// The 4 procedural styles never customized the second hand -- always a
// plain accent-colored thin line -- so one shared preset covers all of them.
static const HandConfig HAND_STYLE_SEC_PRESET = {
  .style = 1, .width = 2, .length = 85, .back_offset = 6, .color = 1,
};

// s_corner_custom_font state + corner_custom_font_resource_id()/
// ensure_corner_custom_font()/get_corner_font()/corner_font_height_estimate()/
// small_analog_feature_count() moved to features_layer.c (also used there
// for the corners/edges overlay itself); the latter two are exposed via
// features_layer.h since this file's hands layer and bottom panel still
// need them.



// The always-on-top overlay for big-analogue mode: hour/minute/second
// hands, optional edge tick markers, and an optional date readout
// behind the hands. Deliberately never fills its own background --
// left untouched, whatever the sky canvas underneath already drew
// shows straight through, the same "transparent overlay" pattern the
// countdown label already uses successfully elsewhere in this file.
// ---- startup clock animation --------------------------------------------
// User setting ("Style" section, on by default): on launch, the clock
// sweeps in from a cold-start position up to the real time instead of
// just appearing already showing it -- digital/small-analog count up
// from 00:00:00, big-analog's custom hands grow out from a center dot
// and sweep into place. Driven by a fast repeating AppTimer (this is
// the one place in the app that redraws faster than once a second,
// let alone once a minute -- deliberately bounded to under
// STARTUP_CLOCK_ANIM_MS and played at most once per app launch, so it
// doesn't become an ongoing battery cost).
#define STARTUP_CLOCK_ANIM_MS 1400
#define STARTUP_ANIM_FRAME_MS 40 // 25fps -- smooth enough for a <1.5s cosmetic sweep, not so fast it's a real battery concern for something this short
#define STARTUP_ANIM_PHASE_A_MS ((STARTUP_CLOCK_ANIM_MS * 3) / 10) // big-analog only: the "grow out from center" phase, see compute_startup_hand_anim()

static AppTimer *s_startup_anim_timer = NULL;
static bool s_startup_clock_anim_active = false;
static bool s_startup_clock_anim_played = false; // guards against replaying on every settings save/data refresh, not just app launch
static uint16_t s_startup_anim_elapsed_ms = 0;

// 0-1000 fixed-point "milli-progress" curves, matching this project's
// existing frac1000 convention elsewhere (e.g. draw_clouds_realistic's
// height_frac1000) rather than floating point.

// Starts slow, accelerates toward the end -- used for the digital-
// clock/small-analog "counting up from 00:00" effect in
// bottom_canvas_update_proc, so the displayed time visibly speeds up
// as it approaches the real one.
static int32_t ease_in_cubic_1000(int32_t t) {
  int64_t t64 = t;
  int32_t r = (int32_t)((t64 * t64 * t64) / 1000000);
  return (r > 1000) ? 1000 : r;
}

// Decelerates into the target, same as ease_in_cubic_1000 but mirrored.
static int32_t ease_out_cubic_1000(int32_t t) {
  int32_t inv = 1000 - t;
  int64_t inv3 = ((int64_t)inv * inv * inv) / 1000000;
  int32_t r = 1000 - (int32_t)inv3;
  return (r > 1000) ? 1000 : r;
}

// ease_out_cubic_1000 with a small decaying wiggle layered on top --
// approximates a spring "settle" (not true spring physics) for the
// big-analog hands' final rotation into place. The wiggle's own
// amplitude is scaled by (1-t)^2, so it's negligible right at the
// start, peaks around the middle of the curve, and decays to exactly
// 0 by t=1000 -- the hand still ends up at exactly the base curve's
// own endpoint (1000), just with a couple of visible wobbles along
// the way rather than a perfectly smooth glide.
static int32_t ease_out_wiggle_1000(int32_t t) {
  int32_t base = ease_out_cubic_1000(t);
  int32_t inv = 1000 - t;
  int32_t decay = (int32_t)(((int64_t)inv * inv) / 1000); // (1-t)^2, 0-1000 scale
  int32_t wiggle_angle = (int32_t)(((int64_t)t * TRIG_MAX_ANGLE * 5) / 1000); // ~2.5 oscillations across the curve
  int32_t wiggle = (int32_t)(((int64_t)sin_lookup(wiggle_angle) * decay) / TRIG_MAX_RATIO / 12); // small amplitude, ~4% of full range at peak
  return base + wiggle;
}

// Big-analog hands only: given a hand's real target angle and how far
// into the startup animation we are, returns the angle/length to
// actually draw it at this frame. Phase A (first
// STARTUP_ANIM_PHASE_A_MS): the hand grows from a center dot (length
// 0) out to full length while sweeping clockwise the short distance
// from -60deg into the 12 o'clock position (0deg) -- "appearing from
// center dot doing sweep clockwise to midnight position". Phase B
// (the rest): at full length, rotates from 12 o'clock to the real
// target angle via whichever direction (clockwise/counter-clockwise)
// is the shorter way around, with the wiggle-settle easing above.
static void compute_startup_hand_anim(int32_t target_angle, uint16_t elapsed_ms,
                                       int32_t *out_angle, uint16_t *out_length_scale_1000) {
  if (elapsed_ms <= STARTUP_ANIM_PHASE_A_MS) {
    int32_t p = ((int32_t)elapsed_ms * 1000) / STARTUP_ANIM_PHASE_A_MS;
    if (p > 1000) p = 1000;
    int32_t eased = ease_out_cubic_1000(p);
    *out_length_scale_1000 = (uint16_t)eased;
    int32_t start_angle = -(TRIG_MAX_ANGLE / 6); // -60deg, native units
    int32_t angle = start_angle + (int32_t)(((int64_t)(-start_angle) * eased) / 1000);
    if (angle < 0) angle += TRIG_MAX_ANGLE;
    *out_angle = angle;
  } else {
    *out_length_scale_1000 = 1000;
    uint16_t phase_b_elapsed = elapsed_ms - STARTUP_ANIM_PHASE_A_MS;
    uint16_t phase_b_total = STARTUP_CLOCK_ANIM_MS - STARTUP_ANIM_PHASE_A_MS;
    int32_t p = ((int32_t)phase_b_elapsed * 1000) / phase_b_total;
    if (p > 1000) p = 1000;
    int32_t eased = ease_out_wiggle_1000(p);
    // Shortest signed path from 0 (12 o'clock) to target_angle, in
    // native units (-TRIG_MAX_ANGLE/2 .. TRIG_MAX_ANGLE/2).
    int32_t delta = target_angle;
    if (delta > TRIG_MAX_ANGLE / 2) delta -= TRIG_MAX_ANGLE;
    int32_t angle = (int32_t)(((int64_t)delta * eased) / 1000);
    if (angle < 0) angle += TRIG_MAX_ANGLE;
    *out_angle = angle;
  }
}

static void hands_layer_update_proc(Layer *layer, GContext *ctx) {
  // Unobstructed (not full) bounds -- this layer has no background
  // fill of its own to worry about leaving gaps in (it's a pure
  // overlay on top of the sky canvas), so everything here can just
  // reposition/resize to fit whatever's actually visible right now,
  // shrinking gracefully when Timeline Quick View is showing.
  GRect bounds = layer_get_unobstructed_bounds(layer);
  GPoint center = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + bounds.size.h / 2);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  GColor bg, main_color, accent_color;
  get_active_color_scheme(&s_data, now, &bg, &main_color, &accent_color);

  // Markers (procedural presets, custom, and bitmap styles alike) are no
  // longer drawn here -- they're part of the sky canvas's own cached
  // redraw now (background_layer.c), composited once per its own
  // once-a-minute/force-redraw cadence rather than every tick this
  // always-on-top hands layer runs.
  ensure_corner_custom_font(s_data.corner_custom_font);

  int32_t hour_angle = (int32_t)(((int64_t)((t->tm_hour % 12) * 3600 + t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600));

  int32_t min_angle = ((t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);

  int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;

  // Startup animation (big-analog only): substitutes each hand's real
  // target angle with an in-progress one, plus how long that hand
  // currently is -- see compute_startup_hand_anim()'s own comment.
  // 1000 = full length/no substitution for a normal, non-animated draw.
  uint16_t hour_length_scale_1000 = 1000, min_length_scale_1000 = 1000, sec_length_scale_1000 = 1000;
  if (s_startup_clock_anim_active) {
    compute_startup_hand_anim(hour_angle, s_startup_anim_elapsed_ms, &hour_angle, &hour_length_scale_1000);
    compute_startup_hand_anim(min_angle, s_startup_anim_elapsed_ms, &min_angle, &min_length_scale_1000);
    compute_startup_hand_anim(sec_angle, s_startup_anim_elapsed_ms, &sec_angle, &sec_length_scale_1000);
  }

  // All 5 hand styles now go through hand_layer_draw() -- custom (4) uses
  // the user's own per-hand settings; 0-3 use one of the hardcoded
  // HAND_STYLE_*_PRESETS above, with the still-global hand settings
  // (outline_enabled, big_analog_hands_transparent, big_analog_hands_shadow)
  // applied uniformly to all 3 hands, matching what draw_big_hand_outlined()
  // used to do.
  HandConfig hour_cfg, min_cfg, sec_cfg;
  if (s_data.big_analog_hand_style == 4) {
    hour_cfg = s_data.hand_hour;
    min_cfg = s_data.hand_minute;
    sec_cfg = s_data.hand_second;
  } else {
    uint8_t idx = (s_data.big_analog_hand_style <= 3) ? s_data.big_analog_hand_style : 0;
    hour_cfg = HAND_STYLE_HOUR_PRESETS[idx];
    min_cfg = HAND_STYLE_MIN_PRESETS[idx];
    sec_cfg = HAND_STYLE_SEC_PRESET;
    hour_cfg.translucent = min_cfg.translucent = sec_cfg.translucent = s_data.big_analog_hands_transparent;
    hour_cfg.outline_enabled = min_cfg.outline_enabled = sec_cfg.outline_enabled = s_data.outline_enabled;
    // contrasting_outline_color() picked black/white dynamically by luma;
    // the closest fixed equivalent in HandConfig's 3-option scheme-color
    // enum is the scheme's own background, which is high-contrast against
    // its text/accent colors in every built-in scheme.
    hour_cfg.outline_color = min_cfg.outline_color = sec_cfg.outline_color = 2;
    // Shadow: one shared on/off toggle like outline_enabled above, but
    // distance is hardcoded rather than user-adjustable for the
    // procedural presets -- only the custom hand system (hand_hour/
    // hand_minute/hand_second above) exposes that as a slider. Angle
    // is never per-hand at all -- see hand_layer_draw()'s own comment.
    hour_cfg.shadow_enabled = min_cfg.shadow_enabled = sec_cfg.shadow_enabled = s_data.big_analog_hands_shadow;
    hour_cfg.shadow_distance_px = min_cfg.shadow_distance_px = sec_cfg.shadow_distance_px = 2;
  }

  hand_layer_draw(ctx, center, hour_angle, &hour_cfg, main_color, accent_color, bg, s_data.shadow_translucent, s_data.shadow_angle_deg, hour_length_scale_1000);
  hand_layer_draw(ctx, center, min_angle, &min_cfg, main_color, accent_color, bg, s_data.shadow_translucent, s_data.shadow_angle_deg, min_length_scale_1000);
  if (s_data.show_seconds) {
    hand_layer_draw(ctx, center, sec_angle, &sec_cfg, main_color, accent_color, bg, s_data.shadow_translucent, s_data.shadow_angle_deg, sec_length_scale_1000);
  }

  if (s_data.big_analog_hand_style == 4) {
    hand_layer_draw_center_circle(ctx, center, s_data.center_circle_radius, s_data.center_circle_color,
                                   main_color, accent_color, bg);
  } else {
    graphics_context_set_fill_color(ctx, main_color);
    graphics_fill_circle(ctx, center, 3);
  }
}

// ---- corners/edges feature overlay ---------------------------------------
// The whole always-on-top text/icon overlay (icon bitmaps, weather/
// timezone/gradient/sleep helpers, features_draw_item(), the metadata
// cache, and the layer itself) now lives in features_layer.c/.h -- see
// that file's own top-of-file note. s_features_layer below is now
// s_features_layer, created via features_layer_create()/destroyed via
// features_layer_destroy() in apply_layout()/window_unload(), and fed
// with features_layer_set_data()/features_layer_set_labels_visible()
// instead of being recomputed on every redraw.


// ---- rendering ---------------------------------------------------------

// The bottom third of the face: either the classic big-time-plus-date
// layout, or (bottom_style == 1) a split view with an analog clock on
// the left and four lines of text on the right. Both obey the color
// scheme and the seconds-visibility setting. Redrawn every second
// when seconds are shown; otherwise still cheap enough (no astronomy,
// just text/line drawing) not to bother throttling separately from
// the sky canvas above it.
static void bottom_canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Startup animation (digital + small-analog): substitutes an eased,
  // sped-up count-up from midnight to the real time for the DISPLAYED
  // time only -- `now`/the color scheme below still use the real
  // current time. Every read of `t->tm_hour`/`tm_min`/`tm_sec` below
  // (both the digital HH:MM(:SS) text and the small-analog hand
  // angles further down) shares this one struct tm, so both get the
  // count-up for free from this single substitution.
  struct tm anim_tm;
  if (s_startup_clock_anim_active) {
    int32_t progress = ((int32_t)s_startup_anim_elapsed_ms * 1000) / STARTUP_CLOCK_ANIM_MS;
    if (progress > 1000) progress = 1000;
    int32_t eased = ease_in_cubic_1000(progress);
    int32_t seconds_since_midnight = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
    int32_t fake_seconds = (int32_t)(((int64_t)seconds_since_midnight * eased) / 1000);
    time_t midnight = now - seconds_since_midnight;
    time_t fake_time = midnight + fake_seconds;
    anim_tm = *localtime(&fake_time);
    t = &anim_tm;
  }

  GColor bg, text_color, accent_color;
  get_active_color_scheme(&s_data, now, &bg, &text_color, &accent_color);

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  char time_buf[10];
  if (s_data.show_seconds && !use_small_seconds_for_digital_clock()) {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S", t);
  } else {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  }
  char sec_buf[4];
  snprintf(sec_buf, sizeof(sec_buf), "%d\n%d",
           t->tm_sec / 10,
           t->tm_sec % 10);

  if (s_data.bottom_style == 1) {
    // ---- analog: clock on the left half, 4 lines of text on the right ----
    int16_t half_w = bounds.size.w / 2;
    GPoint clock_center = GPoint(bounds.origin.x + half_w / 2, bounds.origin.y + bounds.size.h / 2);
    int16_t clock_r = (bounds.size.h / 2) - 6;
    if (clock_r > half_w / 2 - 6) clock_r = half_w / 2 - 6;

    // Face style: 0=solid circle, 1=hour markers, 2=both, 3=tiny
    // procedural 12/3/6/9 numerals instead of any circle/markers.
    if (s_data.analog_style == 0 || s_data.analog_style == 2) {
      graphics_context_set_stroke_color(ctx, text_color);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_circle(ctx, clock_center, clock_r);
    }
    if (s_data.analog_style == 1 || s_data.analog_style == 2) {
      draw_hour_markers(ctx, clock_center, clock_r, text_color);
    }
    if (s_data.analog_style == 3) {
      int16_t px = 1; // "super tiny" -- 3x5px digits at 1px/dot
      int16_t label_r = clock_r - 7;
      draw_tiny_number(ctx, GPoint(clock_center.x, clock_center.y - label_r), 12, text_color, px);
      draw_tiny_number(ctx, GPoint(clock_center.x + label_r, clock_center.y), 3, text_color, px);
      draw_tiny_number(ctx, GPoint(clock_center.x, clock_center.y + label_r), 6, text_color, px);
      draw_tiny_number(ctx, GPoint(clock_center.x - label_r, clock_center.y), 9, text_color, px);
    }

    int32_t hour_angle = (((t->tm_hour % 12) * 60 + t->tm_min) * TRIG_MAX_ANGLE) / (12 * 60);
    int16_t hour_len = (clock_r * 55) / 100;
    GPoint hour_end = GPoint(
      clock_center.x + (hour_len * sin_lookup(hour_angle)) / TRIG_MAX_RATIO,
      clock_center.y - (hour_len * cos_lookup(hour_angle)) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, text_color);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, clock_center, hour_end);

    int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE) / 60;
    int16_t min_len = (clock_r * 80) / 100;
    GPoint min_end = GPoint(
      clock_center.x + (min_len * sin_lookup(min_angle)) / TRIG_MAX_RATIO,
      clock_center.y - (min_len * cos_lookup(min_angle)) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, clock_center, min_end);

    if (s_data.show_seconds) {
      int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;
      int16_t sec_len = (clock_r * 88) / 100;
      GPoint sec_end = GPoint(
        clock_center.x + (sec_len * sin_lookup(sec_angle)) / TRIG_MAX_RATIO,
        clock_center.y - (sec_len * cos_lookup(sec_angle)) / TRIG_MAX_RATIO);
      graphics_context_set_stroke_color(ctx, accent_color);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_line(ctx, clock_center, sec_end);
    }

    graphics_context_set_fill_color(ctx, text_color);
    graphics_fill_circle(ctx, clock_center, 3);

    // ---- 4 user-picked feature rows, right-aligned -------------------
    // These reuse the same 4 content/color_mode fields (and the same
    // phone-side message keys) that big-analogue mode's upper-middle
    // and bottom-middle 2-line slots use -- upper_middle_line1/2 as
    // rows 1-2, bottom_middle_line1/2 as rows 3-4. That pairing is
    // deliberate rather than incidental: neither pair ever actually
    // renders in small-analog mode (corners_layer_update_proc only
    // shows the edge-middle slots when bottom_style == 2), so they're
    // sitting completely idle here, and reusing them means no new
    // persisted fields or message keys were needed for this panel.
    // One real consequence: since it's the same storage, whatever a
    // user picks for upper/bottom-middle line 1/2 while in big-analog
    // mode is exactly what they'll see as rows 1-4 here if they ever
    // switch to small-analog (and vice versa) -- the settings page's
    // slot picker makes this explicit rather than hiding it.
    //
    // draw_corner_item() draws each row itself (icon + text, left-
    // aligned via is_left=true, anchored to the right half of the
    // panel bounds -- matching where the old 4 fixed lines used to
    // start, just after the clock face) rather than the plain
    // graphics_draw_text() those 4 fixed lines used to get -- that's
    // what makes the full corner/edge content list (weather, health,
    // timezones, date formats, ...) available here instead of just
    // the 4 old fixed readouts. allow_outline is false: this panel
    // sits on its own solid background color, not over the busy sky
    // view, so it never needs the contrasting outline the corners/
    // edges rely on.
    ensure_corner_custom_font(s_data.corner_custom_font);
    uint8_t feature_count = small_analog_feature_count(&s_data);
    const uint8_t feature_content[4] = {
      s_data.upper_middle_line1_content, s_data.upper_middle_line2_content,
      s_data.bottom_middle_line1_content, s_data.bottom_middle_line2_content
    };
    const uint8_t feature_color_mode[4] = {
      s_data.upper_middle_line1_color_mode, s_data.upper_middle_line2_color_mode,
      s_data.bottom_middle_line1_color_mode, s_data.bottom_middle_line2_color_mode
    };
    // Restricted to the right half of the panel so is_left's "+2 from
    // the bounds' own left edge" anchors right after the clock face,
    // not at the screen's actual left edge.
    GRect feature_bounds = GRect(bounds.origin.x + half_w, bounds.origin.y,
                                  bounds.size.w - half_w, bounds.size.h);
    int16_t line_h = bounds.size.h / feature_count;
    for (int i = 0; i < feature_count; i++) {
      int16_t row_top = i * line_h + (line_h - CORNER_ROW_H) / 2;
      features_draw_item(ctx, feature_bounds, &s_data, feature_content[i], feature_color_mode[i],
                          text_color, accent_color, bg,
                          true, true, false, row_top, 0, false, false, false);
    }
  } else {
    // ---- digital: big time, small date/week below ----
    graphics_context_set_text_color(ctx, text_color);
    if (s_data.show_seconds && use_small_seconds_for_digital_clock()) {
      graphics_draw_text(ctx, time_buf, clock_font,
                          GRect(bounds.origin.x, bounds.origin.y + get_clock_font_height_offset(), bounds.size.w - 20, 60),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      graphics_context_set_text_color(ctx, accent_color);
      graphics_draw_text(ctx, sec_buf, small_font,
                         GRect(bounds.origin.x + bounds.size.w - 22, bounds.origin.y + 15 + get_small_font_height_offset() * 2, 20, 50),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    } else {
      graphics_draw_text(ctx, time_buf, clock_font,
                          GRect(bounds.origin.x, bounds.origin.y + get_clock_font_height_offset(), bounds.size.w, 60),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    char main_buf[32];
    strftime(main_buf, sizeof(main_buf), "%a %b %d", t);

    // Computed before any second localtime() call below, since that
    // returns a pointer to a shared static buffer and would otherwise
    // clobber `t` before main_buf got built from it.
    time_t sun_event_time = 0;
    bool sun_event_is_rise = false;
    bool show_sun_row = s_data.show_sun_time &&
      get_next_sun_event(now, s_data.sun_rise, s_data.sun_set, s_data.sun_rise_tomorrow, &sun_event_time, &sun_event_is_rise);

    graphics_context_set_text_color(ctx, text_color);
    if (show_sun_row) {
      char sun_time_buf[8];
      struct tm *event_t = localtime(&sun_event_time);
      strftime(sun_time_buf, sizeof(sun_time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", event_t);

      int16_t left_w = (bounds.size.w * 55) / 100;
      graphics_draw_text(ctx, main_buf, small_font,
                          GRect(bounds.origin.x, bounds.origin.y + 60 + get_small_font_height_offset(), left_w, 16),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
      int16_t icon_x = bounds.origin.x + left_w + 6;
      int16_t icon_y = bounds.origin.y + 60 + 5;
      int16_t icon_w = draw_sun_time_icon(ctx, GPoint(icon_x, icon_y), sun_event_is_rise, text_color, bg);
      graphics_context_set_text_color(ctx, text_color);
      graphics_draw_text(ctx, sun_time_buf, small_font,
                          GRect(icon_x + icon_w + 3, bounds.origin.y + 60 + get_small_font_height_offset(), bounds.size.w - (icon_x + icon_w + 3), 16),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    } else {
      char week_buf[8];
      strftime(week_buf, sizeof(week_buf), "%V", t);
      char date_buf[40];
      snprintf(date_buf, sizeof(date_buf), "%s  -  Wk%s", main_buf, week_buf);
      graphics_draw_text(ctx, date_buf, small_font,
                          GRect(bounds.origin.x, bounds.origin.y + 60 + get_small_font_height_offset(), bounds.size.w, 16),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }
}

// The countdown/status label used to be a plain TextLayer, but that
// has no way to draw a custom outline, so it's a plain Layer with its
// own update_proc instead -- reads whatever refresh_status_and_maybe_
// canvas() last stored in s_countdown_buf/s_countdown_text_color
// rather than taking them as parameters, since layer update_procs
// have a fixed signature.
static void countdown_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // In big-analog mode this label floats directly over the busy sky
  // view. Normally draw_text_outlined()'s 4-shifted-copy outline keeps
  // it legible against any background there, but with that setting
  // off there's nothing else backing the text, so it can disappear
  // into a similarly-colored patch of sky. Give it a solid pill
  // background in that specific case instead (contrasting_outline_color()
  // picks black or white, whichever contrasts with the text color) --
  // outline mode already handles legibility fine on its own, and
  // outside big-analog mode the bottom bar/panel is already a solid
  // color the text sits on, so neither of those needs this extra
  // background.
  if (!s_data.outline_enabled && s_data.bottom_style == 2 && s_countdown_buf[0] != '\0') {
    GSize text_size = graphics_text_layout_get_content_size(s_countdown_buf, font, bounds,
                                                              GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
    int16_t pad_x = 6;
    GRect bg_rect = GRect(bounds.origin.x + (bounds.size.w - text_size.w) / 2 - pad_x,
                           bounds.origin.y, text_size.w + pad_x * 2, bounds.size.h);
    graphics_context_set_fill_color(ctx, contrasting_outline_color(s_countdown_text_color));
    graphics_fill_rect(ctx, bg_rect, 4, GCornersAll);
  }

  draw_text_outlined(ctx, s_countdown_buf, font, bounds,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                      s_countdown_text_color, s_data.outline_enabled);
}

static void refresh_status_and_maybe_canvas(bool force_canvas) {
  time_t now = time(NULL);
  eclipse_get_status_text(&s_data, now, s_countdown_buf, sizeof(s_countdown_buf));
  // The countdown label overlays the sky canvas transparently, so its
  // own contrast needs to track the sky brightness underneath it --
  // this check is cheap (no drawing), so it's fine to do every second.
  s_countdown_text_color = eclipse_sky_is_bright(&s_data, now) ? GColorBlack : GColorWhite;
  // Hidden entirely (not just left blank) when there's confirmed to
  // be no eclipse today -- this field is purely about eclipse phases
  // now (see eclipse_get_status_text), so there's nothing for it to
  // show in that case. Error/loading states still display normally,
  // since those aren't "no eclipse," they're "don't know yet."
  layer_set_hidden(s_countdown_layer, s_data.valid && !s_data.has_eclipse);
  layer_mark_dirty(s_countdown_layer);

  // The bottom canvas (time/date, or the analog clock) is cheap
  // enough (no astronomy) to just redraw every second directly. Only
  // exists in digital/analog mode -- big-analogue mode has no bottom
  // bar, and redraws its hands layer every second instead, for the
  // same reason (cheap, no astronomy, ticks smoothly).
  if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  if (s_hands_layer) layer_mark_dirty(s_hands_layer);

  // New data (or an explicit force, e.g. right after window_load)
  // always redraws immediately; otherwise just nudge the canvas every
  // second and let its own internal once-a-minute throttle (tracked
  // inside canvas_update_proc itself) decide whether that actually
  // costs a redraw -- this is the single biggest lever on battery
  // life for this watchface, and now holds regardless of what
  // triggers the nudge, not just this call site's discipline.
  if (force_canvas) {
    eclipse_canvas_set_data(s_canvas_layer, &s_data);
  } else {
    eclipse_canvas_tick(s_canvas_layer);
  }
}

// ---- persistence --------------------------------------------------------

// Build-time guard: if EclipseData ever outgrows the chunk capacity
// above again (the same kind of growth that caused the original
// persistence bug -- the struct reached 776 bytes against Pebble's
// 256-byte-per-key limit before this was caught), this deliberately
// fails to compile instead of silently truncating what gets saved.
typedef char persist_capacity_check[
  (PERSIST_CHUNK_SIZE * PERSIST_CHUNK_COUNT >= (int)sizeof(EclipseData)) ? 1 : -1];

static void save_data(void) {
  const uint8_t *bytes = (const uint8_t *)&s_data;
  size_t remaining = sizeof(s_data);
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t chunk_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    persist_write_data(PERSIST_KEY_DATA_BASE + i, bytes + (size_t)i * PERSIST_CHUNK_SIZE, chunk_len);
    remaining -= chunk_len;
  }
}

// Only trusts a reload when every chunk key that a full, same-version
// save would have produced is present and reports exactly the size
// expected for its position -- an old struct layout (different total
// size, so a different chunk count/sizing) or a save that was
// interrupted partway through won't pass this and the app stays at
// the zeroed (invalid) state instead, which correctly shows "Waiting
// for phone..." rather than loading a partial/misaligned struct. See
// the comment on EclipseData's growth history for why this matters:
// a mismatched reload doesn't just leave new fields at zero, it can
// misread old bytes as entirely different fields.
static void load_data(void) {
  memset(&s_data, 0, sizeof(s_data));
  size_t total = sizeof(s_data);
  size_t remaining = total;
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t expect_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    if (!persist_exists(PERSIST_KEY_DATA_BASE + i) || persist_get_size(PERSIST_KEY_DATA_BASE + i) != (int)expect_len) {
      return;
    }
    remaining -= expect_len;
  }
  uint8_t *bytes = (uint8_t *)&s_data;
  remaining = total;
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t chunk_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    persist_read_data(PERSIST_KEY_DATA_BASE + i, bytes + (size_t)i * PERSIST_CHUNK_SIZE, chunk_len);
    remaining -= chunk_len;
  }
}

// ---- AppMessage ---------------------------------------------------------

static void request_update(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
  app_message_outbox_send();
}

// Retries request_update() with capped exponential backoff (8s, 16s,
// 32s, then settling at 60s) for as long as s_data.valid stays false
// -- covers both a plain race (PKJS not ready yet for the very first
// request) and the update-to-new-message-keys case: a phone still
// running the OLD version of index.js after the watch app itself
// updated would never recognize/answer a request built around new
// keys, leaving the watch waiting on an empty screen indefinitely
// with nothing to prompt it to try again. Stopped for good (see
// inbox_received_handler) the moment real data actually arrives.
static AppTimer *s_request_retry_timer = NULL;
static uint16_t s_request_retry_delay_s = 8;

static void request_retry_callback(void *data) {
  s_request_retry_timer = NULL;
  if (s_data.valid) return; // got real data in the meantime -- nothing to do
  request_update();
  s_request_retry_delay_s = (s_request_retry_delay_s < 60) ? s_request_retry_delay_s * 2 : 60;
  s_request_retry_timer = app_timer_register((uint32_t)s_request_retry_delay_s * 1000, request_retry_callback, NULL);
}


static void startup_anim_timer_callback(void *data) {
  s_startup_anim_elapsed_ms += STARTUP_ANIM_FRAME_MS;
  if (s_startup_anim_elapsed_ms >= STARTUP_CLOCK_ANIM_MS) {
    s_startup_clock_anim_active = false;
    s_startup_anim_timer = NULL;
  } else {
    s_startup_anim_timer = app_timer_register(STARTUP_ANIM_FRAME_MS, startup_anim_timer_callback, NULL);
  }
  if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
}

// Called once from window_load(), after the layers it needs to mark
// dirty already exist. A no-op (and leaves s_startup_clock_anim_active
// false) if the setting is off, or if this app session already played
// it once -- a settings save or a fresh data push shouldn't replay it.
static void maybe_start_startup_clock_animation(void) {
  if (s_startup_clock_anim_played || !s_data.startup_clock_animation_enabled) return;
  s_startup_clock_anim_played = true;
  s_startup_clock_anim_active = true;
  s_startup_anim_elapsed_ms = 0;
  s_startup_anim_timer = app_timer_register(STARTUP_ANIM_FRAME_MS, startup_anim_timer_callback, NULL);
}

// ---- startup background animation ---------------------------------------
// User setting ("Style" section, off by default): on launch, the Sun/
// Moon/planets/sky gradient sweep in from where they were a couple
// hours ago up to their real current state, clouds slide in from the
// side, and markers/text markers animate in from off-screen/zero --
// see background_layer.c's canvas_update_proc (the sun/moon/planet/sky
// sweep itself, driven by eclipse_canvas_set_bg_anim() below) and
// draw_all_markers()/draw_text_markers() (the marker/text-marker
// pieces). A separate timer/state pair from the clock animation above
// -- the two settings are independent, and either, both, or neither
// can be on -- but the same "fast timer, bounded duration, played once
// per session" shape.
#define BG_ANIM_MS 1400
#define BG_ANIM_FRAME_MS 40 // matches STARTUP_ANIM_FRAME_MS -- see its own comment

static AppTimer *s_bg_anim_timer = NULL;
static bool s_bg_anim_active = false;
static bool s_bg_anim_played = false;
static uint16_t s_bg_anim_elapsed_ms = 0;

static void bg_anim_timer_callback(void *data) {
  s_bg_anim_elapsed_ms += BG_ANIM_FRAME_MS;
  if (s_bg_anim_elapsed_ms >= BG_ANIM_MS) {
    s_bg_anim_active = false;
    s_bg_anim_timer = NULL;
  } else {
    s_bg_anim_timer = app_timer_register(BG_ANIM_FRAME_MS, bg_anim_timer_callback, NULL);
  }
  // eclipse_canvas_set_bg_anim() forces the sky canvas to actually
  // redraw every frame despite its own once-a-minute throttle, same
  // "explicit force + mark dirty" shape as eclipse_canvas_set_data()/
  // eclipse_canvas_set_show_labels() already use for their own reasons.
  if (s_canvas_layer) eclipse_canvas_set_bg_anim(s_canvas_layer, s_bg_anim_active, s_bg_anim_elapsed_ms);
}

static void maybe_start_startup_background_animation(void) {
  if (s_bg_anim_played || !s_data.startup_background_animation_enabled) return;
  s_bg_anim_played = true;
  s_bg_anim_active = true;
  s_bg_anim_elapsed_ms = 0;
  if (s_canvas_layer) eclipse_canvas_set_bg_anim(s_canvas_layer, true, 0);
  s_bg_anim_timer = app_timer_register(BG_ANIM_FRAME_MS, bg_anim_timer_callback, NULL);
}

static bool use_small_seconds_for_digital_clock() {
  switch (s_data.clock_font) {
    case 2: // RESOURCE_ID_SFPIXELATE_FONT_48
    case 3: // RESOURCE_ID_RADIOLAND_FONT_48
    case 4: // RESOURCE_ID_MINISYSTEM_FONT_48
    case 5: // RESOURCE_ID_MINECRAFTER_FONT_48
    case 6: // RESOURCE_ID_KITCHENPOLICE_FONT_48
    case 12: // RESOURCE_ID_AUDIOWIDE_FONT_48 // good
    case 14: // RESOURCE_ID_KOMIKAHB_FONT_48 // great one
      return true;
    case 1: //RESOURCE_ID_CLOCKFORGE_FONT_48
    case 7: // RESOURCE_ID_DSDIGIB_FONT_48
    case 8: // RESOURCE_ID_DISTGRG_FONT_48
    case 9: // RESOURCE_ID_DIMITRI_FONT_48
    case 10: // RESOURCE_ID_DIGITALDREAM_FONT_48
    case 11: // RESOURCE_ID_BLACKOUT_FONT_48 // i like this
    case 13: // RESOURCE_ID_FORMATION_FONT_48 // looks ok
    case 15: // RESOURCE_ID_MISO_FONT_48 // good one
    case 16: // RESOURCE_ID_PRICEDOWN_FONT_48 // gta vibes, good one
    case 17: // FONT_KEY_ROBOTO_BOLD_SUBSET_49 // roboto
    case 18: // FONT_KEY_BITHAM_42_LIGHT // bitham light
    case 19: // FONT_KEY_BITHAM_42_BOLD // bitham bold
    case 20: // RESOURCE_ID_BEBAS_FONT_48 // tally font
    default: // FONT_KEY_LECO_42_NUMBERS
      return false;
  }
}

static int get_small_font_height_offset() {
  switch (s_data.clock_font) {
    case 1: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return 0;
    case 2: // FONT_KEY_GOTHIC_14
      return 0;
    case 3: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return -2;
    case 4: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return -2;
    case 5: // RESOURCE_ID_MINECRAFTER_FONT_12
      return 0;
    case 6: // FONT_KEY_GOTHIC_14
      return 0;
    case 7: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return -2;
    case 8: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 9: // FONT_KEY_GOTHIC_14
      return -2;
    case 10: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return 0;
    case 11: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 12: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 13: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 14: // RESOURCE_ID_MISO_FONT_19
      return -4;
    case 15: // RESOURCE_ID_MISO_FONT_19
      return -4;
    case 16: // RESOURCE_ID_MINECRAFTER_FONT_12
      return -2;
    case 17: // FONT_KEY_ROBOTO_CONDENSED_21
      return -2;
    case 18: // FONT_KEY_GOTHIC_14
      return 0;
    case 19: // FONT_KEY_GOTHIC_14
      return 0;
    case 20: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    default: // FONT_KEY_GOTHIC_14
      return 0;
  }
}

static int get_clock_font_height_offset() {
  switch (s_data.clock_font) {
    case 5: // RESOURCE_ID_MINECRAFTER_FONT_48
      return 4;
    case 8: // RESOURCE_ID_DISTGRG_FONT_48
      return -6;
    case 9: // RESOURCE_ID_DIMITRI_FONT_48
    case 11: // RESOURCE_ID_BLACKOUT_FONT_48 // i like this
    case 12: // RESOURCE_ID_AUDIOWIDE_FONT_48 // good
    case 13: // RESOURCE_ID_FORMATION_FONT_48 // looks ok
    case 17: // FONT_KEY_ROBOTO_BOLD_SUBSET_49 // roboto
    case 10: // RESOURCE_ID_DIGITALDREAM_FONT_48
      return -2;
    case 15: // RESOURCE_ID_MISO_FONT_48 // good one
    case 7: // RESOURCE_ID_DSDIGIB_FONT_48
      return -4;
    case 1: //RESOURCE_ID_CLOCKFORGE_FONT_48
    case 2: // RESOURCE_ID_SFPIXELATE_FONT_48
    case 3: // RESOURCE_ID_RADIOLAND_FONT_48
    case 4: // RESOURCE_ID_MINISYSTEM_FONT_48
    case 6: // RESOURCE_ID_KITCHENPOLICE_FONT_48
    case 14: // RESOURCE_ID_KOMIKAHB_FONT_48 // great one
    case 16: // RESOURCE_ID_PRICEDOWN_FONT_48 // gta vibes, good one
    case 18: // FONT_KEY_BITHAM_42_LIGHT // bitham light
    case 19: // FONT_KEY_BITHAM_42_BOLD // bitham bold
    case 20: // RESOURCE_ID_BEBAS_FONT_48 // tally font
    default: // FONT_KEY_LECO_42_NUMBERS
      return 0;
  }
}

static void apply_clock_font(void) {
  if (clock_font_custom) {
    fonts_unload_custom_font(clock_font);
  }
  if (small_font_custom) {
    fonts_unload_custom_font(small_font);
  }
  
  if (s_data.bottom_style == 1) { // small analog + 4 data rows
    clock_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 1) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_CLOCKFORGE_FONT_48)); // looks kinda good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 2) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_SFPIXELATE_FONT_48)); // looks good
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = true;
    small_font_custom = false;
  } else if (s_data.clock_font == 3) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_RADIOLAND_FONT_48)); // looks good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 4) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINISYSTEM_FONT_48)); // looks kinda interesting
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 5) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_48)); // weird, but ok
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 6) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_KITCHENPOLICE_FONT_48)); // good
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = true;
    small_font_custom = false;
  } else if (s_data.clock_font == 7) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DSDIGIB_FONT_48)); // very good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 8) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DISTGRG_FONT_48)); // good, star wars wibes
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 9) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIMITRI_FONT_48)); // good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 10) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_48)); // wideee but good!
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 11) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BLACKOUT_FONT_48)); // i like this
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 12) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_AUDIOWIDE_FONT_48)); // good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 13) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FORMATION_FONT_48)); // looks ok
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 14) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_KOMIKAHB_FONT_48)); // great one
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MISO_FONT_19));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 15) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MISO_FONT_48)); // good one
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MISO_FONT_19));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 16) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_PRICEDOWN_FONT_48)); // gta vibes, good one
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 17) {
    clock_font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49); // roboto
    small_font = fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 18) {
    clock_font = fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT); // bitham light
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 19) {
    clock_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD); // bitham bold
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 20) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_48)); // tally font
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else {
    clock_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  }

  if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_DATA_VALID))) {
    s_data.valid = t->value->uint8 != 0;
    if (s_data.valid && s_request_retry_timer) {
      // Real data made it through -- no need to keep pinging PKJS
      // for it anymore (see request_retry_callback's own comment).
      app_timer_cancel(s_request_retry_timer);
      s_request_retry_timer = NULL;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ERROR_CODE))) {
    s_data.error_code = t->value->uint8;
  }
  // Parsed (and applied) before the early-return below so a
  // font-only settings update still takes effect even if the watch
  // hasn't received a valid eclipse payload yet.
  if ((t = dict_find(iter, MESSAGE_KEY_CLOCK_FONT))) {
    s_data.clock_font = t->value->uint8;
    apply_clock_font();
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TEMP_UNIT))) {
    s_data.temp_unit = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_SPEED_UNIT))) {
    s_data.wind_speed_unit = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_SECONDS))) {
    s_data.show_seconds = t->value->uint8 != 0;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_STYLE))) {
    s_data.bottom_style = t->value->uint8;
    apply_layout(); // may need to tear down/rebuild layers entirely -- see its own comment
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ANALOG_STYLE))) {
    s_data.analog_style = t->value->uint8;
    if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_MOON_SIZE_PCT))) {
    s_data.sun_moon_size_pct = t->value->uint8;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_RENDER_STYLE))) {
    s_data.cloud_render_style = t->value->uint8;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_MODE))) {
    s_data.sky_mode = t->value->uint8;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHAKE_LABEL_SECONDS))) {
    s_data.shake_label_seconds = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_LABEL_STYLE))) {
    s_data.label_style = t->value->uint8;
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_INFO_BAR_MODE))) {
    s_data.bottom_info_bar_mode = t->value->uint8;
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_VIBRATE_ON_PHASE_CHANGE))) {
    s_data.vibrate_on_phase_change = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_STARTUP_CLOCK_ANIMATION_ENABLED))) {
    s_data.startup_clock_animation_enabled = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_STARTUP_BACKGROUND_ANIMATION_ENABLED))) {
    s_data.startup_background_animation_enabled = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHAKE_ANIMATION_ENABLED))) {
    s_data.shake_animation_enabled = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_OUTLINE_ENABLED))) {
    s_data.outline_enabled = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_FONT_SIZE))) {
    s_data.corner_font_size = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_CUSTOM_FONT))) {
    s_data.corner_custom_font = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_HAND_STYLE))) {
    s_data.big_analog_hand_style = t->value->uint8;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_HANDS_TRANSPARENT))) {
    s_data.big_analog_hands_transparent = t->value->uint8 != 0;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_HANDS_SHADOW))) {
    s_data.big_analog_hands_shadow = t->value->uint8 != 0;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHADOW_TRANSLUCENT))) {
    s_data.shadow_translucent = t->value->uint8 != 0;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHADOW_ANGLE))) {
    s_data.shadow_angle_deg = t->value->uint16;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DRAW_FEATURES_BENEATH_HANDS))) {
    s_data.draw_features_beneath_hands = t->value->uint8 != 0;
    apply_layout(); // re-orders the hands/features layers if this actually changed -- see its own comment
  }
  // Custom hand system (big_analog_hand_style == 4) -- see hand_layer.h.
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_STYLE))) s_data.hand_hour.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_WIDTH))) s_data.hand_hour.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_LENGTH))) s_data.hand_hour.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_BACK_OFFSET))) s_data.hand_hour.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_COLOR))) s_data.hand_hour.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_OUTLINE_ENABLED))) s_data.hand_hour.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_OUTLINE_COLOR))) s_data.hand_hour.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_TRANSLUCENT))) s_data.hand_hour.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_SHADOW_ENABLED))) s_data.hand_hour.shadow_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_SHADOW_DISTANCE))) s_data.hand_hour.shadow_distance_px = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_STYLE))) s_data.hand_minute.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_WIDTH))) s_data.hand_minute.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_LENGTH))) s_data.hand_minute.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_BACK_OFFSET))) s_data.hand_minute.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_COLOR))) s_data.hand_minute.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_OUTLINE_ENABLED))) s_data.hand_minute.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_OUTLINE_COLOR))) s_data.hand_minute.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_TRANSLUCENT))) s_data.hand_minute.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_SHADOW_ENABLED))) s_data.hand_minute.shadow_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_SHADOW_DISTANCE))) s_data.hand_minute.shadow_distance_px = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_STYLE))) s_data.hand_second.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_WIDTH))) s_data.hand_second.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_LENGTH))) s_data.hand_second.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_BACK_OFFSET))) s_data.hand_second.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_COLOR))) s_data.hand_second.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_OUTLINE_ENABLED))) s_data.hand_second.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_OUTLINE_COLOR))) s_data.hand_second.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_TRANSLUCENT))) s_data.hand_second.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_SHADOW_ENABLED))) s_data.hand_second.shadow_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_SHADOW_DISTANCE))) s_data.hand_second.shadow_distance_px = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CENTER_CIRCLE_RADIUS))) s_data.center_circle_radius = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CENTER_CIRCLE_COLOR))) s_data.center_circle_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_MARKER_STYLE))) {
    s_data.big_analog_marker_style = t->value->uint8;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  // Custom marker system (big_analog_marker_style == 8) -- see eclipse_data.h
  // (MarkerRingConfig/MarkerTextConfig) and background_layer.c (where these
  // now actually get drawn, as part of its own cached redraw). No per-key
  // dirty-marking needed here beyond copying the values in -- every inbox
  // message unconditionally forces a full canvas redraw at the end of this
  // handler (see refresh_status_and_maybe_canvas(true) below).
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_STYLE))) s_data.custom_hour_marker.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_THICKNESS))) s_data.custom_hour_marker.thickness = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_INNER_ECC))) s_data.custom_hour_marker.inner_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_OUTER_ECC))) s_data.custom_hour_marker.outer_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_INNER_BORDER))) s_data.custom_hour_marker.inner_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_OUTER_BORDER))) s_data.custom_hour_marker.outer_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_TRANSLUCENT))) s_data.custom_hour_marker.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_COLOR))) s_data.custom_hour_marker.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_STYLE))) s_data.custom_second_marker.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_THICKNESS))) s_data.custom_second_marker.thickness = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_INNER_ECC))) s_data.custom_second_marker.inner_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_OUTER_ECC))) s_data.custom_second_marker.outer_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_INNER_BORDER))) s_data.custom_second_marker.inner_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_OUTER_BORDER))) s_data.custom_second_marker.outer_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_TRANSLUCENT))) s_data.custom_second_marker.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_COLOR))) s_data.custom_second_marker.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_BITMAP_MARKER_TRANSPARENT))) s_data.bitmap_marker_transparent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_TARGET))) s_data.marker_text.target = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_FONT))) {
    // Clamped, unlike most other enum-ish fields here -- this one's
    // used as a raw array index (MARKER_FONT_HEIGHTS/_Y_OFFSET in
    // background_layer.c), not a switch with a safe default, so an
    // out-of-range value would be a real out-of-bounds read.
    uint8_t v = t->value->uint8;
    s_data.marker_text.font_choice = (v <= 14) ? v : 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_OFFSET))) s_data.marker_text.offset_px = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_HOUR_MASK))) s_data.marker_text.hour_mask = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_SEC_MASK))) s_data.marker_text.second_mask = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_ROMAN))) s_data.marker_text.roman_numerals = t->value->uint8 != 0;
  // (no explicit layer_mark_dirty here -- refresh_status_and_maybe_canvas()
  // below already unconditionally marks s_hands_layer dirty every inbox batch)
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE1_CONTENT))) {
    s_data.upper_middle_line1_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE1_COLOR_MODE))) {
    s_data.upper_middle_line1_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE2_CONTENT))) {
    s_data.upper_middle_line2_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE2_COLOR_MODE))) {
    s_data.upper_middle_line2_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE1_CONTENT))) {
    s_data.bottom_middle_line1_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE1_COLOR_MODE))) {
    s_data.bottom_middle_line1_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE2_CONTENT))) {
    s_data.bottom_middle_line2_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE2_COLOR_MODE))) {
    s_data.bottom_middle_line2_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE1_CONTENT))) {
    s_data.middle_left_line1_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE1_COLOR_MODE))) {
    s_data.middle_left_line1_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE2_CONTENT))) {
    s_data.middle_left_line2_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE2_COLOR_MODE))) {
    s_data.middle_left_line2_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE1_CONTENT))) {
    s_data.middle_right_line1_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE1_COLOR_MODE))) {
    s_data.middle_right_line1_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE2_CONTENT))) {
    s_data.middle_right_line2_content = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE2_COLOR_MODE))) {
    s_data.middle_right_line2_color_mode = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_SUN_TIME))) {
    s_data.show_sun_time = t->value->uint8 != 0;
    if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_ISS))) {
    s_data.show_iss = t->value->uint8 != 0;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_AURORA_ENABLED))) {
    s_data.aurora_enabled = t->value->uint8 != 0;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_CONTENT))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) s_data.corner_content[i] = raw[i];
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_COLOR_MODE))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) s_data.corner_color_mode[i] = raw[i];
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DAILY_STEP_GOAL))) {
    s_data.daily_step_goal = t->value->uint16;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_BG))) s_data.custom_bg = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_TEXT))) s_data.custom_text = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_ACCENT))) s_data.custom_accent = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_SCHEME_ENABLED))) {
    s_data.night_scheme_enabled = t->value->uint8 != 0;
    if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_CUSTOM_BG))) s_data.night_custom_bg = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_CUSTOM_TEXT))) s_data.night_custom_text = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_CUSTOM_ACCENT))) s_data.night_custom_accent = t->value->uint8;

  // Every field parsed above this point that affects the features
  // overlay's layout (style/marker-style/bottom-info-bar-mode, and all
  // of the corner/edge content+color fields) has now been applied to
  // s_data -- one recompute here replaces the many individual
  // layer_mark_dirty(s_features_layer) calls the old per-field handling
  // used, since features_layer_set_data() both recomputes the cached
  // slot metadata AND marks the layer dirty. Content-only fields parsed
  // further below (weather icon style, AQI/altitude units, ...) don't
  // affect layout, so they keep their own plain layer_mark_dirty() --
  // features_draw_item() reads them straight from s_data at draw time
  // regardless.
  if (s_features_layer) features_layer_set_data(s_features_layer, &s_data);

  if (!s_data.valid) {
    save_data();
    refresh_status_and_maybe_canvas(true);
    return;
  }
  s_data.error_code = 0;

  if ((t = dict_find(iter, MESSAGE_KEY_C1_TIME))) s_data.c1 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_C2_TIME))) s_data.c2 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MAX_TIME))) s_data.max_t = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_C3_TIME))) s_data.c3 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_C4_TIME))) s_data.c4 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SUNSET_TIME))) s_data.sunset = (time_t)t->value->int32;

  if ((t = dict_find(iter, MESSAGE_KEY_MAGNITUDE))) s_data.magnitude_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_ECLIPSE_TYPE))) {
    s_data.type = (EclipseType)t->value->uint8;
    s_data.has_eclipse = s_data.type != ECLIPSE_TYPE_NONE;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_POS_ANGLE))) s_data.pos_angle_deg = t->value->int16;

  if ((t = dict_find(iter, MESSAGE_KEY_SAMPLE_START))) s_data.sample_start = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SAMPLE_INTERVAL))) s_data.sample_interval_s = t->value->uint32;
  if ((t = dict_find(iter, MESSAGE_KEY_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    s_data.sample_count = count > MAX_SEP_SAMPLES ? MAX_SEP_SAMPLES : count;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SEP_SAMPLES))) {
    // Sent as a byte blob of uint16 (little-endian) values.
    uint16_t *samples = (uint16_t *)t->value->data;
    int n = t->length / sizeof(uint16_t);
    if (n > MAX_SEP_SAMPLES) n = MAX_SEP_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.sep_samples_centideg[i] = samples[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MAG_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > MAX_SEP_SAMPLES) n = MAX_SEP_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.mag_pct_samples[i] = raw[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_RADIUS_RATIO_PCT))) s_data.radius_ratio_pct = t->value->uint8;

  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_COVER))) s_data.cloud_cover_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_VIS_SCORE))) s_data.vis_score_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_SOURCES))) s_data.weather_sources = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_CONDITION))) s_data.weather_condition = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_ICON_STYLE))) {
    s_data.weather_icon_style = t->value->uint8;
    if (s_features_layer) layer_mark_dirty(s_features_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_DIR_DEG))) s_data.wind_dir_deg = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_DEW_POINT_C))) s_data.dew_point_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_PRESSURE_HPA))) s_data.pressure_hpa = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_PRESSURE_TREND))) s_data.pressure_trend = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_AQI_US))) s_data.aqi_us = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_AQI_EU))) s_data.aqi_eu = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_AQI_UNIT))) {
    s_data.aqi_unit = t->value->uint8;
    if (s_features_layer) layer_mark_dirty(s_features_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ALTITUDE_M))) s_data.altitude_m = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_AURORA_KP_X10))) s_data.aurora_kp_x10 = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_AURORA_VISIBILITY_PCT))) {
    s_data.aurora_visibility_pct = t->value->uint8;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately -- affects whether the sky glow draws at all
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ALTITUDE_UNIT))) {
    s_data.altitude_unit = t->value->uint8;
    if (s_features_layer) layer_mark_dirty(s_features_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_C))) s_data.weather_temp_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_HIGH_C))) s_data.temp_high_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_LOW_C))) s_data.temp_low_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_UV_INDEX_X10))) s_data.uv_index_x10 = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_RAIN_CHANCE_PCT))) s_data.rain_chance_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HUMIDITY_PCT))) s_data.humidity_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_SPEED_KMH))) s_data.wind_speed_kmh = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_LOCATION_NAME))) {
    strncpy(s_data.location_name, t->value->cstring, sizeof(s_data.location_name) - 1);
    s_data.location_name[sizeof(s_data.location_name) - 1] = '\0';
  }

  // Full-day sky background: sun altitude + cloud cover samples,
  // used to render the gradient and dithered clouds behind the sun.
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SAMPLE_START))) s_data.sky_sample_start = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SAMPLE_INTERVAL))) s_data.sky_sample_interval_s = t->value->uint32;
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    s_data.sky_sample_count = count > MAX_SKY_SAMPLES ? MAX_SKY_SAMPLES : count;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_ALT_SAMPLES))) {
    // Byte blob of int16 (little-endian) tenths-of-a-degree values.
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      uint16_t u = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
      s_data.sun_alt_decideg[i] = (int16_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.cloud_pct_samples[i] = raw[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_ALTITUDE_PCT))) s_data.cloud_altitude_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_ALT_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      uint16_t u = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
      s_data.moon_alt_decideg[i] = (int16_t)u;
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
        s_data.planet_alt_decideg[p][i] = (int16_t)u;
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
      s_data.planet_rise[p] = (time_t)(int32_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_SET))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 4;
    if (n > PLANET_COUNT) n = PLANET_COUNT;
    for (int p = 0; p < n; p++) {
      uint32_t u = (uint32_t)raw[p * 4] | ((uint32_t)raw[p * 4 + 1] << 8) |
                   ((uint32_t)raw[p * 4 + 2] << 16) | ((uint32_t)raw[p * 4 + 3] << 24);
      s_data.planet_set[p] = (time_t)(int32_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SATURN_RING_OPEN_PCT))) s_data.saturn_ring_open_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SCALE_MAX_ALT))) s_data.sky_scale_max_alt_decideg = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_PHASE_PCT))) s_data.moon_phase_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_WAXING))) s_data.moon_waxing = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_RISE))) s_data.sun_rise = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_SET))) s_data.sun_set = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_RISE_TOMORROW))) s_data.sun_rise_tomorrow = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_RISE))) s_data.moon_rise = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_SET))) s_data.moon_set = (time_t)t->value->int32;
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
      s_data.star_alt_decideg[i] = (int16_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_STAR_AZ_SAMPLES))) {
    // Same packing, but always non-negative (0-3600 = 0-360deg x10).
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > STAR_COUNT) n = STAR_COUNT;
    for (int i = 0; i < n; i++) {
      s_data.star_az_decideg[i] = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_METEOR_INTENSITY))) s_data.meteor_intensity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_METEOR_SHOWER_NAME))) {
    strncpy(s_data.meteor_shower_name, t->value->cstring, sizeof(s_data.meteor_shower_name) - 1);
    s_data.meteor_shower_name[sizeof(s_data.meteor_shower_name) - 1] = '\0';
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_ALT))) s_data.iss_alt_deg = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_AZ))) s_data.iss_az_deg = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_COMPUTED_AT))) s_data.iss_computed_at = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_NEXT_PASS))) s_data.iss_next_pass = (time_t)(int32_t)t->value->int32;

  save_data();
  refresh_status_and_maybe_canvas(true);
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ---- tick + click ---------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh_status_and_maybe_canvas(false);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_update();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// ---- shake-to-reveal labels ------------------------------------------------

static AppTimer *s_label_timer = NULL;

static void hide_labels_callback(void *data) {
  s_label_timer = NULL;
  s_labels_visible = false;
  eclipse_canvas_set_show_labels(s_canvas_layer, false);
  // Recomputes (not just marks dirty) since the bottom corners' shift-up-
  // for-the-info-bar offset depends on s_labels_visible -- see
  // features_recompute_slots() in features_layer.c.
  if (s_features_layer) features_layer_set_labels_visible(s_features_layer, false);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  eclipse_canvas_set_show_labels(s_canvas_layer, true);
  s_labels_visible = true;
  if (s_features_layer) features_layer_set_labels_visible(s_features_layer, true);
  uint8_t seconds = s_data.shake_label_seconds > 0 ? s_data.shake_label_seconds : 3;
  uint32_t reveal_ms = (uint32_t)seconds * 1000;
  if (s_label_timer) {
    app_timer_reschedule(s_label_timer, reveal_ms);
  } else {
    s_label_timer = app_timer_register(reveal_ms, hide_labels_callback, NULL);
  }
}

// ---- corners overlay's own independent refresh cycle ----------------------

// A separate, shorter cadence than the sky canvas's once-a-minute
// throttle -- health data (heart rate especially) is worth checking
// more often, and redrawing four small icons/numbers is cheap enough
// that doing it this often doesn't meaningfully affect battery life,
// as long as it never touches the much more expensive sky canvas.
#define CORNERS_REFRESH_MS 5000
static AppTimer *s_corners_timer = NULL;

static void corners_timer_callback(void *data) {
  if (s_features_layer) layer_mark_dirty(s_features_layer);
  s_corners_timer = app_timer_register(CORNERS_REFRESH_MS, corners_timer_callback, NULL);
}

// ---- window lifecycle ----------------------------------------------------

// Creates the right set of layers for the current bottom_style, tearing
// down and rebuilding whatever's there if the style actually changed.
// Needed (rather than just resizing existing layers) because the sky
// canvas's cache bitmap is sized once at creation and Pebble has no
// API to resize a layer's internal state afterward -- a mode switch
// that changes the canvas's height means destroying and recreating
// it, not just adjusting its frame. Idempotent: safe to call after
// every settings update even when the style didn't change, since it
// no-ops in that case.
// Reacts to Timeline Quick View (or any future system overlay using
// this same API) appearing/disappearing at the bottom of the screen.
// Digital/analog mode's bottom panel shrinks from the bottom so it
// never extends past the obstruction; big-analogue mode has no
// separate bottom bar, so its sky canvas itself shrinks the same
// way, and its hands layer repositions itself around whatever's
// actually visible (see hands_layer_update_proc, which reads
// layer_get_unobstructed_bounds() directly).
static void unobstructed_change_handler(AnimationProgress progress, void *context) {
  if (!s_window) return;
  Layer *root = window_get_root_layer(s_window);
  GRect unobstructed = layer_get_unobstructed_bounds(root);

  if (s_data.bottom_style != 2 && s_bottom_layer) {
    GRect frame = layer_get_frame(s_bottom_layer);
    int16_t top = frame.origin.y; // fixed at 152 by apply_layout()
    int16_t new_h = unobstructed.size.h - top;
    if (new_h < 0) new_h = 0;
    if (frame.size.h != new_h) {
      frame.size.h = new_h;
      layer_set_frame(s_bottom_layer, frame);
    }
    layer_mark_dirty(s_bottom_layer);
  }

  if (s_data.bottom_style == 2 && s_canvas_layer) {
    GRect frame = layer_get_frame(s_canvas_layer);
    if (frame.size.h != unobstructed.size.h) {
      frame.size.h = unobstructed.size.h;
      layer_set_frame(s_canvas_layer, frame);
      // Force an immediate full redraw at the new size rather than
      // leaving the cached bitmap sized for the old frame -- the
      // canvas's own throttle would otherwise just blit that stale
      // cache back until its next scheduled minute.
      eclipse_canvas_set_data(s_canvas_layer, &s_data);
    }
  }

  if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  if (s_features_layer) layer_mark_dirty(s_features_layer);
}

static UnobstructedAreaHandlers s_unobstructed_handlers = {
  .change = unobstructed_change_handler
};

static void apply_layout(void) {
  Layer *root = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(root);
  uint8_t style = s_data.bottom_style;
  // Only meaningful (and only shown on the settings page) in big-analog
  // mode, but tracked unconditionally here so a change to it never gets
  // silently ignored if it arrives alongside/after a style switch.
  bool beneath_hands = s_data.draw_features_beneath_hands;

  if (style == s_current_layout_style && beneath_hands == s_current_draw_features_beneath_hands &&
      s_canvas_layer != NULL) {
    return;
  }
  s_current_layout_style = style;
  s_current_draw_features_beneath_hands = beneath_hands;

  if (s_canvas_layer) {
    eclipse_canvas_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
  if (s_bottom_layer) {
    layer_destroy(s_bottom_layer);
    s_bottom_layer = NULL;
  }
  if (s_hands_layer) {
    layer_destroy(s_hands_layer);
    s_hands_layer = NULL;
  }
  if (s_features_layer) {
    features_layer_destroy(s_features_layer);
    s_features_layer = NULL;
  }

  if (style == 2) {
    // Big analogue: sky canvas fills the whole screen; hands render in
    // their own always-on-top transparent layer; no bottom bar. Which
    // of the hands layer and the features overlay gets added (and so
    // painted) second -- i.e. which one ends up on top -- depends on
    // the "draw features beneath hands" setting; everywhere else the
    // features layer is always added last/on top (see below).
    s_canvas_layer = eclipse_canvas_create(GRect(0, 0, bounds.size.w, bounds.size.h));
    layer_add_child(root, s_canvas_layer);
    s_hands_layer = layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
    layer_set_update_proc(s_hands_layer, hands_layer_update_proc);
    if (beneath_hands) {
      s_features_layer = features_layer_create(layer_get_frame(s_canvas_layer));
      layer_add_child(root, s_features_layer);
      layer_add_child(root, s_hands_layer);
    } else {
      layer_add_child(root, s_hands_layer);
    }
  } else {
    // Digital / analog: sky canvas keeps its original top-2/3
    // proportions; bottom third is the digital time or the analog
    // clock + 4-line info panel.
    s_canvas_layer = eclipse_canvas_create(GRect(0, 0, bounds.size.w, 152));
    layer_add_child(root, s_canvas_layer);
    s_bottom_layer = layer_create(GRect(0, 152, bounds.size.w, bounds.size.h - 152));
    layer_set_update_proc(s_bottom_layer, bottom_canvas_update_proc);
    layer_add_child(root, s_bottom_layer);
    apply_clock_font();
  }

  // Overlays exactly the sky canvas's own bounds -- reuses its
  // just-created frame rather than duplicating the size logic above, so
  // it always matches regardless of mode. In every mode except "big
  // analog with features beneath hands" (handled above, where it was
  // already created and added before the hands layer), it's created
  // and added here, last, so it draws on top of everything else built
  // so far -- per the original brief's "on top of the eclipse layer".
  if (!s_features_layer) {
    s_features_layer = features_layer_create(layer_get_frame(s_canvas_layer));
    layer_add_child(root, s_features_layer);
  }

  // The countdown label always sits on top of everything else, so it
  // needs re-adding last after the canvas underneath was just rebuilt.
  if (s_countdown_layer) {
    layer_remove_from_parent(s_countdown_layer);
    layer_add_child(root, s_countdown_layer);
  }

  eclipse_canvas_set_data(s_canvas_layer, &s_data);
  features_layer_set_data(s_features_layer, &s_data);

  // Newly (re)created layers start at their full, unobstructed frame
  // -- if Quick View already happens to be showing right when a mode
  // switch rebuilds them, apply that immediately rather than waiting
  // for the next .change event, matching Pebble's own recommended
  // pattern for handling Quick View already being active on load.
  unobstructed_change_handler(0, NULL);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  // Added first; apply_layout() re-parents it on top of whatever it
  // builds, both here on first load and again on any later mode
  // switch, so its own creation only has to happen once.
  s_countdown_layer = layer_create(GRect(0, 2, bounds.size.w, 20));
  layer_set_update_proc(s_countdown_layer, countdown_layer_update_proc);
  s_countdown_text_color = GColorBlack;
  layer_add_child(root, s_countdown_layer);

  apply_layout();
  apply_clock_font(); // uses whatever was loaded from persistent storage

  refresh_status_and_maybe_canvas(true);
  maybe_start_startup_clock_animation();
  maybe_start_startup_background_animation();
}

static void window_unload(Window *window) {
  layer_destroy(s_countdown_layer);
  if (s_canvas_layer) eclipse_canvas_destroy(s_canvas_layer); // also frees marker_bitmap/marker_text_font now
  if (s_bottom_layer) layer_destroy(s_bottom_layer);
  if (s_hands_layer) layer_destroy(s_hands_layer);
  if (s_features_layer) features_layer_destroy(s_features_layer);
  features_layer_unload_fonts();
}

static void init(void) {
  load_data();

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);
  unobstructed_area_service_subscribe(s_unobstructed_handlers, NULL);
  s_corners_timer = app_timer_register(CORNERS_REFRESH_MS, corners_timer_callback, NULL);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  // Ask the phone for a fresh calculation as soon as we're up; PKJS
  // will also push updates on its own schedule (see index.js). If
  // nothing valid comes back, request_retry_callback keeps asking
  // with backoff rather than leaving the watch stuck on an empty
  // screen forever (see its own comment for why that can happen).
  request_update();
  s_request_retry_timer = app_timer_register((uint32_t)s_request_retry_delay_s * 1000, request_retry_callback, NULL);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
  if (s_label_timer) {
    app_timer_cancel(s_label_timer);
    s_label_timer = NULL;
  }
  if (s_corners_timer) {
    app_timer_cancel(s_corners_timer);
    s_corners_timer = NULL;
  }
  if (s_request_retry_timer) {
    app_timer_cancel(s_request_retry_timer);
    s_request_retry_timer = NULL;
  }
  if (s_startup_anim_timer) {
    app_timer_cancel(s_startup_anim_timer);
    s_startup_anim_timer = NULL;
  }
  if (s_bg_anim_timer) {
    app_timer_cancel(s_bg_anim_timer);
    s_bg_anim_timer = NULL;
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
