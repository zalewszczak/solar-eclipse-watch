#include <pebble.h>
#include "eclipse_data.h"
#include "background_layer.h"
#include "features_layer.h"
#include "font_lookup.h"

// EclipseData is well past Pebble's 256-byte-per-key persist limit
// (PERSIST_DATA_MAX_LENGTH), so it's split across several keys here
// rather than written as one blob -- see save_data()/load_data().
#define PERSIST_KEY_DATA_BASE 1
#define PERSIST_CHUNK_SIZE 200
#define PERSIST_CHUNK_COUNT 12 // 2400 bytes of capacity -- was 5 (1000 bytes), bumped generously to
                                 // cover Planet seek's new az sample arrays (~364 bytes) with real
                                 // headroom for whatever gets added next, now that Pebble's own
                                 // per-app persist budget is 1MB total (confirmed directly with
                                 // Pebble support as of this comment -- undocumented publicly, so
                                 // don't just trust a web search on it), not the old 4KB. Struct
                                 // size is still checked at compile time below
                                 // (persist_capacity_check) -- see that comment if it ever fails to
                                 // compile after adding fields; bump this number again if so, there's
                                 // no real reason to ration chunks tightly anymore.

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
static FontSlot s_clock_font_slot = FONT_SLOT_EMPTY;
static GFont small_font;
static FontSlot s_clock_small_font_slot = FONT_SLOT_EMPTY;

// static declarations:
static bool use_small_seconds_for_digital_clock(void);
static void tick_handler(struct tm *tick_time, TimeUnits units_changed);

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

// ---- shake animation ------------------------------------------------------
// User setting ("Style" section, off by default, radio-style single
// choice via shake_anim_mode: 0=off, 1=gradient outline shift only,
// 2=smooth second hand only, 3=both): while the shake-to-reveal
// labels are up, outlines sweep through a rainbow gradient and/or the
// second hand (if shown) switches from its normal once-a-second jump
// to continuous sub-second motion. Driven by its own fast timer, same
// shape as the two startup animations above -- and, like them, keyed
// off a frame counter (s_shake_anim_elapsed_ms) rather than
// wall-clock time(NULL) seconds, which is what made the color cycle
// visibly jump only once a second regardless of how often the timer
// itself fired: time(NULL) only has whole-second resolution, so many
// consecutive redraws within the same second were all computing the
// identical "elapsed seconds" value and thus the identical color.
#define SHAKE_ANIM_FRAME_MS 33 // 30fps, per request

static AppTimer *s_shake_anim_timer = NULL;
static bool s_shake_anim_active = false;
static uint32_t s_shake_anim_elapsed_ms = 0;
static uint32_t s_shake_anim_duration_ms = 3000;

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

// ---- rainbow outline gradient -----------------------------------------
// A real gradient ACROSS THE SCREEN (color depends on where on screen
// a pixel is, not just on which item or what time it is), rather than
// every outlined item flashing the exact same single color together --
// see rainbow_outline_color_at()'s own comment for how. 24 entries,
// stored as raw packed argb bytes (not GColor structs -- GColorFromRGB()
// can't safely be used inside a static array initializer, since it's a
// macro that isn't guaranteed to reduce to a constant expression
// everywhere; gcolor_from_packed() unpacks a byte back into a real
// GColor at the point of use instead, which is just a normal function
// call and has no such restriction) computed offline via a plain
// integer 6-segment HSV hue sweep, then snapped to the nearest of
// Pebble's own 4 levels per channel (0/85/170/255) -- this project's
// whole 64-color palette is built from exactly those 4 levels per
// channel, so there's no shade this table could contain that isn't
// already a real, exact color in it.
#define SHAKE_RAINBOW_LUT_SIZE 24
static const uint8_t SHAKE_RAINBOW_LUT[SHAKE_RAINBOW_LUT_SIZE] = {
  0xF0, 0xF4, 0xF4, 0xF8, 0xFC, 0xEC, 0xEC, 0xDC, 0xCC, 0xCD, 0xCD, 0xCE,
  0xCF, 0xCB, 0xCB, 0xC7, 0xC3, 0xD3, 0xD3, 0xE3, 0xF3, 0xF2, 0xF2, 0xF1
};

// screen_x: the pixel's own x coordinate -- divided by 3 so each LUT
// entry spans a few pixels of screen width ("predefine some gradient
// line couple pixels width" per the request) rather than changing
// color every single pixel, which read as noise rather than a
// gradient. shift: how far the whole table has scrolled so far (see
// shake_anim_timer_callback() below) -- advancing this ONE integer
// once per frame is the entire "animation" cost; no per-pixel
// recomputation happens here at all, just an index shift + one array
// read + one function call to unpack it, all integer math.
static GColor rainbow_outline_color_at(int16_t screen_x, uint8_t shift) {
  int32_t idx = (screen_x / 3 + shift) % SHAKE_RAINBOW_LUT_SIZE;
  if (idx < 0) idx += SHAKE_RAINBOW_LUT_SIZE;
  return gcolor_from_packed(SHAKE_RAINBOW_LUT[idx]);
}

static uint8_t s_shake_gradient_shift = 0; // advanced by 1 each shake_anim_timer_callback() tick while gradient mode is on

// Returns the color to actually draw an outline pixel/item in right
// now. screen_x is that pixel/item's own x coordinate, used to sample
// the scrolling rainbow strip above when gradient mode
// (shake_anim_mode 1 or 3) is on; normal_color (unchanged) otherwise,
// including whenever the animation isn't currently running at all.
// Genuine per-pixel use (hand outlines, drawn through this project's
// own subpixel rasterizer) gets a real per-pixel gradient; text/icon
// outlines -- drawn through Pebble's own text/bitmap compositing,
// which only accepts one fill color per call, with no way to vary it
// pixel-by-pixel -- fall back to sampling the strip once at that
// item's own screen position instead, which still gives different
// items different colors based on where they are, just not an
// internal gradient within a single item's own outline.
GColor shake_outline_color(GColor normal_color, int16_t screen_x) {
  if (!s_shake_anim_active) return normal_color;
  if (!(s_data.shake_anim_mode == 1 || s_data.shake_anim_mode == 3)) return normal_color;
  return rainbow_outline_color_at(screen_x, s_shake_gradient_shift);
}

// hand_layer.c's own version of the above -- hand outlines are drawn
// through this project's own subpixel rasterizer (see subpixel.h's
// stroke_*_gradient_fp() functions), which CAN sample a color per
// pixel, so it needs the raw shift value to do that itself rather
// than a single pre-resolved color the way features_layer.c's flatter
// text/icon outlines do above. Returns false (nothing written to
// *out_shift) whenever the gradient shouldn't apply right now, so the
// caller knows to fall back to its own normal fixed-color outline.
bool shake_gradient_active(uint8_t *out_shift) {
  if (!s_shake_anim_active) return false;
  if (!(s_data.shake_anim_mode == 1 || s_data.shake_anim_mode == 3)) return false;
  *out_shift = s_shake_gradient_shift;
  return true;
}

// Planet seek's own watch-side compass reading -- subscribed only for
// as long as the animation itself runs (compass/magnetometer use has
// a real, ongoing power cost, unlike a plain timer), storing just the
// latest heading for whatever future rendering code reads it via
// planet_seek_heading_deg() below. "Pulled for each refresh" per the
// request is naturally what this already does: compass_service_
// subscribe()'s own callback fires on every new reading, and the
// redraw loop (driven by shake_anim_timer_callback(), already
// running every SHAKE_ANIM_FRAME_MS while planet seek is active) just
// reads whatever s_planet_seek_heading_deg currently holds each time
// it redraws, rather than the two needing to be tightly synchronized.
//
// Stored smoothed (exponential moving average) rather than as the
// raw reading -- the magnetometer heading on its own is visibly
// jittery frame to frame, which showed up as the arrows/labels
// twitching around instead of moving steadily. Kept as a Q24.8
// fixed-point value (degrees << 8), both because whole-degree-only
// smoothing barely smooths at all (it would just hop through each
// integer degree with rounding noise indistinguishable from the raw
// jitter) and because the exponential blend below needs a real
// fractional step size to settle toward the true heading rather than
// oscillate by +/-1 degree around it forever.
static int32_t s_planet_seek_heading_smoothed_fp = 0; // degrees << 8, true north-relative, CLOCKWISE (see below)
// True once at least one real compass sample has been folded into
// s_planet_seek_heading_smoothed_fp this Planet seek session -- reset
// by maybe_start_shake_animation() each time the mode (re)starts, so
// the very first reading after that jumps straight to wherever the
// compass actually says instead of slowly smoothing in from 0 (or
// from a stale heading left over from last time), which would
// otherwise show up as a big, pointless swing right as the mode opens.
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
  CompassHeading clockwise = TRIG_MAX_ANGLE - data.true_heading;
  int32_t raw_deg = (int32_t)(((int64_t)clockwise * 360) / TRIG_MAX_ANGLE) % 360;

  // Adaptive smoothing: heavier (slower to react) the closer the
  // watch is to flat (face roughly horizontal -- glancing down at it
  // resting on a table, or a bent wrist) than to vertical (arm
  // raised, face roughly upright, the normal "checking the time"
  // pose), per the request. The magnetometer's own heading estimate
  // gets visibly noisier the flatter the watch sits -- small physical
  // wobble swings the reading by a much bigger number of degrees flat
  // than the same wobble would while vertical -- so a fixed smoothing
  // amount was either too twitchy flat or too sluggish vertical; this
  // adjusts on every reading instead. A single one-shot accelerometer
  // read (not a running subscription -- this doesn't need continuous
  // accel data, just "how tilted is it right now") is enough to tell
  // the two apart: the z axis (through the screen) dominates when
  // flat, x/y (across the screen) dominate when vertical. Deliberately
  // not normalizing/sqrt-ing that into a true tilt angle -- this is a
  // smoothing-strength knob, not a measurement, and the plain
  // magnitude ratio moves the same direction just as well.
  AccelData accel = { 0 };
  int32_t flatness_pct = 0; // 0 = vertical (light smoothing), 100 = flat (heavy smoothing)
  if (accel_service_peek(&accel) == 0) {
    int32_t az = accel.z < 0 ? -accel.z : accel.z;
    int32_t axy = (accel.x < 0 ? -accel.x : accel.x) + (accel.y < 0 ? -accel.y : accel.y);
    flatness_pct = (az * 100) / (az + axy + 1); // +1: avoid a div-by-zero on a (0,0,0) reading
  }
  // alpha is how much of THIS reading blends into the smoothed value,
  // as a percent -- low alpha = heavy smoothing/slow to react, high
  // alpha = light smoothing/quick to react. Interpolated between a
  // responsive ~45% vertical and a much gentler ~12% flat, per the
  // request ("not too much to be still responsive").
  int32_t alpha_pct = 45 - ((45 - 12) * flatness_pct) / 100;

  if (!s_planet_seek_heading_has_reading) {
    s_planet_seek_heading_smoothed_fp = raw_deg << 8;
    s_planet_seek_heading_has_reading = true;
  } else {
    int32_t smoothed_deg = s_planet_seek_heading_smoothed_fp >> 8;
    // Shortest signed distance from the smoothed heading to this new
    // raw one, handling the 359->0 wraparound (a naive `raw - smoothed`
    // would otherwise blend the "long way around" through 180 whenever
    // the two straddle north).
    int32_t delta = ((raw_deg - smoothed_deg + 540) % 360) - 180;
    s_planet_seek_heading_smoothed_fp += (delta * 256 * alpha_pct) / 100;
    // Keep the fixed-point value's whole-degree part wrapped into
    // 0-359 so it can't slowly drift outside a sane range over a long
    // Planet seek session, and so the delta math above keeps working
    // the same way call after call.
    while (s_planet_seek_heading_smoothed_fp < 0) s_planet_seek_heading_smoothed_fp += (360 << 8);
    while (s_planet_seek_heading_smoothed_fp >= (360 << 8)) s_planet_seek_heading_smoothed_fp -= (360 << 8);
  }

  // Calibrated = high confidence; Calibrating = a reading exists but
  // is still being refined; DataInvalid/Unavailable = no usable
  // reading at all. Anything short of Calibrated is worth flagging to
  // the wearer, per the compass guide's own "tell the user whether
  // this can be trusted" framing.
  s_planet_seek_compass_low_accuracy = (data.compass_status != CompassStatusCalibrated);
}

// Exposed for background_layer.c's rendering code -- the smoothed
// heading (see s_planet_seek_heading_smoothed_fp's own comment), not
// the raw compass sample.
int32_t planet_seek_heading_deg(void) {
  return s_planet_seek_heading_smoothed_fp >> 8;
}

bool planet_seek_compass_low_accuracy(void) {
  return s_planet_seek_compass_low_accuracy;
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

// Both exposed for features_layer.c's content-85 case.
int32_t compass_feature_heading_deg(void) { return s_compass_feature_heading_deg; }
bool compass_feature_is_asleep(void) { return !s_compass_feature_active; }

// True if any corner/edge slot actually has "Compass" (content 85)
// assigned -- checked before powering the magnetometer on a shake, so
// it doesn't run for 15s on every shake when nothing on screen would
// even show it.
static bool corner_content_in_use(uint8_t content) {
  for (int i = 0; i < 4; i++) {
    if (s_data.corner_content[i] == content) return true;
  }
  return s_data.upper_middle_line1_content == content || s_data.upper_middle_line2_content == content
      || s_data.bottom_middle_line1_content == content || s_data.bottom_middle_line2_content == content
      || s_data.middle_left_line1_content == content || s_data.middle_left_line2_content == content
      || s_data.middle_right_line1_content == content || s_data.middle_right_line2_content == content;
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
  if (s_features_layer) layer_mark_dirty(s_features_layer);
}

// Called from tap_handler() below, once per shake -- restarts the
// window fresh each time, same shape as maybe_start_shake_animation().
static void maybe_start_compass_feature(void) {
  if (!corner_content_in_use(85)) return;
  s_compass_feature_active = true;
  s_compass_feature_elapsed_ms = 0;
  if (s_compass_feature_timer) app_timer_cancel(s_compass_feature_timer);
  s_compass_feature_timer = app_timer_register(COMPASS_FEATURE_FRAME_MS, compass_feature_timer_callback, NULL);
  compass_service_subscribe(compass_feature_handler);
}

// While Planet seek (shake_anim_mode 4) is active, the countdown label
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
// than leaving a stale warning up.
static void update_planet_seek_accuracy_label(bool active) {
  if (!s_countdown_layer) return;
  if (active && planet_seek_compass_low_accuracy()) {
    snprintf(s_countdown_buf, sizeof(s_countdown_buf), "Low compass accuracy");
    s_countdown_text_color = eclipse_sky_is_bright(&s_data, time(NULL)) ? GColorBlack : GColorWhite;
    layer_set_hidden(s_countdown_layer, false);
  } else {
    // Same condition countdown_layer_update_proc/refresh_status_and_maybe_canvas
    // already use elsewhere: hidden whenever there's confirmed to be
    // no eclipse today. Planet seek only ever runs on a non-eclipse
    // day, so this always resolves to "hidden" in practice here, but
    // spelling it out the same way keeps this in sync if that ever
    // changes.
    eclipse_get_status_text(&s_data, time(NULL), s_countdown_buf, sizeof(s_countdown_buf));
    layer_set_hidden(s_countdown_layer, s_data.valid && !s_data.has_eclipse);
  }
}

static void shake_anim_timer_callback(void *data) {
  s_shake_anim_elapsed_ms += SHAKE_ANIM_FRAME_MS;
  bool still_active = s_labels_visible && s_shake_anim_elapsed_ms < s_shake_anim_duration_ms;
  if (!still_active) {
    s_shake_anim_active = false;
    s_shake_anim_timer = NULL;
    if (s_data.shake_anim_mode == 4) compass_service_unsubscribe(); // stop the magnetometer the moment planet seek's own window ends, not just on app exit
  } else {
    s_shake_gradient_shift = (uint8_t)((s_shake_gradient_shift + 1) % SHAKE_RAINBOW_LUT_SIZE);
    s_shake_anim_timer = app_timer_register(SHAKE_ANIM_FRAME_MS, shake_anim_timer_callback, NULL);
  }
  if (s_data.shake_anim_mode == 4) update_planet_seek_accuracy_label(still_active);
  if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  if (s_features_layer) layer_mark_dirty(s_features_layer);
  if (s_countdown_layer) layer_mark_dirty(s_countdown_layer);
  if (s_canvas_layer && s_data.shake_anim_mode == 4) {
    eclipse_canvas_set_planet_seek(s_canvas_layer, still_active, s_shake_anim_elapsed_ms, planet_seek_heading_deg());
  }
  if (s_canvas_layer && s_data.shake_anim_mode == 5) {
    eclipse_canvas_set_shake_paths(s_canvas_layer, still_active, s_shake_anim_elapsed_ms);
  }
}

// Called from tap_handler() below, once per shake -- restarts the
// window fresh each time (unlike the label-reveal timer next to it,
// which reschedules), so a repeated shake mid-animation also resets
// the gradient/hand-smoothing window rather than just extending it.
static void maybe_start_shake_animation(void) {
  if (s_data.shake_anim_mode == 0) return;
  if (s_data.shake_anim_mode == 4 && s_data.has_eclipse) return; // Planet seek never runs on an eclipse day, per request
  s_shake_anim_active = true;
  s_shake_anim_elapsed_ms = 0;
  s_shake_gradient_shift = 0;
  uint8_t seconds = s_data.shake_label_seconds > 0 ? s_data.shake_label_seconds : 3;
  s_shake_anim_duration_ms = (uint32_t)seconds * 1000;
  if (s_shake_anim_timer) app_timer_cancel(s_shake_anim_timer);
  s_shake_anim_timer = app_timer_register(SHAKE_ANIM_FRAME_MS, shake_anim_timer_callback, NULL);
  if (s_data.shake_anim_mode == 4) {
    // Fresh compass smoothing state each time Planet seek (re)starts
    // -- see s_planet_seek_heading_has_reading's own comment for why
    // (otherwise the first reading of a new session would slowly
    // smooth in from last session's leftover heading instead of
    // jumping straight to wherever the compass actually says now).
    s_planet_seek_heading_has_reading = false;
    compass_service_subscribe(planet_seek_compass_handler);
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
  ensure_corner_custom_font(s_data.corner_font);

  int32_t hour_angle = (int32_t)(((int64_t)((t->tm_hour % 12) * 3600 + t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600));

  int32_t min_angle = ((t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);

  int32_t sec_angle;
  if (s_shake_anim_active && s_data.show_seconds) {
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
    compute_startup_hand_anim(hour_angle, s_startup_anim_elapsed_ms, &hour_angle, &hour_length_scale_1000);
    compute_startup_hand_anim(min_angle, s_startup_anim_elapsed_ms, &min_angle, &min_length_scale_1000);
    compute_startup_hand_anim(sec_angle, s_startup_anim_elapsed_ms, &sec_angle, &sec_length_scale_1000);
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
// timezone/gradient/sleep helpers, features_draw_item(), the metadata
// cache, and the layer itself) now lives in features_layer.c/.h -- see
// that file's own top-of-file note. s_features_layer below is now
// s_features_layer, created via features_layer_create()/destroyed via
// features_layer_destroy() in apply_layout()/window_unload(), and fed
// with features_layer_set_data()/features_layer_set_labels_visible()
// instead of being recomputed on every redraw.


// ---- rendering ---------------------------------------------------------

// The bottom third of the face, digital-only now (bottom_style == 1,
// analog, has no bottom bar at all -- see apply_layout()). Obeys the
// color scheme and the seconds-visibility setting. Redrawn every
// second when seconds are shown; otherwise still cheap enough (no
// astronomy, just text drawing) not to bother throttling separately
// from the sky canvas above it.
static void bottom_canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Startup animation: substitutes an eased count-up from midnight to
  // the real time for the DISPLAYED time only -- `now`/the color
  // scheme below still use the real current time. Every read of
  // `t->tm_hour`/`tm_min`/`tm_sec` below shares this one struct tm,
  // so the digital HH:MM(:SS) text gets the count-up for free from
  // this single substitution. Uses the same shared ease-out table
  // every other "settles gracefully into place" animation in this
  // app uses (fast at first, gradually slowing into the real time)
  // -- this used to accelerate INTO the stop instead (ease-in), which
  // read as an abrupt halt right at the end.
  struct tm anim_tm;
  if (s_startup_clock_anim_active) {
    int32_t progress = ((int32_t)s_startup_anim_elapsed_ms * 1000) / STARTUP_CLOCK_ANIM_MS;
    if (progress > 1000) progress = 1000;
    int32_t eased = ease_out_lut_1000(progress);
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

  // ---- big time, small date/week below ----
  graphics_context_set_text_color(ctx, text_color);
  if (s_data.show_seconds && use_small_seconds_for_digital_clock()) {
    graphics_draw_text(ctx, time_buf, clock_font,
                        GRect(bounds.origin.x, bounds.origin.y + font_lookup_y_offset(s_data.clock_font), bounds.size.w - 20, 60),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, accent_color);
    graphics_draw_text(ctx, sec_buf, small_font,
                       GRect(bounds.origin.x + bounds.size.w - 22, bounds.origin.y + 15 + font_lookup_y_offset(s_data.clock_font_small) * 2, 20, 50),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  } else {
    graphics_draw_text(ctx, time_buf, clock_font,
                        GRect(bounds.origin.x, bounds.origin.y + font_lookup_y_offset(s_data.clock_font), bounds.size.w, 60),
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
                        GRect(bounds.origin.x, bounds.origin.y + 60 + font_lookup_y_offset(s_data.clock_font_small), left_w, 16),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    int16_t icon_x = bounds.origin.x + left_w + 6;
    int16_t icon_y = bounds.origin.y + 60 + 5;
    int16_t icon_w = draw_sun_time_icon(ctx, GPoint(icon_x, icon_y), sun_event_is_rise, text_color, bg);
    graphics_context_set_text_color(ctx, text_color);
    graphics_draw_text(ctx, sun_time_buf, small_font,
                        GRect(icon_x + icon_w + 3, bounds.origin.y + 60 + font_lookup_y_offset(s_data.clock_font_small), bounds.size.w - (icon_x + icon_w + 3), 16),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  } else {
    char week_buf[8];
    strftime(week_buf, sizeof(week_buf), "%V", t);
    char date_buf[40];
    snprintf(date_buf, sizeof(date_buf), "%s  -  Wk%s", main_buf, week_buf);
    graphics_draw_text(ctx, date_buf, small_font,
                        GRect(bounds.origin.x, bounds.origin.y + 60 + font_lookup_y_offset(s_data.clock_font_small), bounds.size.w, 16),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
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

  // In analog mode this label floats directly over the busy sky
  // view. Normally draw_text_outlined()'s 4-shifted-copy outline keeps
  // it legible against any background there, but with that setting
  // off there's nothing else backing the text, so it can disappear
  // into a similarly-colored patch of sky. Give it a solid pill
  // background in that specific case instead (contrasting_outline_color()
  // picks black or white, whichever contrasts with the text color) --
  // outline mode already handles legibility fine on its own, and
  // in digital mode the bottom bar is already a solid
  // color the text sits on, so neither of those needs this extra
  // background.
  if (!s_data.outline_enabled && s_data.bottom_style == 1 && s_countdown_buf[0] != '\0') {
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

// The watch's very first outbound request is deliberately held back a
// few seconds after the window actually goes up (see init()'s own
// comment for why) rather than fired the instant app_message_open()
// finishes -- this is that delay's own timer callback, which then
// hands off to the exact same request_update()+backoff-retry scheme
// an on-time startup would have used anyway.
#define STARTUP_REQUEST_DELAY_MS 3000
static void startup_request_delay_callback(void *data) {
  request_update();
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
  if (s_bg_anim_played || s_data.bg_anim_mode == 0) return;
  // Marker animation (bg_anim_mode 3) has no actual visual effect for
  // bitmap marker styles (Modern/Swiss/Tally/Bell/Brown -- the PNG-
  // backed marker backgrounds, big_analog_marker_style 3-7):
  // draw_marker_bitmap() in background_layer.c already draws them
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
  if (s_data.bg_anim_mode == 3 && bitmap_marker_active) {
    s_bg_anim_played = true;
    return;
  }
  s_bg_anim_played = true;
  s_bg_anim_active = true;
  s_bg_anim_elapsed_ms = 0;
  if (s_canvas_layer) eclipse_canvas_set_bg_anim(s_canvas_layer, true, 0);
  s_bg_anim_timer = app_timer_register(BG_ANIM_FRAME_MS, bg_anim_timer_callback, NULL);
}

static bool use_small_seconds_for_digital_clock() {
  return font_lookup_is_wide(s_data.clock_font);
}

// get_small_font_height_offset()/get_clock_font_height_offset() used
// to live here as hand-tuned per-clock_font-value switches -- both
// folded into font_lookup.c's shared FONT_TABLE (its y_offset column)
// now, called directly as font_lookup_y_offset(s_data.clock_font) /
// font_lookup_y_offset(s_data.clock_font_small) at each call site.

static void apply_clock_font(void) {
  clock_font = font_lookup_resolve(&s_clock_font_slot, s_data.clock_font);
  small_font = font_lookup_resolve(&s_clock_small_font_slot, s_data.clock_font_small);
  if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
}

// Battery-saving tick granularity: SECOND_UNIT only when something on
// screen actually needs live seconds -- the second hand / digital
// clock's own seconds (show_seconds), OR a corner/edge slot showing a
// seconds-precision content type (Time: second, Time: full H:M:S, and
// the tens/ones-digit variants) -- MINUTE_UNIT otherwise.
// tick_handler() itself has no per-second-specific behavior beyond
// what's gated on s_tick_unit_is_seconds below, so firing once a
// minute instead of once a second when nothing needs live seconds is
// free correctness, not a tradeoff. tick_timer_service_subscribe()
// itself replaces any existing subscription, so no explicit
// unsubscribe is needed before switching.
static bool s_tick_subscribed = false;    // has any subscription happened yet
static bool s_tick_unit_is_seconds = false; // if so, at which granularity

// Content ids that display a live seconds value -- see
// CORNER_CONTENT_OPTIONS in config-page.js for the full label list.
static const uint8_t SECOND_PRECISION_CONTENT_IDS[] = { 63, 69, 70, 71, 72 };

static bool need_second_precision(void) {
  if (s_data.show_seconds) return true;
  for (int i = 0; i < (int)(sizeof(SECOND_PRECISION_CONTENT_IDS) / sizeof(SECOND_PRECISION_CONTENT_IDS[0])); i++) {
    if (corner_content_in_use(SECOND_PRECISION_CONTENT_IDS[i])) return true;
  }
  return false;
}

static void update_tick_subscription(void) {
  bool need_seconds = need_second_precision();
  if (s_tick_subscribed && need_seconds == s_tick_unit_is_seconds) return; // already at the right granularity
  s_tick_subscribed = true;
  s_tick_unit_is_seconds = need_seconds;
  tick_timer_service_subscribe(need_seconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;

  // Purely diagnostic -- every field below is still applied via its
  // own dict_find(), which already tolerates a partial dictionary, so
  // parsing doesn't branch on this. It just labels which chunk (see
  // MsgType in eclipse_data.h) this inbox message was, for APP_LOG.
  if ((t = dict_find(iter, MESSAGE_KEY_MESSAGE_TYPE))) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "inbox: chunk type %d", (int)t->value->uint8);
  }

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
  bool clock_font_changed = false;
  if ((t = dict_find(iter, MESSAGE_KEY_CLOCK_FONT))) {
    s_data.clock_font = t->value->uint8;
    clock_font_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOCK_FONT_SMALL))) {
    s_data.clock_font_small = t->value->uint8;
    clock_font_changed = true;
  }
  if (clock_font_changed) apply_clock_font();
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
  if ((t = dict_find(iter, MESSAGE_KEY_BG_ANIM_MODE))) {
    uint8_t v = t->value->uint8;
    s_data.bg_anim_mode = (v <= 3) ? v : 0; // clamped -- used as a raw array-free switch/compare, but still worth guarding against a stray out-of-range byte
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHAKE_ANIM_MODE))) {
    uint8_t v = t->value->uint8;
    s_data.shake_anim_mode = (v <= 5) ? v : 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_OUTLINE_ENABLED))) {
    s_data.outline_enabled = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_FONT))) {
    s_data.corner_font = t->value->uint8;
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
  // Hand system -- see hand_layer.h. Every hand (however it was picked
  // on the settings page -- a preset button or the manual editor) is
  // sent as one of these full field sets; the watch itself has no
  // separate "preset" concept.
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_STYLE))) s_data.hand_hour.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_WIDTH))) s_data.hand_hour.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_LENGTH))) s_data.hand_hour.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_BACK_OFFSET))) s_data.hand_hour.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_MIDDLE_OFFSET))) s_data.hand_hour.middle_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_SECONDARY_WIDTH))) s_data.hand_hour.secondary_width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_COLOR))) s_data.hand_hour.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_OUTLINE_ENABLED))) s_data.hand_hour.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_OUTLINE_COLOR))) s_data.hand_hour.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_TRANSLUCENT))) s_data.hand_hour.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_SHADOW_ENABLED))) s_data.hand_hour.shadow_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_SHADOW_DISTANCE))) s_data.hand_hour.shadow_distance_px = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_HOLLOW))) s_data.hand_hour.hollow = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_HOLLOW_THICKNESS))) s_data.hand_hour.hollow_thickness = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_STYLE))) s_data.hand_minute.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_WIDTH))) s_data.hand_minute.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_LENGTH))) s_data.hand_minute.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_BACK_OFFSET))) s_data.hand_minute.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_MIDDLE_OFFSET))) s_data.hand_minute.middle_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_SECONDARY_WIDTH))) s_data.hand_minute.secondary_width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_COLOR))) s_data.hand_minute.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_OUTLINE_ENABLED))) s_data.hand_minute.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_OUTLINE_COLOR))) s_data.hand_minute.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_TRANSLUCENT))) s_data.hand_minute.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_SHADOW_ENABLED))) s_data.hand_minute.shadow_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_SHADOW_DISTANCE))) s_data.hand_minute.shadow_distance_px = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_HOLLOW))) s_data.hand_minute.hollow = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_HOLLOW_THICKNESS))) s_data.hand_minute.hollow_thickness = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_STYLE))) s_data.hand_second.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_WIDTH))) s_data.hand_second.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_LENGTH))) s_data.hand_second.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_BACK_OFFSET))) s_data.hand_second.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_MIDDLE_OFFSET))) s_data.hand_second.middle_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_SECONDARY_WIDTH))) s_data.hand_second.secondary_width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_COLOR))) s_data.hand_second.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_OUTLINE_ENABLED))) s_data.hand_second.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_OUTLINE_COLOR))) s_data.hand_second.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_TRANSLUCENT))) s_data.hand_second.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_SHADOW_ENABLED))) s_data.hand_second.shadow_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_SHADOW_DISTANCE))) s_data.hand_second.shadow_distance_px = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_HOLLOW))) s_data.hand_second.hollow = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_HOLLOW_THICKNESS))) s_data.hand_second.hollow_thickness = t->value->uint8;
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
    // No manual clamp needed (unlike before font resolution moved to
    // font_lookup.c) -- font_lookup_resolve()/_height()/_y_offset()
    // all defend against an out-of-range id themselves now, same as
    // clock_font/clock_font_small/corner_font already rely on below.
    s_data.marker_text.font_choice = t->value->uint8;
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
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_ERROR_CODE))) {
    s_data.weather_error_code = t->value->uint8;
    if (s_data.weather_error_code == 0) {
      s_data.weather_error_streak = 0;
      s_data.weather_ever_valid = true;
    } else if (s_data.weather_error_streak < 250) { // saturate well clear of overflow -- only ">= 10" is ever checked
      s_data.weather_error_streak++;
    }
    if (s_features_layer) layer_mark_dirty(s_features_layer);
  }
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
  if ((t = dict_find(iter, MESSAGE_KEY_AURORA_ERROR_CODE))) s_data.aurora_error_code = t->value->uint8;
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
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_AZ_SAMPLES))) {
    // Same byte-blob shape as SUN_ALT_SAMPLES, but the field itself is
    // an unsigned uint16 (azimuth is always 0-359.9deg, never
    // negative) -- see sun_az_decideg's own eclipse_data.h comment.
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.sun_az_decideg[i] = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
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
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_AZ_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.moon_az_decideg[i] = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
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
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_AZ_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int total_int16 = t->length / 2;
    int per_planet = total_int16 / PLANET_COUNT;
    if (per_planet > MAX_SKY_SAMPLES) per_planet = MAX_SKY_SAMPLES;
    for (int p = 0; p < PLANET_COUNT; p++) {
      for (int i = 0; i < per_planet; i++) {
        int src_idx = p * (total_int16 / PLANET_COUNT) + i;
        s_data.planet_az_decideg[p][i] = (uint16_t)raw[src_idx * 2] | ((uint16_t)raw[src_idx * 2 + 1] << 8);
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
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_ERROR_CODE))) s_data.iss_error_code = t->value->uint8;

  update_tick_subscription(); // re-checks whether live seconds are actually needed -- switches SECOND_UNIT/MINUTE_UNIT if this settings update changed that (show_seconds, or which content a corner/edge slot now shows)
  save_data();
  refresh_status_and_maybe_canvas(true);
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ---- tick + click ---------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh_status_and_maybe_canvas(false);
  // Piggybacks on the same SECOND_UNIT ticks show_seconds already
  // needs, rather than a separate wake source, to give seconds-
  // precision corner/edge content (Time: second, etc.) genuinely live
  // per-second updates -- see need_second_precision()'s own comment.
  // Never fires when this subscription is at MINUTE_UNIT, since
  // s_tick_unit_is_seconds is false in that case.
  if (s_tick_unit_is_seconds && s_features_layer) layer_mark_dirty(s_features_layer);
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
  // Planet seek (shake_anim_mode 4) draws its own dynamic label next
  // to each body, tracking its live compass-relative position as the
  // watch turns (see draw_planet_seek_body() in background_layer.c) --
  // turning on the regular, fixed-position shake-to-reveal labels too
  // would show both at once for the same body: one stuck at the plain
  // eclipse-time position, one actually moving with the seek
  // animation, laid right on top of each other. Skipped only for the
  // canvas's own per-body labels (eclipse_canvas_set_show_labels) --
  // the reveal window itself (s_labels_visible, the small-analog
  // panel's layout shift, the shake_anim burst) still needs to open
  // normally regardless of which mode is active.
  if (s_data.shake_anim_mode != 4) {
    eclipse_canvas_set_show_labels(s_canvas_layer, true);
  }
  s_labels_visible = true;
  if (s_features_layer) features_layer_set_labels_visible(s_features_layer, true);
  uint8_t seconds = s_data.shake_label_seconds > 0 ? s_data.shake_label_seconds : 3;
  uint32_t reveal_ms = (uint32_t)seconds * 1000;
  if (s_label_timer) {
    app_timer_reschedule(s_label_timer, reveal_ms);
  } else {
    s_label_timer = app_timer_register(reveal_ms, hide_labels_callback, NULL);
  }
  maybe_start_shake_animation();
  maybe_start_compass_feature();
}

// ---- corners overlay's own independent refresh cycle ----------------------

// Once a minute, matching update_tick_subscription()'s own baseline
// for anything that doesn't need to be genuinely live -- used to be a
// much shorter 5s cadence specifically for health data (heart rate
// especially), but Pebble's own HealthService doesn't actually refresh
// a heart-rate reading that often either, so 5s bought little real
// freshness for a real, constant battery cost. Seconds-precision
// content (Time: second, etc.) no longer depends on this timer at all
// -- see tick_handler()'s own piggyback on the SECOND_UNIT ticks
// need_second_precision() already requests when that content is
// active, which gives it genuinely live per-second updates instead of
// whatever staleness this cadence would otherwise leave it with.
#define CORNERS_REFRESH_MS 60000
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

  if (s_data.bottom_style != 1 && s_bottom_layer) {
    // 152 -- the panel's own always-unobstructed top, fixed by
    // apply_layout() -- not read from the layer's current frame,
    // since that may already be shifted up from a previous
    // obstruction change and would compound instead of staying
    // anchored to the real, original position.
    int16_t full_top = 152;
    int16_t new_top = full_top - obstruction_h;
    if (new_top < 0) new_top = 0; // clamp -- an obstruction this tall would leave no room for the panel at all otherwise
    GRect frame = layer_get_frame(s_bottom_layer);
    if (frame.origin.y != new_top) {
      frame.origin.y = new_top;
      layer_set_frame(s_bottom_layer, frame);
      layer_mark_dirty(s_bottom_layer);
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

  if (s_data.bottom_style == 1 && s_canvas_layer) {
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

  if (style == 1) {
    // Analog: sky canvas fills the whole screen; hands render in
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
    // Digital: sky canvas keeps its original top-2/3
    // proportions; bottom third is the digital time.
    s_canvas_layer = eclipse_canvas_create(GRect(0, 0, bounds.size.w, 152));
    layer_add_child(root, s_canvas_layer);
    s_bottom_layer = layer_create(GRect(0, 152, bounds.size.w, bounds.size.h - 152));
    layer_set_update_proc(s_bottom_layer, bottom_canvas_update_proc);
    layer_add_child(root, s_bottom_layer);
    apply_clock_font();
  }

  // Overlays exactly the sky canvas's own bounds -- reuses its
  // just-created frame rather than duplicating the size logic above, so
  // it always matches regardless of mode. In every mode except "analog
  // with features beneath hands" (handled above, where it was
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
  font_lookup_release(&s_clock_font_slot);
  font_lookup_release(&s_clock_small_font_slot);
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

  update_tick_subscription();
  accel_tap_service_subscribe(tap_handler);
  unobstructed_area_service_subscribe(s_unobstructed_handlers, NULL);
  s_corners_timer = app_timer_register(CORNERS_REFRESH_MS, corners_timer_callback, NULL);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  // Sized to the biggest single chunk PKJS now sends (MSG_TYPE_ASTRONOMY)
  // plus headroom, rather than the platform maximum -- see the
  // APPMSG_INBOX_SIZE/APPMSG_OUTBOX_SIZE comment in eclipse_data.h.
  // app_message_open() clamps internally if a value is still too big
  // for the platform, so this is safe even if the estimate drifts.
  app_message_open(APPMSG_INBOX_SIZE, APPMSG_OUTBOX_SIZE);

  // Ask the phone for a fresh calculation once things have had a
  // moment to settle rather than the instant the window goes up --
  // per the request, so the very first thing the watch does isn't a
  // network round-trip racing against its own first frame(s) still
  // laying out/painting. PKJS holds its own first push back a bit
  // longer too (see index.js) for the same reason on that side.
  // request_retry_callback keeps asking with backoff after that if
  // nothing valid comes back, same as before this delay existed --
  // see its own comment for why that matters.
  app_timer_register(STARTUP_REQUEST_DELAY_MS, startup_request_delay_callback, NULL);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
  compass_service_unsubscribe(); // safe even if planet seek never subscribed it this session -- a no-op in that case
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
  if (s_shake_anim_timer) {
    app_timer_cancel(s_shake_anim_timer);
    s_shake_anim_timer = NULL;
  }
  if (s_compass_feature_timer) {
    app_timer_cancel(s_compass_feature_timer);
    s_compass_feature_timer = NULL;
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
