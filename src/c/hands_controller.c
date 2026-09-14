#include <pebble.h>
#include "eclipse_status.h"
#include "hands_controller.h"
#include "hand_layer.h"
#include "input.h"
#include "background_animation.h"
#include "eclipse_ui.h"
#include "features_layer.h"

#include "background_layer.h"
static EclipseData *s_data = NULL;
static Layer *s_hands_layer = NULL;
static HandsControllerInvalidateHandler s_invalidate_handler = NULL;
static void *s_invalidate_context = NULL;

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
  eclipse_ui_get_active_color_scheme(s_data, now, &bg, &main_color, &accent_color);

  // Markers (procedural presets, custom, and bitmap styles alike) are no
  // longer drawn here -- they're part of the sky canvas's own cached
  // redraw now (background_layer.c), composited once per its own
  // once-a-minute/force-redraw cadence rather than every tick this
  // always-on-top hands layer runs.
  features_ensure_corner_custom_font(s_data->corner_font);

  int32_t hour_angle = (int32_t)(((int64_t)((t->tm_hour % 12) * 3600 + t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600));

  int32_t min_angle = ((t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);

  int32_t sec_angle;
  // Smooth sub-second motion for shake_anim_mode 1 (Smooth second hand
  // alone) AND mode 3 (Both) -- see shake_anim_wants_smooth_second()'s
  // own comment. Planet seek (mode 2 or 3) repositions the sky by
  // compass and is otherwise independent of this; when both are
  // requested together (mode 3) the second hand still gets the
  // continuous sub-second motion Smooth second hand promises.
  if (input_shake_animation_active() && s_data->show_seconds && input_shake_animation_wants_smooth_second(s_data->shake_anim_mode)) {
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
    // time shift to chase in that case. Shares BACKGROUND_ANIMATION_DURATION_MS as its own
    // total duration and ease_out_cubic_1000
    // (identical curve to background_layer.c's own
    // bg_anim_ease_out_1000 -- see that function's own comment) so the
    // two sweeps advance in step with each other.
    if (s_data->startup_clock_anim_mode == 2 && background_animation_is_active() && s_data->bg_anim_mode == 1) {
      int32_t progress = background_animation_progress_1000();
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
  HandConfig hour_cfg = s_data->hand_hour;
  HandConfig min_cfg = s_data->hand_minute;
  HandConfig sec_cfg = s_data->hand_second;

  hand_layer_draw(ctx, center, hour_angle, &hour_cfg, main_color, accent_color, bg, s_data->shadow_translucent, s_data->shadow_angle_deg, hour_length_scale_1000);
  hand_layer_draw(ctx, center, min_angle, &min_cfg, main_color, accent_color, bg, s_data->shadow_translucent, s_data->shadow_angle_deg, min_length_scale_1000);
  if (s_data->show_seconds) {
    hand_layer_draw(ctx, center, sec_angle, &sec_cfg, main_color, accent_color, bg, s_data->shadow_translucent, s_data->shadow_angle_deg, sec_length_scale_1000);
  }

  hand_layer_draw_center_circle(ctx, center, s_data->center_circle_radius, s_data->center_circle_color,
                                 main_color, accent_color, bg);
}

static void startup_anim_timer_callback(void *data) {
  s_startup_anim_elapsed_ms += STARTUP_ANIM_FRAME_MS;
  if (s_startup_anim_elapsed_ms >= STARTUP_CLOCK_ANIM_MS) {
    s_startup_clock_anim_active = false;
    s_startup_anim_timer = NULL;
  } else {
    s_startup_anim_timer = app_timer_register(STARTUP_ANIM_FRAME_MS, startup_anim_timer_callback, NULL);
  }
  if (s_invalidate_handler) s_invalidate_handler(s_invalidate_context);
  else if (s_hands_layer) layer_mark_dirty(s_hands_layer);
}




void hands_controller_init(EclipseData *data, HandsControllerInvalidateHandler handler, void *context) {
  s_data = data;
  s_invalidate_handler = handler;
  s_invalidate_context = context;
}

void hands_controller_deinit(void) {
  if (s_startup_anim_timer) {
    app_timer_cancel(s_startup_anim_timer);
    s_startup_anim_timer = NULL;
  }
  s_data = NULL;
  s_invalidate_handler = NULL;
  s_invalidate_context = NULL;
}

Layer *hands_controller_create_layer(GRect frame) {
  s_hands_layer = layer_create(frame);
  layer_set_update_proc(s_hands_layer, hands_layer_update_proc);
  return s_hands_layer;
}

void hands_controller_destroy_layer(Layer *layer) {
  if (layer) layer_destroy(layer);
  if (layer == s_hands_layer) s_hands_layer = NULL;
}

bool hands_controller_animation_active(void) {
  return s_startup_clock_anim_active;
}

int32_t hands_controller_animation_progress_1000(void) {
  if (!s_startup_clock_anim_active) return 1000;
  int32_t progress = ((int32_t)s_startup_anim_elapsed_ms * 1000) / STARTUP_CLOCK_ANIM_MS;
  return progress > 1000 ? 1000 : progress;
}

int32_t hands_controller_animation_eased_progress_1000(void) {
  return ease_out_lut_1000(hands_controller_animation_progress_1000());
}

void hands_controller_start_startup_animation(void) {
  if (s_startup_clock_anim_played || !s_data || s_data->startup_clock_anim_mode == 0) return;
  if (eclipse_status_is_active(s_data, time(NULL))) return;
  s_startup_clock_anim_played = true;
  s_startup_clock_anim_active = true;
  s_startup_anim_elapsed_ms = 0;
  s_startup_anim_timer = app_timer_register(STARTUP_ANIM_FRAME_MS, startup_anim_timer_callback, NULL);
}
