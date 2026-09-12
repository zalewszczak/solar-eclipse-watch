#include "background_layer.h"
#include "subpixel.h"
#include "features_layer.h"
#include "marker_layer.h"
#include "weather_layer.h"

// ---------------------------------------------------------------------------
// Marker rendering lives in marker_layer.c. The background canvas owns the
// cached sky and decides WHEN markers need to be painted; marker_layer owns
// the geometry, text, bitmap resources, and marker animation itself.
// ---------------------------------------------------------------------------

// Height of the black "horizon" strip at the bottom of the canvas --
// also where altitude 0 lines up, so the sun/moon visibly sink behind
// it as they set (it's drawn last, on top, so it naturally clips
// whatever's behind it once a disc's center passes below this line).
// Defined up here (rather than down near alt_to_y, which is what
// actually uses SKY_TOP_MARGIN) because draw_clouds() above that also
// needs GROUND_H, and needs it declared before its own definition.
#define GROUND_H 18
#define SKY_TOP_MARGIN 20

// Pixel radii. SUN_R_ECLIPSE is the Sun's on-screen radius while an
// eclipse is in progress; the occluding Moon's radius is computed
// from it at draw time, scaled by the real Moon/Sun angular radius
// ratio (see canvas_update_proc), so annular and total eclipses
// actually look different instead of both using a fixed size.
// Outside an eclipse, both bodies render smaller (SUN_R_NORMAL/
// MOON_R_NORMAL) -- there's no occlusion animation to read clearly
// at that point, and the extra headroom makes it easier to keep them
// visibly apart. See the README for the wider geometry writeup.
#define SUN_R_ECLIPSE 30
#define SUN_R_NORMAL 20
#define MOON_R_NORMAL 16

// 4x4 ordered (Bayer) dither matrix, values 0-15 -- shared from subpixel.h
// now (see that header's own comment on why keeping a duplicate here
// would actually be a compile error, not just redundant). Used to
// stipple the cloud puffs at a density proportional to cloud cover %,
// rather than drawing them as flat filled shapes -- keeps them looking
// like e-paper "clouds" rather than solid gray blobs, and lets the sky
// and sun show through underneath.

typedef struct {
  EclipseData *data;
  bool show_labels; // shake-to-reveal Sun/Moon/planet name labels
  time_t last_full_draw;  // when the expensive sky was last actually redrawn
  bool force_next_draw;    // set by set_data()/set_show_labels(), always honored
  int last_eclipse_phase;   // see compute_eclipse_phase(): forces an immediate redraw
                              // the moment this changes, rather than waiting for the
                              // normal once-a-minute cadence to happen to catch up
  time_t last_eclipse_max;   // d->max_t last seen -- lets the "just passed greatest eclipse"
                               // vibration below tell "still the same eclipse, already
                               // handled" apart from "a genuinely new eclipse's max time
                               // just arrived", across the repeated set_data() calls a
                               // normal refresh cycle causes throughout the same eclipse day
  bool max_vibrated;          // fired the "at maximum eclipse" vibration yet for last_eclipse_max?
  bool last_iss_visible;     // same idea, for the ISS appearing/disappearing
  time_t storm_flash_end;    // 0 = no lightning flash in progress; otherwise the time
                               // (see draw_clouds_realistic's storm-flash comment) the
                               // current flash finishes and the sky reverts to normal
  bool storm_flash_was_active; // storm_flash_end > now as of the last tick -- lets the
                                 // once-a-second throttle check (which runs before the
                                 // expensive full redraw below even happens) notice the
                                 // instant a flash starts or ends and force a redraw for
                                 // just that transition, without abandoning the normal
                                 // once-a-minute cadence the rest of the time
  bool bg_anim_active;       // "animate background on start" -- see eclipse_canvas_set_bg_anim()
  uint16_t bg_anim_elapsed_ms; // and canvas_update_proc's own use of both these fields
  bool planet_seek_active;      // "Planet seek" (shake_anim_mode 2 or 3) -- see
  uint16_t planet_seek_elapsed_ms; // eclipse_canvas_set_planet_seek() and canvas_update_proc's
  int32_t planet_seek_heading_deg; // own use of these three fields
  GBitmap *sky_cache;       // last full render, captured via graphics_capture_frame_buffer;
                             // blitted back on the seconds in between instead of leaving
                             // the screen untouched (which is what caused flicker -- Pebble
                             // doesn't guarantee framebuffer content persists between
                             // update_proc invocations, so "just don't draw" isn't safe)

  // Planet seek's own cached body positions, in NORMAL (non-rotated)
  // screen space -- the Sun/Moon/planet paint calls below skip
  // actually painting a disc whenever planet_seek_active is true (see
  // each one's own "skip_body_paint" check), so sky_cache above never
  // gets bodies baked into it at their old positions during Planet
  // seek -- avoiding a "ghost" of the un-rotated position bleeding
  // through once the real (repositioned) body gets painted over the
  // cache each frame. These fields are what a Planet-seek-only cache-
  // blit frame (no full draw at all that tick) reads as the "normal"
  // endpoint for its own position blend, since a cache-blit frame
  // doesn't run the normal position math that would otherwise
  // recompute them -- see canvas_update_proc's own Planet-seek
  // section for how they're actually used.
  GPoint cached_sun_center, cached_moon_center, cached_planet_center[PLANET_COUNT];
  bool cached_sun_up, cached_moon_visible, cached_planet_visible[PLANET_COUNT];
  GColor cached_sun_fill_color; // the Sun's own altitude-dependent color (see sun_color_for_altitude()) -- cached alongside its position for the same reason: a "Planets" bg-anim overlay frame needs it and has no other way to re-derive it without redoing the whole altitude/sky_now computation above
  int16_t cached_sun_r, cached_moon_r; // the REAL (sun_moon_size_pct-scaled) radii, not SUN_R_NORMAL/MOON_R_NORMAL directly -- same caching reason as everything else here
  GPoint cached_iss_center;
  bool cached_iss_visible; // same reasoning as cached_sun_up/cached_moon_visible above -- ISS's own visibility test (show_iss + dark sky + altitude + freshness) lives entirely in the normal draw path, so Planet seek's own separate overlay pass needs it cached rather than re-deriving it

  MarkerLayerState markers;
} CanvasState;

// ---- hour/second markers (sub-pixel & rotation fix) --------------------

// Symmetric integer division with rounding to nearest integer for sub-pixel precision
static inline int32_t div_round(int32_t num, int32_t den) {
  if (den == 0) return 0;
  if ((num ^ den) >= 0) {
    return (num + den / 2) / den;
  } else {
    return (num - den / 2) / den;
  }
}

// ---- generic sample interpolation -----------------------------------------
//
// D: the nine functions below (interp_separation_centideg, interp_mag_pct,
// interp_sun_alt_decideg, interp_moon_alt_decideg, interp_planet_alt_decideg,
// interp_sun_az_decideg, interp_moon_az_decideg, interp_planet_az_decideg,
// interp_cloud_pct) used to each carry their own copy of the same ~20-line
// "find which two samples bracket time t, blend between them" body, differing
// only in which array, element type, count/interval field pair, and
// "no data" fallback each one used. grid_bracket() below is that shared body,
// factored out once; interp_grid()/interp_grid_az() are the two ways its
// result gets turned into a value (plain linear blend, or azimuth's own
// shortest-way-around wraparound blend -- see interp_az_wrapped() below,
// unchanged); everything past that is a ~2-line typed wrapper per field.

typedef enum { SAMPLE_U8, SAMPLE_U16, SAMPLE_I16 } SampleKind;

static int32_t read_sample(const void *samples, SampleKind kind, int idx) {
  switch (kind) {
    case SAMPLE_U8:  return ((const uint8_t *)samples)[idx];
    case SAMPLE_U16: return ((const uint16_t *)samples)[idx];
    case SAMPLE_I16:  return ((const int16_t *)samples)[idx];
  }
  return 0;
}

// Locates time `t` within a uniform sample grid of `count` points spaced
// `interval_s` apart starting at `start`. Returns false (and sets *idx to
// the single sample the caller should just read directly, no blend needed)
// when `t` falls at or before the first sample, at or after the last, or
// interval_s is 0 -- exactly the three "clamp to an edge" cases every one
// of the nine interpolators used to special-case individually. Returns
// true (and sets *idx/*frac_num/*frac_den) when a genuine two-point blend
// between samples[*idx] and samples[*idx + 1] is needed.
static bool grid_bracket(int count, time_t start, int32_t interval_s, time_t t,
                          int *idx, int32_t *frac_num, int32_t *frac_den) {
  if (interval_s == 0) { *idx = 0; return false; }

  int32_t offset_s = (int32_t)(t - start); // both time_t on the same day; int32 is exact (see E)
  int32_t idx_f = offset_s / interval_s;

  if (idx_f <= 0) { *idx = 0; return false; }
  if (idx_f >= count - 1) { *idx = count - 1; return false; }

  *idx = (int)idx_f;
  time_t t0 = start + (*idx) * (time_t)interval_s;
  *frac_num = (int32_t)(t - t0);
  *frac_den = interval_s;
  if (*frac_den <= 0) *frac_den = 1;
  return true;
}

// Plain linear blend, for every grid above except the two azimuth ones.
static int32_t interp_grid(const void *samples, SampleKind kind, int count,
                            time_t start, uint32_t interval_s, time_t t, int32_t fallback) {
  if (count == 0) return fallback;

  int idx; int32_t frac_num = 0, frac_den = 1;
  bool blend = grid_bracket(count, start, (int32_t)interval_s, t, &idx, &frac_num, &frac_den);
  int32_t a = read_sample(samples, kind, idx);
  if (!blend) return a;
  int32_t b = read_sample(samples, kind, idx + 1);
  return a + ((b - a) * frac_num) / frac_den;
}

// Azimuth blend for the *_az_decideg interpolators below -- unlike
// altitude, azimuth wraps at 0/3600 (360.0deg), so a plain linear
// blend between e.g. 3590 and 10 would sweep the WRONG, long way
// around (350deg backwards through 180) instead of the short 20deg
// hop through 0/360 -- this takes whichever of the two directions is
// actually shorter before blending, then normalizes the result back
// into 0-3599.
static uint16_t interp_az_wrapped(uint16_t a, uint16_t b, int32_t frac_num, int32_t frac_den) {
  int32_t diff = (int32_t)b - (int32_t)a;
  if (diff > 1800) diff -= 3600;
  if (diff < -1800) diff += 3600;
  int32_t result = (int32_t)a + (diff * frac_num) / frac_den;
  result = result % 3600;
  if (result < 0) result += 3600;
  return (uint16_t)result;
}

static uint16_t interp_grid_az(const uint16_t *samples, int count,
                                time_t start, uint32_t interval_s, time_t t, uint16_t fallback) {
  if (count == 0) return fallback;

  int idx; int32_t frac_num = 0, frac_den = 1;
  bool blend = grid_bracket(count, start, (int32_t)interval_s, t, &idx, &frac_num, &frac_den);
  if (!blend) return samples[idx];
  return interp_az_wrapped(samples[idx], samples[idx + 1], frac_num, frac_den);
}

// Linearly interpolate the transmitted separation-sample array to get
// the sun/moon angular gap (in hundredths of a degree) at time `t`.
// Samples run from data->sample_start in steps of sample_interval_s.
static uint16_t interp_separation_centideg(const EclipseData *d, time_t t) {
  return (uint16_t)interp_grid(d->sep_samples_centideg, SAMPLE_U16, d->sample_count,
                                d->sample_start, d->sample_interval_s, t, 0);
}

// Same grid, same interpolation, for the live "% of Sun covered"
// samples -- lets the countdown line show a running percentage
// rather than only the fixed peak magnitude.
static uint8_t interp_mag_pct(const EclipseData *d, time_t t) {
  return (uint8_t)interp_grid(d->mag_pct_samples, SAMPLE_U8, d->sample_count,
                               d->sample_start, d->sample_interval_s, t, 0);
}

// Same idea, for the full-day sun-altitude samples (tenths of a
// degree, signed -- negative once the sun is below the horizon).
static int16_t interp_sun_alt_decideg(const EclipseData *d, time_t t) {
  return (int16_t)interp_grid(d->sun_alt_decideg, SAMPLE_I16, d->sky_sample_count,
                               d->sky_sample_start, d->sky_sample_interval_s, t, -900); // "no data" = deep night
}

// Same idea again, for the full-day moon-altitude samples (same grid
// as the sun, used for the night moon's own rise/set animation).
static int16_t interp_moon_alt_decideg(const EclipseData *d, time_t t) {
  return (int16_t)interp_grid(d->moon_alt_decideg, SAMPLE_I16, d->sky_sample_count,
                               d->sky_sample_start, d->sky_sample_interval_s, t, -900);
}

// Same idea, generic over any planet slot (see PlanetId) rather than
// one duplicated function per planet.
static int16_t interp_planet_alt_decideg(const EclipseData *d, PlanetId planet, time_t t) {
  return (int16_t)interp_grid(d->planet_alt_decideg[planet], SAMPLE_I16, d->sky_sample_count,
                               d->sky_sample_start, d->sky_sample_interval_s, t, -900);
}

// Same grid/lookup shape as interp_sun_alt_decideg() above, but for
// azimuth (see sun_az_decideg's own eclipse_data.h comment) -- "Planet
// seek"'s own real compass-relative bearing for the Sun.
static uint16_t interp_sun_az_decideg(const EclipseData *d, time_t t) {
  return interp_grid_az(d->sun_az_decideg, d->sky_sample_count,
                         d->sky_sample_start, d->sky_sample_interval_s, t, 0);
}

static uint16_t interp_moon_az_decideg(const EclipseData *d, time_t t) {
  return interp_grid_az(d->moon_az_decideg, d->sky_sample_count,
                         d->sky_sample_start, d->sky_sample_interval_s, t, 0);
}

static uint16_t interp_planet_az_decideg(const EclipseData *d, PlanetId planet, time_t t) {
  return interp_grid_az(d->planet_az_decideg[planet], d->sky_sample_count,
                         d->sky_sample_start, d->sky_sample_interval_s, t, 0);
}

// How long before/after the actual rise or set moment to animate the
// body sinking behind (or rising out of) the horizon strip. Time-based
// rather than derived from the altitude-to-pixel scale above: that
// scale is deliberately compressed (a whole day's arc has to fit in
// ~120px), which makes a fixed-radius disc correspond to a much
// bigger apparent angular size than reality -- so clipping it purely
// by "disc edge crosses the horizon line in pixel-space" made it
// start disappearing tens of degrees too early. A short, fixed
// real-time window sidesteps that mismatch entirely.
#define RISE_SET_TRANSITION_S 180

// Mirrors the actual on-screen gating (canvas_update_proc's sky_is_dark
// check, plus body_screen_y()'s own rise/set window below) exactly --
// a planet being geometrically above the horizon isn't enough on its
// own; several are routinely "up" in raw altitude terms in broad
// daylight (that's just where their orbit puts them), completely
// washed out and invisible until the sky is actually dark. Previously
// this only checked raw altitude, so it could report several
// "visible" planets in full daylight with nothing actually on screen,
// and disagree with what's drawn at night too (that also depends on
// each planet's own today's rise/set window, which raw altitude alone
// doesn't capture -- interpolated samples can dip positive outside it,
// or negative just inside it, especially right around rise/set).
uint8_t background_count_visible_planets(const EclipseData *d, time_t now) {
  if (interp_sun_alt_decideg(d, now) > -60) return 0; // sky not dark enough for any planet to read

  uint8_t count = 0;
  for (int p = 0; p < PLANET_COUNT; p++) {
    time_t rise = d->planet_rise[p];
    time_t set = d->planet_set[p];
    bool up;
    if (rise != 0 && set != 0) {
      up = now >= rise - RISE_SET_TRANSITION_S && now <= set + RISE_SET_TRANSITION_S;
    } else {
      // Rare fallback (e.g. rises today but doesn't set until
      // tomorrow) -- same fallback body_screen_y() itself uses.
      up = interp_planet_alt_decideg(d, (PlanetId)p, now) > 0;
    }
    if (up) count++;
  }
  return count;
}

// Same idea again, for the full-day cloud-cover samples (0-100 %).
static uint8_t interp_cloud_pct(const EclipseData *d, time_t t) {
  return (uint8_t)interp_grid(d->cloud_pct_samples, SAMPLE_U8, d->sky_sample_count,
                               d->sky_sample_start, d->sky_sample_interval_s, t, 0);
}

// ---- moon position ----------------------------------------------------

// Where the moon's disc should be drawn relative to the sun's, in
// pixels, for the current time. The moon travels a straight line
// through pos_angle_deg (approach direction) and its opposite
// (recede direction), scaled so the two discs are exactly
// edge-to-edge at the first transmitted sample (which PKJS aligns
// with C1 / C4). Takes the actual on-screen radii so the touching
// point lines up correctly whether the Moon is drawn smaller
// (annular) or full-size (total) relative to the Sun.
static GPoint moon_offset_px(const EclipseData *d, time_t now, int16_t sun_r, int16_t moon_r) {
  if (d->sample_count == 0) return GPoint(10000, 10000); // park off-screen

  uint16_t sep_now = interp_separation_centideg(d, now);
  uint16_t sep_ref = d->sep_samples_centideg[0]; // ~= sun radius + moon radius
  if (sep_ref == 0) sep_ref = 1;

  int32_t max_offset_px = sun_r + moon_r;
  int32_t offset_px = ((int32_t)sep_now * max_offset_px) / sep_ref;
  if (offset_px > max_offset_px) offset_px = max_offset_px;
  if (offset_px < 0) offset_px = 0;

  int32_t dir_deg = d->pos_angle_deg;
  if (now >= d->max_t) {
    dir_deg = (dir_deg + 180) % 360;
  }

  int32_t angle = (dir_deg * TRIG_MAX_ANGLE) / 360;
  int32_t dx = (offset_px * sin_lookup(angle)) / TRIG_MAX_RATIO;
  int32_t dy = -(offset_px * cos_lookup(angle)) / TRIG_MAX_RATIO;

  return GPoint(dx, dy);
}

// ---- sky colour -------------------------------------------------------

// Piecewise-linear colour ramp keyed on sun altitude. Two colours per
// anchor: one for the top of the sky (zenith-ish), one for the
// horizon glow (concentrated near the bottom of the canvas) -- lets
// sunrise/sunset render as a warm band low down under a still-blue
// (or already-dark) upper sky, the way it actually looks.
typedef struct {
  int16_t alt_decideg;
  uint8_t top_r, top_g, top_b;
  uint8_t hz_r, hz_g, hz_b;
} SkyAnchor;

static const SkyAnchor SKY_ANCHORS[] = {
  { 200,  40, 110, 200,   120, 180, 230 }, // high day sun
  {  60,  50, 120, 205,   160, 190, 210 }, // low day sun
  {   0,  55,  95, 155,   240, 150,  90 }, // sunrise/sunset, sun on horizon
  { -60,  40,  40,  90,   200,  90,  60 }, // civil twilight
  {-120,  15,  15,  45,    70,  40,  70 }, // nautical twilight
  {-180,   5,   5,  15,    10,  10,  25 }, // astronomical twilight / night
};
#define SKY_ANCHOR_COUNT (int)(sizeof(SKY_ANCHORS) / sizeof(SKY_ANCHORS[0]))

typedef struct { uint8_t r, g, b; } RGB8;

static uint8_t lerp8(uint8_t a, uint8_t b, int32_t num, int32_t den) {
  if (den == 0) return a;
  return (uint8_t)(a + ((int32_t)(b - a) * num) / den);
}

// Decelerates into the target -- "animate background on start"'s own
// easing (graceful, slowing down at the end), separate from
// pebble-eclipse-watch.c's identically-behaved ease_out_cubic_1000
// (used for its own, unrelated clock-hand animation) since these are
// two different translation units and this is a small enough helper
// that duplicating it is simpler than threading a shared declaration
// through a header for one function -- same reasoning subpixel.h's
// own top-of-file comment already gives for this project's small
// self-contained helpers generally.
#define BG_ANIM_MS 1400 // must match pebble-eclipse-watch.c's own BG_ANIM_MS -- same duplication reasoning as above
static int32_t bg_anim_ease_out_1000(int32_t t) {
  int32_t inv = 1000 - t;
  int64_t inv3 = ((int64_t)inv * inv * inv) / 1000000;
  int32_t r = 1000 - (int32_t)inv3;
  return (r > 1000) ? 1000 : r;
}

// Starts slow, accelerates toward the end -- the text markers' own
// "count up from 0" effect (see draw_text_markers), matching
// pebble-eclipse-watch.c's identically-behaved ease_in_cubic_1000
// used for its own, separate digital-clock count-up. Positions still
// use the decelerating curve above (bg_anim_ease_out_1000) -- only
// the counted-up number itself speeds up toward its real value, the
// same "odometer" feel the clock's own count-up already has.
static int32_t bg_anim_ease_in_1000(int32_t t) {
  int64_t t64 = t;
  int32_t r = (int32_t)((t64 * t64 * t64) / 1000000);
  return (r > 1000) ? 1000 : r;
}

// The Sun's own disc color, white near the zenith and shifting through
// yellow/orange to a deep red right at the horizon -- real sunlight
// reddens as it travels through more atmosphere at low altitude
// (Rayleigh scattering strips out blue/green wavelengths first, the
// same physical effect SKY_ANCHORS above already models for the sky
// itself). Same anchor-lerp technique as sky_colors_for_altitude()
// below, just a much shorter table covering only the Sun's own
// visible range (alt_decideg > 0, thanks to sun_up's own gating at
// the call site) rather than the whole day/night cycle.
typedef struct {
  int16_t alt_decideg;
  uint8_t r, g, b;
} SunColorAnchor;

static const SunColorAnchor SUN_COLOR_ANCHORS[] = {
  { 300, 255, 255, 245 }, // high in the sky: near-white, faint warm tint
  { 100, 255, 220, 140 }, // mid altitude: warm yellow
  {  30, 255, 150,  60 }, // getting low: orange
  {   0, 220,  60,  30 }, // right on the horizon: deep red
};
#define SUN_COLOR_ANCHOR_COUNT (int)(sizeof(SUN_COLOR_ANCHORS) / sizeof(SUN_COLOR_ANCHORS[0]))

// The Sun's own color wherever it's shown in a "space view" instead
// of the normal sky -- sun_color_for_altitude() above models how
// Earth's atmosphere reddens sunlight near the horizon, which doesn't
// mean anything in a view that isn't really representing an
// atmospheric vantage point at a specific moment in the first place:
// an eclipse's fullscreen Sun already ignores the real altitude
// entirely for its own positioning (see fullscreen_sun's own comment
// in canvas_update_proc), Planet seek repositions bodies by compass
// heading rather than altitude, and the "Planets" startup animation
// fast-forwards through several hours of real sky in under 2 seconds.
// A flat, recognizably-sun yellow-orange reads better in all three
// than a color shift whose real-world meaning doesn't apply.
#define SUN_COLOR_SPACE_R 255
#define SUN_COLOR_SPACE_G 190
#define SUN_COLOR_SPACE_B 60

static RGB8 sun_color_for_altitude(int16_t alt_decideg) {
  RGB8 out;
  if (alt_decideg >= SUN_COLOR_ANCHORS[0].alt_decideg) {
    out.r = SUN_COLOR_ANCHORS[0].r; out.g = SUN_COLOR_ANCHORS[0].g; out.b = SUN_COLOR_ANCHORS[0].b;
    return out;
  }
  const SunColorAnchor *last = &SUN_COLOR_ANCHORS[SUN_COLOR_ANCHOR_COUNT - 1];
  if (alt_decideg <= last->alt_decideg) {
    out.r = last->r; out.g = last->g; out.b = last->b;
    return out;
  }
  for (int i = 0; i < SUN_COLOR_ANCHOR_COUNT - 1; i++) {
    const SunColorAnchor *hi = &SUN_COLOR_ANCHORS[i];
    const SunColorAnchor *lo = &SUN_COLOR_ANCHORS[i + 1];
    if (alt_decideg <= hi->alt_decideg && alt_decideg >= lo->alt_decideg) {
      int32_t num = hi->alt_decideg - alt_decideg;
      int32_t den = hi->alt_decideg - lo->alt_decideg;
      out.r = lerp8(hi->r, lo->r, num, den);
      out.g = lerp8(hi->g, lo->g, num, den);
      out.b = lerp8(hi->b, lo->b, num, den);
      return out;
    }
  }
  out.r = last->r; out.g = last->g; out.b = last->b; // shouldn't reach here given the bracketing above
  return out;
}

static void sky_colors_for_altitude(int16_t alt_decideg, RGB8 *top_out, RGB8 *hz_out) {
  if (alt_decideg >= SKY_ANCHORS[0].alt_decideg) {
    top_out->r = SKY_ANCHORS[0].top_r; top_out->g = SKY_ANCHORS[0].top_g; top_out->b = SKY_ANCHORS[0].top_b;
    hz_out->r = SKY_ANCHORS[0].hz_r; hz_out->g = SKY_ANCHORS[0].hz_g; hz_out->b = SKY_ANCHORS[0].hz_b;
    return;
  }
  const SkyAnchor *last = &SKY_ANCHORS[SKY_ANCHOR_COUNT - 1];
  if (alt_decideg <= last->alt_decideg) {
    top_out->r = last->top_r; top_out->g = last->top_g; top_out->b = last->top_b;
    hz_out->r = last->hz_r; hz_out->g = last->hz_g; hz_out->b = last->hz_b;
    return;
  }
  for (int i = 0; i < SKY_ANCHOR_COUNT - 1; i++) {
    const SkyAnchor *hi = &SKY_ANCHORS[i];
    const SkyAnchor *lo = &SKY_ANCHORS[i + 1];
    if (alt_decideg <= hi->alt_decideg && alt_decideg >= lo->alt_decideg) {
      int32_t num = hi->alt_decideg - alt_decideg;
      int32_t den = hi->alt_decideg - lo->alt_decideg;
      top_out->r = lerp8(hi->top_r, lo->top_r, num, den);
      top_out->g = lerp8(hi->top_g, lo->top_g, num, den);
      top_out->b = lerp8(hi->top_b, lo->top_b, num, den);
      hz_out->r = lerp8(hi->hz_r, lo->hz_r, num, den);
      hz_out->g = lerp8(hi->hz_g, lo->hz_g, num, den);
      hz_out->b = lerp8(hi->hz_b, lo->hz_b, num, den);
      return;
    }
  }
  // Shouldn't reach here given the bracketing above; fall back to night.
  top_out->r = last->top_r; top_out->g = last->top_g; top_out->b = last->top_b;
  hz_out->r = last->hz_r; hz_out->g = last->hz_g; hz_out->b = last->hz_b;
}

// Where the cloud deck sits vertically -- `cloud_altitude_pct` (0=low
// cloud, 100=high cloud, from Open-Meteo's low/mid/high breakdown,
// see weather.js) biases where in the lower half of the canvas it
// sits. Shared by the sky gradient (which needs to know where its
// "beneath the deck" gray zone starts) and the cloud puffs themselves
// (which need to know where to sit), so both agree on the same line.
static int16_t compute_cloud_band_y(GRect bounds, uint8_t cloud_altitude_pct) {
  int16_t half_h = bounds.size.h / 2;
  int16_t lower_top = bounds.origin.y + half_h;
  int16_t lower_bottom = bounds.origin.y + bounds.size.h - GROUND_H - 10;
  if (lower_bottom < lower_top) lower_bottom = lower_top;
  return lower_bottom - (((int32_t)(lower_bottom - lower_top) * cloud_altitude_pct) / 100);
}

// Digital top layout only: same "where does the cloud deck's graying
// kick in" math as compute_cloud_band_y() above, just computed against
// a conceptual gradient span (virtual_top_y/virtual_total_y) taller
// than any one physical layer -- see fill_sky_gradient_ex()'s own
// comment for why that split exists at all. Returns a value in that
// SAME virtual coordinate space (0 = the conceptual gradient's own
// top), for fill_sky_gradient_ex()'s band_y param, not a screen y.
static int16_t compute_cloud_band_y_virtual(int16_t virtual_top_y, int16_t virtual_total_h, uint8_t cloud_altitude_pct) {
  int16_t half_h = virtual_total_h / 2;
  int16_t lower_top = virtual_top_y + half_h;
  int16_t lower_bottom = virtual_top_y + virtual_total_h - GROUND_H - 10;
  if (lower_bottom < lower_top) lower_bottom = lower_top;
  return lower_bottom - (((int32_t)(lower_bottom - lower_top) * cloud_altitude_pct) / 100);
}

// Fills `bounds` with a dithered vertical gradient from `top` down to
// `hz`, optionally kinking through a third `band` color at `band_y`
// along the way -- this is how overcast/rainy conditions show up as a
// grayer lower sky, like the view crossing beneath a cloud deck seen
// from a plane window, rather than the whole sky stretching through
// unbroken blue regardless of weather. Pass band_y at or past the
// bottom row (and band == hz) to skip the effect entirely and get a
// plain two-point gradient.
//
// virtual_top_y/virtual_total_h decouple "where do row 0 and the last
// row sit for the top-to-band-to-hz color math" (a CONCEPTUAL gradient
// span, in the same coordinate space band_y is given in) from `bounds`
// itself (the REAL pixels actually painted) -- ordinarily callers just
// pass bounds.origin.y/bounds.size.h straight through, making the two
// spans identical (a plain single-bounds gradient, same as before this
// split existed), but Digital top's own gradient-only strip needs to
// paint just its own DIGITAL_PANEL_H-tall slice of a conceptual
// gradient that's actually 228px tall overall (matching the full
// screen), so the colors it shows line up seamlessly with where the
// sky canvas's OWN (separately painted, unmodified-since-Digital-bar)
// gradient wash picks up right where this strip leaves off, rather
// than each independently stretching the same top/band/hz colors
// across its own much-shorter span and visibly disagreeing at the
// seam. Each row's true continuous colour is computed first, then
// every pixel in that row is ordered-dithered down to the palette
// individually -- that's what turns hard colour bands into a smooth-
// looking blend on real hardware. Row colours only depend on y, so the
// per-row RGB lerp happens once; only the 4 possible x-phases of the
// Bayer matrix are then dithered and cached before sweeping across the
// row, to avoid redoing that work per pixel.
static void fill_sky_gradient_ex(GContext *ctx, GRect bounds, int16_t virtual_top_y, int16_t virtual_total_h,
                                  RGB8 top, RGB8 band, int16_t band_y, RGB8 hz) {
  int16_t virtual_bottom_y = virtual_top_y + virtual_total_h - 1;
  if (band_y > virtual_bottom_y) band_y = virtual_bottom_y;
  if (band_y < virtual_top_y) band_y = virtual_top_y;
  int16_t upper_span = band_y - virtual_top_y;
  int16_t lower_span = virtual_bottom_y - band_y;

  for (int16_t y = 0; y < bounds.size.h; y++) {
    int16_t virtual_y = virtual_top_y + y;
    RGB8 row;
    if (virtual_y <= band_y) {
      row.r = lerp8(top.r, band.r, y, upper_span > 0 ? upper_span : 1);
      row.g = lerp8(top.g, band.g, y, upper_span > 0 ? upper_span : 1);
      row.b = lerp8(top.b, band.b, y, upper_span > 0 ? upper_span : 1);
    } else {
      int16_t rel = virtual_y - band_y;
      row.r = lerp8(band.r, hz.r, rel, lower_span > 0 ? lower_span : 1);
      row.g = lerp8(band.g, hz.g, rel, lower_span > 0 ? lower_span : 1);
      row.b = lerp8(band.b, hz.b, rel, lower_span > 0 ? lower_span : 1);
    }

    GColor phase_colors[4];
    for (int p = 0; p < 4; p++) {
      phase_colors[p] = dither_pixel(row, BAYER4[y & 3][p]);
    }

    GColor last_color = phase_colors[0];
    int16_t run_start = 0;
    graphics_context_set_fill_color(ctx, last_color);
    for (int16_t x = 0; x < bounds.size.w; x++) {
      GColor c = phase_colors[x & 3];
      if (c.argb != last_color.argb) {
        graphics_fill_rect(ctx, GRect(bounds.origin.x + run_start, bounds.origin.y + y, x - run_start, 1), 0, GCornerNone);
        last_color = c;
        graphics_context_set_fill_color(ctx, last_color);
        run_start = x;
      }
    }
    graphics_fill_rect(ctx, GRect(bounds.origin.x + run_start, bounds.origin.y + y, bounds.size.w - run_start, 1), 0, GCornerNone);
  }
}

// Ordinarily (Analog, Digital bar) the conceptual gradient span IS
// `bounds` itself -- callers just pass bounds.origin.y/bounds.size.h
// straight through for virtual_top_y/virtual_total_h, reproducing the
// plain single-bounds formula this function replaced exactly. Digital
// top's own gradient calls (both the sky canvas's own, and
// eclipse_top_gradient_*()'s) are the only ones that pass something
// taller -- see canvas_update_proc()'s own local virtual_top_y/
// virtual_total_h for why.

// Digital top layout only: computes the same "what should the plain
// sky wash look like right now" colors canvas_update_proc()'s own
// gradient block below works out inline (sun altitude -> top/horizon
// colors, then weather-driven graying toward a band/horizon color and
// where that graying band sits) -- factored out here so
// eclipse_top_gradient_*()'s own thin gradient-only strip can call the
// exact same logic instead of a second, hand-duplicated copy that
// could quietly drift out of sync with the sky canvas's own version
// over time. Returns via out_flat_black instead of drawing anything
// itself for sky_mode 2 (Space) -- that mode has no gradient at all
// (flat near-black, handled by canvas_update_proc()'s own separate
// branch) -- callers fill flat black themselves rather than this
// function reaching for a GContext it doesn't otherwise need.
//
// Deliberately uses the REAL current sun altitude (interp_sun_alt_
// decideg(d, now), `now` being whatever the caller passes) rather than
// chasing canvas_update_proc()'s own sky_now animated-sweep
// substitution (see that function's own local `sky_now` and the
// "Planets" background-animation comment above it) -- reproducing that
// whole eased-sweep state machine here, just to keep a 76px sliver
// with no sun/moon/stars in it in perfect lockstep during a well-
// under-2-second startup animation, isn't worth either the code size
// or coupling this function to CanvasState (which it otherwise has no
// need to know about at all). The two can very briefly disagree during
// that animation; they're back in exact agreement, same as any other
// moment, the instant it finishes.
static void compute_sky_wash(const EclipseData *d, time_t now, int16_t virtual_top_y, int16_t virtual_total_h,
                              RGB8 *out_top, RGB8 *out_band, int16_t *out_band_y, RGB8 *out_hz, bool *out_flat_black) {
  *out_flat_black = (d->sky_mode == 2);
  if (*out_flat_black) return;

  int16_t alt = interp_sun_alt_decideg(d, now);
  RGB8 sky_top_rgb, sky_hz_rgb;
  sky_colors_for_altitude(alt, &sky_top_rgb, &sky_hz_rgb);

  uint8_t cloud_pct = interp_cloud_pct(d, now);
  bool stormy = d->weather_condition == 4;
  bool weather_enabled = d->sky_mode == 0; // Clear sky (1) skips the haze entirely, same as canvas_update_proc()'s own gating
  uint8_t gray_amount = 0;
  if (weather_enabled) {
    if (cloud_pct > 35) {
      int32_t g = ((int32_t)(cloud_pct - 35) * 100) / 65;
      gray_amount = (uint8_t)(g > 100 ? 100 : g);
    }
    if (stormy) {
      if (gray_amount < 85) gray_amount = 85;
    } else if (d->weather_condition == 2 || d->weather_condition == 3) {
      if (gray_amount < 55) gray_amount = 55;
    }
  }

  RGB8 band_rgb = sky_hz_rgb;
  RGB8 hz_rgb = sky_hz_rgb; // what actually reaches fill_sky_gradient_ex's horizon row
  int16_t band_y = virtual_top_y + virtual_total_h; // off-canvas by default -- no visible band
  if (gray_amount > 0) {
    RGB8 neutral_gray = { 115, 117, 120 };
    RGB8 dark_gray = { 40, 41, 46 };
    band_rgb.r = lerp8(sky_hz_rgb.r, neutral_gray.r, gray_amount, 100);
    band_rgb.g = lerp8(sky_hz_rgb.g, neutral_gray.g, gray_amount, 100);
    band_rgb.b = lerp8(sky_hz_rgb.b, neutral_gray.b, gray_amount, 100);
    hz_rgb.r = lerp8(sky_hz_rgb.r, dark_gray.r, gray_amount, 100);
    hz_rgb.g = lerp8(sky_hz_rgb.g, dark_gray.g, gray_amount, 100);
    hz_rgb.b = lerp8(sky_hz_rgb.b, dark_gray.b, gray_amount, 100);
    band_y = compute_cloud_band_y_virtual(virtual_top_y, virtual_total_h, d->cloud_altitude_pct);
  }

  *out_top = sky_top_rgb; *out_band = band_rgb; *out_band_y = band_y; *out_hz = hz_rgb;
}

// ---- rise/set vertical position -----------------------------------------

// Hard ceiling: no object's *center* renders above this row, no
// matter what the altitude-to-scale math computes. This is what
// actually protects the countdown label area at the top of the
// canvas -- SKY_TOP_MARGIN above shapes the normal scale so bodies
// approach this line smoothly, but doesn't by itself guarantee nothing
// ever exceeds it (a day's true peak altitude landing between two
// hourly samples, for instance, could otherwise nudge a fast-moving
// body slightly past the top of its expected range).
#define SKY_MIN_Y 20

// Maps a body's altitude to a vertical pixel position within the
// canvas, using a shared scale (the higher of today's max sun/moon
// altitude) so the Sun and Moon's rise/set motion reads on one
// consistent scale rather than each independently stretched to fill
// the frame -- a body that only ever gets 20 degrees up on a given
// day genuinely should look low in the sky, not artificially high.
//
// Takes the disc's on-screen radius and clamps the result so its
// bottom edge can never dip into the ground strip while the body is
// still genuinely above the horizon (alt >= 0): without this, a body
// at just a degree or two of altitude -- which is common right
// before the animated set transition even starts -- would already
// have its raw altitude-mapped position overlapping the strip, so it
// visibly started "setting" well before body_screen_y's 3-minute
// window ever kicked in. The strip is opaque and drawn on top, so
// any part of the disc under it just silently disappears regardless
// of what the animation logic intended.
static int16_t alt_to_y(int16_t alt_decideg, int16_t scale_max_decideg, int16_t canvas_h, int16_t radius) {
  int16_t horizon_y = canvas_h - GROUND_H;
  int16_t usable = horizon_y - SKY_TOP_MARGIN;
  if (scale_max_decideg < 50) scale_max_decideg = 50; // guard near-zero/polar edge cases
  int32_t y = horizon_y - ((int32_t)alt_decideg * usable) / scale_max_decideg;

  if (alt_decideg >= 0) {
    int16_t max_y_when_up = horizon_y - radius;
    if (y > max_y_when_up) y = max_y_when_up;
    if (y < SKY_MIN_Y) y = SKY_MIN_Y;
  }

  if (y > canvas_h + 60) y = canvas_h + 60;   // clamp so deep-night altitudes
  if (y < -60) y = -60;                        // don't produce absurd coordinates
  return (int16_t)y;
}

// Resolves whether (and where) to draw a body given its normal
// altitude-based Y position and today's rise/set times: fully hidden
// well outside [rise, set], animating linearly between "just behind
// the horizon" and its normal position during the transition window
// on either edge. Falls back to a plain altitude cut-off if we don't
// have both a rise *and* a set for today (e.g. the Moon rose today
// but doesn't set until tomorrow) -- rare enough not to need the
// extra edge-case handling for a precise fade there too.
static bool body_screen_y(int16_t alt_based_y, time_t rise, time_t set, time_t now,
                            int16_t horizon_y, int16_t radius, int16_t *y_out) {
  int16_t hidden_y = horizon_y + radius;
  bool has_rise = rise != 0;
  bool has_set = set != 0;

  if (!has_rise || !has_set) {
    if (alt_based_y > horizon_y) return false;
    *y_out = alt_based_y;
    return true;
  }

  if (now < rise - RISE_SET_TRANSITION_S || now > set + RISE_SET_TRANSITION_S) {
    return false;
  }

  int32_t span = RISE_SET_TRANSITION_S * 2;
  if (now < rise + RISE_SET_TRANSITION_S) {
    int32_t progress = (int32_t)(now - (rise - RISE_SET_TRANSITION_S));
    if (progress < 0) progress = 0;
    if (progress > span) progress = span;
    *y_out = hidden_y - (int16_t)(((int32_t)(hidden_y - alt_based_y) * progress) / span);
    return true;
  }
  if (now > set - RISE_SET_TRANSITION_S) {
    int32_t progress = (int32_t)(now - (set - RISE_SET_TRANSITION_S));
    if (progress < 0) progress = 0;
    if (progress > span) progress = span;
    *y_out = alt_based_y + (int16_t)(((int32_t)(hidden_y - alt_based_y) * progress) / span);
    return true;
  }
  *y_out = alt_based_y;
  return true;
}

// Plain integer square root (binary/digit-by-digit method) -- used
// instead of sqrt()/sqrtf() from math.h. Pebble apps call into the
// firmware through a curated jump table rather than linking a full
// libm, and libm float/double functions aren't reliably part of
// that table across platforms/firmware versions; a fault calling
// through a missing symbol looks exactly like a jump to a bogus
// near-null address, which is what motivated dropping the libm
// dependency here entirely rather than gambling on which variant
// (float vs double) happens to be available.
static uint16_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  uint32_t x = (uint32_t)v;
  uint32_t res = 0;
  uint32_t bit = 1u << 30; // highest even power of 4 <= any 32-bit value
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= res + bit) {
      x -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return (uint16_t)res;
}

// Outside an eclipse, the Moon must never look close enough to the
// Sun to suggest one is occluding the other -- pushes it away along
// the same direction if it ends up nearer than this.
static GPoint enforce_min_separation(GPoint a, GPoint b, int32_t min_dist) {
  int32_t dx = b.x - a.x;
  int32_t dy = b.y - a.y;
  int32_t dist = (int32_t)isqrt32(dx * dx + dy * dy);
  if (dist >= min_dist) return b;
  if (dist == 0) return GPoint(a.x + min_dist, a.y);
  int32_t nx = a.x + (dx * min_dist) / dist;
  int32_t ny = a.y + (dy * min_dist) / dist;
  return GPoint((int16_t)nx, (int16_t)ny);
}

// ---- moon phase rendering -------------------------------------------------

// Ordered-dither-free, crisp two-tone phase disc: lit vs dark side
// split by a terminator ellipse whose horizontal "bulge" is derived
// from the illuminated fraction. See the README for the derivation;
// briefly, at k<=0.5 (new -> first/last quarter) the ellipse's
// semi-axis shrinks from the full disc radius (fully dark) to 0
// (half-lit), tracing a crescent; at k>0.5 it grows back out from 0
// to the full radius (fully lit), tracing a gibbous. `waxing` just
// picks which side (+x or -x) is the lit one -- a simplification
// that doesn't attempt to get true sky orientation correct for every
// hemisphere/viewing angle, which would need a lot more geometry for
// a watch-sized icon to show it accurately anyway.
void draw_moon_phase(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                       uint8_t phase_pct, bool waxing, GColor lit_color) {
  int32_t k100 = phase_pct; // 0..100
  int32_t side = waxing ? 1 : -1;

  int16_t x0 = center.x - radius, x1 = center.x + radius;
  int16_t y0 = center.y - radius, y1 = center.y + radius;

  for (int16_t y = y0; y <= y1; y++) {
    int16_t dy = y - center.y;
    int32_t term = (int32_t)radius * radius - (int32_t)dy * dy;
    if (term < 0) continue;
    int16_t half_width = (int16_t)isqrt32(term); // circle boundary at this row

    int32_t a;
    bool gibbous = k100 > 50;
    if (!gibbous) a = (radius * (100 - 2 * k100)) / 100;
    else a = (radius * (2 * k100 - 100)) / 100;
    int32_t ellipse_w = (a * half_width) / (radius == 0 ? 1 : radius);

    for (int16_t x = x0 + (radius - half_width); x <= x1 - (radius - half_width); x++) {
      if (x < bounds.origin.x || x >= bounds.origin.x + bounds.size.w) continue;
      if (y < bounds.origin.y || y >= bounds.origin.y + bounds.size.h) continue;
      int16_t dx = x - center.x;
      bool lit = gibbous ? (side * dx > -ellipse_w) : (side * dx >= ellipse_w);
      graphics_context_set_fill_color(ctx, lit ? lit_color : GColorDarkGray);
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, center, radius);
}

// Draws `text` in the shake-to-reveal 3-style label look (see
// draw_label()'s own label_style comment below) into exactly the box
// the caller hands in -- no positioning logic of its own. Split out
// of draw_label() so a caller that already knows precisely where the
// label needs to go (draw_planet_seek_body()'s off-screen case, which
// anchors directly against its own edge arrow rather than a generic
// nearby point) can reuse the same 3-style rendering without
// draw_label()'s own generic "flip whichever side stays on canvas"
// placement getting in the way.
static void draw_label_in_box(GContext *ctx, GRect r, const char *text, uint8_t label_style, GColor main_color) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GRect text_box = GRect(r.origin.x, r.origin.y - 2, r.size.w, r.size.h + 2);

  if (label_style == 1) {
    draw_text_outlined(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                        main_color, 1);
    return;
  }
  if (label_style == 2) {
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, r, 2, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, text_box,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// A small black-backed label for the shake-to-reveal body names.
// Placed to whichever side of `near` keeps it on-canvas, since a
// body can be anywhere from the left edge to the right edge of the
// sky depending on its own column position.
// label_style: 0=Boxed (opaque rounded rect, white text -- the
// original/default look), 1=Outlined (main_color text with a 4-
// direction-shifted contrasting outline, via features_layer.h's
// shared draw_text_outlined() -- same technique corner/edge feature
// text and hand outlines already use), 2=Soft (plain light-gray text,
// no background or outline at all). User setting, right below "Shake
// to see labels" in the Style section.
static void draw_label(GContext *ctx, GRect bounds, GPoint near, const char *text, uint8_t label_style, GColor main_color) {
  // 46px fits every short body name ("Mercury", "Jupiter", ...) with
  // room to spare, so it stays the floor -- only actually measured and
  // grown for the handful of labels long enough to need it (aurora's
  // "Aurora Kp X.X", a custom meteor shower name). Capped well short
  // of the screen width so a label can never eat the whole sky; text
  // still measured against the real font rather than guessed from
  // character count, so it grows exactly as much as it needs to and
  // no more.
  int16_t w = 46, h = 14;
  GSize measured = graphics_text_layout_get_content_size(text, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                                                          GRect(0, 0, 140, h + 4), GTextOverflowModeFill, GTextAlignmentCenter);
  if (measured.w + 8 > w) w = (int16_t)(measured.w + 8);
  if (w > 140) w = 140;
  int16_t x = near.x + 8;
  if (x + w > bounds.origin.x + bounds.size.w) x = near.x - w - 8;
  if (x < bounds.origin.x) x = bounds.origin.x;
  int16_t y = near.y - h / 2;
  if (y < bounds.origin.y) y = bounds.origin.y;
  if (y + h > bounds.origin.y + bounds.size.h) y = bounds.origin.y + bounds.size.h - h;
  draw_label_in_box(ctx, GRect(x, y, w, h), text, label_style, main_color);
}

// A minimal 3x5-pixel digit font, drawn procedurally rather than
// loaded as a resource -- same design as the analog clock's "tiny
// numerals" face style in pebble-eclipse-watch.c (duplicated here
// rather than shared across the two translation units, since it's
// ---- planets ---------------------------------------------------------

#define PLANET_R 3
#define ISS_R 3

static const char *PLANET_NAMES[PLANET_COUNT] = { "Mercury", "Venus", "Mars", "Jupiter", "Saturn" };

// ---- bright named stars (space-view sky mode) -------------------------
// Display names only -- position (alt/az) comes from d->star_alt_decideg/
// star_az_decideg, computed phone-side. Order MUST match astro.js's
// STAR_CATALOG exactly (see eclipse_data.h's STAR_COUNT comment).
static const char *STAR_NAMES[STAR_COUNT] = {
  "Sirius", "Canopus", "Arcturus", "Vega", "Capella", "Rigel", "Procyon", "Betelgeuse",
  "Altair", "Aldebaran", "Antares", "Spica", "Pollux", "Fomalhaut", "Deneb", "Regulus"
};
// 2px for the ~7 brightest (mag < ~0.4), 1px for the rest -- real
// stars vary continuously in brightness, but this app's canvas is far
// too small for anything finer than "a little bigger" to read at all.
static const uint8_t STAR_RADIUS[STAR_COUNT] = {
  2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

// Fixed sky "columns" (percent of canvas width) so multiple planets
// visible at once don't collide -- same simplification already used
// for the Sun/Moon, real azimuth isn't tracked.
static const int16_t PLANET_COLUMN_PCT[PLANET_COUNT] = { 15, 85, 42, 58, 33 };

static GColor planet_color(PlanetId p) {
  switch (p) {
    case PLANET_MERCURY: return GColorLightGray;
    case PLANET_VENUS: return GColorWhite;
    case PLANET_MARS: return GColorRed;
    case PLANET_JUPITER: return GColorYellow;
    case PLANET_SATURN: default: return GColorYellow;
  }
}

// Saturn gets its own renderer: a ring band through the disc whose
// thickness reflects the real current ring-opening angle as seen
// from Earth (0-100%, from astro.js's saturnRingAngle()) -- thin
// near a ring-plane crossing (rings edge-on, as they were as
// recently as March 2025), thicker as they open up over the next
// several years.
static void draw_saturn(GContext *ctx, GPoint center, uint8_t ring_open_pct) {
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_circle(ctx, center, PLANET_R);

  int16_t ring_span = PLANET_R + 4;
  int16_t ring_thickness = 1 + (int16_t)((ring_open_pct * 2) / 100); // 1..3px
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx,
    GRect(center.x - ring_span, center.y - ring_thickness / 2, ring_span * 2, ring_thickness),
    0, GCornerNone);
}

// ---- forced-redraw triggers -------------------------------------------

// A coarse "which part of the eclipse are we in" id: 0 = no eclipse
// today, 1 = before C1, 2 = C1-C2 (partial, entering), 3 = C2-C3
// (totality/annularity, or just "C2-C3" for a partial-only eclipse
// where C2/C3 are both 0 -- see below), 4 = C3-C4 (partial, exiting),
// 5 = after C4. Used only to detect *when this changes*, so the exact
// numbering doesn't matter as long as each real phase gets a distinct
// value.
//
// For a partial-only eclipse (c2 and c3 both 0, no totality/
// annularity), the "now < c2" and "now < c3" checks below both
// immediately fail (0 is never greater than a real epoch time), so
// this degrades gracefully to just two transitions (into and out of
// C1-C4) rather than needing special-case handling.
static int compute_eclipse_phase(const EclipseData *d, time_t now) {
  if (!d->has_eclipse) return 0;
  if (now < d->c1) return 1;
  if (now < d->c2) return 2;
  if (now < d->c3) return 3;
  if (now < d->c4) return 4;
  return 5;
}

// Mirrors the actual ISS visibility gate inside canvas_update_proc
// (see there for why each condition exists) -- kept in sync
// deliberately rather than factored into one shared call, since the
// full version there also needs the computed screen position, not
// just a yes/no.
static bool compute_iss_visible(const EclipseData *d, time_t now, bool sky_is_dark) {
  if (!d->show_iss || !sky_is_dark || d->iss_alt_deg <= 0 || d->iss_computed_at == 0) return false;
  time_t age = now - d->iss_computed_at;
  return age >= 0 && age < 900;
}

// ---- drawing ---------------------------------------------------------------

// ---- "Planet seek" (shake_anim_mode 2 or 3) ---------------------------------
// See shake_anim_mode's own eclipse_data.h comment for the feature as a
// whole. Draws each visible body (cached_sun_up/cached_moon_visible/
// cached_planet_visible[], all populated by the most recent normal
// draw -- see their own comment on CanvasState) at a position blended
// between its normal (non-rotated) screen spot and a compass-relative
// one: azimuth mapped across a 90deg field of view centered on the
// watch's current heading, in a 500ms ease-in / hold / 500ms ease-out
// sweep. A body outside that 90deg window gets an edge-pinned label +
// arrow instead of being drawn off-canvas invisibly. Altitude (and so
// screen Y) never changes here -- only the compass-relative X does.

// azimuth (0-3599 decideg) -> signed offset from the view's own
// center, wrapped to the shorter of the two ways around the compass
// (-1800..1800 decideg) -- same wraparound reasoning as
// interp_az_wrapped() above, just centered on the live heading
// instead of blending between two samples.
static int32_t planet_seek_az_offset_decideg(uint16_t az_decideg, int32_t heading_deg) {
  int32_t diff = (int32_t)az_decideg - heading_deg * 10;
  diff = diff % 3600;
  if (diff > 1800) diff -= 3600;
  if (diff < -1800) diff += 3600;
  return diff;
}

// A small filled triangle pointing left or right, for the off-screen
// edge label's own arrow -- built from GPathInfo/gpath_draw_filled,
// the standard Pebble primitive for exactly this ("simple flat-shaded
// polygon") rather than a custom rasterizer, since it's a single
// static 3-point shape with no need for subpixel.h's own machinery.
// Its own base-to-tip width, shared with draw_planet_seek_edge_label()
// below so the label can anchor directly against the arrow's flat
// base rather than duplicating this number.
#define PLANET_SEEK_ARROW_W 6
static void draw_planet_seek_arrow(GContext *ctx, GPoint tip, bool points_left, GColor color) {
  int16_t w = PLANET_SEEK_ARROW_W, h = 8;
  GPoint pts_left[3] = { GPoint(tip.x, tip.y), GPoint(tip.x + w, tip.y - h / 2), GPoint(tip.x + w, tip.y + h / 2) };
  GPoint pts_right[3] = { GPoint(tip.x, tip.y), GPoint(tip.x - w, tip.y - h / 2), GPoint(tip.x - w, tip.y + h / 2) };
  GPathInfo info = { .num_points = 3, .points = points_left ? pts_left : pts_right };
  GPath *path = gpath_create(&info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// The off-screen edge label. Unlike draw_label()'s own generic "flip
// whichever side keeps it on canvas" placement (tuned for a label
// near an arbitrary point out in the open sky), this one anchors
// directly against its own arrow's flat base with a small fixed gap,
// so the two always sit right next to each other regardless of label
// width or screen size. draw_label() used to be reused here too, but
// its near-point flip logic put the label's own edge a further
// ~30-40px away from the arrow depending on which way it flipped --
// an inconsistent gap that had nothing to do with the arrow's actual
// position, per the request.
static void draw_planet_seek_edge_label(GContext *ctx, GRect bounds, GPoint arrow_tip, bool pin_right,
                                         const char *text, uint8_t label_style, GColor main_color) {
  int16_t w = 46, h = 14, gap = 2;
  int16_t arrow_base_x = pin_right ? (arrow_tip.x - PLANET_SEEK_ARROW_W) : (arrow_tip.x + PLANET_SEEK_ARROW_W);
  int16_t x = pin_right ? (arrow_base_x - gap - w) : (arrow_base_x + gap);
  int16_t y = arrow_tip.y - h / 2;
  if (y < bounds.origin.y) y = bounds.origin.y;
  if (y + h > bounds.origin.y + bounds.size.h) y = bounds.origin.y + bounds.size.h - h;
  draw_label_in_box(ctx, GRect(x, y, w, h), text, label_style, main_color);
}

// is_moon/moon_phase_pct/moon_waxing: when is_moon is true, the on-screen
// case below draws a correctly-shaded moon-phase disc (draw_moon_phase())
// instead of a flat filled circle -- otherwise Planet seek's Moon reads as
// a second plain white/yellow "sun", with no phase shading at all. Only
// affects the on-screen circle; the off-screen edge label + arrow are the
// same for every body regardless of is_moon.
static void draw_planet_seek_body(GContext *ctx, GRect bounds, const char *name,
                                   uint16_t az_decideg, GPoint normal_center, int16_t radius,
                                   GColor fill_color, int32_t heading_deg, int32_t blend_t_1000,
                                   uint8_t label_style, GColor main_color,
                                   bool is_moon, uint8_t moon_phase_pct, bool moon_waxing) {
  int32_t offset_decideg = planet_seek_az_offset_decideg(az_decideg, heading_deg);
  // 90deg field of view across the full screen width -- +-45deg maps
  // to the left/right edges. Whether this body ends up drawn as an
  // on-screen circle or an off-screen edge arrow is decided from this
  // raw (unblended) offset -- a fixed property of the body's real sky
  // position vs the current heading -- so it doesn't flip back and
  // forth mid-transition; only the drawn X position itself eases in
  // via blend_t_1000 below, from the body's own normal (non-compass)
  // position toward wherever it's actually headed, on-screen or off.
  // This used to snap the off-screen case straight to its pinned edge
  // position with no blend at all, which is what showed up as
  // "planets just jump" in and out of the mode.
  bool in_fov = (offset_decideg >= -450 && offset_decideg <= 450);

  if (in_fov) {
    int32_t compass_x = bounds.origin.x + bounds.size.w / 2 + (int32_t)((int64_t)offset_decideg * bounds.size.w / 900);
    int16_t blended_x = (int16_t)(normal_center.x + (((int32_t)compass_x - normal_center.x) * blend_t_1000) / 1000);
    GPoint pos = GPoint(blended_x, normal_center.y);
    if (pos.x >= bounds.origin.x - radius && pos.x <= bounds.origin.x + bounds.size.w + radius) {
      if (is_moon) {
        draw_moon_phase(ctx, bounds, pos, radius, moon_phase_pct, moon_waxing, fill_color);
      } else {
        graphics_context_set_fill_color(ctx, fill_color);
        graphics_fill_circle(ctx, pos, radius);
      }
      draw_label(ctx, bounds, pos, name, label_style, main_color);
      return;
    }
  }

  // Off screen: label+arrow pinned to whichever edge is the shorter
  // way to turn to actually reach it -- offset_decideg > 0 means the
  // body is clockwise (east) of center, i.e. reached by turning
  // right, hence pinned to the RIGHT edge (and vice versa for < 0/
  // left) -- see the request's own worked example ("Sun behind on my
  // left" -> left edge, left-pointing arrow). Slides in from the
  // body's own normal position via blend_t_1000, same as the
  // on-screen case above.
  bool pin_right = offset_decideg > 0;
  int16_t edge_arrow_x = pin_right ? (bounds.origin.x + bounds.size.w - 2) : (bounds.origin.x + 2);
  int16_t blended_arrow_x = (int16_t)(normal_center.x + (((int32_t)edge_arrow_x - normal_center.x) * blend_t_1000) / 1000);
  GPoint arrow_tip = GPoint(blended_arrow_x, normal_center.y);
  draw_planet_seek_edge_label(ctx, bounds, arrow_tip, pin_right, name, label_style, main_color);
  draw_planet_seek_arrow(ctx, arrow_tip, !pin_right, main_color);
}

// eased_t_1000: 0 = fully at the normal (non-rotated) position, 1000 =
// fully at the compass-relative one -- see canvas_update_proc's own
// call site for how the 500ms-in/hold/500ms-out phases produce this.
// 500ms ease-in from the normal position, then a hold at the fully
// compass-relative position, then a 500ms ease-out back to normal --
// see draw_planet_seek_overlay's own top comment. Shared by both of
// canvas_update_proc's own call sites (the early cache-blit return
// and the end of a full draw) so the timing logic lives in exactly
// one place.
// 500ms ease-in from 0, then a hold at 1000, then a 500ms ease-out
// back to 0 -- the shared extend/hold/contract shape both Planet
// seek (its position blend) and Paths (its reveal-length budget)
// use, given how far into a shake_label_seconds-long window we
// currently are. Pulled out as its own elapsed-ms-parameterized
// function (rather than reading state->planet_seek_elapsed_ms
// directly the way this used to) so Paths -- which tracks its own,
// separate elapsed-ms field, since it can run independently of
// Planet seek (a different shake_anim_mode value) -- can share the
// exact same timing math instead of duplicating it.
static int32_t shake_anim_eased_t_1000(uint16_t elapsed, const EclipseData *d) {
  uint32_t duration_ms = (uint32_t)(d->shake_label_seconds > 0 ? d->shake_label_seconds : 3) * 1000;
  if (elapsed < 500) {
    return bg_anim_ease_out_1000(((int32_t)elapsed * 1000) / 500);
  }
  if (duration_ms > 500 && elapsed > duration_ms - 500) {
    int32_t remaining = (int32_t)duration_ms - (int32_t)elapsed;
    if (remaining < 0) remaining = 0;
    return bg_anim_ease_out_1000((remaining * 1000) / 500);
  }
  return 1000;
}

static int32_t planet_seek_eased_t_1000(const CanvasState *state, const EclipseData *d) {
  return shake_anim_eased_t_1000(state->planet_seek_elapsed_ms, d);
}

// ---- "Planets" background-on-start animation (bg_anim_mode 2) ---------
// Same "keep it out of the cache, paint it fresh every frame instead"
// fix Planet seek already uses (see cached_sun_center's own comment) --
// this is the plainer version: no FOV mapping, no off-screen labels,
// just the Sun/Moon/planets at their already-computed, already-cached
// normal positions (which sweep on their own between frames, since
// they were computed from the animated sky_now substitution further up
// in canvas_update_proc, not this function). moon_visible here means
// "not itself the eclipsing moon" too, same as the normal paint code's
// own moon_visible flag -- eclipse mode never reaches this animation at
// all (bg_anim_mode is a Style-section setting with no eclipse-time
// relevance), but the flag's meaning carries over regardless.
static void draw_bg_anim_planets_overlay(GContext *ctx, CanvasState *state, const EclipseData *d, GRect bounds) {
  if (state->cached_sun_up) {
    // Space view (see SUN_COLOR_SPACE_R's own comment): flat color
    // rather than state->cached_sun_fill_color's own altitude-based
    // one (this overlay is the one exception, not a change to that
    // shared cached value itself); Weather/Clear sky modes use the
    // real altitude-based color instead -- state->cached_sun_fill_color
    // already reflects wherever sky_now (the animated sweep) currently
    // sits, so the Sun genuinely shades from white through orange to
    // red as it sweeps toward the horizon, the same way it would on a
    // normal (non-animated) redraw.
    bool is_space_view = d->sky_mode == 2;
    GColor sun_fill = is_space_view
      ? GColorFromRGB(SUN_COLOR_SPACE_R, SUN_COLOR_SPACE_G, SUN_COLOR_SPACE_B)
      : state->cached_sun_fill_color;
    graphics_context_set_fill_color(ctx, sun_fill);
    graphics_fill_circle(ctx, state->cached_sun_center, state->cached_sun_r);
  }
  if (state->cached_moon_visible) {
    draw_moon_phase(ctx, bounds, state->cached_moon_center, state->cached_moon_r, d->moon_phase_pct, d->moon_waxing, GColorWhite);
  }
  for (int p = 0; p < PLANET_COUNT; p++) {
    if (!state->cached_planet_visible[p]) continue;
    if (p == PLANET_SATURN) {
      draw_saturn(ctx, state->cached_planet_center[p], d->saturn_ring_open_pct);
    } else {
      graphics_context_set_fill_color(ctx, planet_color((PlanetId)p));
      graphics_fill_circle(ctx, state->cached_planet_center[p], PLANET_R);
    }
  }
}

// "Markers" background-on-start animation (bg_anim_mode 2) -- same
// shape as draw_bg_anim_planets_overlay() above, for the same
// reason: only the hour ring's own reveal-in position changes frame to
// frame (see draw_marker_ring()'s own animation handling), the rest of
// the sky backdrop stays fixed for the whole animation, so it only
// needs to be captured once rather than redrawn every frame. Draws
// BOTH rings (the settled second ring too, not just the animating hour
// one) since neither was baked into the cache this frame -- see
// canvas_update_proc's own skip_marker_paint for why -- cheap either
// way (one sin/cos per mark, see draw_marker_ring()'s own comment).
static void draw_bg_anim_markers_overlay(GContext *ctx, CanvasState *state, const EclipseData *d,
                                          GRect bounds, time_t now) {
  if (d->bottom_style != 1) return; // markers are analog-mode only

  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(d, now, &bg, &main_color, &accent_color);

  int32_t progress_1000 = ((int32_t)state->bg_anim_elapsed_ms * 1000) / BG_ANIM_MS;
  if (progress_1000 > 1000) progress_1000 = 1000;

  marker_layer_draw(ctx, &state->markers, center, bounds, d, main_color, accent_color, bg, true, progress_1000, d->draw_debug);
}

static void draw_planet_seek_overlay(GContext *ctx, CanvasState *state, const EclipseData *d,
                                      GRect bounds, time_t now, int32_t eased_t_1000, GColor main_color) {
  int32_t heading_deg = state->planet_seek_heading_deg;

  uint8_t pct = d->sun_moon_size_pct > 0 ? d->sun_moon_size_pct : 100;
  int16_t sun_r = (SUN_R_NORMAL * pct) / 100;
  if (sun_r < 4) sun_r = 4;
  int16_t moon_r = (MOON_R_NORMAL * pct) / 100;
  if (moon_r < 4) moon_r = 4;

  if (state->cached_sun_up) {
    // Space view (see SUN_COLOR_SPACE_R's own comment): flat color,
    // not the normal altitude-based shift. Weather/Clear sky modes use
    // the real altitude-based color instead, same reasoning and same
    // fix as draw_bg_anim_planets_overlay()'s own identical case above
    // -- state->cached_sun_fill_color already reflects the Sun's real
    // current altitude (Planet seek doesn't substitute sky_now the way
    // the "Planets" bg-anim does, it only repositions bodies by
    // compass heading, so there's no animated sweep to worry about
    // here, just the correct color for right now).
    GColor sun_fill = (d->sky_mode == 2)
      ? GColorFromRGB(SUN_COLOR_SPACE_R, SUN_COLOR_SPACE_G, SUN_COLOR_SPACE_B)
      : state->cached_sun_fill_color;
    draw_planet_seek_body(ctx, bounds, "Sun", interp_sun_az_decideg(d, now), state->cached_sun_center, sun_r,
                           sun_fill, heading_deg, eased_t_1000, d->label_style, main_color,
                           false, 0, false);
  }
  if (state->cached_moon_visible) {
    draw_planet_seek_body(ctx, bounds, "Moon", interp_moon_az_decideg(d, now), state->cached_moon_center, moon_r,
                           GColorWhite, heading_deg, eased_t_1000, d->label_style, main_color,
                           true, d->moon_phase_pct, d->moon_waxing);
  }
  for (int p = 0; p < PLANET_COUNT; p++) {
    if (!state->cached_planet_visible[p]) continue;
    draw_planet_seek_body(ctx, bounds, PLANET_NAMES[p], interp_planet_az_decideg(d, (PlanetId)p, now),
                           state->cached_planet_center[p], PLANET_R, planet_color((PlanetId)p),
                           heading_deg, eased_t_1000, d->label_style, main_color,
                           false, 0, false);
  }
  // Stars and ISS have no full-day sample grid to interpolate through
  // (see interp_sun_az_decideg's own comment for what that grid is
  // for) -- each only ever carries a single "right now" azimuth from
  // the phone, unlike the Sun/Moon/planets' whole-day arcs, so there's
  // nothing to interpolate: d->star_az_decideg[]/d->iss_az_deg IS
  // already "now".
  if (d->sky_mode == 2 && d->show_major_stars) {
    for (int s = 0; s < STAR_COUNT; s++) {
      if (d->star_alt_decideg[s] <= 0) continue; // below the horizon
      int16_t s_y = alt_to_y(d->star_alt_decideg[s], d->sky_scale_max_alt_decideg, bounds.size.h, STAR_RADIUS[s]);
      int16_t s_x = (bounds.size.w * (int32_t)d->star_az_decideg[s]) / 3600;
      draw_planet_seek_body(ctx, bounds, STAR_NAMES[s], (uint16_t)d->star_az_decideg[s], GPoint(s_x, s_y),
                             STAR_RADIUS[s], GColorWhite, heading_deg, eased_t_1000, d->label_style, main_color,
                             false, 0, false);
    }
  }
  if (state->cached_iss_visible) {
    draw_planet_seek_body(ctx, bounds, "ISS", (uint16_t)(d->iss_az_deg * 10), state->cached_iss_center,
                           ISS_R, GColorWhite, heading_deg, eased_t_1000, d->label_style, main_color,
                           false, 0, false);
  }
}


static void canvas_update_proc(Layer *layer, GContext *ctx) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  EclipseData *d = state->data;
  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  time_t now = time(NULL);

  if (!d->valid) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    const char *msg = "Waiting for phone...";
    if (d->error_code == 1) msg = "Location unavailable";
    else if (d->error_code == 2) msg = "Calculation error";
    else if (d->error_code == 3) msg = "Couldn't reach watch";
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, msg,
                        fonts_get_system_font(FONT_KEY_GOTHIC_18),
                        GRect(0, center.y - 10, bounds.size.w, 20),
                        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // "Animate background on start": sweeps the Sun/Moon/planets' own
  // effective observation time from a couple hours ago up to the real
  // `now`, eased to slow down toward the end -- since sky_colors_for_
  // altitude()/the Sun's own color/planet-visibility-threshold below
  // all key off whichever altitude this produces, substituting it in
  // place of `now` for just those position/color lookups (NOT
  // anything else in this function -- eclipse timing, the weather-
  // driven cloud coverage amount, the user's own day/night color
  // scheme, and the countdown label all keep using the real `now`)
  // cascades the sweep across the sky gradient, the Sun's disc color,
  // and every body's screen position all at once from this one
  // substitution point.
  time_t sky_now = now;
  if (state->bg_anim_active && d->bg_anim_mode == 1) {
    int32_t progress = ((int32_t)state->bg_anim_elapsed_ms * 1000) / BG_ANIM_MS;
    if (progress > 1000) progress = 1000;
    int32_t eased = bg_anim_ease_out_1000(progress);
    time_t past = now - 2 * 3600; // "a couple hours ago"
    sky_now = past + (time_t)(((int64_t)(now - past) * eased) / 1000);
  }

  // Computed early -- cheap (just interpolation, no drawing) -- since
  // the throttle decision below needs sky darkness to tell whether
  // the ISS's visibility just changed.
  int16_t alt = interp_sun_alt_decideg(d, sky_now);
  bool sky_is_dark = alt <= -60;
  // Space-view sky mode has no atmosphere to dim the sky in the first
  // place, so its celestial bodies (planets, ISS) are never gated by
  // brightness -- only by whether they're actually above the horizon,
  // same as the Sun/Moon already are in every mode. Stars get their
  // own separate always-drawn treatment further down, since they
  // don't exist at all outside this mode.
  bool sky_dark_for_bodies = sky_is_dark || d->sky_mode == 2;

  // Shared by every Sun/Moon/planet paint call below: skip painting
  // the body into this frame (it still gets fully positioned/sized as
  // normal, just not drawn) whenever something else is going to draw
  // it separately, on top of whatever gets cached here, instead --
  // Planet seek (see its own cached_sun_center comment above) and now
  // "Planets" background-on-start mode too, which had the same
  // "duplicate" bug Planet seek was built to avoid: painting the
  // Sun/Moon/planets straight into the frame that becomes sky_cache
  // meant the LAST animated frame's positions stayed baked into that
  // cache, and every SUBSEQUENT blit-from-cache redraw (this canvas's
  // normal once-a-minute throttle) kept showing them there even as
  // real time moved the bodies elsewhere -- a second, stale copy
  // wherever they'd been mid-sweep, alongside the real one. See
  // draw_bg_anim_planets_overlay() below for the actual fix.
  bool skip_body_paint = state->planet_seek_active || (state->bg_anim_active && d->bg_anim_mode == 1);

  // Same "skip it here, draw it fresh as an overlay after the cache
  // capture" trick as skip_body_paint above, now for the "Markers"
  // background-on-start mode -- see draw_bg_anim_markers_overlay()'s
  // own comment for why its backdrop can be captured once (unlike
  // mode 1's, which keeps changing throughout the sweep) and only the
  // overlaid element needs to be fresh every frame.
  bool skip_marker_paint = state->bg_anim_active && d->bg_anim_mode == 2;

  int current_phase = compute_eclipse_phase(d, now);
  bool current_iss_visible = compute_iss_visible(d, now, sky_dark_for_bodies);
  bool phase_just_changed = current_phase != state->last_eclipse_phase;
  bool was_first_draw = (state->last_eclipse_phase == -1);

  // Fires once per eclipse at the moment of greatest eclipse itself
  // (d->max_t, always populated regardless of type), independent of the
  // phase-boundary vibration below -- that one only ever fires for
  // total/annular eclipses (crossing C1/C2/C3/C4), so a plain partial
  // eclipse never had ANY vibration at its actual visual climax before
  // this. Tracks last_eclipse_max (not just a bool) so repeated
  // set_data() calls for the same eclipse during a normal refresh
  // cycle don't re-arm and re-fire this after it's already happened,
  // while a genuinely new eclipse (different max time) correctly does.
  if (d->max_t != state->last_eclipse_max) {
    state->last_eclipse_max = d->max_t;
    state->max_vibrated = false;
  }
  bool just_passed_max = d->has_eclipse && d->max_t != 0 && !state->max_vibrated && now >= d->max_t;

  // The sky/sun/moon/clouds/planets barely change within a minute,
  // and this is an e-paper display, so the expensive part of this
  // redraw is self-throttled to once a minute -- tracked here rather
  // than only relying on the caller not to mark us dirty too often,
  // so the guarantee holds regardless of what triggers the redraw.
  // New data (set_data), a label toggle (set_show_labels), crossing
  // an eclipse phase boundary (C1/C2/C3/C4), passing the moment of
  // greatest eclipse, or the ISS appearing or disappearing all force
  // through immediately, since those are visible state changes that
  // must show up right away rather than waiting for the next
  // scheduled minute.
  //
  // Critically, the seconds *in between* don't just skip drawing --
  // Pebble doesn't guarantee a layer's previous pixels survive until
  // its next update_proc call, so doing nothing here flickered. The
  // real fix is a cached bitmap: on a full redraw we draw everything
  // as before, then capture the just-drawn framebuffer region into
  // sky_cache via the documented graphics_capture_frame_buffer() API;
  // on the throttled seconds we just blit that cached bitmap back,
  // which is cheap and always leaves valid pixels on screen.
  // Realistic-cloud lightning during a storm: a cheap per-second check
  // (not itself a redraw -- just arithmetic) for whether a flash
  // should start or stop right now, so a strike can appear/disappear
  // on the actual second it's due rather than waiting for the next
  // scheduled once-a-minute redraw. Deliberately NOT a general
  // exception to the once-a-minute throttle -- outside an active
  // storm this is always false and costs nothing extra; see
  // draw_clouds_realistic() for what a flash actually looks like.
  bool storm_now = d->sky_mode == 0 && d->weather_condition == 4;
  bool flash_currently_active = storm_now && now < state->storm_flash_end;
  if (storm_now && !flash_currently_active) {
    // Deterministic pseudo-random hash of the current second, not a
    // real RNG (nothing here needs cryptographic quality, and this
    // avoids persisting extra seed state) -- gives each second during
    // a storm roughly a 1-in-43 chance of being a strike, averaging
    // one flash every ~40s.
    uint32_t h = (uint32_t)now * 2654435761u;
    if ((h & 0xFF) < 6) {
      state->storm_flash_end = now + 1; // strikes read as a single ~1s flash
      flash_currently_active = true;
    }
  }
  bool storm_flash_transition = flash_currently_active != state->storm_flash_was_active;
  state->storm_flash_was_active = flash_currently_active;

  bool need_full_draw = state->force_next_draw
    || phase_just_changed
    || just_passed_max
    || current_iss_visible != state->last_iss_visible
    || storm_flash_transition;
  if (!need_full_draw) {
    time_t elapsed = now - state->last_full_draw;
    if (elapsed < 0 || elapsed >= 60) need_full_draw = true;
  }

  if (!need_full_draw && state->sky_cache) {
    graphics_draw_bitmap_in_rect(ctx, state->sky_cache, bounds);
    if (state->planet_seek_active) {
      GColor bg, main_color, accent_color;
      get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
      draw_planet_seek_overlay(ctx, state, d, bounds, now, planet_seek_eased_t_1000(state, d), main_color);
    }
    if (state->bg_anim_active && d->bg_anim_mode == 1) {
      draw_bg_anim_planets_overlay(ctx, state, d, bounds);
    }
    if (state->bg_anim_active && d->bg_anim_mode == 2) {
      draw_bg_anim_markers_overlay(ctx, state, d, bounds, now);
    }
    return;
  }

  // A brief double-buzz on a real contact-time crossing (C1/C2/C3/C4,
  // i.e. current_phase 2-5) -- not on app launch happening to land
  // mid-eclipse (was_first_draw), and not on the earlier "there's an
  // eclipse today, waiting" 0->1 transition, which isn't really the
  // start of anything happening yet.
  if (phase_just_changed && !was_first_draw && current_phase >= 2 && d->vibrate_on_phase_change) {
    vibes_double_pulse();
  }
  // Same idea, but for the moment of greatest eclipse itself (see
  // just_passed_max above) -- this is the one that actually fires for
  // a plain partial eclipse, and for total/annular ones it's a second,
  // near-simultaneous buzz alongside the C2 phase-boundary one above
  // (max isn't guaranteed to land exactly at C2), which is a minor,
  // harmless redundancy rather than a bug.
  if (just_passed_max && !was_first_draw && d->vibrate_on_phase_change) {
    vibes_double_pulse();
  }
  if (just_passed_max) state->max_vibrated = true; // mark done either way -- was_first_draw just suppresses the buzz itself, not the bookkeeping

  state->force_next_draw = false;
  state->last_full_draw = now;
  state->last_eclipse_phase = current_phase;
  state->last_iss_visible = current_iss_visible;

  int16_t moon_alt = interp_moon_alt_decideg(d, sky_now);
  uint8_t cloud_pct = interp_cloud_pct(d, now);
  bool stormy = d->weather_condition == 4;

  // sky_mode: 0=Weather sky (below, unchanged), 1=Clear sky (same
  // day/night gradient, but weather_enabled below skips the haze and
  // the cloud/weather-effect calls further down), 2=Space view (no
  // gradient at all -- flat near-black, handled entirely in this
  // branch instead of falling through to fill_sky_gradient_ex()).
  bool weather_enabled = d->sky_mode == 0;
  if (d->sky_mode == 2) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  } else {
    RGB8 sky_top_rgb, sky_hz_rgb;
    sky_colors_for_altitude(alt, &sky_top_rgb, &sky_hz_rgb);

    // Digital top layout only: this canvas is the BOTTOM
    // DIGITAL_PANEL_H..(DIGITAL_PANEL_H+bounds.size.h) slice of a
    // conceptually 228px-tall gradient whose top DIGITAL_PANEL_H rows
    // are painted separately by eclipse_top_gradient_*() (the
    // transparent panel's own reserved strip -- see that module's own
    // comment) -- computing this canvas's OWN band/horizon math
    // against that same taller virtual span, instead of just its own
    // local bounds, is what makes the two independently-drawn pieces
    // line up into one seamless gradient rather than each stretching
    // the same top/band/hz colors across its own shorter span and
    // visibly disagreeing at the seam. Every other layout keeps the
    // virtual span identical to `bounds` itself -- i.e. no change at
    // all from before this existed.
    bool is_digital_top = bottom_style_is_digital_top(d->bottom_style);
    int16_t virtual_top_y = is_digital_top ? DIGITAL_PANEL_H : bounds.origin.y;
    int16_t virtual_total_h = is_digital_top ? (DIGITAL_PANEL_H + bounds.size.h) : bounds.size.h;

    // Beneath an overcast deck the sky reads grayer, like the view
    // crossing under cloud cover from a plane window -- the effect
    // ramps in past 35% cover, and rain/snow/storm push it further
    // gray on top of whatever the coverage alone would give. Skipped
    // entirely in Clear sky mode -- no weather means no haze either.
    uint8_t gray_amount = 0;
    if (weather_enabled) {
      if (cloud_pct > 35) {
        int32_t g = ((int32_t)(cloud_pct - 35) * 100) / 65;
        gray_amount = (uint8_t)(g > 100 ? 100 : g);
      }
      if (stormy) {
        if (gray_amount < 85) gray_amount = 85;
      } else if (d->weather_condition == 2 || d->weather_condition == 3) {
        if (gray_amount < 55) gray_amount = 55;
      }
    }

    RGB8 band_rgb = sky_hz_rgb;
    RGB8 hz_rgb = sky_hz_rgb; // what actually reaches fill_sky_gradient_ex's horizon row
    int16_t band_y_screen = virtual_top_y + virtual_total_h; // off-canvas: no visible band by default
    if (gray_amount > 0) {
      // The gradient used to fade back UP to the raw (often bright/
      // warm, especially at sunset/sunrise) horizon color right at
      // the bottom row, undoing the graying effect exactly where a
      // heavy deck should block the most light -- straight down,
      // near the ground. Now the horizon row darkens too, toward a
      // much darker target than the band itself (which sits right at
      // the cloud deck's own height, not blocked by anything above
      // it yet) -- so a heavy storm reads as "darkening further the
      // lower/closer to the ground you look" rather than just a flat
      // gray band that un-grays again beneath it.
      RGB8 neutral_gray = { 115, 117, 120 };
      RGB8 dark_gray = { 40, 41, 46 };
      band_rgb.r = lerp8(sky_hz_rgb.r, neutral_gray.r, gray_amount, 100);
      band_rgb.g = lerp8(sky_hz_rgb.g, neutral_gray.g, gray_amount, 100);
      band_rgb.b = lerp8(sky_hz_rgb.b, neutral_gray.b, gray_amount, 100);
      hz_rgb.r = lerp8(sky_hz_rgb.r, dark_gray.r, gray_amount, 100);
      hz_rgb.g = lerp8(sky_hz_rgb.g, dark_gray.g, gray_amount, 100);
      hz_rgb.b = lerp8(sky_hz_rgb.b, dark_gray.b, gray_amount, 100);
      band_y_screen = compute_cloud_band_y_virtual(virtual_top_y, virtual_total_h, d->cloud_altitude_pct);
    }

    fill_sky_gradient_ex(ctx, bounds, virtual_top_y, virtual_total_h, sky_top_rgb, band_rgb, band_y_screen, hz_rgb);
  }

  // Space-view sky mode's bright-star field: real azimuth-to-x /
  // altitude-to-y placement (same simplification the ISS already uses
  // further down, just for many bodies at once instead of one) rather
  // than the Sun/Moon/planets' fixed-column trick, since 16 stars
  // sharing one column would be an unreadable stack. Drawn early, as
  // a backdrop behind the Sun/Moon/planets/clouds that follow --
  // real stars sit "behind" everything else too. Tracked per-star for
  // the shake-to-reveal labels alongside the Sun/Moon/planets below.
  //
  // suppress_other_bodies_for_eclipse: per request, Space view shows
  // ONLY the Sun and Moon while an eclipse is actively in progress --
  // stars/planets/ISS are all hidden for the duration, the same way
  // they'd be washed out by real daylight in Clear/Weather sky mode
  // (Space view has no atmosphere to do that on its own).
  //
  // show_major_stars: user setting, on by default -- turning it off
  // limits Space view to the Sun/Moon/planets and the sky-effects
  // layer (aurora/ISS/meteor showers, drawn further down, entirely
  // unaffected by this flag) rather than this star field specifically.
  bool suppress_other_bodies_for_eclipse = d->sky_mode == 2 && eclipse_is_active(d, now);
  bool star_visible[STAR_COUNT];
  GPoint star_center[STAR_COUNT];
  for (int s = 0; s < STAR_COUNT; s++) {
    star_visible[s] = false;
    star_center[s] = GPoint(0, 0);
  }
  if (d->sky_mode == 2 && d->show_major_stars && !skip_body_paint && !suppress_other_bodies_for_eclipse) {
    for (int s = 0; s < STAR_COUNT; s++) {
      int16_t s_alt = d->star_alt_decideg[s];
      if (s_alt <= 0) continue; // below the horizon -- no atmosphere doesn't mean no ground
      int16_t s_y = alt_to_y(s_alt, d->sky_scale_max_alt_decideg, bounds.size.h, STAR_RADIUS[s]);
      int16_t s_x = (bounds.size.w * (int32_t)d->star_az_decideg[s]) / 3600;
      GPoint c = GPoint(s_x, s_y);
      star_visible[s] = true;
      star_center[s] = c;
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, c, STAR_RADIUS[s]);
    }
  }

  // "Dark enough to see planets/meteors" -- same threshold used
  // elsewhere for picking light vs dark overlay text, reused here for
  // consistency rather than inventing a second one. Meteors are an
  // atmospheric-entry phenomenon -- with no atmosphere left in space-
  // view mode, there's nothing for one to burn up in, so this stays
  // gated by the real sky_is_dark (not sky_dark_for_bodies) and is
  // additionally suppressed outright in that mode.
  bool meteors_visible = sky_is_dark && d->meteor_intensity > 0 && d->sky_mode != 2;
  GPoint meteor_label_point = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + 40);
  if (meteors_visible) {
    weather_layer_draw_meteors(ctx, bounds, d->meteor_intensity);
  }

  int16_t horizon_y = bounds.size.h - GROUND_H;
  bool eclipse_moon_active = d->has_eclipse && now >= d->c1 && now <= d->c4;

  // Once the Sun has actually set, an eclipse in progress on paper
  // (still within c1-c4) has nothing left to show -- without this,
  // the Sun/Moon just silently vanish (body_screen_y correctly hides
  // them past sunset) while the rest of this function kept treating
  // it as an active eclipse (eclipse-sized moon_r, etc.), an
  // inconsistent state. Falls back to plain night-sky rendering, same
  // as the "Sun set" branch already used for the countdown text.
  // Analog mode is exempt: its fullscreen Sun is a deliberate
  // dramatic backdrop for the whole eclipse regardless of the real
  // horizon, per the brief.
  if (eclipse_moon_active && d->bottom_style != 1 && d->sunset != 0 && now >= d->sunset) {
    eclipse_moon_active = false;
  }

  // Analog mode (bottom_style == 1) has no bottom bar to make
  // room for and its hands render in a separate always-on-top layer,
  // so during an actual eclipse the Sun can fill the whole canvas as
  // a dramatic background rather than sitting at its normal small,
  // altitude-positioned size -- "basically fullscreen sun," per the
  // brief. Outside of an active eclipse, analog mode renders
  // the sky exactly like digital mode (small Sun/Moon/planets),
  // just stretched across the full screen height since there's no
  // bottom third reserved for anything else.
  bool fullscreen_sun = (d->bottom_style == 1) && eclipse_moon_active;

  int16_t sun_r;
  if (eclipse_moon_active) {
    sun_r = SUN_R_ECLIPSE;
  } else {
    // Sun/Moon size setting only applies outside an active eclipse --
    // the eclipse sizing above (and fullscreen-sun below) is already
    // deliberately chosen and shouldn't be scaled by it.
    uint8_t pct = d->sun_moon_size_pct > 0 ? d->sun_moon_size_pct : 100;
    sun_r = (SUN_R_NORMAL * pct) / 100;
    if (sun_r < 4) sun_r = 4;
  }
  if (fullscreen_sun) {
    // Capped at 95% of whichever screen dimension is smaller, not the
    // larger -- sizing off the larger dimension could make the disc
    // wider than the screen is tall (or vice versa) and clip.
    int16_t min_dim = bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h;
    sun_r = (min_dim * 95) / 200; // 95% diameter == 47.5% radius
  }
  // While an eclipse is actually happening, size the occluding disc
  // by the *real* Moon/Sun radius ratio rather than a fixed size --
  // this is what makes annular (ratio < 100%, a ring of Sun stays
  // visible even at maximum) look visibly different from total
  // (ratio >= 100%, full coverage) instead of both looking identical.
  // Scaled from sun_r itself (not a fixed constant), so this stays
  // correct whether sun_r is its normal eclipse size or the
  // fullscreen-mode size above.
  int16_t moon_r;
  if (eclipse_moon_active) {
    int32_t ratio = d->radius_ratio_pct > 0 ? d->radius_ratio_pct : 100;
    moon_r = (int16_t)(((int32_t)sun_r * ratio) / 100);
    if (moon_r < 4) moon_r = 4; // stay visible even for a very deep annular
  } else {
    uint8_t pct = d->sun_moon_size_pct > 0 ? d->sun_moon_size_pct : 100;
    moon_r = (MOON_R_NORMAL * pct) / 100;
    if (moon_r < 3) moon_r = 3;
  }

  // The sun disc: warm fill, thin outline so it still reads against
  // both bright day blue and dark night navy (fullscreen-sun mode
  // aside -- see SUN_COLOR_SPACE_R's own comment, its fill is a flat
  // space color rather than the altitude-based shift this normally
  // refers to). Positioned by its real altitude while up, but
  // *whether* it's visible at all -- and the animated sink/rise right
  // at the edges -- comes from today's actual sunrise/sunset times
  // rather than the altitude scale (see body_screen_y's comment for
  // why). In fullscreen-sun mode it's simply centered and always "up"
  // -- the whole point is to fill the screen throughout the eclipse
  // regardless of the Sun's real altitude at that moment.
  GPoint sun_center;
  bool sun_up;
  if (fullscreen_sun) {
    sun_center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
    sun_up = true;
  } else {
    int16_t sun_alt_y = alt_to_y(alt, d->sky_scale_max_alt_decideg, bounds.size.h, sun_r);
    int16_t sun_y;
    sun_up = body_screen_y(sun_alt_y, d->sun_rise, d->sun_set, now, horizon_y, sun_r, &sun_y);
    sun_center = GPoint(bounds.size.w / 2, sun_y);
  }
  if (sun_up && !skip_body_paint) {
    // fullscreen-sun (an eclipse's space view, see fullscreen_sun's
    // own comment above) AND ordinary Space view both get the flat
    // space color instead of the normal altitude-based white-to-red
    // shift -- see SUN_COLOR_SPACE_R's own comment for why: Space view
    // is defined as having no atmosphere, and that white-to-red shift
    // models exactly the atmospheric scattering/reddening a real
    // sunset shows, so imitating it here would contradict the mode's
    // own premise. (fullscreen_sun implies sky_mode == 2 already --
    // see its own definition -- so this is really just "sky_mode == 2"
    // written to keep both call sites' reasoning visible together.)
    bool is_space_view = fullscreen_sun || d->sky_mode == 2;
    RGB8 sun_rgb = is_space_view
      ? (RGB8){ SUN_COLOR_SPACE_R, SUN_COLOR_SPACE_G, SUN_COLOR_SPACE_B }
      : sun_color_for_altitude(alt);
    GColor sun_fill = GColorFromRGB(sun_rgb.r, sun_rgb.g, sun_rgb.b);
    // A darker rim in the same hue, rather than a fixed color -- keeps
    // the disc readable against the sky at every altitude without
    // fighting the fill's own white-to-red shift the way a single
    // fixed outline color (this used to always be GColorBulgarianRose)
    // would once the fill itself turned red.
    GColor sun_outline = GColorFromRGB((uint8_t)((uint16_t)sun_rgb.r * 55 / 100),
                                        (uint8_t)((uint16_t)sun_rgb.g * 55 / 100),
                                        (uint8_t)((uint16_t)sun_rgb.b * 55 / 100));
    graphics_context_set_fill_color(ctx, sun_fill);
    graphics_fill_circle(ctx, sun_center, sun_r);
    graphics_context_set_stroke_color(ctx, sun_outline);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, sun_center, sun_r);
  }

  // Tracked across whichever branch actually draws the Moon (eclipse
  // vs. plain night moon), so the shake-to-reveal label logic at the
  // end doesn't need to re-derive its position.
  bool moon_visible = false;
  GPoint moon_center = GPoint(0, 0);

  // The eclipse-occluding moon rides along with the sun's own
  // position (it's defined relative to it), so it naturally inherits
  // the same rise/set motion -- a sunrise/sunset eclipse will show
  // both discs sinking together.
  if (sun_up && eclipse_moon_active) {
    GPoint offset = moon_offset_px(d, now, sun_r, moon_r);
    moon_center = GPoint(sun_center.x + offset.x, sun_center.y + offset.y);
    moon_visible = true;
    if (!skip_body_paint) {
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_fill_circle(ctx, moon_center, moon_r);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_circle(ctx, moon_center, moon_r);
    }
  }

  // Outside of an active eclipse, show the real Moon in its correct
  // phase, rising and setting on its own schedule (its own rise/set
  // times, same time-based visibility logic as the Sun). It's offset
  // to a different sky "column" than the Sun so the two don't
  // collide when both happen to be up at once (which does happen for
  // a few days each month) -- and even then, never closer than one
  // and a half sun-radii, so a daytime Moon can never look like it's
  // occluding the Sun when no eclipse is actually happening.
  if (!eclipse_moon_active) {
    int16_t moon_alt_y = alt_to_y(moon_alt, d->sky_scale_max_alt_decideg, bounds.size.h, moon_r);
    int16_t moon_y;
    bool moon_up = body_screen_y(moon_alt_y, d->moon_rise, d->moon_set, now, horizon_y, moon_r, &moon_y);
    if (moon_up) {
      moon_center = GPoint((bounds.size.w * 2) / 3, moon_y);
      if (sun_up) {
        int32_t min_dist = (sun_r * 3) / 2; // "sun radius and a half"
        moon_center = enforce_min_separation(sun_center, moon_center, min_dist);
      }
      moon_visible = true;
      if (!skip_body_paint) {
        draw_moon_phase(ctx, bounds, moon_center, moon_r, d->moon_phase_pct, d->moon_waxing, GColorWhite);
      }
    }
  }

  // Planets: small, deliberately unobtrusive dots -- real planets are
  // only visible once the sky is properly dark, well after the
  // Sun/civil-twilight glow the Moon can still cut through. Tracked
  // per-planet for the shake-to-reveal labels at the end.
  bool planet_visible[PLANET_COUNT];
  GPoint planet_center[PLANET_COUNT];
  for (int p = 0; p < PLANET_COUNT; p++) {
    planet_visible[p] = false;
    planet_center[p] = GPoint(0, 0);
  }
  if (sky_dark_for_bodies && !suppress_other_bodies_for_eclipse) {
    for (int p = 0; p < PLANET_COUNT; p++) {
      int16_t p_alt = interp_planet_alt_decideg(d, (PlanetId)p, sky_now);
      int16_t p_alt_y = alt_to_y(p_alt, d->sky_scale_max_alt_decideg, bounds.size.h, PLANET_R);
      int16_t p_y;
      bool p_up = body_screen_y(p_alt_y, d->planet_rise[p], d->planet_set[p], now, horizon_y, PLANET_R, &p_y);
      if (!p_up) continue;
      GPoint c = GPoint((bounds.size.w * PLANET_COLUMN_PCT[p]) / 100, p_y);
      planet_visible[p] = true;
      planet_center[p] = c;
      if (!skip_body_paint) {
        if (p == PLANET_SATURN) {
          draw_saturn(ctx, c, d->saturn_ring_open_pct);
        } else {
          graphics_context_set_fill_color(ctx, planet_color((PlanetId)p));
          graphics_fill_circle(ctx, c, PLANET_R);
        }
      }
    }
  }

  // Cache this frame's normal (non-rotated) body positions -- see
  // cached_sun_center's own comment above for why: a Planet-seek
  // cache-blit frame (no full draw this tick) needs a "normal" endpoint
  // for its own position blend without re-running all the rise/set/
  // altitude math above just to get it again.
  state->cached_sun_center = sun_center;
  state->cached_sun_up = sun_up;
  RGB8 cached_sun_rgb = sun_color_for_altitude(alt);
  state->cached_sun_fill_color = GColorFromRGB(cached_sun_rgb.r, cached_sun_rgb.g, cached_sun_rgb.b);
  state->cached_sun_r = sun_r;
  state->cached_moon_r = moon_r;
  state->cached_moon_center = moon_center;
  state->cached_moon_visible = moon_visible;
  for (int p = 0; p < PLANET_COUNT; p++) {
    state->cached_planet_center[p] = planet_center[p];
    state->cached_planet_visible[p] = planet_visible[p];
  }

  // ISS: uses its real azimuth (not a fixed column like the planets,
  // since we actually have it) combined with altitude. Only drawn if
  // enabled, above the horizon, the sky's dark enough, and the
  // snapshot isn't stale -- the position is computed phone-side once
  // per refresh (not continuously propagated on-watch, given how fast
  // the ISS moves), so an old snapshot would be visibly wrong rather
  // than just slightly dated, hence the 15-minute cutoff. This doesn't
  // account for the ISS itself needing to be sunlit while the
  // observer's sky is dark (real naked-eye passes need both) -- that
  // needs proper Earth-shadow geometry this simplified model doesn't
  // attempt, so it can occasionally show the ISS when it wouldn't
  // really be visible.
  bool iss_visible = false;
  GPoint iss_center = GPoint(0, 0);
  if (d->show_iss && sky_dark_for_bodies && d->iss_alt_deg > 0 && d->iss_computed_at != 0 && !skip_body_paint
      && !suppress_other_bodies_for_eclipse) {
    time_t iss_age = now - d->iss_computed_at;
    if (iss_age >= 0 && iss_age < 900) {
      int16_t iss_alt_decideg = d->iss_alt_deg * 10;
      int16_t iss_y = alt_to_y(iss_alt_decideg, d->sky_scale_max_alt_decideg, bounds.size.h, ISS_R);
      int16_t iss_x = (bounds.size.w * (int32_t)d->iss_az_deg) / 360;
      iss_center = GPoint(iss_x, iss_y);
      iss_visible = true;
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, iss_center, ISS_R);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_circle(ctx, iss_center, ISS_R);
    }
  }
  // Cached (independent of skip_body_paint above) so Planet seek's own
  // overlay pass -- which runs instead of, not alongside, the plain
  // dot just drawn -- can compass-track the ISS the same way it
  // already does the Sun/Moon/planets, rather than the ISS being left
  // out and just disappearing for the duration. Also suppressed during
  // an active eclipse in Space view, same as the plain draw above --
  // Planet seek can never actually be running then anyway (shake
  // animations are disabled for the duration, see maybe_start_shake_
  // animation()), but keeping the cache in sync avoids a stale visible
  // ISS position lingering in state if that ever changes.
  state->cached_iss_visible = d->show_iss && sky_dark_for_bodies && d->iss_alt_deg > 0 && d->iss_computed_at != 0
    && (now - d->iss_computed_at) >= 0 && (now - d->iss_computed_at) < 900 && !suppress_other_bodies_for_eclipse;
  if (state->cached_iss_visible) {
    int16_t iss_alt_decideg = d->iss_alt_deg * 10;
    int16_t iss_y = alt_to_y(iss_alt_decideg, d->sky_scale_max_alt_decideg, bounds.size.h, ISS_R);
    int16_t iss_x = (bounds.size.w * (int32_t)d->iss_az_deg) / 360;
    state->cached_iss_center = GPoint(iss_x, iss_y);
  }

  // Aurora: dark sky, opted in, and the current Kp index plausibly
  // reaches this latitude (see eclipse_data.h's aurora_visibility_pct
  // comment) -- an atmospheric phenomenon like clouds, so it's absent
  // in Space view (sky_mode == 2, "no atmosphere") same as meteors,
  // but unlike clouds/weather it's NOT gated by weather_enabled --
  // Clear sky mode still shows it (arguably the more realistic
  // combination: clear skies are exactly when aurora is best seen).
  // Threshold of 15 (not >0) avoids drawing a barely-there glow for a
  // visibility estimate this approximate.
  bool aurora_visible = d->sky_mode != 2 && d->aurora_enabled && sky_is_dark && d->aurora_visibility_pct > 15;
  GPoint aurora_label_point = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + SKY_TOP_MARGIN + 20);
  if (aurora_visible) {
    weather_layer_draw_aurora(ctx, bounds, d->aurora_visibility_pct, d->aurora_kp_x10);
  }

  // Cloud clusters, drawn last so they visibly sit in front of (and
  // can partially obscure) the sun/moon, same as real clouds. Skipped
  // entirely outside Weather sky mode -- Clear sky and Space view
  // both represent weather-free skies by definition -- and, per
  // request, during Planet seek too (its own separate reason: weather
  // is suppressed for the whole animation, not just this one frame,
  // so it doesn't get baked into the bodies-free cache Planet seek
  // reuses every frame -- see cached_sun_center's own comment above).
  if (weather_enabled && !state->planet_seek_active) {
    weather_layer_draw_clouds(ctx, bounds, cloud_pct, d->cloud_altitude_pct, d->vis_score_pct, stormy,
                              sun_center, sun_up, flash_currently_active, alt,
                              cached_sun_rgb.r, cached_sun_rgb.g, cached_sun_rgb.b);
    weather_layer_draw_effect(ctx, bounds, d->weather_condition, cloud_pct, d->cloud_altitude_pct);
  }

  // Shake-to-reveal: brief name labels next to whichever bodies are
  // actually on screen right now. Needs its own main_color -- the
  // scheme lookup below is otherwise only computed further down,
  // scoped to the big-analog marker-drawing block, and shake labels
  // apply in every mode.
  if (state->show_labels) {
    GColor label_bg, label_main_color, label_accent;
    get_active_color_scheme(d, now, &label_bg, &label_main_color, &label_accent);
    // Sun/Moon/planets/stars/ISS only get their plain static label
    // OUTSIDE Planet seek -- during it, draw_planet_seek_overlay()
    // (called separately, above) already drew each one its own live,
    // compass-tracked label, so drawing this fixed one too would lay
    // a second, non-moving label right on top of it. Aurora and
    // meteor showers below have no planet-seek equivalent of their
    // own (they're diffuse/wide-area sky elements, not a single
    // point-like body), so they keep revealing at their normal fixed
    // position regardless of mode -- per the request, they shouldn't
    // be panned around by the compass the way a point body is.
    if (!state->planet_seek_active) {
      if (sun_up) draw_label(ctx, bounds, sun_center, "Sun", d->label_style, label_main_color);
      if (moon_visible) draw_label(ctx, bounds, moon_center, "Moon", d->label_style, label_main_color);
      for (int p = 0; p < PLANET_COUNT; p++) {
        if (planet_visible[p]) {
          // Re-drawn on top of the clouds above -- a planet's tiny 3px
          // dot can otherwise get almost entirely obscured by cloud
          // cover, leaving its shake label pointing at nothing visible.
          if (p == PLANET_SATURN) {
            draw_saturn(ctx, planet_center[p], d->saturn_ring_open_pct);
          } else {
            graphics_context_set_fill_color(ctx, planet_color((PlanetId)p));
            graphics_fill_circle(ctx, planet_center[p], PLANET_R);
          }
          draw_label(ctx, bounds, planet_center[p], PLANET_NAMES[p], d->label_style, label_main_color);
        }
      }
      for (int s = 0; s < STAR_COUNT; s++) {
        if (star_visible[s]) draw_label(ctx, bounds, star_center[s], STAR_NAMES[s], d->label_style, label_main_color);
      }
      if (iss_visible) draw_label(ctx, bounds, iss_center, "ISS", d->label_style, label_main_color);
    }
    if (meteors_visible) draw_label(ctx, bounds, meteor_label_point,
                                     d->meteor_shower_name[0] != '\0' ? d->meteor_shower_name : "Meteors",
                                     d->label_style, label_main_color);
    if (aurora_visible) {
      static char aurora_label_buf[16];
      snprintf(aurora_label_buf, sizeof(aurora_label_buf), "Aurora Kp %d.%d", d->aurora_kp_x10 / 10, d->aurora_kp_x10 % 10);
      draw_label(ctx, bounds, aurora_label_point, aurora_label_buf, d->label_style, label_main_color);
    }
  }

  // Hour/second markers -- analog mode only. Drawn on top of
  // everything above (sky, sun/moon, clouds, labels) so they stay
  // visible over any part of the sky -- matching hands_layer_update_proc's
  // own positioning, which always uses the full unobstructed screen, so
  // markers and hands stay aligned with each other.
  // skip_marker_paint: this frame's markers get drawn afterward
  // instead, by draw_bg_anim_markers_overlay(), so the cache stays
  // marker-free for every subsequent cheap frame to blit and overlay
  // onto -- same "skip it here, draw it fresh as an overlay after"
  // trick skip_body_paint uses above for bg_anim_mode 1's bodies.
  if (d->bottom_style == 1 && !skip_marker_paint) {
    GColor bg, main_color, accent_color;
    get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
    marker_layer_draw(ctx, &state->markers, center, bounds, d, main_color, accent_color, bg, false, 0, d->draw_debug);
  }

  // Cache what was just drawn: capture the real framebuffer (this is
  // only valid to call inside an update_proc, and only reflects
  // drawing already issued against ctx, which is why this comes last)
  // and copy our canvas's region of it into sky_cache row by row, so
  // the next up-to-59 seconds can cheaply blit it back instead of
  // redoing all of the above. Works because this canvas is anchored
  // at the window's top-left, so its local bounds and its position in
  // the full-screen framebuffer coincide.
  if (state->sky_cache) {
    GBitmap *fb = graphics_capture_frame_buffer(ctx);
    if (fb) {
      uint8_t *src_base = gbitmap_get_data(fb);
      uint16_t src_stride = gbitmap_get_bytes_per_row(fb);
      GRect fb_bounds = gbitmap_get_bounds(fb);
      uint8_t *dst_base = gbitmap_get_data(state->sky_cache);
      uint16_t dst_stride = gbitmap_get_bytes_per_row(state->sky_cache);

      int16_t copy_w = bounds.size.w;
      int16_t copy_h = bounds.size.h;
      if (bounds.origin.x + copy_w > fb_bounds.size.w) copy_w = fb_bounds.size.w - bounds.origin.x;
      if (bounds.origin.y + copy_h > fb_bounds.size.h) copy_h = fb_bounds.size.h - bounds.origin.y;

      for (int16_t y = 0; y < copy_h; y++) {
        uint8_t *src_row = src_base + (bounds.origin.y + y) * src_stride + bounds.origin.x;
        uint8_t *dst_row = dst_base + y * dst_stride;
        memcpy(dst_row, src_row, copy_w);
      }

      graphics_release_frame_buffer(ctx, fb);
    }
  }

  if (state->planet_seek_active) {
    GColor bg, main_color, accent_color;
    get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
    draw_planet_seek_overlay(ctx, state, d, bounds, now, planet_seek_eased_t_1000(state, d), main_color);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 2) {
    draw_bg_anim_markers_overlay(ctx, state, d, bounds, now);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 1) {
    draw_bg_anim_planets_overlay(ctx, state, d, bounds);
  }
}

Layer *eclipse_canvas_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(CanvasState));
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->data = NULL;
  state->show_labels = false;
  state->last_full_draw = 0;
  state->force_next_draw = true; // always draw the first time
  state->last_eclipse_phase = -1; // sentinel: guaranteed to differ from compute_eclipse_phase()'s 0-5
  state->last_eclipse_max = 0;
  state->max_vibrated = false;
  state->last_iss_visible = false;
  state->cached_iss_visible = false;
  // GBitmapFormat8Bit matches the framebuffer's own pixel format on
  // color platforms (emery included), so the row-by-row memcpy in
  // canvas_update_proc's capture step needs no per-pixel conversion.
  state->sky_cache = gbitmap_create_blank(frame.size, GBitmapFormat8Bit);
  marker_layer_init(&state->markers);
  layer_set_update_proc(layer, canvas_update_proc);
  return layer;
}

void eclipse_canvas_destroy(Layer *layer) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  if (state->sky_cache) {
    gbitmap_destroy(state->sky_cache);
    state->sky_cache = NULL;
  }
  marker_layer_deinit(&state->markers);
  layer_destroy(layer);
}

void eclipse_canvas_set_data(Layer *layer, EclipseData *data) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->data = data;
  state->force_next_draw = true;
  layer_mark_dirty(layer);
}

void eclipse_canvas_set_show_labels(Layer *layer, bool show) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->show_labels = show;
  state->force_next_draw = true;
  layer_mark_dirty(layer);
}

void eclipse_canvas_set_bg_anim(Layer *layer, bool active, uint16_t elapsed_ms) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  bool was_active = state->bg_anim_active;
  state->bg_anim_active = active;
  state->bg_anim_elapsed_ms = elapsed_ms;
  // Mode 2 ("Markers") only ever animates its own overlaid element
  // (see draw_bg_anim_markers_overlay()'s own comment) on top of an
  // otherwise-unchanging backdrop -- same "full draw once on entering/
  // leaving the mode, cheap overlay every frame in between" shape
  // eclipse_canvas_set_planet_seek() below already uses -- so only the
  // active/inactive TRANSITION needs a genuine full redraw, not every
  // single frame. Mode 1 ("Planets") is different: its sky_now
  // substitution (see canvas_update_proc's own comment) means the
  // gradient/Sun color itself keeps changing throughout the sweep, not
  // just body position, so it still needs a real full redraw every
  // frame -- forced unconditionally here whenever it's the active
  // mode, same as every mode used to do. state->data may not be set
  // yet the very first time this is ever called (app launch, before
  // the first eclipse_canvas_set_data()); forcing in that case too is
  // the safe default.
  uint8_t mode = state->data ? state->data->bg_anim_mode : 1;
  if (mode == 1 || active != was_active) state->force_next_draw = true;
  layer_mark_dirty(layer);
}

// Deliberately does NOT force a full redraw the way eclipse_canvas_set_
// bg_anim() above does -- Planet seek's whole point is to redraw ONLY
// the repositioned bodies each frame on top of a cached backdrop
// (see canvas_update_proc's own "Planet seek" section), so forcing
// the full, expensive gradient+clouds+markers pipeline on every single
// 33ms tick would defeat that entirely. Just updates state and marks
// the layer dirty so canvas_update_proc runs -- it decides for itself
// whether that means a full redraw or the lightweight Planet-seek path.
void eclipse_canvas_set_planet_seek(Layer *layer, bool active, uint16_t elapsed_ms, int32_t heading_deg) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  bool was_active = state->planet_seek_active;
  state->planet_seek_active = active;
  state->planet_seek_elapsed_ms = elapsed_ms;
  state->planet_seek_heading_deg = heading_deg;
  if (active != was_active) state->force_next_draw = true; // the one moment it DOES need a full draw: entering/leaving the mode, so the cache/backdrop itself gets refreshed at the right moment
  layer_mark_dirty(layer);
}

// Lightweight per-second nudge: marks the layer dirty (so the OS
// invokes canvas_update_proc), but does NOT force a redraw -- the
// canvas's own once-a-minute throttle inside canvas_update_proc
// decides whether anything actually gets recomputed. Safe to call
// every second without it costing a full redraw every time.
void eclipse_canvas_tick(Layer *layer) {
  layer_mark_dirty(layer);
}

// ---- Digital top's own gradient-only strip ------------------------------
// See this pair's own declaration comment in background_layer.h for
// the "why a separate tiny module instead of another eclipse_canvas_
// create() frame" reasoning. Layer-local state is just the EclipseData
// pointer -- no cache, no animation/tick bookkeeping, nothing else this
// needs to remember between redraws (compute_sky_wash() is cheap
// enough -- a handful of lerps, no per-pixel work of its own -- to just
// re-run in full on every call rather than caching its own result).
typedef struct {
  EclipseData *data;
} TopGradientState;

static void top_gradient_update_proc(Layer *layer, GContext *ctx) {
  TopGradientState *state = (TopGradientState *)layer_get_data(layer);
  EclipseData *d = state->data;
  GRect bounds = layer_get_bounds(layer);
  if (!d) return;

  if (d->sky_mode == 2) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    return;
  }

  RGB8 top, band, hz;
  int16_t band_y;
  bool flat_black;
  // virtual_top_y 0 / virtual_total_h 228 -- this strip is always the
  // TOP slice of one conceptual gradient spanning the app's fixed
  // 200x228 screen (see the marker ring's own `GRect screen = GRect(0,
  // 0, 200, 228)` in features_layer.c for the same fixed-screen-size
  // convention elsewhere), the sky canvas's own bottom slice
  // continuing it from DIGITAL_PANEL_H down to 228 (see
  // canvas_update_proc()'s own matching virtual_top_y/virtual_total_h
  // for that other end) -- the two are laid out edge-to-edge with no
  // gap, so together they cover the whole thing exactly once.
  compute_sky_wash(d, time(NULL), 0, 228, &top, &band, &band_y, &hz, &flat_black);
  if (flat_black) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    return;
  }
  fill_sky_gradient_ex(ctx, bounds, 0, 228, top, band, band_y, hz);
}

Layer *eclipse_top_gradient_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(TopGradientState));
  TopGradientState *state = (TopGradientState *)layer_get_data(layer);
  state->data = NULL;
  layer_set_update_proc(layer, top_gradient_update_proc);
  return layer;
}

void eclipse_top_gradient_destroy(Layer *layer) {
  layer_destroy(layer);
}

void eclipse_top_gradient_set_data(Layer *layer, EclipseData *data) {
  TopGradientState *state = (TopGradientState *)layer_get_data(layer);
  state->data = data;
  layer_mark_dirty(layer);
}

// Cheap (no redraw needed) check for whether the sky is currently
// bright enough that the overlaid countdown label should use dark
// text instead of light -- lets that label stay legible over the
// gradient without needing to redraw the whole canvas every second.
bool eclipse_sky_is_bright(const EclipseData *d, time_t now) {
  if (!d->valid || d->sky_sample_count == 0) return true;
  int16_t alt = interp_sun_alt_decideg(d, now);
  return alt > -60; // still light through civil twilight
}

// True from first contact up to (not including) last contact -- the
// same c1/c4 window eclipse_get_status_text() itself uses to decide
// PHASE_BEFORE_C1 vs PHASE_DONE, exposed here so other call sites
// (shake/startup animation gating, the tick-rate override, Space
// view's celestial-body suppression) can all agree on exactly the
// same definition of "the eclipse is happening right now" instead of
// each re-deriving their own slightly different c1/c4 comparison.
bool eclipse_is_active(const EclipseData *d, time_t now) {
  return d->valid && d->has_eclipse && now >= d->c1 && now < d->c4;
}

// ---- status text -------------------------------------------------------

static void fmt_countdown(char *buf, size_t buf_len, const char *label, time_t target, time_t now, bool minutes_only) {
  time_t remaining = target - now;
  if (remaining < 0) remaining = 0;
  int h = (int)(remaining / 3600);
  int m = (int)((remaining % 3600) / 60);
  int s = (int)(remaining % 60);
  if (h > 0) {
    snprintf(buf, buf_len, "%s %dh%02dm", label, h, m);
  } else if (minutes_only) {
    snprintf(buf, buf_len, "%s %dm", label, m);
  } else {
    snprintf(buf, buf_len, "%s %d:%02d", label, m, s);
  }
}

// Compact enough to fit the corners overlay's box width alongside an
// icon -- the old full names ("Waxing Gibbous") were fine for a
// full-width countdown line but don't fit there. Same thresholds as
// before, just shorter labels.
const char *moon_phase_short_name(uint8_t pct, bool waxing) {
  if (pct <= 2) return "New";
  if (pct >= 98) return "Full";
  if (waxing) {
    if (pct < 48) return "WxCr";
    if (pct <= 52) return "1stQ";
    return "WxGb";
  }
  if (pct > 52) return "WnGb";
  if (pct >= 48) return "3rdQ";
  return "WnCr";
}

EclipsePhase eclipse_get_status_text(const EclipseData *d, time_t now, char *buf, size_t buf_len, bool live_seconds) {
  if (!d->valid) {
    if (d->error_code == 1) snprintf(buf, buf_len, "No location");
    else if (d->error_code == 2) snprintf(buf, buf_len, "Calc error");
    else if (d->error_code == 3) snprintf(buf, buf_len, "Send failed");
    else snprintf(buf, buf_len, "No data yet");
    return PHASE_NO_ECLIPSE;
  }
  // This field is purely about eclipse phases now -- Moon phase has
  // its own corner content type instead (see corners_layer_update_proc
  // in pebble-eclipse-watch.c). No eclipse today (or today's has
  // already finished) means nothing to report here at all; the caller
  // hides the whole text layer in that case rather than leaving it
  // visible-but-blank.
  if (!d->has_eclipse) {
    buf[0] = '\0';
    return PHASE_NO_ECLIPSE;
  }
  if (d->sunset != 0 && now >= d->sunset && now < d->c4) {
    snprintf(buf, buf_len, "Sun set");
    return PHASE_NIGHT;
  }
  if (now < d->c1) {
    // Before the eclipse actually starts, the screen only ticks every
    // second if something ELSE already needs that (show_seconds, a
    // seconds-precision corner content, ...) -- eclipse_is_active()
    // is false out here, so it doesn't itself force a live-seconds
    // subscription (see update_tick_subscription()'s own comment).
    // A seconds-precision "M:SS" readout would just sit frozen for up
    // to a minute at a time in that case, so this falls back to a
    // plain whole-minutes (+hours past 59m) countdown instead, matching
    // however often this label is actually being redrawn.
    fmt_countdown(buf, buf_len, "Starts in", d->c1, now, !live_seconds);
    return PHASE_BEFORE_C1;
  }
  if (now >= d->c4) {
    buf[0] = '\0';
    return PHASE_DONE;
  }

  // From here on an eclipse is actively in progress (between C1 and
  // C4) -- prefix whichever phase label applies with the live "% of
  // Sun covered" so both are visible on the one line. eclipse_is_active()
  // is true for this whole stretch, which by itself now forces a live-
  // seconds subscription (see update_tick_subscription()), so these can
  // always afford full M:SS precision.
  char phase_buf[24];
  EclipsePhase phase;
  if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now < d->c2) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality in", d->c2, now, false);
    phase = PHASE_PARTIAL_IN;
  } else if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now >= d->c2 && now < d->c3) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality ends", d->c3, now, false);
    phase = PHASE_TOTAL;
  } else if (now < d->max_t && (d->type == ECLIPSE_TYPE_PARTIAL || d->c2 == 0)) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Peak in", d->max_t, now, false);
    phase = PHASE_PARTIAL_IN;
  } else {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Clears in", d->c4, now, false);
    phase = PHASE_PARTIAL_OUT;
  }

  snprintf(buf, buf_len, "%d%% - %s", interp_mag_pct(d, now), phase_buf);
  return phase;
}
