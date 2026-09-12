#include <pebble.h>
#include "eclipse_data.h"
#include "battery_saver.h"
#include "input.h"

static EclipseData *s_data;
static InputCallbacks s_callbacks;
static bool s_labels_visible;

// ---- shake animation ------------------------------------------------------
// User setting ("On shake animation" section, off by default, radio-style
// single choice via shake_anim_mode: 0=off, 1=smooth second hand, 2=Planet
// seek, 3=Both -- see its own eclipse_data.h comment): while the shake-to-
// reveal labels are up, the second hand (if shown) switches from its normal
// once-a-second jump to continuous sub-second motion (modes 1 and 3 -- see
// shake_anim_wants_smooth_second() below and hands_layer_update_proc()'s
// own use of it; Planet seek running at the same time, mode 3, doesn't
// suppress this), and/or the sky view repositions to face wherever the compass
// currently points (mode 2 or 3, see maybe_start_compass_feature()). Every
// site below that cares whether Planet seek specifically is wanted goes
// through shake_anim_wants_planet_seek() rather than comparing
// shake_anim_mode to 2 directly -- this used to just key off
// s_shake_anim_active being true, which meant Planet seek (mode 2) silently
// dragged smooth-second along with it any time seconds were shown, since
// nothing actually distinguished "shake anim is running at all" from
// "smooth second hand specifically was the one requested". Driven by its
// own fast timer, same shape as the two startup animations above -- and,
// like them, keyed off a frame counter (s_shake_anim_elapsed_ms) rather
// than wall-clock time(NULL) seconds, so many consecutive redraws within
// the same second don't all compute an identical value.
#define SHAKE_ANIM_FRAME_MS 33 // 30fps, per request

static AppTimer *s_shake_anim_timer = NULL;
static bool s_shake_anim_active = false;
static uint32_t s_shake_anim_elapsed_ms = 0;
static uint32_t s_shake_anim_duration_ms = 3000;

// True for shake_anim_mode 2 (Planet seek only) or 3 (Both).
static bool shake_anim_wants_planet_seek(uint8_t mode) {
  return mode == 2 || mode == 3;
}

// True for shake_anim_mode 1 (Smooth second hand only) or 3 (Both).
static bool shake_anim_wants_smooth_second(uint8_t mode) {
  return mode == 1 || mode == 3;
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
// Planet seek's own watch-side compass reading -- subscribed only for
// as long as the animation itself runs (compass/magnetometer use has
// a real, ongoing power cost, unlike a plain timer), storing just the
// latest heading for whatever future rendering code reads it via
// planet_seek_heading_deg() below.
//
// Smoothing here follows the same target-angle/presented-angle split
// Pebble's own compass app uses (github.com/coredevices/pebble-compass,
// data_provider.{h,c} -- its README describes the presented heading as
// "fak[ing] a physical model with friction and inertia", and its
// DataProviderHandlers keep an "attraction_modifier"/"friction_modifier"
// pair separate from the raw input heading rather than one single
// smoothing knob). Two things about that shape mattered enough to pull
// over:
//
// 1. The raw compass sample only ever updates a *target* heading here
//    (s_planet_seek_heading_target_deg) -- the actual on-screen value
//    (s_planet_seek_heading_smoothed_fp) is advanced separately, once
//    per animation frame, by planet_seek_heading_physics_step() below.
//    Previously this smoothed every raw sample right here in the
//    compass callback, but compass_service_subscribe()'s callback
//    doesn't fire on a fixed schedule -- the OS delivers a new
//    magnetometer sample whenever one's ready, which isn't the same
//    cadence as the 30fps SHAKE_ANIM_FRAME_MS animation loop actually
//    drawing the result. Smoothing per-sample meant the *effective*
//    smoothing time constant quietly depended on how often samples
//    happened to arrive. Stepping it from the fixed-rate animation
//    timer instead (already running throughout Planet seek -- see
//    shake_anim_timer_callback()) makes every smoothing step the same
//    real-world size regardless of sensor timing, which is a large
//    part of why the reference app holds up smoothly.
//
// 2. The smoothing itself is a velocity-based spring (attraction pulls
//    the presented angle toward the target, friction damps how much
//    of that pull actually shows up as motion) rather than a plain
//    one-shot exponential blend. A raw sample's noise only nudges the
//    *velocity* term, which friction then bleeds off over a couple of
//    frames, instead of being applied straight to the displayed
//    position the way every single-alpha EMA update does -- so it
//    settles into a steady heading rather than visibly hunting around
//    it, while still tracking a real, deliberate turn promptly.
static int32_t s_planet_seek_heading_target_deg = 0;   // degrees 0-359, true north-relative, CLOCKWISE (see below) -- latest raw compass sample, untouched
static int32_t s_planet_seek_heading_smoothed_fp = 0;  // Q24.8 fixed-point (degrees << 8) -- the actual presented/rendered heading
static int32_t s_planet_seek_heading_velocity_fp = 0;  // Q24.8 fixed-point, degrees-per-frame -- the spring's "inertia"
// True once at least one real compass sample has arrived this Planet
// seek session -- reset by maybe_start_shake_animation() each time the
// mode (re)starts, so the very first reading after that jumps the
// presented angle straight to wherever the compass actually says
// instead of the spring pulling in from 0 (or a stale heading left
// over from last time), which would otherwise show up as a big,
// pointless swing right as the mode opens.
static bool s_planet_seek_heading_has_reading = false;
// True whenever the compass isn't fully calibrated yet (or has no
// reading at all) -- see planet_seek_compass_handler()'s own comment.
// Starts true (not false) since there's no reading at all until the
// first callback fires, and "we don't know the heading yet" is exactly
// the "don't trust this" state the low-accuracy warning is for.
static bool s_planet_seek_compass_low_accuracy = true;

static void planet_seek_compass_handler(CompassHeadingData data) {
  // CompassHeading (both magnetic_heading and true_heading -- the
  // latter is currently just an alias for the former, see Pebble's
  // own CompassService docs) increases COUNTER-clockwise from north:
  // https://developer.rebble.io/docs/c/Foundation/Event_Service/CompassService/
  // "Measured angle that increases counter-clockwise from magnetic
  // north (use int clockwise_heading = TRIG_MAX_ANGLE -
  // heading_data.magnetic_heading ... to find your heading clockwise
  // from magnetic north)." Every other bearing in this app -- the
  // az_decideg samples PKJS sends (0=north, 90=east, ...) and the
  // compass-rose math in draw_compass_icon() below -- assumes the
  // usual CLOCKWISE-from-north convention instead, so without this
  // flip, "heading" here was actually the mirror image of the
  // wearer's real facing direction: turning right (clockwise) made
  // the stored value swing as if the wearer had turned left, which is
  // exactly the "objects move away instead of towards me" symptom.
  //
  // Just the target, here -- see this whole block's own top comment
  // for why the actual smoothing has moved out of this handler and
  // into planet_seek_heading_physics_step().
  CompassHeading clockwise = TRIG_MAX_ANGLE - data.true_heading;
  s_planet_seek_heading_target_deg = (int32_t)(((int64_t)clockwise * 360) / TRIG_MAX_ANGLE) % 360;

  if (!s_planet_seek_heading_has_reading) {
    s_planet_seek_heading_smoothed_fp = s_planet_seek_heading_target_deg << 8;
    s_planet_seek_heading_velocity_fp = 0;
    s_planet_seek_heading_has_reading = true;
  }

  // Calibrated = high confidence; Calibrating = a reading exists but
  // is still being refined; DataInvalid/Unavailable = no usable
  // reading at all. Anything short of Calibrated is worth flagging to
  // the wearer, per the compass guide's own "tell the user whether
  // this can be trusted" framing.
  s_planet_seek_compass_low_accuracy = (data.compass_status != CompassStatusCalibrated);
}

// Advances the presented heading one animation frame toward whatever
// the compass most recently said -- called once per
// SHAKE_ANIM_FRAME_MS from shake_anim_timer_callback() while Planet
// seek is active (see this block's own top comment for why a fixed
// animation-frame cadence, rather than the compass callback itself,
// drives this).
static void planet_seek_heading_physics_step(void) {
  if (!s_planet_seek_heading_has_reading) return;

  // Same one-shot "how tilted is the watch right now" read the old
  // per-sample smoothing used: a single accel_service_peek() (no
  // running subscription -- this only needs "right now", not a
  // stream) tells flat (face roughly horizontal, z axis dominant)
  // apart from upright/raised (x/y dominant) cheaply. Kept for the
  // same reason as before -- the magnetometer heading is visibly
  // noisier the flatter the watch sits -- but note Pebble's own
  // compass guide independently backs the "raised is the good case"
  // half of that: CompassService is documented as expecting "the top
  // of watch parallel to the ground", i.e. the normal raised,
  // glance-at-the-time pose, to read the wearer's facing direction
  // correctly (https://developer.rebble.io/guides/events-and-services/compass/).
  AccelData accel = { 0 };
  int32_t flatness_pct = 0; // 0 = vertical/raised (light smoothing), 100 = flat (heavy smoothing)
  if (accel_service_peek(&accel) == 0) {
    int32_t az = accel.z < 0 ? -accel.z : accel.z;
    int32_t axy = (accel.x < 0 ? -accel.x : accel.x) + (accel.y < 0 ? -accel.y : accel.y);
    flatness_pct = (az * 100) / (az + axy + 1); // +1: avoid a div-by-zero on a (0,0,0) reading
  }
  // Two independent knobs instead of the old single alpha -- attraction
  // is how hard the target angle pulls on the velocity each frame,
  // friction is how much of the existing velocity survives each frame.
  // Both tuned by the same flatness reading as before (responsive
  // while raised, gentle while flat), and deliberately conservative
  // (friction comfortably above attraction at both ends) so the arrow
  // eases into place rather than overshooting and ringing back --
  // raised's numbers are just enough livelier than flat's to feel
  // responsive without wobbling.
  int32_t attraction_pct = 32 - ((32 - 14) * flatness_pct) / 100; // 32% raised -> 14% flat
  int32_t friction_pct   = 55 + ((80 - 55) * flatness_pct) / 100; // 55% raised -> 80% flat

  int32_t smoothed_deg = s_planet_seek_heading_smoothed_fp >> 8;
  // Shortest signed distance from the presented heading to the
  // current target, handling the 359->0 wraparound (a naive
  // `target - smoothed` would otherwise pull the "long way around"
  // through 180 whenever the two straddle north).
  int32_t delta = ((s_planet_seek_heading_target_deg - smoothed_deg + 540) % 360) - 180;

  // Semi-implicit Euler, one animation frame at a time (the frame's
  // dt is fixed at SHAKE_ANIM_FRAME_MS, so it's baked into the two
  // percentages above rather than multiplied in separately): pull the
  // velocity toward the target by "attraction", then damp whatever's
  // left by "friction", then move the presented angle by that
  // velocity. Errors only ever enter through the pull step, so a
  // single noisy sample shows up as one small nudge to velocity that
  // friction bleeds off over the next couple of frames, rather than
  // an instant jump in the displayed heading.
  s_planet_seek_heading_velocity_fp += (delta * 256 * attraction_pct) / 100;
  s_planet_seek_heading_velocity_fp = (s_planet_seek_heading_velocity_fp * (100 - friction_pct)) / 100;
  s_planet_seek_heading_smoothed_fp += s_planet_seek_heading_velocity_fp;

  // Keep the fixed-point value's whole-degree part wrapped into 0-359
  // so it can't slowly drift outside a sane range over a long Planet
  // seek session, and so the delta math above keeps working the same
  // way frame after frame.
  while (s_planet_seek_heading_smoothed_fp < 0) s_planet_seek_heading_smoothed_fp += (360 << 8);
  while (s_planet_seek_heading_smoothed_fp >= (360 << 8)) s_planet_seek_heading_smoothed_fp -= (360 << 8);
}



// ---- Compass feature (corner/edge content, "Compass" under Utilities) --
// Independent of Planet seek's own compass use above -- this one is
// tied to whichever corner/edge slot has "Compass" (content 85)
// assigned, not to shake_anim_mode. Active (compass subscribed,
// redrawing) for exactly 15s after a shake, then sleeps (unsubscribed,
// shows "Z z" and three dashes instead of a heading) until the next
// one -- see compass_feature_is_asleep()/compass_feature_heading_deg()
// in features_layer.c's own content-85 case for how this gets drawn.
#define COMPASS_FEATURE_DURATION_MS 15000
#define COMPASS_FEATURE_FRAME_MS 300 // ~3fps -- plenty for a heading readout; far cheaper than the 30fps shake-gradient system, which needs to look like continuous motion and this doesn't

static AppTimer *s_compass_feature_timer = NULL;
static bool s_compass_feature_active = false;
static uint32_t s_compass_feature_elapsed_ms = 0;
static int32_t s_compass_feature_heading_deg = 0;

static void compass_feature_handler(CompassHeadingData data) {
  if (data.compass_status != CompassStatusDataInvalid) {
    // Same counter-clockwise-vs-clockwise fix as
    // planet_seek_compass_handler() above -- see its comment.
    CompassHeading clockwise = TRIG_MAX_ANGLE - data.true_heading;
    s_compass_feature_heading_deg = (int32_t)(((int64_t)clockwise * 360) / TRIG_MAX_ANGLE) % 360;
  }
}



static void compass_feature_timer_callback(void *data) {
  s_compass_feature_elapsed_ms += COMPASS_FEATURE_FRAME_MS;
  if (s_compass_feature_elapsed_ms >= COMPASS_FEATURE_DURATION_MS) {
    s_compass_feature_active = false;
    s_compass_feature_timer = NULL;
    compass_service_unsubscribe();
  } else {
    s_compass_feature_timer = app_timer_register(COMPASS_FEATURE_FRAME_MS, compass_feature_timer_callback, NULL);
  }
  if (s_callbacks.compass_feature_refresh) s_callbacks.compass_feature_refresh(s_callbacks.context);
}

// Called from tap_handler() below, once per shake -- restarts the
// window fresh each time, same shape as maybe_start_shake_animation().
static void maybe_start_compass_feature(void) {
  if (!features_layer_content_in_use(s_data, 85)) return;
  s_compass_feature_active = true;
  s_compass_feature_elapsed_ms = 0;
  if (s_compass_feature_timer) app_timer_cancel(s_compass_feature_timer);
  s_compass_feature_timer = app_timer_register(COMPASS_FEATURE_FRAME_MS, compass_feature_timer_callback, NULL);
  compass_service_subscribe(compass_feature_handler);
}

// While Planet seek (shake_anim_mode 2 or 3) is active, the countdown label
// at the top of the screen would otherwise just sit hidden (per
// countdown_layer_update_proc's own comment, it only ever shows
// eclipse-phase text, and Planet seek never runs on an eclipse day --
// see maybe_start_shake_animation() below) -- so it's free real estate
// for a compass accuracy warning instead. Shown only while genuinely
// needed (compass not yet Calibrated) and only for as long as Planet
// seek itself is on screen; reverted to its normal eclipse-countdown
// state (blank/hidden, since there's no eclipse today) the instant
// either the low-accuracy condition clears (the wearer finished
// calibrating mid-animation) or the animation itself ends, rather
// than leaving a stale warning up. While shown, it flashes on/off
// once a second (visible on odd seconds, hidden on even ones) rather
// than sitting on screen solid, so it reads as an active warning that
// needs attention rather than a normal static label.
static void shake_anim_timer_callback(void *data) {
  (void)data;
  s_shake_anim_elapsed_ms += SHAKE_ANIM_FRAME_MS;
  bool planet_seek_wanted = shake_anim_wants_planet_seek(s_data->shake_anim_mode);
  bool still_active = s_labels_visible && s_shake_anim_elapsed_ms < s_shake_anim_duration_ms;
  if (!still_active) {
    s_shake_anim_active = false;
    s_shake_anim_timer = NULL;
    if (planet_seek_wanted) compass_service_unsubscribe();
  } else {
    s_shake_anim_timer = app_timer_register(SHAKE_ANIM_FRAME_MS, shake_anim_timer_callback, NULL);
  }
  if (planet_seek_wanted) planet_seek_heading_physics_step();
  if (s_callbacks.animation_frame) s_callbacks.animation_frame(still_active, s_shake_anim_elapsed_ms, planet_seek_wanted, s_callbacks.context);
}

// Called from tap_handler() below, once per shake -- restarts the
// window fresh each time (unlike the label-reveal timer next to it,
// which reschedules), so a repeated shake mid-animation also resets
// the gradient/hand-smoothing window rather than just extending it.
static void maybe_start_shake_animation(void) {
  if (s_data->shake_anim_mode == 0) return;
  if (eclipse_is_active(&s_data, time(NULL))) return; // no shake animations (smooth second OR Planet seek) while the eclipse itself is actively in progress, per request
  if (shake_anim_wants_planet_seek(s_data->shake_anim_mode) && s_data->has_eclipse) return; // Planet seek (modes 2 and 3) never runs on an eclipse day, per request
  s_shake_anim_active = true;
  s_shake_anim_elapsed_ms = 0;
  uint8_t seconds = s_data->shake_label_seconds > 0 ? s_data->shake_label_seconds : 3;
  s_shake_anim_duration_ms = (uint32_t)seconds * 1000;
  if (s_shake_anim_timer) app_timer_cancel(s_shake_anim_timer);
  s_shake_anim_timer = app_timer_register(SHAKE_ANIM_FRAME_MS, shake_anim_timer_callback, NULL);
  if (shake_anim_wants_planet_seek(s_data->shake_anim_mode)) {
    // Fresh compass smoothing state each time Planet seek (re)starts
    // -- see s_planet_seek_heading_has_reading's own comment for why
    // (otherwise the first reading of a new session would slowly
    // smooth in from last session's leftover heading instead of
    // jumping straight to wherever the compass actually says now).
    s_planet_seek_heading_has_reading = false;
    compass_service_subscribe(planet_seek_compass_handler);
  }
}

// Moved up from next to the rest of the "background on start" state
// below (s_bg_anim_timer/s_bg_anim_played/bg_anim_timer_callback) --
// only these 2 need to be visible this early, for
// hands_layer_update_proc()'s own use of them (see its comment on the
// Planets-sweep hand animation) below.
#define BG_ANIM_MS 1400
static bool s_bg_anim_active = false;
static uint16_t s_bg_anim_elapsed_ms = 0;


static AppTimer *s_label_timer;

static void hide_labels_callback(void *data) {
  (void)data;
  s_label_timer = NULL;
  s_labels_visible = false;
  if (s_callbacks.labels_changed) s_callbacks.labels_changed(false, s_callbacks.context);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  (void)axis; (void)direction;
  time_t now = time(NULL);
  bool was_resting = battery_saver_phase() != BATTERY_SAVER_AWAKE;
  battery_saver_note_activity(now);
  battery_saver_update(now);
  if (was_resting && s_callbacks.wake) s_callbacks.wake(s_callbacks.context);
  s_labels_visible = true;
  if (s_callbacks.labels_changed) s_callbacks.labels_changed(true, s_callbacks.context);
  uint8_t seconds = s_data->shake_label_seconds > 0 ? s_data->shake_label_seconds : 3;
  uint32_t reveal_ms = (uint32_t)seconds * 1000;
  if (s_label_timer) app_timer_reschedule(s_label_timer, reveal_ms);
  else s_label_timer = app_timer_register(reveal_ms, hide_labels_callback, NULL);
  maybe_start_shake_animation();
  maybe_start_compass_feature();
}

void input_init(EclipseData *data, InputCallbacks callbacks, void *context) {
  s_data = data; s_callbacks = callbacks; s_callbacks.context = context;
  accel_tap_service_subscribe(tap_handler);
}

void input_deinit(void) {
  accel_tap_service_unsubscribe();
  compass_service_unsubscribe();
  if (s_shake_anim_timer) { app_timer_cancel(s_shake_anim_timer); s_shake_anim_timer = NULL; }
  if (s_compass_feature_timer) { app_timer_cancel(s_compass_feature_timer); s_compass_feature_timer = NULL; }
  if (s_label_timer) { app_timer_cancel(s_label_timer); s_label_timer = NULL; }
  s_data = NULL; s_callbacks = (InputCallbacks){0}; s_labels_visible = false;
}

bool input_shake_animation_active(void) { return s_shake_anim_active; }
bool input_shake_animation_wants_smooth_second(uint8_t mode) { return shake_anim_wants_smooth_second(mode); }
int32_t planet_seek_heading_deg(void) { return s_planet_seek_heading_smoothed_fp >> 8; }
bool planet_seek_compass_low_accuracy(void) { return s_planet_seek_compass_low_accuracy; }
int32_t compass_feature_heading_deg(void) { return s_compass_feature_heading_deg; }
bool compass_feature_is_asleep(void) { return !s_compass_feature_active; }
