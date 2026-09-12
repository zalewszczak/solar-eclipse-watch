#include <pebble.h>
#include "eclipse_data.h"
#include "comms.h"
#include "persistence.h"
#include "battery_saver.h"
#include "input.h"
#include "time_service.h"
#include "background_layer.h"
#include "sky_layer.h"
#include "features_layer.h"
#include "font_lookup.h"
#include "message_key_index.h" // MK_* is used for the startup key-index consistency check.

static Window *s_window;
static Layer *s_countdown_layer; // custom-drawn (not TextLayer) so it can draw the 1px outline
static GColor s_countdown_text_color;
static Layer *s_canvas_layer;
static Layer *s_panel_layer; // NULL in analog mode -- the digital clock panel (bar OR top layout;
                               // see draw_digital_clock_panel()'s own opaque_bg param for how the two
                               // differ) that holds the clock text itself
static Layer *s_top_gradient_layer; // Digital top only -- the reserved band's own gradient-only wash,
                                      // BELOW s_panel_layer in z-order so the panel's transparent
                                      // background lets it show through -- see background_layer.h's own
                                      // sky_layer_top_gradient_create() comment
static Layer *s_hands_layer;  // big-analogue mode only
static Layer *s_features_layer; // always present -- overlays the FULL screen (not just the sky
                                  // canvas's own frame, which in digital mode is only the top 152px --
                                  // this layer's own bottom-anchored slots need the real screen bottom);
                                  // see features_layer.h -- owns its own per-layer state now
static uint8_t s_current_layout_style = 255; // sentinel: forces initial layout setup
static bool s_current_draw_features_beneath_hands = false; // mirrors s_data's own default

// True while the shake-to-reveal ground bar is up (mirrors
// eclipse_layer.c's own private show_labels state, tracked
// separately here since main.c already owns the shake timer and the
// corners layer needs this to shift its bottom corners up out of the
// bar's way).

// UI layout/font functions are kept in the application layer; communication
// reports the changes they need to react to through comms_data_applied().
static void apply_layout(void);
static void apply_clock_font(void);
static void comms_data_applied(CommsChangeFlags changes, void *context);

static char s_countdown_buf[40];

static EclipseData s_data;

// Shared state for the startup background sweep. The hands animation
// reads this state so the optional planet-sweep time shift follows the
// same swept observation time as the background.
#define BG_ANIM_MS 1400
static bool s_bg_anim_active = false;
static uint16_t s_bg_anim_elapsed_ms = 0;

// Declare a file-scope variable
static GFont clock_font;
static FontSlot s_clock_font_slot = FONT_SLOT_EMPTY;

// static declarations:

// Battery-saver policy lives in battery_saver.c. The app owns the
// visual/timing consequences of a phase change.
static void battery_saver_phase_changed(BatterySaverPhase previous_phase, BatterySaverPhase phase, void *context);
static void battery_saver_sync_phase_to_phone(void);

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

bool weather_should_show_error(const EclipseData *d) {
  if (d->weather_error_code == 0) return false; // this refresh's fetch was fine
  if (!d->weather_ever_valid) return true;       // nothing to fall back to -- show it right away
  return d->weather_error_streak >= 10;
}

// Picks the day or night set of colors based on the Sun's altitude
// (reusing sky_layer_is_bright()'s existing civil-twilight threshold
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
  bool night = d->night_scheme_enabled && !sky_layer_is_bright(d, now);
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

// draw_sun_time_icon() (the sunrise/sunset corner-glyph renderer) used
// to live here, hand-drawing an arrow + horizon-sun with fill
// primitives every frame. It's now a plain image (resources/images/
// icon_sun_time_rise.png / icon_sun_time_set.png), drawn by
// features_layer.c's draw_render_icon() the same way every other
// bitmap corner icon is -- see that file's case 11.

// ---- big-analogue mode: fullscreen hands over the sky layer --------------

// point_in_convex_polygon()/fill_polygon_dithered()/contrasting_outline_color()/
// draw_text_outlined() moved to features_layer.c (also used there), exposed
// via features_layer.h for this file's own countdown-label use.

// ---- big-analogue marker styles (procedural + bitmap) --------------------
// Moved into background_layer.c -- markers now draw as part of the sky
// canvas's own cached redraw (see the design note at the top of that
// file), not from here every tick.

// Corner/edge/date font resolution is now font_lookup_resolve() plus a
// shared FontSlot (see font_lookup.h) owned by features_layer.c, whose
// ensure_corner_custom_font() is exposed via features_layer.h since
// this file's hands layer still needs it.



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
// from 120 minutes before the real time, big-analog's custom hands
// grow out from a center dot and sweep into place (chasing that same
// 120-minutes-ago starting point too, if the "Planets" background
// sweep is also running -- see hands_layer_update_proc's own comment).
// Driven by a fast repeating AppTimer (this is
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
// clock/small-analog "counting up from 120 minutes ago" effect in
// draw_digital_clock_panel, so the displayed time visibly speeds up
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

// ---- shared ease-out lookup table -----------------------------------
// Cubic ease-out (1-(1-t)^3), precomputed at 21 points (0, 50, 100,
// ..., 1000) -- avoids the 2 multiplications ease_out_cubic_1000()
// used to do on every single call in favor of one table lookup + a
// cheap linear interpolation between its 2 nearest points, and gives
// every animation that wants this same "starts quick, eases into
// place" feel (the startup clock/hand sweep, the background sweep,
// marker reveals, the shake color cycle) one shared table to pull
// from instead of each recomputing its own curve. Integer-only, no
// floating point anywhere in here.
static const int16_t EASE_OUT_LUT[21] = {
  0, 143, 271, 386, 488, 579, 657, 726, 784, 834, 875, 909, 936, 958, 973, 985, 992, 997, 999, 1000, 1000
};
static int32_t ease_out_lut_1000(int32_t t) {
  if (t <= 0) return 0;
  if (t >= 1000) return 1000;
  int32_t idx = t / 50;
  int32_t frac = t - idx * 50;
  int32_t lo = EASE_OUT_LUT[idx];
  int32_t hi = EASE_OUT_LUT[idx + 1];
  return lo + ((hi - lo) * frac) / 50;
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
  ensure_corner_custom_font(s_data.corner_font);

  int32_t hour_angle = (int32_t)(((int64_t)((t->tm_hour % 12) * 3600 + t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600));

  int32_t min_angle = ((t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);

  int32_t sec_angle;
  // Smooth sub-second motion for shake_anim_mode 1 (Smooth second hand
  // alone) AND mode 3 (Both) -- see shake_anim_wants_smooth_second()'s
  // own comment. Planet seek (mode 2 or 3) repositions the sky by
  // compass and is otherwise independent of this; when both are
  // requested together (mode 3) the second hand still gets the
  // continuous sub-second motion Smooth second hand promises.
  if (input_shake_animation_active() && s_data.show_seconds && input_shake_animation_wants_smooth_second(s_data.shake_anim_mode)) {
    // Shake animation: continuous sub-second motion instead of the
    // normal once-a-second jump -- time_ms() gives a fresh timestamp
    // with its own within-the-second millisecond offset, read
    // together so they can't land a second apart from each other.
    time_t smooth_now;
    uint16_t smooth_ms;
    time_ms(&smooth_now, &smooth_ms);
    struct tm *smooth_t = localtime(&smooth_now);
    sec_angle = (int32_t)((((int64_t)smooth_t->tm_sec * 1000 + smooth_ms) * TRIG_MAX_ANGLE) / 60000);
  } else {
    sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;
  }

  // Startup animation (big-analog only): substitutes each hand's real
  // target angle with an in-progress one, plus how long that hand
  // currently is -- see compute_startup_hand_anim()'s own comment.
  // 1000 = full length/no substitution for a normal, non-animated draw.
  uint16_t hour_length_scale_1000 = 1000, min_length_scale_1000 = 1000, sec_length_scale_1000 = 1000;
  if (s_startup_clock_anim_active) {
    int32_t target_hour_angle = hour_angle, target_min_angle = min_angle, target_sec_angle = sec_angle;
    // Only when the user explicitly picked "planet sweep time shift"
    // (startup_clock_anim_mode 2) AND the Planets background sweep
    // (bg_anim_mode 1) is ALSO actually running right now do the hands
    // chase the SAME swept time the sky itself is sweeping through
    // (see canvas_update_proc's own sky_now substitution in
    // background_layer.c) instead of the real, fixed current time --
    // so the hands visibly advance through the same ~2 hours the
    // planets are moving through in the background, rather than the
    // sky alone appearing to animate while the hands just swing into
    // their already-correct resting position. Falls back to chasing
    // the real current time (same as mode 1, "animate clock") whenever
    // Planets isn't the active background animation, since there's no
    // time shift to chase in that case. Shares BG_ANIM_MS as its own
    // total duration (both are 1400ms) and ease_out_cubic_1000
    // (identical curve to background_layer.c's own
    // bg_anim_ease_out_1000 -- see that function's own comment) so the
    // two sweeps advance in step with each other.
    if (s_data.startup_clock_anim_mode == 2 && s_bg_anim_active && s_data.bg_anim_mode == 1) {
      int32_t progress = ((int32_t)s_bg_anim_elapsed_ms * 1000) / BG_ANIM_MS;
      if (progress > 1000) progress = 1000;
      int32_t eased = ease_out_cubic_1000(progress);
      time_t past = now - 120 * 60;
      time_t swept_now = past + (time_t)(((int64_t)(now - past) * eased) / 1000);
      struct tm *st = localtime(&swept_now);
      target_hour_angle = (int32_t)(((int64_t)((st->tm_hour % 12) * 3600 + st->tm_min * 60 + st->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600));
      target_min_angle = ((st->tm_min * 60 + st->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);
      // Deliberately NOT substituting target_sec_angle here -- per
      // request, the second hand doesn't chase the same ~2-hour swept
      // past the hour/minute hands and the sky do (that would mean
      // visibly spinning through hundreds of revolutions in under
      // 1.5s). It's left as the real current second (already computed
      // above, before this block, from the real `now`), so
      // compute_startup_hand_anim() below gives it the same single
      // ease-in move from 12 o'clock straight to the real current time
      // that every hand gets in the plain "animate clock" case (mode 1).
    }
    compute_startup_hand_anim(target_hour_angle, s_startup_anim_elapsed_ms, &hour_angle, &hour_length_scale_1000);
    compute_startup_hand_anim(target_min_angle, s_startup_anim_elapsed_ms, &min_angle, &min_length_scale_1000);
    compute_startup_hand_anim(target_sec_angle, s_startup_anim_elapsed_ms, &sec_angle, &sec_length_scale_1000);
  }

  // Every hand style is a "custom" hand now, whether the person got
  // there by picking one of the built-in preset buttons or by editing
  // hour/minute/second by hand -- pkjs is what tells the two apart
  // (see config-page.js's hand style picker popup); by the time
  // settings reach the watch, a preset has already been expanded into
  // the exact same hand_hour/hand_minute/hand_second fields a fully
  // custom hand uses, so there's nothing left to branch on here.
  HandConfig hour_cfg = s_data.hand_hour;
  HandConfig min_cfg = s_data.hand_minute;
  HandConfig sec_cfg = s_data.hand_second;

  hand_layer_draw(ctx, center, hour_angle, &hour_cfg, main_color, accent_color, bg, s_data.shadow_translucent, s_data.shadow_angle_deg, hour_length_scale_1000);
  hand_layer_draw(ctx, center, min_angle, &min_cfg, main_color, accent_color, bg, s_data.shadow_translucent, s_data.shadow_angle_deg, min_length_scale_1000);
  if (s_data.show_seconds) {
    hand_layer_draw(ctx, center, sec_angle, &sec_cfg, main_color, accent_color, bg, s_data.shadow_translucent, s_data.shadow_angle_deg, sec_length_scale_1000);
  }

  hand_layer_draw_center_circle(ctx, center, s_data.center_circle_radius, s_data.center_circle_color,
                                 main_color, accent_color, bg);
}

// ---- corners/edges feature overlay ---------------------------------------
// The whole always-on-top text/icon overlay (icon bitmaps, weather/
// timezone/gradient/sleep helpers, the table-driven per-slot recompute/
// draw split, and the layer itself) lives in features_layer.c/.h -- see
// that file's own top-of-file note. s_features_layer below is created
// via features_layer_create()/destroyed via features_layer_destroy() in
// apply_layout()/window_unload(), and fed with
// features_layer_set_data()/refresh_values()/refresh_second_slots()/
// refresh_content() instead of being recomputed on every redraw.


// ---- rendering ---------------------------------------------------------

// The digital clock's own panel -- Digital bar's opaque bottom-third
// bar (bottom_style 0/2/3/4) and Digital top's transparent top strip
// (5/7/8/9) share this one draw function (see s_panel_layer's own
// comment for why one Layer variable now covers both): is_top (derived
// from bottom_style_is_digital_top()) picks which -- false fills
// `bounds` solid first, exactly Digital bar's original always-had-a-
// solid-backing look; true leaves the frame buffer alone so whatever
// s_top_gradient_layer/the sky canvas already painted underneath
// (added as children BEFORE this layer -- see apply_layout()) shows
// straight through, which is Digital top's own "transparent panel over
// the sky" look. Analog (bottom_style == 1) has no panel at all -- see
// apply_layout() -- so this never runs then. Obeys the color scheme
// and the seconds-visibility setting either way. Redrawn every second
// when seconds are shown; otherwise still cheap enough (no astronomy,
// just text drawing) not to bother throttling separately from the sky
// canvas underneath it.
static void draw_digital_clock_panel(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  bool is_top = bottom_style_is_digital_top(s_data.bottom_style);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Startup animation: substitutes an eased count-up from 120 minutes
  // before the real time (not all the way from midnight -- a many-
  // hour jump used to make the digits blur through most of the day
  // in under 1.5s, which read as noisy digit-flicker rather than a
  // deliberate sweep) up to the real time, for the DISPLAYED time
  // only -- `now`/the color scheme below still use the real current
  // time. Every read of `t->tm_hour`/`tm_min`/`tm_sec` below shares
  // this one struct tm, so the digital HH:MM(:SS) text gets the
  // count-up for free from this single substitution. Uses the same
  // shared ease-out table every other "settles gracefully into place"
  // animation in this app uses (fast at first, gradually slowing into
  // the real time) -- this used to accelerate INTO the stop instead
  // (ease-in), which read as an abrupt halt right at the end.
  struct tm anim_tm;
  if (s_startup_clock_anim_active) {
    int32_t progress = ((int32_t)s_startup_anim_elapsed_ms * 1000) / STARTUP_CLOCK_ANIM_MS;
    if (progress > 1000) progress = 1000;
    int32_t eased = ease_out_lut_1000(progress);
    time_t anim_start = now - 120 * 60; // 2 hours before the real time
    int32_t total_seconds = (int32_t)(now - anim_start);
    int32_t fake_seconds = (int32_t)(((int64_t)total_seconds * eased) / 1000);
    time_t fake_time = anim_start + fake_seconds;
    anim_tm = *localtime(&fake_time);
    t = &anim_tm;
  }

  GColor bg, text_color, accent_color;
  get_active_color_scheme(&s_data, now, &bg, &text_color, &accent_color);

  if (!is_top) {
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  }

  // show_seconds is only ever true here for a font PKJS has already
  // determined can fit a full "HH:MM:SS" readout at its normal size --
  // wide fonts have seconds forced off and the checkbox hidden
  // entirely on the settings page, so there's no on-watch "does this
  // font fit seconds" decision left to make, and no small-side-digit
  // fallback rendering needed for the fonts that don't.
  char time_buf[10];
  if (s_data.show_seconds) {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S", t);
  } else {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  }

  // Shifts away from whichever single side-feature column is active
  // (digital_side_mode() 2 or 3, regardless of which layout), or stays
  // centered/full-width otherwise (0, or 4 with both columns on -- see
  // digital_clock_area()'s own comment for why "both" doesn't shrink
  // the clock further). The features_layer overlay draws the side
  // columns themselves and the single bottom feature (which used to be
  // the fixed date/sun-time row directly below, now a user-selectable
  // content slot instead) -- this layer only ever draws the clock
  // digits.
  int16_t clock_x, clock_w;
  digital_clock_area(s_data.bottom_style, bounds.size.w, &clock_x, &clock_w);
  int16_t font_h = font_lookup_height(s_data.clock_font) + font_lookup_y_offset(s_data.clock_font);
  // Centered 24px from whichever edge of the panel sits next to the
  // clock's own "inner" boundary with the sky -- the panel's own top
  // for Digital bar (the sky is right above it), its own bottom for
  // Digital top (mirrored -- the sky is right below it instead), so
  // the two layouts read as a literal vertical flip of one another
  // rather than each being independently tuned.
  int16_t clock_y = is_top ? (bounds.size.h - 24 - font_h / 2) : (24 - font_h / 2);
  GRect clock_rect = GRect(bounds.origin.x + clock_x, bounds.origin.y + clock_y, clock_w, font_h);
  GTextAlignment alignment = GTextAlignmentCenter;

  // Trick to avoid clipping of shifted clocks and not having text trimmed (...) or having clock outside of the screen in extreme cases
  uint8_t side = digital_side_mode(s_data.bottom_style);
  if (side == 2) { // right side only -- shift left
    int16_t initial_allowed_area = clock_rect.size.w;
    clock_rect.size.w += 30;
    GSize calculated_size = graphics_text_layout_get_content_size(time_buf, clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment);
    if (calculated_size.w >= initial_allowed_area) {
      alignment = GTextAlignmentLeft;
    } else {
      // revert spacing coz we fit
      clock_rect.size.w -= 30;
    }
  } else if (side == 3) { // left side only -- shift right
    int16_t initial_allowed_area = clock_rect.size.w;
    clock_rect.size.w += 30;
    clock_rect.origin.x -= 30;
    GSize calculated_size = graphics_text_layout_get_content_size(time_buf, clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment);
    if (calculated_size.w >= initial_allowed_area) {
      alignment = GTextAlignmentRight;
    } else {
      // revert spacing coz we fit
      clock_rect.size.w -= 30;
      clock_rect.origin.x += 30;
    }
  }

  if (s_data.draw_debug){
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorCyan);
    graphics_draw_rect(ctx, clock_rect);
  }

  // ---- big time ----
  // Digital top only: routed through the shared outline primitive
  // (same one corner/edge text and the countdown label already use)
  // instead of a plain graphics_draw_text() -- this is the one clock
  // panel with sky visible directly behind it, so it's the one place
  // outline_style's existing "stay readable over any part of the sky"
  // job actually applies to the clock digits themselves. Digital bar's
  // own opaque backing has never needed this, so passing outline_style
  // 0 there (via draw_text_outlined's own no-op-at-0 handling) keeps
  // its look pixel-identical to before.
  if (is_top) {
    draw_text_outlined(ctx, time_buf, clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment, text_color, s_data.outline_style);
  } else {
    graphics_context_set_text_color(ctx, text_color);
    graphics_draw_text(ctx, time_buf, clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment, NULL);
  }
  // The date/week-or-sunrise row that used to sit directly below the
  // clock is now the features_layer overlay's own "digital bottom"
  // feature slot (content-selectable in settings, defaulting to
  // "long date + sunrise/sunset" -- the exact information this fixed
  // row always showed) -- drawn by that layer, not this one.
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

  // This label floats directly over the busy sky view in Analog mode
  // and (since its own panel is transparent) Digital top too -- both
  // have real sky right behind it at this label's fixed position (near
  // the screen's top edge). Normally draw_text_outlined()'s 4-shifted-
  // copy outline keeps it legible against any background there, but
  // with that setting off there's nothing else backing the text, so it
  // can disappear into a similarly-colored patch of sky. Give it a
  // solid pill background in that specific case instead
  // (contrasting_outline_color() picks black or white, whichever
  // contrasts with the text color) -- outline mode already handles
  // legibility fine on its own, and Digital bar's own panel is already
  // a solid color the text sits on, so neither of those needs this
  // extra background.
  if (s_data.outline_style == 0 && (s_data.bottom_style == 1 || bottom_style_is_digital_top(s_data.bottom_style)) && s_countdown_buf[0] != '\0') {
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
                      s_countdown_text_color, s_data.outline_style);
}

static void refresh_status_and_maybe_canvas(bool force_canvas) {
  time_t now = time(NULL);
  EclipsePhase phase = eclipse_get_status_text(&s_data, now, s_countdown_buf, sizeof(s_countdown_buf), time_service_live_seconds_now(now));
  // The countdown label overlays the sky canvas transparently, so its
  // own contrast needs to track the sky brightness underneath it --
  // this check is cheap (no drawing), so it's fine to do every second.
  s_countdown_text_color = sky_layer_is_bright(&s_data, now) ? GColorBlack : GColorWhite;
  // Hidden entirely (not just left blank) when there's confirmed to
  // be no eclipse today -- this field is purely about eclipse phases
  // now (see eclipse_get_status_text), so there's nothing for it to
  // show in that case. Error/loading states still display normally,
  // since those aren't "no eclipse," they're "don't know yet."
  bool hide_label = s_data.valid && !s_data.has_eclipse;

  // Totality flashes the countdown label on/off once a second -- the
  // same attention-grabbing treatment update_planet_seek_accuracy_label()
  // gives the compass-accuracy warning -- rather than sitting on
  // screen solid for the whole totality window. eclipse_is_active()
  // being true for this entire phase is exactly what already forces
  // time_service_live_seconds_now() (and so the SECOND_UNIT tick rate this actually
  // needs to look like flashing rather than a slow blink) above.
  if (phase == PHASE_TOTAL && (now % 2) == 0) { // hidden on even seconds, shown on odd ones
    s_countdown_buf[0] = '\0';
    hide_label = true;
  }

  // Battery saver's sleep/deep-sleep indicator takes over this same
  // top-of-screen label -- same free-real-estate reasoning
  // update_planet_seek_accuracy_label() already uses for Planet
  // seek's own compass-accuracy warning just below. The two never
  // actually collide: Planet seek only ever runs in the few seconds
  // right after a shake, which is exactly what resets battery saver
  // straight back to BATTERY_SAVER_AWAKE (see tap_handler()).
  if (battery_saver_phase() == BATTERY_SAVER_DEEP_SLEEP) {
    snprintf(s_countdown_buf, sizeof(s_countdown_buf), "Zzzzzzz");
    hide_label = false;
  } else if (battery_saver_phase() == BATTERY_SAVER_SLEEP) {
    snprintf(s_countdown_buf, sizeof(s_countdown_buf), "Zzz");
    hide_label = false;
  }

  layer_set_hidden(s_countdown_layer, hide_label);
  layer_mark_dirty(s_countdown_layer);

  // The bottom canvas (time/date, or the analog clock) is cheap
  // enough (no astronomy) to just redraw every second directly. Only
  // exists in digital/analog mode -- big-analogue mode has no bottom
  // bar, and redraws its hands layer every second instead, for the
  // same reason (cheap, no astronomy, ticks smoothly).
  if (s_panel_layer) layer_mark_dirty(s_panel_layer);
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

// ---- AppMessage ---------------------------------------------------------

static void startup_anim_timer_callback(void *data) {
  s_startup_anim_elapsed_ms += STARTUP_ANIM_FRAME_MS;
  if (s_startup_anim_elapsed_ms >= STARTUP_CLOCK_ANIM_MS) {
    s_startup_clock_anim_active = false;
    s_startup_anim_timer = NULL;
  } else {
    s_startup_anim_timer = app_timer_register(STARTUP_ANIM_FRAME_MS, startup_anim_timer_callback, NULL);
  }
  if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  if (s_panel_layer) layer_mark_dirty(s_panel_layer);
}

// Called once from window_load(), after the layers it needs to mark
// dirty already exist. A no-op (and leaves s_startup_clock_anim_active
// false) if the setting is off, or if this app session already played
// it once -- a settings save or a fresh data push shouldn't replay it.
// Also a no-op (without marking it "played" -- window_load only ever
// runs once per session anyway, so there's no later chance for it to
// replay) if the eclipse is actively in progress right at launch: per
// request, no startup animation while an active eclipse is on screen.
static void maybe_start_startup_clock_animation(void) {
  if (s_startup_clock_anim_played || s_data.startup_clock_anim_mode == 0) return;
  if (eclipse_is_active(&s_data, time(NULL))) return;
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
// marker_layer_draw()/draw_text_markers() (the marker/text-marker
// pieces). A separate timer/state pair from the clock animation above
// -- the two settings are independent, and either, both, or neither
// can be on -- but the same "fast timer, bounded duration, played once
// per session" shape.
#define BG_ANIM_FRAME_MS 40 // matches STARTUP_ANIM_FRAME_MS -- see its own comment

static AppTimer *s_bg_anim_timer = NULL;
static bool s_bg_anim_played = false;

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
  if (s_bg_anim_played || s_data.bg_anim_mode == 0) return;
  if (eclipse_is_active(&s_data, time(NULL))) return; // no startup background animation while an active eclipse is on screen, per request
  // Marker animation (bg_anim_mode 2) has no actual visual effect for
  // bitmap marker styles (Modern/Shadow/Tally/Bell/Fancy -- the PNG-
  // backed marker backgrounds, big_analog_marker_style 3-7):
  // marker_layer_draw() in marker_layer.c already draws them
  // immediately regardless of anim_active/anim_progress_1000, since a
  // real circular-reveal effect for an arbitrary bitmap isn't
  // implemented (see that function's own comment for why -- Pebble's
  // graphics API has no per-context clip-rect or arbitrary-shape
  // compositing to build one from). Running the 35-frame, 25fps
  // full-canvas redraw burst below anyway -- right at startup, the
  // same moment the marker bitmap's own first-ever PNG decode is ALSO
  // happening for the first time, on top of everything else the app
  // is doing at launch -- was capable of pushing heap pressure high
  // enough to fail that PNG decode outright ("PNG memory allocation
  // failed" / "Failed to load PNG" in the logs, marker not drawn at
  // all as a result). Skipping the animation burst entirely for this
  // combination avoids that: a bitmap marker style still gets its one
  // normal, un-animated redraw, exactly as if bg_anim_mode were off --
  // which is all it was ever visually doing anyway.
  bool bitmap_marker_active = s_data.big_analog_marker_style >= 3 && s_data.big_analog_marker_style <= 7;
  if (s_data.bg_anim_mode == 2 && bitmap_marker_active) {
    s_bg_anim_played = true;
    return;
  }
  s_bg_anim_played = true;
  s_bg_anim_active = true;
  s_bg_anim_elapsed_ms = 0;
  if (s_canvas_layer) eclipse_canvas_set_bg_anim(s_canvas_layer, true, 0);
  s_bg_anim_timer = app_timer_register(BG_ANIM_FRAME_MS, bg_anim_timer_callback, NULL);
}

// get_clock_font_height_offset() used to live here as a hand-tuned
// per-clock_font-value switch -- folded into font_lookup.c's shared
// FONT_TABLE (its y_offset column) now, called directly as
// font_lookup_y_offset(s_data.clock_font) at each call site.

static void apply_clock_font(void) {
  clock_font = font_lookup_resolve(&s_clock_font_slot, s_data.clock_font);
  if (s_panel_layer) layer_mark_dirty(s_panel_layer);
}

// Battery-saver tick policy and Pebble tick subscription live in
// time_service.c. The application still owns what each tick means visually
// (redraws, vibrations, feature refreshes, etc.); time_service only decides
// SECOND_UNIT versus MINUTE_UNIT and forwards the resulting tick here.

// ---- tick + click ---------------------------------------------------------

// ---- hourly vibrations ------------------------------------------------
// A periodic reminder buzz, entirely independent of vibrate_on_phase_change
// above (which is about the eclipse itself) -- see hourly_vibe_mode's own
// comment in eclipse_data.h for the full field layout.

// hourly_vibe_start_min == hourly_vibe_end_min (including the 0==0
// default) means "all 24 hours", not a single-minute window -- see
// that field's own comment in eclipse_data.h. Otherwise start > end
// wraps past midnight (e.g. 22:00-6:00 covers 22:00 through 23:59 AND
// 00:00 through 6:00), same as a normal "quiet hours" range would.
static bool hourly_vibe_time_in_range(int minute_of_day) {
  int start = s_data.hourly_vibe_start_min, end = s_data.hourly_vibe_end_min;
  if (start == end) return true;
  if (start < end) return minute_of_day >= start && minute_of_day <= end;
  return minute_of_day >= start || minute_of_day <= end;
}

static void do_hourly_vibe(void) {
  switch (s_data.hourly_vibe_pattern) {
    case 1: vibes_double_pulse(); break;
    case 2: vibes_long_pulse(); break;
    default: vibes_short_pulse(); break; // 0, and any unrecognized value
  }
}

// Called once a minute regardless of the tick subscription's own
// granularity or battery-saver phase -- a reminder buzz shouldn't miss its
// minute just because the display isn't redrawing that often.
static void maybe_do_hourly_vibe(struct tm *tick_time) {
  if (s_data.hourly_vibe_mode == 0) return;
  if (!((s_data.hourly_vibe_days_mask >> tick_time->tm_wday) & 1)) return;
  int minute_of_day = tick_time->tm_hour * 60 + tick_time->tm_min;
  if (!hourly_vibe_time_in_range(minute_of_day)) return;

  bool should_fire;
  if (s_data.hourly_vibe_mode == 1) {
    should_fire = (tick_time->tm_min == 0);
  } else {
    int interval = s_data.hourly_vibe_interval_min > 0 ? s_data.hourly_vibe_interval_min : 30;
    should_fire = (minute_of_day % interval) == 0;
  }

  if (!should_fire) return;
  if (!s_data.hourly_vibe_override_quiet && quiet_time_is_active()) return;
  do_hourly_vibe();
}

static void time_service_tick_handler(struct tm *tick_time, TimeUnits units_changed, void *context) {
  (void)context;

  if (units_changed & MINUTE_UNIT) maybe_do_hourly_vibe(tick_time);

  battery_saver_update(time(NULL));
  battery_saver_sync_phase_to_phone();

  // Deep sleep still subscribes at MINUTE_UNIT, because Pebble has no
  // five-minute TickTimerService unit. Skip redraw work on the four minutes
  // between five-minute boundaries.
  bool deep_sleep_skip = (battery_saver_phase() == BATTERY_SAVER_DEEP_SLEEP) && (tick_time->tm_min % 5 != 0);
  if (!deep_sleep_skip) {
    refresh_status_and_maybe_canvas(false);
    if (battery_saver_phase() != BATTERY_SAVER_AWAKE && s_features_layer) {
      features_layer_refresh_values(s_features_layer);
    }
  }

  // Seconds-precision feature slots piggyback on SECOND_UNIT rather than
  // maintaining another wake source.
  if (time_service_is_seconds() && s_features_layer) {
    features_layer_refresh_second_slots(s_features_layer);
  }

  // Re-check on every tick because an eclipse can enter or leave its active
  // contact window without a settings update. The time service changes
  // subscription granularity only when it actually needs to.
  time_service_update();
}

static void update_planet_seek_accuracy_label(bool active) {
  if (!s_countdown_layer) return;
  if (active && planet_seek_compass_low_accuracy()) {
    time_t now = time(NULL);
    bool flash_visible = (now % 2) != 0; // on for odd seconds, off for even seconds
    if (flash_visible) {
      snprintf(s_countdown_buf, sizeof(s_countdown_buf), "Low compass accuracy");
      s_countdown_text_color = sky_layer_is_bright(&s_data, now) ? GColorBlack : GColorWhite;
      layer_set_hidden(s_countdown_layer, false);
    } else {
      s_countdown_buf[0] = '\0';
      layer_set_hidden(s_countdown_layer, true);
    }
  } else {
    // Same condition countdown_layer_update_proc/refresh_status_and_maybe_canvas
    // already use elsewhere: hidden whenever there's confirmed to be
    // no eclipse today. Planet seek only ever runs on a non-eclipse
    // day, so this always resolves to "hidden" in practice here, but
    // spelling it out the same way keeps this in sync if that ever
    // changes.
    eclipse_get_status_text(&s_data, time(NULL), s_countdown_buf, sizeof(s_countdown_buf), time_service_live_seconds_now(time(NULL)));
    layer_set_hidden(s_countdown_layer, s_data.valid && !s_data.has_eclipse);
  }
}


// ---- input integration ---------------------------------------------------

static void input_wake_handler(void *context) {
  (void)context;
  refresh_status_and_maybe_canvas(true);
}

static void input_labels_changed(bool visible, void *context) {
  (void)context;
  if (s_canvas_layer) eclipse_canvas_set_show_labels(s_canvas_layer, visible);
}

static void input_animation_frame_handler(bool active, uint32_t elapsed_ms, bool planet_seek, void *context) {
  (void)context;
  if (planet_seek) update_planet_seek_accuracy_label(active);
  if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  if (s_features_layer) layer_mark_dirty(s_features_layer);
  if (s_countdown_layer) layer_mark_dirty(s_countdown_layer);
  if (s_canvas_layer && planet_seek) eclipse_canvas_set_planet_seek(s_canvas_layer, active, elapsed_ms, planet_seek_heading_deg());
}

static void input_compass_feature_refresh_handler(void *context) {
  (void)context;
  if (s_features_layer) features_layer_refresh_content(s_features_layer, 85);
}

static void input_init_services(void) {
  input_init(&s_data, (InputCallbacks){
    .wake = input_wake_handler,
    .labels_changed = input_labels_changed,
    .animation_frame = input_animation_frame_handler,
    .compass_feature_refresh = input_compass_feature_refresh_handler,
  }, NULL);
}

// ---- corners overlay's own independent refresh cycle ----------------------

// Once a minute, matching the time service's normal MINUTE_UNIT baseline
// for anything that doesn't need to be genuinely live -- used to be a
// much shorter 5s cadence specifically for health data (heart rate
// especially), but Pebble's own HealthService doesn't actually refresh
// a heart-rate reading that often either, so 5s bought little real
// freshness for a real, constant battery cost. Seconds-precision
// content (Time: second, etc.) no longer depends on this timer at all
// -- see time_service_tick_handler()'s piggyback on SECOND_UNIT ticks.
// The time service requests seconds whenever that content is
// active, which gives it genuinely live per-second updates instead of
// whatever staleness this cadence would otherwise leave it with.
// CORNERS_REFRESH_MS retired -- see FEATURES_REFRESH_MS in features_layer.h
static AppTimer *s_corners_timer = NULL;

static void corners_timer_callback(void *data) {
  if (s_features_layer) features_layer_refresh_values(s_features_layer);
  s_corners_timer = app_timer_register(FEATURES_REFRESH_MS, corners_timer_callback, NULL);
}

// Battery-saver policy/state is implemented in battery_saver.c.
// This file only owns the consequences of a phase change: tick
// subscription, the corners refresh timer, redraws, and phone sync.

static void battery_saver_sync_phase_to_phone(void) {
  static uint8_t s_last_sent_phase = 0xFF;
  uint8_t phase = (uint8_t)battery_saver_phase();
  if (phase == s_last_sent_phase) return;
  if (comms_send_battery_saver_phase(phase)) s_last_sent_phase = phase;
}

static void battery_saver_phase_changed(BatterySaverPhase previous_phase, BatterySaverPhase new_phase, void *context) {
  (void)context;
  bool was_resting = previous_phase != BATTERY_SAVER_AWAKE;
  bool now_resting = new_phase != BATTERY_SAVER_AWAKE;

  // battery_saver_update() has already committed the new phase.
  time_service_update();

  if (now_resting && !was_resting) {
    if (s_corners_timer) {
      app_timer_cancel(s_corners_timer);
      s_corners_timer = NULL;
    }
  } else if (!now_resting && was_resting) {
    if (!s_corners_timer) {
      s_corners_timer = app_timer_register(FEATURES_REFRESH_MS, corners_timer_callback, NULL);
    }
  }

  battery_saver_sync_phase_to_phone();
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
// Digital mode's bottom panel used to just shrink its own
// height from the bottom (top edge fixed) as the obstruction grew --
// which cropped/hid its content (most visibly the digital clock's own
// time text) rather than keeping it fully visible, since the text's
// own position inside that panel never moved to compensate. Now the
// panel instead shifts UP by exactly however much the obstruction
// ate into it, keeping its own full height (and everything drawn in
// it) intact, with the sky canvas above it shrinking by that same
// amount to make room -- same "make room by moving, not cropping"
// idea analog mode's hands/canvas already used for this.
static void unobstructed_change_handler(AnimationProgress progress, void *context) {
  if (!s_window) return;
  Layer *root = window_get_root_layer(s_window);
  GRect full_bounds = layer_get_bounds(root); // the real screen size, unaffected by any obstruction
  GRect unobstructed = layer_get_unobstructed_bounds(root);
  int16_t obstruction_h = full_bounds.size.h - unobstructed.size.h;
  if (obstruction_h < 0) obstruction_h = 0;

  // Digital bar only (panel at the screen's bottom, right where a
  // system notification/Quick View obstruction grows from) -- Digital
  // top's own panel sits at the TOP instead, never touched by a bottom
  // obstruction, so it has nothing to shift here (see the plain
  // Digital-top canvas-shrink case further down instead, which mirrors
  // analog's own handling).
  if (s_data.bottom_style != 1 && !bottom_style_is_digital_top(s_data.bottom_style) && s_panel_layer) {
    // 152 -- the panel's own always-unobstructed top, fixed by
    // apply_layout() -- not read from the layer's current frame,
    // since that may already be shifted up from a previous
    // obstruction change and would compound instead of staying
    // anchored to the real, original position.
    int16_t full_top = 152;
    int16_t new_top = full_top - obstruction_h;
    if (new_top < 0) new_top = 0; // clamp -- an obstruction this tall would leave no room for the panel at all otherwise
    GRect frame = layer_get_frame(s_panel_layer);
    if (frame.origin.y != new_top) {
      frame.origin.y = new_top;
      layer_set_frame(s_panel_layer, frame);
      layer_mark_dirty(s_panel_layer);
    }

    if (s_canvas_layer) {
      GRect canvas_frame = layer_get_frame(s_canvas_layer);
      if (canvas_frame.size.h != new_top) {
        canvas_frame.size.h = new_top;
        layer_set_frame(s_canvas_layer, canvas_frame);
        // Force an immediate full redraw at the new size rather than
        // leaving the cached bitmap sized for the old frame -- the
        // canvas's own throttle would otherwise just blit that stale
        // cache back until its next scheduled minute.
        eclipse_canvas_set_data(s_canvas_layer, &s_data);
      }
    }
  }

  // Analog (canvas fills the whole screen) and Digital top (canvas
  // fills everything BELOW the fixed top panel) both just shrink the
  // sky canvas's own bottom edge to match the obstruction, same "make
  // room by shrinking from the edge nearest the obstruction" idea --
  // neither has a panel down there that would need to move out of the
  // way the way Digital bar's own does above.
  bool canvas_tracks_unobstructed_bottom = s_data.bottom_style == 1 || bottom_style_is_digital_top(s_data.bottom_style);
  if (canvas_tracks_unobstructed_bottom && s_canvas_layer) {
    int16_t canvas_top = bottom_style_is_digital_top(s_data.bottom_style) ? DIGITAL_PANEL_H : 0;
    int16_t new_h = unobstructed.size.h - canvas_top;
    if (new_h < 0) new_h = 0; // clamp -- matches the panel-side clamp above for the same reason
    GRect frame = layer_get_frame(s_canvas_layer);
    if (frame.size.h != new_h) {
      frame.size.h = new_h;
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
  // Only meaningful (and only shown on the settings page) in analog
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
  if (s_panel_layer) {
    layer_destroy(s_panel_layer);
    s_panel_layer = NULL;
  }
  if (s_top_gradient_layer) {
    sky_layer_top_gradient_destroy(s_top_gradient_layer);
    s_top_gradient_layer = NULL;
  }
  if (s_hands_layer) {
    layer_destroy(s_hands_layer);
    s_hands_layer = NULL;
  }
  if (s_features_layer) {
    features_layer_destroy(s_features_layer);
    s_features_layer = NULL;
  }

  if (style == 1) {
    // Analog: sky canvas fills the whole screen; hands render in
    // their own always-on-top transparent layer; no panel at all. Which
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
  } else if (bottom_style_is_digital_top(style)) {
    // Digital top: the sky canvas is exactly Digital bar's own 152px-
    // tall panel-less canvas (below, unchanged), just relocated to the
    // screen's BOTTOM instead of its top -- none of its own internal
    // astronomy/gradient math needs to know or care where its layer's
    // frame actually sits on screen. s_top_gradient_layer fills the
    // freed-up DIGITAL_PANEL_H strip at the real top with a plain
    // continuation of that same gradient (see its own header comment),
    // and s_panel_layer -- same Layer variable and draw function
    // Digital bar itself uses, just told via bottom_style_is_digital_top()
    // to skip its own opaque background fill -- sits on top of THAT,
    // transparent, so the gradient shows through behind the clock text.
    // Added in exactly this bottom-to-top z-order for that to work.
    s_canvas_layer = eclipse_canvas_create(GRect(0, DIGITAL_PANEL_H, bounds.size.w, bounds.size.h - DIGITAL_PANEL_H));
    layer_add_child(root, s_canvas_layer);
    s_top_gradient_layer = sky_layer_top_gradient_create(GRect(0, 0, bounds.size.w, DIGITAL_PANEL_H));
    layer_add_child(root, s_top_gradient_layer);
    s_panel_layer = layer_create(GRect(0, 0, bounds.size.w, DIGITAL_PANEL_H));
    layer_set_update_proc(s_panel_layer, draw_digital_clock_panel);
    layer_add_child(root, s_panel_layer);
    apply_clock_font();
  } else {
    // Digital bar: sky canvas keeps its original top-2/3
    // proportions; bottom third is the digital time.
    s_canvas_layer = eclipse_canvas_create(GRect(0, 0, bounds.size.w, 152));
    layer_add_child(root, s_canvas_layer);
    s_panel_layer = layer_create(GRect(0, 152, bounds.size.w, bounds.size.h - 152));
    layer_set_update_proc(s_panel_layer, draw_digital_clock_panel);
    layer_add_child(root, s_panel_layer);
    apply_clock_font();
  }

  // Overlays the FULL screen in every layout now -- not just reusing
  // the sky canvas's own frame the way this used to, since that frame
  // is only DIGITAL_PANEL_H-shrunk-from-one-side in either digital
  // layout (see the branches above) and features_recompute_layout()'s
  // digital-only slots (the 3-line side columns + single bottom
  // feature) need the real screen edge their own layout actually
  // anchors off -- not the sky's -- to land in the right place at all.
  // Corner features stay correctly anchored to the sky's own edge
  // regardless (see DIGITAL_PANEL_H's own comment in features_layer.c)
  // despite this layer being taller than the sky in either digital
  // layout.
  if (!s_features_layer) {
    s_features_layer = features_layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
    layer_add_child(root, s_features_layer);
  }

  // The countdown label always sits on top of everything else, so it
  // needs re-adding last after the canvas underneath was just rebuilt.
  if (s_countdown_layer) {
    layer_remove_from_parent(s_countdown_layer);
    layer_add_child(root, s_countdown_layer);
  }

  eclipse_canvas_set_data(s_canvas_layer, &s_data);
  if (s_top_gradient_layer) sky_layer_top_gradient_set_data(s_top_gradient_layer, &s_data);
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
  s_countdown_layer = layer_create(GRect(0, 20, bounds.size.w, 20));
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
  if (s_canvas_layer) eclipse_canvas_destroy(s_canvas_layer); // also releases marker renderer resources now
  if (s_panel_layer) layer_destroy(s_panel_layer);
  if (s_top_gradient_layer) sky_layer_top_gradient_destroy(s_top_gradient_layer);
  if (s_hands_layer) layer_destroy(s_hands_layer);
  if (s_features_layer) features_layer_destroy(s_features_layer);
  features_layer_unload_fonts();
  font_lookup_release(&s_clock_font_slot);
}

static void comms_data_applied(CommsChangeFlags changes, void *context) {
  (void)context;

  if (changes & COMMS_CHANGE_CLOCK_FONT) apply_clock_font();
  if (changes & COMMS_CHANGE_LAYOUT) apply_layout();
  if (s_hands_layer && (changes & COMMS_CHANGE_HANDS)) layer_mark_dirty(s_hands_layer);
  if (s_panel_layer && (changes & COMMS_CHANGE_PANEL)) layer_mark_dirty(s_panel_layer);
  if (s_features_layer && (changes & (COMMS_CHANGE_FEATURES | COMMS_CHANGE_FEATURE_VALUES))) {
    features_layer_set_data(s_features_layer, &s_data);
  }
  if (s_canvas_layer && (changes & COMMS_CHANGE_CANVAS)) {
    eclipse_canvas_set_data(s_canvas_layer, &s_data);
  }

  // Preserve the original invalid-payload fast path: settings that arrive
  // before the first valid eclipse payload are applied and rendered, but
  // battery-saver/tick policy is not re-evaluated until valid data exists.
  if (!s_data.valid) {
    persistence_save(&s_data);
    refresh_status_and_maybe_canvas(true);
    return;
  }

  battery_saver_set_enabled(s_data.battery_saver_enabled);
  battery_saver_update(time(NULL));
  time_service_update();
  persistence_save(&s_data);
  refresh_status_and_maybe_canvas(true);
}

static void init(void) {
  // The generated MK_* table is intentionally based on package.json's
  // messageKeys ordering. Verify the one arithmetic assumption that the
  // communication parser relies on so a future SDK/key-generation change
  // fails loudly instead of silently corrupting incoming fields.
  if (MESSAGE_KEY_WEATHER_LAST_UPDATE - MESSAGE_KEY_MESSAGE_TYPE != MK_WEATHER_LAST_UPDATE) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "message key base assumption broken -- regenerate message_key_index.h");
  }

  persistence_load(&s_data);
  battery_saver_init(s_data.battery_saver_enabled, time(NULL), battery_saver_phase_changed, NULL);

  // Initialize the time service before pushing the window because Pebble may
  // invoke window_load() synchronously during window_stack_push(). The load
  // path computes the countdown's precision, so the scheduling policy must
  // already have a valid data pointer at that point.
  time_service_init(&s_data, time_service_tick_handler, NULL);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  input_init_services();
  unobstructed_area_service_subscribe(s_unobstructed_handlers, NULL);
  s_corners_timer = app_timer_register(FEATURES_REFRESH_MS, corners_timer_callback, NULL);

  comms_init(&s_data, comms_data_applied, NULL);
  battery_saver_sync_phase_to_phone();
}

static void deinit(void) {
  time_service_deinit();
  input_deinit();
  unobstructed_area_service_unsubscribe();
  if (s_corners_timer) {
    app_timer_cancel(s_corners_timer);
    s_corners_timer = NULL;
  }
  comms_deinit();
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
