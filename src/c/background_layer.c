#include "background_layer.h"
#include "subpixel.h"
#include "features_layer.h"

// ---------------------------------------------------------------------------
// Markers merged in (formerly marker_layer.c): the hour/second marker ring,
// its optional text numerals, and the tinted bitmap marker styles all now
// draw directly into canvas_update_proc()'s own live GContext, right before
// the frame gets captured into sky_cache below -- so they're part of the
// SAME cached bitmap the sky/sun/moon already use, not a second one.
//
// This also means the marker ring no longer needs its own bitmap cache or
// manual pixel rasterizer: an earlier, standalone version of this code had
// to rasterize into an offscreen GBitmapFormat8Bit buffer by hand (writing
// raw bytes) because Pebble's SDK has no way to get a GContext for an
// arbitrary offscreen bitmap outside a LayerUpdateProc. Now that the ring
// draws inside THIS layer's own update_proc, that restriction doesn't
// apply -- it uses plain gpath_draw_filled()/graphics_fill_circle() calls,
// same as the rest of this file (and hand_layer.c), and rides along on
// this canvas's existing once-a-minute throttle (see need_full_draw below)
// instead of maintaining a separate change-detection cache of its own.
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
  GBitmap *sky_cache;       // last full render, captured via graphics_capture_frame_buffer;
                             // blitted back on the seconds in between instead of leaving
                             // the screen untouched (which is what caused flicker -- Pebble
                             // doesn't guarantee framebuffer content persists between
                             // update_proc invocations, so "just don't draw" isn't safe)

  // Bitmap marker styles (big_analog_marker_style 3-7) -- moved in from
  // pebble-eclipse-watch.c along with the rest of marker drawing, so the
  // tint+blend happens once per full redraw here rather than being
  // re-blended into the live framebuffer on every single tick the way it
  // used to be (hands_layer_update_proc ran it every call).
  GBitmap *marker_bitmap;
  uint8_t marker_bitmap_style;         // 255 = none loaded
  bool marker_bitmap_tinted;            // has tint_marker_bitmap() run since the last (re)load?
  GColor marker_bitmap_tint_color;
  bool marker_bitmap_tint_transparent;

  // Custom text-marker numerals' font (big_analog_marker_style == 8) --
  // same lazy load/unload lifecycle as the corner text font in
  // pebble-eclipse-watch.c, just scoped to this layer instead of file-static.
  GFont marker_text_font;
  uint8_t marker_text_font_loaded_choice; // 255 = none loaded
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

// Linearly interpolate the transmitted separation-sample array to get
// the sun/moon angular gap (in hundredths of a degree) at time `t`.
// Samples run from data->sample_start in steps of sample_interval_s.
static uint16_t interp_separation_centideg(const EclipseData *d, time_t t) {
  if (d->sample_count == 0) return 0;
  if (d->sample_interval_s == 0) return d->sep_samples_centideg[0];

  int64_t offset_s = (int64_t)t - (int64_t)d->sample_start;
  int64_t idx_f = offset_s / (int64_t)d->sample_interval_s;

  if (idx_f <= 0) return d->sep_samples_centideg[0];
  if (idx_f >= d->sample_count - 1) return d->sep_samples_centideg[d->sample_count - 1];

  int idx = (int)idx_f;
  time_t t0 = d->sample_start + idx * (time_t)d->sample_interval_s;
  int32_t frac_num = (int32_t)(t - t0);
  int32_t frac_den = (int32_t)d->sample_interval_s;
  if (frac_den <= 0) frac_den = 1;

  uint16_t a = d->sep_samples_centideg[idx];
  uint16_t b = d->sep_samples_centideg[idx + 1];
  int32_t delta = (int32_t)b - (int32_t)a;
  return (uint16_t)(a + (delta * frac_num) / frac_den);
}

// Same grid, same interpolation, for the live "% of Sun covered"
// samples -- lets the countdown line show a running percentage
// rather than only the fixed peak magnitude.
static uint8_t interp_mag_pct(const EclipseData *d, time_t t) {
  if (d->sample_count == 0) return 0;
  if (d->sample_interval_s == 0) return d->mag_pct_samples[0];

  int64_t offset_s = (int64_t)t - (int64_t)d->sample_start;
  int64_t idx_f = offset_s / (int64_t)d->sample_interval_s;

  if (idx_f <= 0) return d->mag_pct_samples[0];
  if (idx_f >= d->sample_count - 1) return d->mag_pct_samples[d->sample_count - 1];

  int idx = (int)idx_f;
  time_t t0 = d->sample_start + idx * (time_t)d->sample_interval_s;
  int32_t frac_num = (int32_t)(t - t0);
  int32_t frac_den = (int32_t)d->sample_interval_s;
  if (frac_den <= 0) frac_den = 1;

  int32_t a = d->mag_pct_samples[idx];
  int32_t b = d->mag_pct_samples[idx + 1];
  return (uint8_t)(a + ((b - a) * frac_num) / frac_den);
}

// Same idea, for the full-day sun-altitude samples (tenths of a
// degree, signed -- negative once the sun is below the horizon).
static int16_t interp_sun_alt_decideg(const EclipseData *d, time_t t) {
  if (d->sky_sample_count == 0) return -900; // treat "no data" as deep night
  if (d->sky_sample_interval_s == 0) return d->sun_alt_decideg[0];

  int64_t offset_s = (int64_t)t - (int64_t)d->sky_sample_start;
  int64_t idx_f = offset_s / (int64_t)d->sky_sample_interval_s;

  if (idx_f <= 0) return d->sun_alt_decideg[0];
  if (idx_f >= d->sky_sample_count - 1) return d->sun_alt_decideg[d->sky_sample_count - 1];

  int idx = (int)idx_f;
  time_t t0 = d->sky_sample_start + idx * (time_t)d->sky_sample_interval_s;
  int32_t frac_num = (int32_t)(t - t0);
  int32_t frac_den = (int32_t)d->sky_sample_interval_s;
  if (frac_den <= 0) frac_den = 1;

  int32_t a = d->sun_alt_decideg[idx];
  int32_t b = d->sun_alt_decideg[idx + 1];
  return (int16_t)(a + ((b - a) * frac_num) / frac_den);
}

// Same idea again, for the full-day moon-altitude samples (same grid
// as the sun, used for the night moon's own rise/set animation).
static int16_t interp_moon_alt_decideg(const EclipseData *d, time_t t) {
  if (d->sky_sample_count == 0) return -900;
  if (d->sky_sample_interval_s == 0) return d->moon_alt_decideg[0];

  int64_t offset_s = (int64_t)t - (int64_t)d->sky_sample_start;
  int64_t idx_f = offset_s / (int64_t)d->sky_sample_interval_s;

  if (idx_f <= 0) return d->moon_alt_decideg[0];
  if (idx_f >= d->sky_sample_count - 1) return d->moon_alt_decideg[d->sky_sample_count - 1];

  int idx = (int)idx_f;
  time_t t0 = d->sky_sample_start + idx * (time_t)d->sky_sample_interval_s;
  int32_t frac_num = (int32_t)(t - t0);
  int32_t frac_den = (int32_t)d->sky_sample_interval_s;
  if (frac_den <= 0) frac_den = 1;

  int32_t a = d->moon_alt_decideg[idx];
  int32_t b = d->moon_alt_decideg[idx + 1];
  return (int16_t)(a + ((b - a) * frac_num) / frac_den);
}

// Same idea, generic over any planet slot (see PlanetId) rather than
// one duplicated function per planet.
static int16_t interp_planet_alt_decideg(const EclipseData *d, PlanetId planet, time_t t) {
  const int16_t *samples = d->planet_alt_decideg[planet];
  if (d->sky_sample_count == 0) return -900;
  if (d->sky_sample_interval_s == 0) return samples[0];

  int64_t offset_s = (int64_t)t - (int64_t)d->sky_sample_start;
  int64_t idx_f = offset_s / (int64_t)d->sky_sample_interval_s;

  if (idx_f <= 0) return samples[0];
  if (idx_f >= d->sky_sample_count - 1) return samples[d->sky_sample_count - 1];

  int idx = (int)idx_f;
  time_t t0 = d->sky_sample_start + idx * (time_t)d->sky_sample_interval_s;
  int32_t frac_num = (int32_t)(t - t0);
  int32_t frac_den = (int32_t)d->sky_sample_interval_s;
  if (frac_den <= 0) frac_den = 1;

  int32_t a = samples[idx];
  int32_t b = samples[idx + 1];
  return (int16_t)(a + ((b - a) * frac_num) / frac_den);
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
  if (d->sky_sample_count == 0) return 0;
  if (d->sky_sample_interval_s == 0) return d->cloud_pct_samples[0];

  int64_t offset_s = (int64_t)t - (int64_t)d->sky_sample_start;
  int64_t idx_f = offset_s / (int64_t)d->sky_sample_interval_s;

  if (idx_f <= 0) return d->cloud_pct_samples[0];
  if (idx_f >= d->sky_sample_count - 1) return d->cloud_pct_samples[d->sky_sample_count - 1];

  int idx = (int)idx_f;
  time_t t0 = d->sky_sample_start + idx * (time_t)d->sky_sample_interval_s;
  int32_t frac_num = (int32_t)(t - t0);
  int32_t frac_den = (int32_t)d->sky_sample_interval_s;
  if (frac_den <= 0) frac_den = 1;

  int32_t a = d->cloud_pct_samples[idx];
  int32_t b = d->cloud_pct_samples[idx + 1];
  return (uint8_t)(a + ((b - a) * frac_num) / frac_den);
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

// Ordered-dithers one 8-bit channel down to Pebble's 2-bit-per-channel
// palette (levels 0/85/170/255). Rather than just rounding to the
// nearest of those four (which is what produces flat horizontal
// bands), each pixel compares its fractional position between the
// two bracketing levels against a Bayer threshold, so neighbouring
// pixels alternate between the two levels in proportion to how close
// the true value is to each -- the classic ordered-dither trick for
// getting a smooth-looking gradient out of a small palette.
static uint8_t dither_channel(uint8_t continuous_255, uint8_t bayer_0_15) {
  int32_t scaled = (int32_t)continuous_255 * 3;      // 0..765 (3 steps between 4 levels)
  int32_t level = scaled / 255;                        // integer level 0..3 (floor)
  int32_t rem = scaled - level * 255;                   // 0..254, position within this step
  int32_t threshold = (bayer_0_15 * 255) / 16;
  if (rem > threshold && level < 3) level++;
  return (uint8_t)level;
}

static GColor dither_pixel(RGB8 c, uint8_t bayer_0_15) {
  return GColorFromRGB(
    dither_channel(c.r, bayer_0_15) * 85,
    dither_channel(c.g, bayer_0_15) * 85,
    dither_channel(c.b, bayer_0_15) * 85);
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

// Fills `bounds` with a dithered vertical gradient from `top` (row 0)
// down to `hz` (last row), optionally kinking through a third `band`
// color at `band_y` along the way -- this is how overcast/rainy
// conditions show up as a grayer lower sky, like the view crossing
// beneath a cloud deck seen from a plane window, rather than the
// whole sky stretching through unbroken blue regardless of weather.
// Pass band_y at or past the bottom row (and band == hz) to skip the
// effect entirely and get a plain two-point gradient, same as before.
// Each row's true continuous colour is computed first, then every
// pixel in that row is ordered-dithered down to the palette
// individually -- that's what turns hard colour bands into a
// smooth-looking blend on real hardware. Row colours only depend on
// y, so the per-row RGB lerp happens once; only the 4 possible x-phases
// of the Bayer matrix are then dithered and cached before sweeping
// across the row, to avoid redoing that work per pixel.
static void fill_sky_gradient(GContext *ctx, GRect bounds, RGB8 top, RGB8 band, int16_t band_y, RGB8 hz) {
  int16_t bottom_y = bounds.origin.y + bounds.size.h - 1;
  if (band_y > bottom_y) band_y = bottom_y;
  if (band_y < bounds.origin.y) band_y = bounds.origin.y;
  int16_t upper_span = band_y - bounds.origin.y;
  int16_t lower_span = bottom_y - band_y;

  for (int16_t y = 0; y < bounds.size.h; y++) {
    int16_t screen_y = bounds.origin.y + y;
    RGB8 row;
    if (screen_y <= band_y) {
      row.r = lerp8(top.r, band.r, y, upper_span > 0 ? upper_span : 1);
      row.g = lerp8(top.g, band.g, y, upper_span > 0 ? upper_span : 1);
      row.b = lerp8(top.b, band.b, y, upper_span > 0 ? upper_span : 1);
    } else {
      int16_t rel = screen_y - band_y;
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

// isqrt32 is defined further down (see its own comment there for
// why this avoids libm) -- forward-declared here since the cloud
// lighting code below needs it to normalize the sun-direction vector,
// ahead of where the rest of the file happens to define it.
static uint16_t isqrt32(int32_t v);

// Each cloud mass is a continuous procedural field (a "metaball" --
// each seed point contributes a soft, bounded falloff blob, and
// overlapping seeds' contributions add together) rather than a
// handful of discrete circles, so the silhouette merges into one
// organic, irregular shape with soft edges instead of visibly
// separate blobs. Seed positions/radii are deliberately irregular
// (not a grid or ring) so the summed field reads as an actual cloud
// mass. `scale_pct` scales every seed uniformly to grow/shrink the
// whole mass with coverage.
typedef struct {
  int16_t dx, dy;
  int16_t r;
} CloudSeed;

static const CloudSeed CLOUD_SEEDS[13] = {
  { -24,  8, 13 }, { -14, 14, 11 }, {  -2, 15, 12 }, {  11, 13, 10 }, {  23,  7, 11 },
  { -17, -2, 14 }, {  -3, -5, 16 }, {  14, -1, 13 },
  {  -9,-15,  9 }, {   5,-17, 10 }, {  18, -9,  8 },
  { -26, -6,  8 }, {  27, -3,  8 },
};
#define CLOUD_SEED_COUNT 13
#define CLOUD_FIELD_THRESHOLD 400

// Sum of each seed's smooth falloff contribution at (px, py), offset
// from the cluster's own center. Each seed contributes
// max(0, 1-(d/r)^2)^2 (scaled to a ~0-1000 range) -- bounded and
// well-behaved close to the seed's own center (unlike a raw inverse-
// square metaball kernel, which blows up there), while still merging
// smoothly with its neighbors: two adjacent seeds' overlap region
// sums well past the threshold even though neither alone would clear
// it there, which is what makes the union read as one continuous
// mass instead of a cluster of separate circles.
static int32_t cloud_field_value(int16_t px, int16_t py, int16_t scale_pct) {
  int32_t field = 0;
  for (int i = 0; i < CLOUD_SEED_COUNT; i++) {
    int16_t sx = (CLOUD_SEEDS[i].dx * scale_pct) / 100;
    int16_t sy = (CLOUD_SEEDS[i].dy * scale_pct) / 100;
    int16_t sr = (CLOUD_SEEDS[i].r * scale_pct) / 100;
    if (sr < 2) sr = 2;
    int32_t dx = px - sx, dy = py - sy;
    int32_t d2 = dx * dx + dy * dy;
    int32_t r2 = (int32_t)sr * sr;
    if (d2 >= r2) continue;
    int32_t frac = ((r2 - d2) * 1000) / r2; // 0..1000, (1 - t^2)*1000
    field += (frac * frac) / 1000;           // ~(1-t^2)^2, still ~0..1000 per seed
  }
  return field;
}

// Up to 4 cloud masses across the band; how many are actually drawn
// scales with coverage (see cloud_cluster_count), each one nudged up
// or down slightly so a multi-cluster sky doesn't look like the same
// shape copy-pasted in a row. Shared with draw_weather_effect so
// rain/snow fall from the same positions the clouds actually occupy.
#define CLOUD_CLUSTER_SLOTS 4
static const int16_t CLUSTER_X_PCT[CLOUD_CLUSTER_SLOTS] = { 18, 45, 68, 88 };
static const int16_t CLUSTER_Y_OFFSET[CLOUD_CLUSTER_SLOTS] = { 0, -6, 4, -3 };

static int cloud_cluster_count(uint8_t cloud_pct, bool stormy) {
  if (stormy) return CLOUD_CLUSTER_SLOTS;
  int count = 1;
  if (cloud_pct > 15) count = 2;
  if (cloud_pct > 45) count = 3;
  if (cloud_pct > 75) count = 4;
  return count;
}

// Warm (sun-facing) / cool (shadow-facing) color pairs -- blended
// per pixel by how directly that point faces the Sun's actual
// on-screen position (see draw_clouds), the way real clouds pick up
// warm light on their sunward side and read cool/blue-gray in their
// own shadow. Thin cloud stays close to white either way; heavier
// cover and storms push both ends darker and more saturated toward
// gray, per the same coverage/storminess logic as before.
// 0 (full daylight brightness) .. 100 (fully night-darkened) -- ramps
// linearly as the Sun sinks from the horizon (alt 0) to -10deg, well
// past the -6deg (-60 decideg) civil-twilight threshold used
// elsewhere for "sky_is_dark", so clouds visibly dim through sunset/
// sunrise rather than popping instantly dark/bright at a threshold.
static uint8_t cloud_night_factor(int16_t sun_alt_decideg) {
  if (sun_alt_decideg >= 0) return 0;
  if (sun_alt_decideg <= -100) return 100;
  return (uint8_t)(((int32_t)(-sun_alt_decideg) * 100) / 100);
}

static void cloud_shading_colors(uint8_t cloud_pct, bool stormy, int16_t sun_alt_decideg, RGB8 *warm, RGB8 *cool) {
  if (stormy) {
    warm->r = 130; warm->g = 122; warm->b = 128;
    cool->r =  42; cool->g =  44; cool->b =  54;
  } else if (cloud_pct > 70) {
    warm->r = 255; warm->g = 236; warm->b = 220;
    cool->r = 150; cool->g = 158; cool->b = 174;
  } else if (cloud_pct > 35) {
    warm->r = 255; warm->g = 242; warm->b = 230;
    cool->r = 190; cool->g = 196; cool->b = 206;
  } else {
    warm->r = 255; warm->g = 250; warm->b = 244;
    cool->r = 222; cool->g = 226; cool->b = 232;
  }

  // These bright/pale colors were the same at any hour, so clouds at
  // night looked identical to a bright overcast afternoon -- clearly
  // wrong against a near-black night sky. Darken both warm and cool
  // toward a dark near-black gray as the Sun sinks, same idea (and
  // same lerp8-toward-a-target-color trick) as the overcast horizon
  // darkening above.
  uint8_t night = cloud_night_factor(sun_alt_decideg);
  if (night > 0) {
    RGB8 night_dark = { 28, 29, 36 };
    warm->r = lerp8(warm->r, night_dark.r, night, 100);
    warm->g = lerp8(warm->g, night_dark.g, night, 100);
    warm->b = lerp8(warm->b, night_dark.b, night, 100);
    cool->r = lerp8(cool->r, night_dark.r, night, 100);
    cool->g = lerp8(cool->g, night_dark.g, night, 100);
    cool->b = lerp8(cool->b, night_dark.b, night, 100);
  }
}

// "Realistic" cloud style -- metaball field with sun-relative
// warm/cool lighting (see cloud_field_value above). More CPU-hungry
// per redraw than the "Simple" style below (per-pixel field
// evaluation across 13 seeds vs. plain circle fills), traded for a
// painterly, organically-shaped result.
//
// `cloud_altitude_pct` biases where in the lower half of the canvas
// the deck sits (see compute_cloud_band_y). Cluster count and puff
// scale both grow with coverage -- a mostly-clear sky shows one
// modest wisp, an overcast one fills the band with several
// overlapping masses -- and low visibility thickens the haze density
// a little further on top of that, since poor visibility in real
// weather usually means denser moisture in the air generally, not
// just more cloud. `sun_center`/`sun_up` drive the per-pixel warm/cool
// lighting: each cluster gets its own light direction toward the
// Sun's actual current position (clusters on either side of it
// naturally end up lit from opposite sides), falling back to a
// flat overhead light when the Sun's below the horizon.
//
// `flash_active` (see canvas_update_proc's storm-flash comment for
// the timing) briefly lights the cloud mass from within -- every
// pixel's blend leans toward white rather than its normal warm/cool
// shading -- and drops a jagged bolt down from the cloud base,
// exactly like a real strike briefly overexposing the clouds around
// it while a thin bright channel reaches the ground.
static void draw_clouds_realistic(GContext *ctx, GRect bounds, uint8_t cloud_pct, uint8_t cloud_altitude_pct,
                         uint8_t visibility_pct, bool stormy, GPoint sun_center, bool sun_up, bool flash_active,
                         int16_t sun_alt_decideg) {
  if (cloud_pct == 0 && !stormy) return;

  RGB8 warm_rgb, cool_rgb;
  cloud_shading_colors(cloud_pct, stormy, sun_alt_decideg, &warm_rgb, &cool_rgb);

  int16_t band_y = compute_cloud_band_y(bounds, cloud_altitude_pct);
  int cluster_count = cloud_cluster_count(cloud_pct, stormy);

  uint8_t density = cloud_pct < 30 ? 30 : cloud_pct; // even thin cloud reads as a real puff, not a ghost
  if (visibility_pct < 70) density += (70 - visibility_pct) / 3; // hazier air thickens the puffs a little further
  if (density > 96) density = 96; // never fully opaque outside real storm cover
  if (stormy && density < 92) density = 92;

  int16_t scale_pct = 70 + (cloud_pct * 60) / 100; // 70%..130% across the coverage range
  if (stormy && scale_pct < 130) scale_pct = 130;

  int16_t half_w = (45 * scale_pct) / 100 + 5;
  int16_t up_h = (35 * scale_pct) / 100 + 5;
  int16_t down_h = (30 * scale_pct) / 100 + 5;

  for (int c = 0; c < cluster_count; c++) {
    int16_t cx = bounds.origin.x + (bounds.size.w * CLUSTER_X_PCT[c]) / 100;
    int16_t cy = band_y + CLUSTER_Y_OFFSET[c];

    // Light direction from this cluster toward the Sun, normalized to
    // ~100 magnitude -- falls back to straight up (Sun below horizon,
    // or degenerate zero-distance case) so lighting stays well-defined
    // at night rather than undefined/erratic.
    int32_t light_dx = 0, light_dy = -100;
    if (sun_up) {
      int32_t to_sun_x = sun_center.x - cx;
      int32_t to_sun_y = sun_center.y - cy;
      int32_t mag = (int32_t)isqrt32(to_sun_x * to_sun_x + to_sun_y * to_sun_y);
      if (mag > 0) {
        light_dx = (to_sun_x * 100) / mag;
        light_dy = (to_sun_y * 100) / mag;
      }
    }

    int16_t x0 = cx - half_w, x1 = cx + half_w;
    int16_t y0 = cy - up_h, y1 = cy + down_h;
    if (x0 < bounds.origin.x) x0 = bounds.origin.x;
    if (y0 < bounds.origin.y) y0 = bounds.origin.y;
    if (x1 >= bounds.origin.x + bounds.size.w) x1 = bounds.origin.x + bounds.size.w - 1;
    if (y1 >= bounds.origin.y + bounds.size.h) y1 = bounds.origin.y + bounds.size.h - 1;

    for (int16_t y = y0; y <= y1; y++) {
      int16_t py = y - cy;
      for (int16_t x = x0; x <= x1; x++) {
        int16_t px = x - cx;
        int32_t field = cloud_field_value(px, py, scale_pct);
        if (field < CLOUD_FIELD_THRESHOLD) continue;

        // Soft edge: pixels just past the threshold get reduced
        // density, full density once well inside -- reads as a soft
        // painted edge rather than a hard silhouette cutoff.
        int32_t edge_pct = ((field - CLOUD_FIELD_THRESHOLD) * 100) / CLOUD_FIELD_THRESHOLD;
        if (edge_pct > 100) edge_pct = 100;
        int32_t local_density = ((int32_t)density * (60 + (edge_pct * 40) / 100)) / 100;
        if (local_density > 100) local_density = 100;

        uint8_t bayer = BAYER4[y & 3][x & 3];
        uint8_t threshold16 = (uint8_t)((local_density * 16) / 100);
        if (bayer >= threshold16) continue; // translucent gap -- sky/Sun/Moon shows through here

        int32_t facing = (px * light_dx + py * light_dy) / 15; // roughly -1000..1000 across a cluster
        if (facing > 1000) facing = 1000;
        if (facing < -1000) facing = -1000;
        int32_t warm_frac = (facing + 1000) / 2; // 0..1000

        RGB8 blend;
        blend.r = lerp8(cool_rgb.r, warm_rgb.r, warm_frac, 1000);
        blend.g = lerp8(cool_rgb.g, warm_rgb.g, warm_frac, 1000);
        blend.b = lerp8(cool_rgb.b, warm_rgb.b, warm_frac, 1000);
        if (flash_active) {
          // Lit from within: lean hard toward white rather than the
          // normal warm/cool shading, same "brief overexposure" look
          // a real strike gives the clouds around it.
          RGB8 white = { 255, 255, 255 };
          blend.r = lerp8(blend.r, white.r, 70, 100);
          blend.g = lerp8(blend.g, white.g, 70, 100);
          blend.b = lerp8(blend.b, white.b, 70, 100);
        }
        GColor color = dither_pixel(blend, bayer);
        graphics_context_set_fill_color(ctx, color);
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }

  if (flash_active) {
    // A single jagged bolt from the first (always-present) cluster's
    // base down toward the ground -- fixed zigzag shape, not
    // randomized per strike, which keeps this cheap (no RNG state to
    // carry) and is barely noticeable given how brief each flash is.
    int16_t bx = bounds.origin.x + (bounds.size.w * CLUSTER_X_PCT[0]) / 100;
    int16_t by = band_y + 10;
    int16_t ground_y = bounds.origin.y + bounds.size.h - GROUND_H;
    int16_t span = ground_y - by;
    if (span > 8) {
      GPoint bolt[6];
      bolt[0] = GPoint(bx, by);
      bolt[1] = GPoint(bx - 6, by + span * 2 / 10);
      bolt[2] = GPoint(bx + 4, by + span * 4 / 10);
      bolt[3] = GPoint(bx - 8, by + span * 6 / 10);
      bolt[4] = GPoint(bx + 2, by + span * 8 / 10);
      bolt[5] = GPoint(bx - 4, ground_y);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 2);
      for (int i = 0; i < 5; i++) {
        graphics_draw_line(ctx, bolt[i], bolt[i + 1]);
      }
    }
  }
}

// "Simple" cloud style -- a multi-puff cumulus mass built from
// discrete circles (a flat-ish shaded underside, a bulkier mid-body,
// and a bumpy sunlit top) rather than a continuous field. Much
// cheaper per redraw than the "Realistic" style (plain O(area) circle
// fills instead of per-pixel metaball evaluation across many seeds),
// at the cost of visibly-circular puffs instead of one organic mass.
typedef struct {
  int16_t dx, dy;
  uint8_t r;
  bool lit;
} CloudPuffSpec;

static const CloudPuffSpec CLOUD_TEMPLATE[11] = {
  // Flat shaded underside -- reads as the cloud's shadowed base
  { -26,  9, 10, false },
  { -13, 12, 13, false },
  {   0, 13, 14, false },
  {  13, 12, 13, false },
  {  26,  9, 10, false },
  // Bulkier mid-body, still on the shadow side so the silhouette
  // reads bottom-heavy the way real cumulus does
  { -18, -1, 14, false },
  {   0, -3, 17, false },
  {  18, -1, 14, false },
  // Bumpy sunlit crown -- smaller puffs, bright
  {  -9, -14,  9, true },
  {   4, -16, 10, true },
  {  16, -10,  8, true },
};
#define CLOUD_TEMPLATE_COUNT 11

// Bright-crown / shadow-base colors for the puffs above.
static void simple_cloud_colors(uint8_t cloud_pct, bool stormy, int16_t sun_alt_decideg, GColor *lit, GColor *shadow) {
  RGB8 lit_rgb, shadow_rgb;
  if (stormy) {
    lit_rgb = (RGB8){ 140, 140, 148 };
    shadow_rgb = (RGB8){ 45, 45, 52 };
  } else if (cloud_pct > 70) {
    lit_rgb = (RGB8){ 255, 255, 255 };
    shadow_rgb = (RGB8){ 120, 120, 126 };
  } else if (cloud_pct > 35) {
    lit_rgb = (RGB8){ 255, 255, 255 };
    shadow_rgb = (RGB8){ 172, 172, 178 };
  } else {
    lit_rgb = (RGB8){ 255, 255, 255 };
    shadow_rgb = (RGB8){ 216, 216, 220 };
  }

  // Same night-darkening as the Realistic style's cloud_shading_colors()
  // -- these puffs were a flat white/light-gray at any hour otherwise,
  // glaringly bright against a near-black night sky.
  uint8_t night = cloud_night_factor(sun_alt_decideg);
  if (night > 0) {
    RGB8 night_dark = { 28, 29, 36 };
    lit_rgb.r = lerp8(lit_rgb.r, night_dark.r, night, 100);
    lit_rgb.g = lerp8(lit_rgb.g, night_dark.g, night, 100);
    lit_rgb.b = lerp8(lit_rgb.b, night_dark.b, night, 100);
    shadow_rgb.r = lerp8(shadow_rgb.r, night_dark.r, night, 100);
    shadow_rgb.g = lerp8(shadow_rgb.g, night_dark.g, night, 100);
    shadow_rgb.b = lerp8(shadow_rgb.b, night_dark.b, night, 100);
  }

  *lit = GColorFromRGB(lit_rgb.r, lit_rgb.g, lit_rgb.b);
  *shadow = GColorFromRGB(shadow_rgb.r, shadow_rgb.g, shadow_rgb.b);
}

// Stipples a dithered disc of `color` at `density_pct` (0-100)
// coverage -- deliberately pixel-level (not a flat fill) so the
// sky/sun shows through in proportion to how overcast it actually is.
static void dither_fill_circle(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                                 GColor color, uint8_t density_pct) {
  uint8_t threshold = (uint8_t)((density_pct * 16) / 100);
  graphics_context_set_fill_color(ctx, color);
  int16_t x0 = center.x - radius, x1 = center.x + radius;
  int16_t y0 = center.y - radius, y1 = center.y + radius;
  if (x0 < bounds.origin.x) x0 = bounds.origin.x;
  if (y0 < bounds.origin.y) y0 = bounds.origin.y;
  if (x1 >= bounds.origin.x + bounds.size.w) x1 = bounds.origin.x + bounds.size.w - 1;
  if (y1 >= bounds.origin.y + bounds.size.h) y1 = bounds.origin.y + bounds.size.h - 1;

  for (int16_t y = y0; y <= y1; y++) {
    int16_t dy = y - center.y;
    for (int16_t x = x0; x <= x1; x++) {
      int16_t dx = x - center.x;
      if (dx * dx + dy * dy > (int32_t)radius * radius) continue;
      if (BAYER4[y & 3][x & 3] < threshold) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

static void draw_clouds_simple(GContext *ctx, GRect bounds, uint8_t cloud_pct, uint8_t cloud_altitude_pct,
                                uint8_t visibility_pct, bool stormy, int16_t sun_alt_decideg) {
  if (cloud_pct == 0 && !stormy) return;

  GColor lit_color, shadow_color;
  simple_cloud_colors(cloud_pct, stormy, sun_alt_decideg, &lit_color, &shadow_color);

  int16_t band_y = compute_cloud_band_y(bounds, cloud_altitude_pct);
  int cluster_count = cloud_cluster_count(cloud_pct, stormy);

  uint8_t density = cloud_pct < 30 ? 30 : cloud_pct;
  if (visibility_pct < 70) density += (70 - visibility_pct) / 3;
  if (density > 96) density = 96;
  if (stormy && density < 92) density = 92;

  int16_t scale_pct = 70 + (cloud_pct * 60) / 100;
  if (stormy && scale_pct < 130) scale_pct = 130;

  for (int c = 0; c < cluster_count; c++) {
    int16_t cx = bounds.origin.x + (bounds.size.w * CLUSTER_X_PCT[c]) / 100;
    int16_t cy = band_y + CLUSTER_Y_OFFSET[c];
    for (int p = 0; p < CLOUD_TEMPLATE_COUNT; p++) {
      int16_t px = (CLOUD_TEMPLATE[p].dx * scale_pct) / 100;
      int16_t py = (CLOUD_TEMPLATE[p].dy * scale_pct) / 100;
      int16_t pr = (CLOUD_TEMPLATE[p].r * scale_pct) / 100;
      if (pr < 3) pr = 3;
      GPoint puff_center = GPoint(cx + px, cy + py);
      GColor color = CLOUD_TEMPLATE[p].lit ? lit_color : shadow_color;
      dither_fill_circle(ctx, bounds, puff_center, pr, color, density);
    }
  }
}

// Picks between the two cloud styles above based on the user's
// "Cloud style" setting (0=Simple/battery-friendly, 1=Realistic).
static void draw_clouds(GContext *ctx, GRect bounds, uint8_t cloud_pct, uint8_t cloud_altitude_pct,
                         uint8_t visibility_pct, bool stormy, GPoint sun_center, bool sun_up,
                         uint8_t render_style, bool flash_active, int16_t sun_alt_decideg) {
  if (render_style == 0) {
    draw_clouds_simple(ctx, bounds, cloud_pct, cloud_altitude_pct, visibility_pct, stormy, sun_alt_decideg);
  } else {
    draw_clouds_realistic(ctx, bounds, cloud_pct, cloud_altitude_pct, visibility_pct, stormy, sun_center, sun_up, flash_active, sun_alt_decideg);
  }
}

// Rain/snow columns are anchored to the same cluster x-positions and
// band_y the clouds themselves use (see CLUSTER_X_PCT / cloud_cluster_count
// above), so precipitation visibly falls from the cloud masses rather
// than scattering across the whole sky regardless of where the clouds
// are. Fixed, deliberately-not-random offsets within each column --
// redraws happen at most once a minute, so per-frame randomness
// wouldn't read as motion anyway, and fixed positions are cheap and
// reproducible. Weather-condition codes match what index.js sends:
// 1=fog, 2=rain, 3=snow, 4=thunderstorm.
static const GPoint RAIN_OFFSETS[5] = {
  { -16,  8 }, { -5, 15 }, { 6, 10 }, { 17, 17 }, { 0, 24 },
};
#define RAIN_OFFSET_COUNT 5

static const GPoint SNOW_OFFSETS[6] = {
  { -18, 10 }, { -7, 19 }, { 4, 13 }, { 15, 23 }, { -2, 29 }, { 20, 16 },
};
#define SNOW_OFFSET_COUNT 6

static void draw_weather_effect(GContext *ctx, GRect bounds, uint8_t condition,
                                 uint8_t cloud_pct, uint8_t cloud_altitude_pct) {
  int16_t sky_h = bounds.size.h - GROUND_H; // don't draw effects over the ground strip

  if (condition == 2 || condition == 4) { // rain, or a storm's heavier rain
    int16_t band_y = compute_cloud_band_y(bounds, cloud_altitude_pct);
    int cluster_count = cloud_cluster_count(cloud_pct < 60 ? 60 : cloud_pct, condition == 4);
    graphics_context_set_stroke_color(ctx, GColorFromRGB(40, 100, 210));
    graphics_context_set_stroke_width(ctx, condition == 4 ? 2 : 1);
    for (int c = 0; c < cluster_count; c++) {
      int16_t cx = bounds.origin.x + (bounds.size.w * CLUSTER_X_PCT[c]) / 100;
      int16_t base_y = band_y + CLUSTER_Y_OFFSET[c] + 15; // just below the puff cluster's underside
      for (int i = 0; i < RAIN_OFFSET_COUNT; i++) {
        int16_t x = cx + RAIN_OFFSETS[i].x;
        int16_t y = base_y + RAIN_OFFSETS[i].y;
        if (y + 8 >= sky_h) continue;
        graphics_draw_line(ctx, GPoint(x, y), GPoint(x - 3, y + 8));
      }
    }
    if (condition == 4) { // plus a static lightning-bolt accent
      graphics_context_set_stroke_color(ctx, GColorYellow);
      graphics_context_set_stroke_width(ctx, 2);
      static const GPoint BOLT[4] = { { 95, 45 }, { 88, 65 }, { 100, 65 }, { 90, 90 } };
      for (int i = 0; i < 3; i++) {
        graphics_draw_line(ctx,
          GPoint(bounds.origin.x + BOLT[i].x, bounds.origin.y + BOLT[i].y),
          GPoint(bounds.origin.x + BOLT[i + 1].x, bounds.origin.y + BOLT[i + 1].y));
      }
    }
  } else if (condition == 3) { // snow
    int16_t band_y = compute_cloud_band_y(bounds, cloud_altitude_pct);
    int cluster_count = cloud_cluster_count(cloud_pct < 60 ? 60 : cloud_pct, false);
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int c = 0; c < cluster_count; c++) {
      int16_t cx = bounds.origin.x + (bounds.size.w * CLUSTER_X_PCT[c]) / 100;
      int16_t base_y = band_y + CLUSTER_Y_OFFSET[c] + 15;
      for (int i = 0; i < SNOW_OFFSET_COUNT; i++) {
        int16_t x = cx + SNOW_OFFSETS[i].x;
        int16_t y = base_y + SNOW_OFFSETS[i].y;
        if (y >= sky_h) continue;
        graphics_fill_rect(ctx, GRect(x, y, 2, 2), 0, GCornerNone);
      }
    }
  } else if (condition == 1) { // fog
    // Ground-hugging haze: density ramps up toward the horizon and
    // fades going up, the way real fog actually behaves (thickest
    // near the ground, thinning with altitude), rather than a
    // uniform sparkle spread evenly over the whole sky.
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int16_t y = 0; y < sky_h; y++) {
      int32_t density_pct = ((int32_t)y * 55) / (sky_h > 0 ? sky_h : 1) + 10; // 10%..65%
      uint8_t threshold = (uint8_t)((density_pct * 16) / 100);
      for (int16_t x = 0; x < bounds.size.w; x++) {
        if (BAYER4[y & 3][x & 3] < threshold) {
          graphics_fill_rect(ctx, GRect(bounds.origin.x + x, bounds.origin.y + y, 1, 1), 0, GCornerNone);
        }
      }
    }
  }
}

// Fixed streak positions/angles for meteor showers -- how many are
// actually drawn scales with meteor_intensity (0-100, ramped by
// astro.js's activeMeteorShower() around whichever shower's active
// window covers today). Only meaningful against a genuinely dark
// sky, so the caller gates this on sun altitude.
static const GPoint METEOR_STARTS[6] = {
  { 30, 15 }, { 80, 10 }, { 130, 20 }, { 165, 40 }, { 50, 45 }, { 110, 55 },
};
static const GPoint METEOR_ENDS[6] = {
  { 45, 35 }, { 100, 28 }, { 148, 40 }, { 180, 58 }, { 68, 65 }, { 128, 75 },
};

static void draw_meteors(GContext *ctx, GRect bounds, uint8_t intensity) {
  int count = (intensity * 6) / 100;
  if (count > 6) count = 6;
  if (count <= 0) return;
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < count; i++) {
    graphics_draw_line(ctx,
      GPoint(bounds.origin.x + METEOR_STARTS[i].x, bounds.origin.y + METEOR_STARTS[i].y),
      GPoint(bounds.origin.x + METEOR_ENDS[i].x, bounds.origin.y + METEOR_ENDS[i].y));
  }
}

// ---- aurora -------------------------------------------------------------
// Kp-index-driven upper-sky glow. Only ever considered when it's dark
// (aurora_visible below, computed in canvas_update_proc) and
// aurora_enabled is on; the caller further gates on
// aurora_visibility_pct (see eclipse_data.h) before calling either of
// these -- both assume they're only being asked to draw because that
// check already passed. Shares the Simple/Realistic toggle cloud
// rendering already uses (cloud_render_style) rather than a separate
// setting of its own.

// Simple style: one soft dithered glow band near the top of the sky,
// density fading linearly toward its lower edge -- deliberately cheap
// (a single flat per-row density falloff, no per-streak structure),
// same "Simple = battery-friendly" trade-off draw_clouds_simple makes
// against draw_clouds_realistic.
static void draw_aurora_simple(GContext *ctx, GRect bounds, uint8_t visibility_pct, uint8_t kp_x10) {
  int16_t top_y = bounds.origin.y + SKY_TOP_MARGIN;
  int16_t band_h = 46 + (kp_x10 * 2) / 9; // taller glow at higher Kp
  int16_t horizon_y = bounds.origin.y + bounds.size.h - GROUND_H;
  int16_t bottom_y = top_y + band_h;
  if (bottom_y > horizon_y) bottom_y = horizon_y;
  if (bottom_y <= top_y) return;

  // Green at typical (lower) Kp, shifting toward violet once a storm
  // is strong enough to push the visible structure higher/redder --
  // the same idea (just simpler -- one flat color, not a height
  // blend) as the Realistic style's per-row color blend below.
  RGB8 glow = (kp_x10 > 60) ? (RGB8){ 130, 60, 200 } : (RGB8){ 30, 200, 120 };
  uint8_t density_cap = (uint8_t)(((int32_t)visibility_pct * 45) / 100); // always a glow, never a wall

  for (int16_t y = top_y; y < bottom_y; y++) {
    int16_t rel = y - top_y;
    uint8_t density = (uint8_t)(density_cap - ((int32_t)density_cap * rel) / (bottom_y - top_y));
    uint8_t threshold = (uint8_t)((density * 16) / 100);
    for (int16_t x = bounds.origin.x; x < bounds.origin.x + bounds.size.w; x++) {
      uint8_t bayer = BAYER4[y & 3][x & 3];
      if (bayer >= threshold) continue;
      graphics_context_set_fill_color(ctx, dither_pixel(glow, bayer));
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }
}

#define AURORA_STREAK_COUNT 7
static const int16_t AURORA_STREAK_X_PCT[AURORA_STREAK_COUNT] = { 8, 22, 38, 50, 64, 80, 94 };
// Uneven heights (percent of the band's own max) so the streaks read
// as a rippling curtain skyline rather than a uniform wall.
static const int16_t AURORA_STREAK_HEIGHT_PCT[AURORA_STREAK_COUNT] = { 70, 100, 55, 85, 65, 95, 60 };
// Native-angle (0-65535) ripple phase offsets, so neighboring streaks
// don't wave in lockstep.
static const int32_t AURORA_STREAK_PHASE[AURORA_STREAK_COUNT] = { 0, 9362, 18725, 28087, 37449, 46811, 56174 };

// Realistic style: several vertical "curtain" streaks, each with a
// genuine sine-wave horizontal ripple (via sin_lookup, same fixed-
// point trig every hand/marker in this app already uses) and a
// top-to-bottom color blend -- green at the base fading toward violet/
// magenta higher up (redder/more magenta overall as Kp climbs), the
// classic look of a real display's lower green arc topped by faint
// red/purple structure. Notably more expensive per redraw than the
// Simple style above (a per-pixel color blend and dither across 7
// overlapping streaks, not one flat band) -- the same "Realistic
// costs more, looks more painterly" trade-off draw_clouds_realistic
// already makes over draw_clouds_simple.
static void draw_aurora_realistic(GContext *ctx, GRect bounds, uint8_t visibility_pct, uint8_t kp_x10) {
  int16_t top_y = bounds.origin.y + SKY_TOP_MARGIN;
  int16_t horizon_y = bounds.origin.y + bounds.size.h - GROUND_H;
  int16_t max_band_h = 60 + (kp_x10 * 3) / 9;
  int16_t streak_w = (bounds.size.w / AURORA_STREAK_COUNT) + 4; // slight overlap merges streaks into one curtain

  uint8_t base_density = 35 + (uint8_t)(((int32_t)visibility_pct * 35) / 100); // 35-70%, richer than Simple's flat 45% cap
  RGB8 base_color = { 20, 210, 110 };   // low arc: green
  RGB8 top_color = (kp_x10 > 60) ? (RGB8){ 170, 40, 200 } : (RGB8){ 90, 40, 180 }; // upper structure: violet, more magenta once storming

  for (int s = 0; s < AURORA_STREAK_COUNT; s++) {
    int16_t cx = bounds.origin.x + (bounds.size.w * AURORA_STREAK_X_PCT[s]) / 100;
    int16_t streak_h = (max_band_h * AURORA_STREAK_HEIGHT_PCT[s]) / 100;
    int16_t bottom_y = top_y + streak_h;
    if (bottom_y > horizon_y) bottom_y = horizon_y;
    if (bottom_y <= top_y) continue;

    for (int16_t y = top_y; y < bottom_y; y++) {
      int16_t rel = y - top_y;
      int32_t wave_angle = (AURORA_STREAK_PHASE[s] + (int32_t)rel * 900) & 0xFFFF;
      int16_t wobble = (int16_t)((6 * sin_lookup(wave_angle)) / TRIG_MAX_RATIO);
      int16_t row_cx = cx + wobble;

      int32_t height_frac1000 = ((int32_t)rel * 1000) / streak_h;
      RGB8 blend;
      blend.r = lerp8(top_color.r, base_color.r, height_frac1000, 1000);
      blend.g = lerp8(top_color.g, base_color.g, height_frac1000, 1000);
      blend.b = lerp8(top_color.b, base_color.b, height_frac1000, 1000);

      // Fades toward both edges (faint upper reach, dissolving into
      // the sky at the bottom) -- richest through the middle third.
      int32_t edge_fade = height_frac1000 < 500 ? height_frac1000 : (1000 - height_frac1000);
      uint8_t density = (uint8_t)((base_density * edge_fade) / 500);
      uint8_t threshold = (uint8_t)((density * 16) / 100);

      int16_t x0 = row_cx - streak_w / 2, x1 = row_cx + streak_w / 2;
      if (x0 < bounds.origin.x) x0 = bounds.origin.x;
      if (x1 >= bounds.origin.x + bounds.size.w) x1 = bounds.origin.x + bounds.size.w - 1;
      for (int16_t x = x0; x <= x1; x++) {
        uint8_t bayer = BAYER4[y & 3][x & 3];
        if (bayer >= threshold) continue;
        graphics_context_set_fill_color(ctx, dither_pixel(blend, bayer));
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

static void draw_aurora(GContext *ctx, GRect bounds, uint8_t render_style, uint8_t visibility_pct, uint8_t kp_x10) {
  if (render_style == 0) {
    draw_aurora_simple(ctx, bounds, visibility_pct, kp_x10);
  } else {
    draw_aurora_realistic(ctx, bounds, visibility_pct, kp_x10);
  }
}

// Short weather-condition word for the "23C Overcast" style readout
// (now drawn by the corners overlay in pebble-eclipse-watch.c, which
// is why this isn't static -- exported via background_layer.h instead of
// duplicated). Reuses weather_condition for anything with a dedicated
// visual effect (rain/snow/fog/storm) and falls back to reading it
// off cloud_cover_pct otherwise, so no extra data is needed beyond
// what's already in EclipseData.
const char *short_condition_text(uint8_t weather_condition, uint8_t cloud_pct) {
  switch (weather_condition) {
    case 1: return "Fog";
    case 2: return "Rain";
    case 3: return "Snow";
    case 4: return "Storm";
    default:
      if (cloud_pct < 20) return "Sunny";
      if (cloud_pct < 60) return "P.Cloudy";
      if (cloud_pct < 90) return "Cloudy";
      return "Overcast";
  }
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
  int16_t w = 46, h = 14;
  int16_t x = near.x + 8;
  if (x + w > bounds.origin.x + bounds.size.w) x = near.x - w - 8;
  if (x < bounds.origin.x) x = bounds.origin.x;
  int16_t y = near.y - h / 2;
  if (y < bounds.origin.y) y = bounds.origin.y;
  if (y + h > bounds.origin.y + bounds.size.h) y = bounds.origin.y + bounds.size.h - h;

  GRect r = GRect(x, y, w, h);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GRect text_box = GRect(r.origin.x, r.origin.y - 2, r.size.w, r.size.h + 2);

  if (label_style == 1) {
    draw_text_outlined(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                        main_color, true);
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

// ---- hour/second markers (merged from marker_layer.c) --------------------

static int16_t mk_min(int16_t a, int16_t b) { return a < b ? a : b; }
static int16_t mk_max(int16_t a, int16_t b) { return a > b ? a : b; }

// isqrt32() (integer square root) is already defined above, for the
// planet-separation math -- reused here for the marker ring's segment
// length rather than adding a second copy.

// Converts a 0-100% "reach" into an actual distance from center, in the
// same Q24.8 fixed-point units point_on_ring_fp() below works in
// throughout -- this is what actually keeps everything on-screen: 0% =
// the largest circle that's guaranteed to stay fully within the screen
// at every angle (the shorter half-dimension), 100% = the far edge the
// screen-fitted rectangle reaches along its dominant axis (the longer
// half-dimension). Returns fixed-point directly (rather than rounding
// to a whole pixel first and re-promoting that) so the sub-pixel
// precision survives into the rest of point_on_ring_fp()'s math.
static int32_t marker_reach_fp(GRect screen, uint8_t pct) {
  int16_t reach_min = mk_min(screen.size.w / 4, screen.size.h / 4);
  int16_t reach_max = mk_max(screen.size.w / 2, screen.size.h / 2);
  int32_t reach_min_fp = (int32_t)reach_min << SUBPIXEL_BITS;
  int32_t reach_max_fp = (int32_t)reach_max << SUBPIXEL_BITS;
  return reach_min_fp + div_round((reach_max_fp - reach_min_fp) * pct, 100);
}

// Blends a point on a circle of radius marker_reach_fp(pct) with a point
// on a screen-proportioned rectangle sized so its longer half-extent
// equals that same reach, by eccentricity_pct (0=circle, 100=rectangle)
// -- same sub-pixel fixed-point system (subpixel.h) hand_layer.c's
// compute_hand_geometry_fp() builds hand shapes in, rather than rounding
// each mark's endpoint to a whole pixel before working out its
// thickness. Markers rotate around the dial exactly like hands do, so
// they get the same precision now instead of a coarser plain-integer
// version of the same math. sin_v/cos_v are passed in (rather than an
// angle) since draw_marker_ring() below already looks them up once per
// mark and reuses them for the mark's thickness offset too.
static FGPoint point_on_ring_fp(FGPoint center, GRect screen, int32_t sin_v, int32_t cos_v,
                                 uint8_t pct, uint8_t eccentricity_pct) {
  int16_t screen_hw = screen.size.w / 2, screen_hh = screen.size.h / 2;
  int32_t reach_fp = marker_reach_fp(screen, pct);

  FGPoint circle_pt = fgpoint_new(
    center.x + (int32_t)(((int64_t)reach_fp * sin_v) / TRIG_MAX_RATIO),
    center.y - (int32_t)(((int64_t)reach_fp * cos_v) / TRIG_MAX_RATIO));

  if (eccentricity_pct == 0) return circle_pt;

  int16_t reach_max = mk_max(screen_hw, screen_hh);
  int32_t rect_hw_fp = (int32_t)(((int64_t)reach_fp * screen_hw) / reach_max);
  int32_t rect_hh_fp = (int32_t)(((int64_t)reach_fp * screen_hh) / reach_max);

  int32_t adx = sin_v < 0 ? -sin_v : sin_v;
  int32_t ady = cos_v < 0 ? -cos_v : cos_v;
  int32_t t_x = (adx == 0) ? INT32_MAX : (int32_t)(((int64_t)rect_hw_fp * TRIG_MAX_RATIO) / adx);
  int32_t t_y = (ady == 0) ? INT32_MAX : (int32_t)(((int64_t)rect_hh_fp * TRIG_MAX_RATIO) / ady);
  int32_t t = t_x < t_y ? t_x : t_y;

  FGPoint rect_pt = fgpoint_new(
    center.x + (int32_t)(((int64_t)t * sin_v) / TRIG_MAX_RATIO),
    center.y - (int32_t)(((int64_t)t * cos_v) / TRIG_MAX_RATIO));

  FGPoint result;
  result.x = circle_pt.x + (int32_t)(((int64_t)(rect_pt.x - circle_pt.x) * eccentricity_pct) / 100);
  result.y = circle_pt.y + (int32_t)(((int64_t)(rect_pt.y - circle_pt.y) * eccentricity_pct) / 100);
  return result;
}

// Thin GPoint-returning wrapper for draw_text_markers() below, which
// only ever needs a final whole-pixel position for a numeral -- looks
// up sin/cos from `angle` itself since it doesn't already have them
// the way draw_marker_ring() does.
static GPoint point_on_ring(GPoint center, GRect screen, int32_t angle,
                             uint8_t pct, uint8_t eccentricity_pct) {
  int32_t norm_angle = angle & 0xFFFF; // mask to prevent trig table lookup overflow/underflow
  int32_t sin_v = sin_lookup(norm_angle), cos_v = cos_lookup(norm_angle);
  FGPoint center_fp = fgpoint_from_gpoint(center);
  return fgpoint_to_gpoint(point_on_ring_fp(center_fp, screen, sin_v, cos_v, pct, eccentricity_pct));
}

// Draws one mark as a straight quad from inner to outer, half_thick_fp
// wide, with the requested cap style -- built directly from sin_v/cos_v
// (the same radial direction point_on_ring_fp() placed inner/outer
// along) rather than re-deriving a direction from the two points via
// vector subtraction, exactly mirroring how compute_hand_geometry_fp()
// in hand_layer.c builds a hand's own dot/square body from its own
// angle. style: 0=dot (round caps, via filled circles at both ends),
// 1=line (flush/butt ends), 2=square (ends extended outward by
// half_thick_fp, like SVG's stroke-linecap:square). translucent
// switches every fill/stroke in here to subpixel.h's dithered variants.
static void draw_ring_mark_fp(GContext *ctx, FGPoint inner, FGPoint outer, int32_t sin_v, int32_t cos_v,
                               int32_t half_thick_fp, uint8_t style, GColor color, bool translucent) {
  if (inner.x == outer.x && inner.y == outer.y) {
    // Degenerate zero-length mark (inner/outer border reach configured
    // equal) -- no direction to build a quad from, so just draw a dot
    // at that single point regardless of style, same fallback the
    // pre-fixed-point version of this code used.
    fill_circle_fp(ctx, inner, half_thick_fp, color, translucent);
    return;
  }

  int32_t dx_w = (int32_t)(((int64_t)half_thick_fp * cos_v) / TRIG_MAX_RATIO);
  int32_t dy_w = (int32_t)(((int64_t)half_thick_fp * sin_v) / TRIG_MAX_RATIO);

  FGPoint a = inner, b = outer;
  if (style == 2) { // square caps -- extend along the same radial direction outer sits on
    int32_t ex = (int32_t)(((int64_t)half_thick_fp * sin_v) / TRIG_MAX_RATIO);
    int32_t ey = (int32_t)(((int64_t)half_thick_fp * cos_v) / TRIG_MAX_RATIO);
    a = fgpoint_new(inner.x - ex, inner.y + ey);
    b = fgpoint_new(outer.x + ex, outer.y - ey);
  }

  FGPoint points[4] = {
    fgpoint_new(a.x - dx_w, a.y - dy_w), fgpoint_new(a.x + dx_w, a.y + dy_w),
    fgpoint_new(b.x + dx_w, b.y + dy_w), fgpoint_new(b.x - dx_w, b.y - dy_w),
  };

  if (translucent) {
    fill_polygon_dithered_fp(ctx, points, 4, color);
  } else {
    fill_polygon_fp(ctx, points, 4, color);
  }

  if (style == 0) { // dot caps
    fill_circle_fp(ctx, inner, half_thick_fp, color, translucent);
    fill_circle_fp(ctx, outer, half_thick_fp, color, translucent);
  }
}

// Resolves a MarkerRingConfig's own color choice against the active
// scheme -- same 3-way (main/accent/background) selection used all
// over this app, just not through hand_layer.c's own private
// resolve_scheme_color() (file-local there, and this is the only
// place background_layer.c needs the same lookup).
static GColor marker_ring_color(uint8_t choice, GColor main_color, GColor accent_color, GColor bg_color) {
  switch (choice) {
    case 1: return accent_color;
    case 2: return bg_color;
    case 0: default: return main_color;
  }
}

static void draw_marker_ring(GContext *ctx, GPoint center, GRect screen, const MarkerRingConfig *cfg,
                              int marks, int skip_step, GColor main_color, GColor accent_color, GColor bg_color) {
  if (cfg->thickness == 0) return;
  GColor color = marker_ring_color(cfg->color, main_color, accent_color, bg_color);
  uint8_t inner_pct = cfg->inner_border_pct, outer_pct = cfg->outer_border_pct;
  if (outer_pct < inner_pct) outer_pct = inner_pct;

  // Same 0.5px-minimum floor compute_hand_geometry_fp() uses for a
  // hand's half-width -- without it, a thickness of 1 (half_thick_fp
  // rounding down to 0) would collapse the mark's quad to zero area at
  // every angle except the four cardinal ones, same "second hand only
  // draws at right angles" bug round_div()'s comment in subpixel.h
  // describes.
  int32_t half_thick_fp = ((int32_t)cfg->thickness << SUBPIXEL_BITS) / 2;
  if (half_thick_fp < SUBPIXEL_HALF) half_thick_fp = SUBPIXEL_HALF;

  FGPoint center_fp = fgpoint_from_gpoint(center);

  for (int i = 0; i < marks; i++) {
    if (skip_step > 0 && i % skip_step == 0) continue;

    // Strictly mask with 0xFFFF to fix missing rotated markers
    int32_t angle = (((int32_t)i * TRIG_MAX_ANGLE) / marks) & 0xFFFF;
    int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

    FGPoint outer_fp = point_on_ring_fp(center_fp, screen, sin_v, cos_v, outer_pct, cfg->outer_eccentricity);
    FGPoint inner_fp = point_on_ring_fp(center_fp, screen, sin_v, cos_v, inner_pct, cfg->inner_eccentricity);
    draw_ring_mark_fp(ctx, inner_fp, outer_fp, sin_v, cos_v, half_thick_fp, cfg->style, color, cfg->translucent);
  }
}

// Hardcoded MarkerRingConfig pairs recreating the 3 non-bitmap,
// non-custom marker styles (big_analog_marker_style 0/1/2 -- minimal/
// small/big) through this same shared rasterizer, rather than each
// keeping its own separate procedural-drawing code. Approximated (not
// pixel-identical to the old formula-driven version, which varied hour
// mark length every 3rd hour and positioned everything relative to the
// screen radius directly) -- a deliberate simplification, tuned to look
// reasonably close within the 0-100% reach range every style now shares.
static const MarkerRingConfig MARKER_STYLE_HOUR_PRESETS[3] = {
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 0: minimal
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 60, .outer_border_pct = 85 }, // 1: small
  { .style = 2, .thickness = 5, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 60, .outer_border_pct = 85 }, // 2: big
};
static const MarkerRingConfig MARKER_STYLE_SECOND_PRESETS[3] = {
  { .style = 1, .thickness = 0, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 0: minimal -- thickness 0 = off, matches "hour markers only"
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 1: small
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 2: big
};

static uint32_t marker_text_font_resource_id(uint8_t choice) {
  switch (choice) {
    case 3: return RESOURCE_ID_DIGITALDREAM_FONT_12;
    case 4: return RESOURCE_ID_MINECRAFTER_FONT_12;
    case 5: return RESOURCE_ID_SFPIXELATE_FONT_14;
    case 6: return RESOURCE_ID_MISO_FONT_19;
    case 14: return RESOURCE_ID_BEBAS_FONT_20; // already loaded for clock_font's own small-readout companion; reused as-is here
    default: return 0; // 0-2 and 7-13 are all fonts_get_system_font() calls -- see get_marker_text_font() below
  }
}

// Rough export heights for each font_choice (0-2 system, 3-6 custom,
// 7-13 more system, 14 custom again), used only to size/vertically-
// center each numeral's text box.
static const uint8_t MARKER_FONT_HEIGHTS[15] = {
  14, 16, 20,       // 0-2: system S/M/L
  12, 12, 14, 19,   // 3-6: Digital/Minecraft/Pixelate/Miso
  17, 20, 23, 17,   // 7-10: Leco/Leco L/Leco XL/Droid Serif
  15, 19, 21,       // 11-13: Roboto Condensed/Bitham bold/Bitham M
  20                // 14: Bebas
};
// Per-font vertical fine-tune, added to MARKER_FONT_HEIGHTS when placing
// the text box -- all still 0 (unmeasured on a real watch yet) for
// every entry, including the new ones added alongside choices 7-14.
//TODO: This needs manual tweaking and looks like fonts are not matching the settings page - investigate
static const int8_t MARKER_FONT_Y_OFFSET[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static GFont get_marker_text_font(CanvasState *state, uint8_t choice) {
  // Values 7-13 are all fonts_get_system_font() -- no load lifecycle
  // needed, so they're resolved directly here rather than going
  // through marker_text_font_resource_id()'s custom-font path below
  // (which is only for resource-backed fonts: 3-6 and 14). Still need
  // to unload whatever custom font might already be loaded first,
  // though, or switching from e.g. Digital straight to Leco would
  // leave DigitalDream's font resource loaded in memory forever.
  switch (choice) {
    case 7: case 8: case 9: case 10: case 11: case 12: case 13:
      if (state->marker_text_font) {
        fonts_unload_custom_font(state->marker_text_font);
        state->marker_text_font = NULL;
      }
      state->marker_text_font_loaded_choice = choice;
      switch (choice) {
        case 7: return fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS);
        case 8: return fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
        case 9: return fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
        case 10: return fonts_get_system_font(FONT_KEY_DROID_SERIF_28_BOLD);
        case 11: return fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21);
        case 12: return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
        default: return fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS); // 13
      }
    default: break;
  }
  if (choice != state->marker_text_font_loaded_choice) {
    if (state->marker_text_font) { fonts_unload_custom_font(state->marker_text_font); state->marker_text_font = NULL; }
    uint32_t res_id = marker_text_font_resource_id(choice);
    if (res_id != 0) state->marker_text_font = fonts_load_custom_font(resource_get_handle(res_id));
    state->marker_text_font_loaded_choice = choice;
  }
  if (state->marker_text_font) return state->marker_text_font;
  if (choice == 2) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (choice == 1) return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  return fonts_get_system_font(FONT_KEY_GOTHIC_14); // 0, and fallback for an unrecognized choice
}

// Converts 1-59 (our only actual range: hour labels 1-12, second labels
// 0/5.../55) to a Roman numeral string. 0 has no traditional Roman
// numeral -- shown as "0" rather than an empty label, since a blank
// marker would look like a rendering bug rather than a deliberate choice.
static void int_to_roman(int num, char *buf, size_t buf_size) {
  if (num <= 0) { snprintf(buf, buf_size, "%d", num); return; }
  static const int VALUES[] = {50, 40, 10, 9, 5, 4, 1};
  static const char *SYMBOLS[] = {"L", "XL", "X", "IX", "V", "IV", "I"};
  size_t pos = 0;
  buf[0] = '\0';
  for (int i = 0; i < 7 && num > 0; i++) {
    while (num >= VALUES[i]) {
      size_t len = strlen(SYMBOLS[i]);
      if (pos + len + 1 > buf_size) return; // out of room -- truncate rather than overflow
      memcpy(buf + pos, SYMBOLS[i], len);
      pos += len;
      buf[pos] = '\0';
      num -= VALUES[i];
    }
  }
}

static void draw_text_markers(GContext *ctx, GPoint center, GRect screen, CanvasState *state,
                               const MarkerTextConfig *text_cfg, const MarkerRingConfig *hour_cfg,
                               const MarkerRingConfig *second_cfg, GColor color) {
  if (text_cfg->target == 0) return;
  bool is_hour = (text_cfg->target == 1);
  const MarkerRingConfig *ring = is_hour ? hour_cfg : second_cfg;
  uint16_t mask = is_hour ? text_cfg->hour_mask : text_cfg->second_mask;
  if (mask == 0) return;

  GFont font = get_marker_text_font(state, text_cfg->font_choice);
  int16_t fh = MARKER_FONT_HEIGHTS[text_cfg->font_choice] + MARKER_FONT_Y_OFFSET[text_cfg->font_choice];

  graphics_context_set_text_color(ctx, color);

  for (int i = 0; i < 12; i++) {
    if (!(mask & (1 << i))) continue;
    
    int32_t angle = (((int32_t)i * TRIG_MAX_ANGLE) / 12) & 0xFFFF;
    int offset_text_pct = ring->inner_border_pct + text_cfg->offset_px;
    if(offset_text_pct>100) {
      offset_text_pct = 100;
    } else if (offset_text_pct < 0) {
      offset_text_pct = 0;
    }
    GPoint pos = point_on_ring(center, screen, angle, offset_text_pct, ring->inner_eccentricity);

//    int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);
//    GPoint pos = GPoint(
//      base.x + div_round((int32_t)text_cfg->offset_px * sin_v, TRIG_MAX_RATIO),
//      base.y - div_round((int32_t)text_cfg->offset_px * cos_v, TRIG_MAX_RATIO));

    char buf[8];
    int label = is_hour ? (i == 0 ? 12 : i) : (i * 5);
    if (text_cfg->roman_numerals) int_to_roman(label, buf, sizeof(buf));
    else snprintf(buf, sizeof(buf), "%d", label);

    int16_t box_w = 30, box_h = fh + 4;
    GRect box = GRect(pos.x - box_w / 2, pos.y - box_h / 2, box_w, box_h);
    graphics_draw_text(ctx, buf, font, box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

// ---- bitmap marker styles (moved in from pebble-eclipse-watch.c) --------

static uint32_t marker_style_resource_id(uint8_t style) {
  switch (style) {
    case 3: return RESOURCE_ID_MODERN_BACKGROUND;
    case 4: return RESOURCE_ID_SWISS_BACKGROUND;
    case 5: return RESOURCE_ID_TALLY_BACKGROUND;
    case 6: return RESOURCE_ID_BELL_BACKGROUND;
    case 7: return RESOURCE_ID_BROWN_BACKGROUND;
    default: return 0;
  }
}

static void ensure_marker_bitmap_loaded(CanvasState *state, uint8_t style) {
  if (style < 3) {
    if (state->marker_bitmap) { gbitmap_destroy(state->marker_bitmap); state->marker_bitmap = NULL; }
    state->marker_bitmap_style = 255;
    state->marker_bitmap_tinted = false;
    return;
  }
  if (state->marker_bitmap_style == style && state->marker_bitmap) return; // already the right one
  if (state->marker_bitmap) { gbitmap_destroy(state->marker_bitmap); state->marker_bitmap = NULL; }
  uint32_t res_id = marker_style_resource_id(style);
  if (res_id != 0) state->marker_bitmap = gbitmap_create_with_resource(res_id);
  state->marker_bitmap_style = style;
  state->marker_bitmap_tinted = false; // freshly loaded, still in its original exported colors
}

// Recolors state->marker_bitmap to tint_color, once, in place -- see the
// original (now-removed) tint_marker_bitmap() in pebble-eclipse-watch.c
// for the fuller writeup of the two cases (palettized vs 8-bit) this
// handles; unchanged other than now living per-canvas-instance instead
// of file-static, and running once per full redraw (this canvas's own
// once-a-minute/force-redraw cadence) instead of every tick.
static void tint_marker_bitmap(CanvasState *state, GColor tint_color, bool transparent) {
  if (!state->marker_bitmap) return;
  if (state->marker_bitmap_tinted && state->marker_bitmap_tint_color.argb == tint_color.argb
      && state->marker_bitmap_tint_transparent == transparent) return;

  uint8_t forced_alpha_bits = transparent ? 0x80 : 0xC0; // alpha 2 (~67%) or 3 (opaque)

  GBitmapFormat format = gbitmap_get_format(state->marker_bitmap);
  if (format == GBitmapFormat1BitPalette || format == GBitmapFormat2BitPalette || format == GBitmapFormat4BitPalette) {
    GColor *palette = gbitmap_get_palette(state->marker_bitmap);
    if (palette) {
      int count = (format == GBitmapFormat1BitPalette) ? 2 : (format == GBitmapFormat2BitPalette) ? 4 : 16;
      for (int i = 0; i < count; i++) {
        if ((palette[i].argb & 0xC0) == 0) continue; // fully transparent entry -- leave it alone
        GColor new_color;
        new_color.argb = forced_alpha_bits | (tint_color.argb & 0x3F);
        palette[i] = new_color;
      }
    }
  } else if (format == GBitmapFormat8Bit) {
    uint8_t *data = gbitmap_get_data(state->marker_bitmap);
    uint16_t stride = gbitmap_get_bytes_per_row(state->marker_bitmap);
    GRect b = gbitmap_get_bounds(state->marker_bitmap);
    for (int16_t y = 0; y < b.size.h; y++) {
      uint8_t *row = data + (int32_t)y * stride;
      for (int16_t x = 0; x < b.size.w; x++) {
        if ((row[x] & 0xC0) == 0) continue; // fully transparent pixel -- leave it alone
        row[x] = forced_alpha_bits | (tint_color.argb & 0x3F);
      }
    }
  }
  // Any other format: left as-is, drawn with its original colors.

  state->marker_bitmap_tinted = true;
  state->marker_bitmap_tint_transparent = transparent;
  state->marker_bitmap_tint_color = tint_color;
}

static void draw_marker_bitmap(GContext *ctx, GBitmap *mask, GRect bounds) {
  if (!mask) return;
  GRect bmp_bounds = gbitmap_get_bounds(mask);
  GRect dest = GRect(bounds.origin.x + (bounds.size.w - bmp_bounds.size.w) / 2,
                      bounds.origin.y + (bounds.size.h - bmp_bounds.size.h) / 2,
                      bmp_bounds.size.w, bmp_bounds.size.h);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, mask, dest);
}

// Draws whichever marker style is active (procedural preset, custom, or
// bitmap) into `ctx`, using `screen`/`center` -- called from
// canvas_update_proc() during a full redraw, before the frame gets
// captured, so this only actually runs on this canvas's own throttled
// cadence (once a minute, or immediately on a forced redraw) rather than
// every tick. big-analog mode only; callers must gate on d->bottom_style.
static void draw_all_markers(GContext *ctx, CanvasState *state, GPoint center, GRect screen,
                              const EclipseData *d, GColor main_color, GColor accent_color, GColor bg_color) {
  uint8_t marker_style = d->big_analog_marker_style;

  if (marker_style == 9) { // none -- no ring, no bitmap, nothing to draw
    ensure_marker_bitmap_loaded(state, marker_style); // frees any previously-loaded bitmap
    return;
  }

  bool is_bitmap_style = marker_style >= 3 && marker_style != 8;

  ensure_marker_bitmap_loaded(state, marker_style);

  if (is_bitmap_style) {
    tint_marker_bitmap(state, main_color, d->bitmap_marker_transparent);
    draw_marker_bitmap(ctx, state->marker_bitmap, screen);
    return;
  }

  const MarkerRingConfig *hour_cfg, *second_cfg;
  if (marker_style == 8) {
    hour_cfg = &d->custom_hour_marker;
    second_cfg = &d->custom_second_marker;
  } else {
    uint8_t idx = (marker_style <= 2) ? marker_style : 0;
    hour_cfg = &MARKER_STYLE_HOUR_PRESETS[idx];
    second_cfg = &MARKER_STYLE_SECOND_PRESETS[idx];
  }

  // Second ring first so the hour ring's marks draw on top at shared
  // 12-o'clock-aligned slots (matches the original procedural markers'
  // precedent of hour ticks winning at shared positions).
  draw_marker_ring(ctx, center, screen, second_cfg, 60, 5, main_color, accent_color, bg_color);
  draw_marker_ring(ctx, center, screen, hour_cfg, 12, 0, main_color, accent_color, bg_color);

  if (marker_style == 8) {
    draw_text_markers(ctx, center, screen, state, &d->marker_text, hour_cfg, second_cfg, main_color);
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

  // Computed early -- cheap (just interpolation, no drawing) -- since
  // the throttle decision below needs sky darkness to tell whether
  // the ISS's visibility just changed.
  int16_t alt = interp_sun_alt_decideg(d, now);
  bool sky_is_dark = alt <= -60;
  // Space-view sky mode has no atmosphere to dim the sky in the first
  // place, so its celestial bodies (planets, ISS) are never gated by
  // brightness -- only by whether they're actually above the horizon,
  // same as the Sun/Moon already are in every mode. Stars get their
  // own separate always-drawn treatment further down, since they
  // don't exist at all outside this mode.
  bool sky_dark_for_bodies = sky_is_dark || d->sky_mode == 2;

  // Full screen, captured before any shrink below -- the bottom info
  // bar always sits at the true bottom of the physical screen
  // regardless of whether the sky content above it has been
  // compressed to make room.
  GRect full_bounds = bounds;
  if (d->bottom_info_bar_mode == 2 && d->bottom_style != 1) {
    bounds.size.h -= 20;
    center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  }

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
  bool storm_realistic = d->sky_mode == 0 && d->weather_condition == 4 && d->cloud_render_style == 1;
  bool flash_currently_active = storm_realistic && now < state->storm_flash_end;
  if (storm_realistic && !flash_currently_active) {
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

  int16_t moon_alt = interp_moon_alt_decideg(d, now);
  uint8_t cloud_pct = interp_cloud_pct(d, now);
  bool stormy = d->weather_condition == 4;

  // sky_mode: 0=Weather sky (below, unchanged), 1=Clear sky (same
  // day/night gradient, but weather_enabled below skips the haze and
  // the cloud/weather-effect calls further down), 2=Space view (no
  // gradient at all -- flat near-black, handled entirely in this
  // branch instead of falling through to fill_sky_gradient()).
  bool weather_enabled = d->sky_mode == 0;
  if (d->sky_mode == 2) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  } else {
    RGB8 sky_top_rgb, sky_hz_rgb;
    sky_colors_for_altitude(alt, &sky_top_rgb, &sky_hz_rgb);

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
    RGB8 hz_rgb = sky_hz_rgb; // what actually reaches fill_sky_gradient's horizon row
    int16_t band_y_screen = bounds.origin.y + bounds.size.h; // off-canvas: no visible band by default
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
      band_y_screen = compute_cloud_band_y(bounds, d->cloud_altitude_pct);
    }

    fill_sky_gradient(ctx, bounds, sky_top_rgb, band_rgb, band_y_screen, hz_rgb);
  }

  // Space-view sky mode's bright-star field: real azimuth-to-x /
  // altitude-to-y placement (same simplification the ISS already uses
  // further down, just for many bodies at once instead of one) rather
  // than the Sun/Moon/planets' fixed-column trick, since 16 stars
  // sharing one column would be an unreadable stack. Drawn early, as
  // a backdrop behind the Sun/Moon/planets/clouds that follow --
  // real stars sit "behind" everything else too. Tracked per-star for
  // the shake-to-reveal labels alongside the Sun/Moon/planets below.
  bool star_visible[STAR_COUNT];
  GPoint star_center[STAR_COUNT];
  for (int s = 0; s < STAR_COUNT; s++) {
    star_visible[s] = false;
    star_center[s] = GPoint(0, 0);
  }
  if (d->sky_mode == 2) {
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
    draw_meteors(ctx, bounds, d->meteor_intensity);
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
  // Big-analogue mode is exempt: its fullscreen Sun is a deliberate
  // dramatic backdrop for the whole eclipse regardless of the real
  // horizon, per the brief.
  if (eclipse_moon_active && d->bottom_style != 2 && d->sunset != 0 && now >= d->sunset) {
    eclipse_moon_active = false;
  }

  // Big-analogue mode (bottom_style == 2) has no bottom bar to make
  // room for and its hands render in a separate always-on-top layer,
  // so during an actual eclipse the Sun can fill the whole canvas as
  // a dramatic background rather than sitting at its normal small,
  // altitude-positioned size -- "basically fullscreen sun," per the
  // brief. Outside of an active eclipse, big-analogue mode renders
  // the sky exactly like the other modes (small Sun/Moon/planets),
  // just stretched across the full screen height since there's no
  // bottom third reserved for anything else.
  bool fullscreen_sun = (d->bottom_style == 2) && eclipse_moon_active;

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
  // both bright day blue and dark night navy. Positioned by its real
  // altitude while up, but *whether* it's visible at all -- and the
  // animated sink/rise right at the edges -- comes from today's
  // actual sunrise/sunset times rather than the altitude scale (see
  // body_screen_y's comment for why). In fullscreen-sun mode it's
  // simply centered and always "up" -- the whole point is to fill the
  // screen throughout the eclipse regardless of the Sun's real
  // altitude at that moment.
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
  if (sun_up) {
    graphics_context_set_fill_color(ctx, GColorOrange);
    graphics_fill_circle(ctx, sun_center, sun_r);
    graphics_context_set_stroke_color(ctx, GColorBulgarianRose);
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
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_circle(ctx, moon_center, moon_r);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_circle(ctx, moon_center, moon_r);
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
      draw_moon_phase(ctx, bounds, moon_center, moon_r, d->moon_phase_pct, d->moon_waxing, GColorWhite);
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
  if (sky_dark_for_bodies) {
    for (int p = 0; p < PLANET_COUNT; p++) {
      int16_t p_alt = interp_planet_alt_decideg(d, (PlanetId)p, now);
      int16_t p_alt_y = alt_to_y(p_alt, d->sky_scale_max_alt_decideg, bounds.size.h, PLANET_R);
      int16_t p_y;
      bool p_up = body_screen_y(p_alt_y, d->planet_rise[p], d->planet_set[p], now, horizon_y, PLANET_R, &p_y);
      if (!p_up) continue;
      GPoint c = GPoint((bounds.size.w * PLANET_COLUMN_PCT[p]) / 100, p_y);
      planet_visible[p] = true;
      planet_center[p] = c;
      if (p == PLANET_SATURN) {
        draw_saturn(ctx, c, d->saturn_ring_open_pct);
      } else {
        graphics_context_set_fill_color(ctx, planet_color((PlanetId)p));
        graphics_fill_circle(ctx, c, PLANET_R);
      }
    }
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
  if (d->show_iss && sky_dark_for_bodies && d->iss_alt_deg > 0 && d->iss_computed_at != 0) {
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
    draw_aurora(ctx, bounds, d->cloud_render_style, d->aurora_visibility_pct, d->aurora_kp_x10);
  }

  // Cloud clusters, drawn last so they visibly sit in front of (and
  // can partially obscure) the sun/moon, same as real clouds. Skipped
  // entirely outside Weather sky mode -- Clear sky and Space view
  // both represent weather-free skies by definition.
  if (weather_enabled) {
    draw_clouds(ctx, bounds, cloud_pct, d->cloud_altitude_pct, d->vis_score_pct, stormy, sun_center, sun_up, d->cloud_render_style,
                flash_currently_active, alt);
    draw_weather_effect(ctx, bounds, d->weather_condition, cloud_pct, d->cloud_altitude_pct);
  }

  // The clouds%/visibility/location bar (and its black "horizon"
  // backing strip): off, shown only on shake (alongside the
  // Sun/Moon/planet name labels), or permanently visible (in which
  // case the sky content above has already been compressed by 20px,
  // see full_bounds/bounds above, so this doesn't overlap it) --
  // except in analog mode (bottom_style == 1), which already shows
  // this same information persistently in its 4-line panel, so
  // showing it here too would just be a redundant duplicate.
  GRect ground = GRect(full_bounds.origin.x, full_bounds.origin.y + full_bounds.size.h - 18, full_bounds.size.w, 18);
  bool show_bottom_bar = d->bottom_style != 1 &&
    ((d->bottom_info_bar_mode == 2) || (d->bottom_info_bar_mode == 1 && state->show_labels));
  if (show_bottom_bar) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, ground, 0, GCornerNone);

    static char weather_buf[64];
    if (d->location_name[0] != '\0') {
      snprintf(weather_buf, sizeof(weather_buf), "Clds %d%% Vis %d%% @ %s",
               d->cloud_cover_pct, d->vis_score_pct, d->location_name);
    } else {
      snprintf(weather_buf, sizeof(weather_buf), "Clouds %d%%  Vis %d%%",
               d->cloud_cover_pct, d->vis_score_pct);
    }
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, weather_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                        GRect(ground.origin.x + 4, ground.origin.y, ground.size.w - 8, 16),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // Shake-to-reveal: brief name labels next to whichever bodies are
  // actually on screen right now. Needs its own main_color -- the
  // scheme lookup below is otherwise only computed further down,
  // scoped to the big-analog marker-drawing block, and shake labels
  // apply in every mode.
  if (state->show_labels) {
    GColor label_bg, label_main_color, label_accent;
    get_active_color_scheme(d, now, &label_bg, &label_main_color, &label_accent);
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
    if (meteors_visible) draw_label(ctx, bounds, meteor_label_point,
                                     d->meteor_shower_name[0] != '\0' ? d->meteor_shower_name : "Meteors",
                                     d->label_style, label_main_color);
    if (aurora_visible) {
      static char aurora_label_buf[16];
      snprintf(aurora_label_buf, sizeof(aurora_label_buf), "Aurora Kp %d.%d", d->aurora_kp_x10 / 10, d->aurora_kp_x10 % 10);
      draw_label(ctx, bounds, aurora_label_point, aurora_label_buf, d->label_style, label_main_color);
    }
    for (int s = 0; s < STAR_COUNT; s++) {
      if (star_visible[s]) draw_label(ctx, bounds, star_center[s], STAR_NAMES[s], d->label_style, label_main_color);
    }
    if (iss_visible) draw_label(ctx, bounds, iss_center, "ISS", d->label_style, label_main_color);
  }

  // Hour/second markers -- big-analog mode only. Drawn on top of
  // everything above (sky, sun/moon, clouds, ground, labels) so they
  // stay visible over any part of the sky, using full_bounds/its own
  // unshrunk center rather than the (possibly bottom-bar-shrunk) `bounds`
  // above -- matching hands_layer_update_proc's own positioning, which
  // always uses the full unobstructed screen regardless of the bottom
  // info bar, so markers and hands stay aligned with each other.
  if (d->bottom_style == 2) {
    GPoint full_center = GPoint(full_bounds.size.w / 2, full_bounds.size.h / 2);
    GColor bg, main_color, accent_color;
    get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
    draw_all_markers(ctx, state, full_center, full_bounds, d, main_color, accent_color, bg);
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
  // GBitmapFormat8Bit matches the framebuffer's own pixel format on
  // color platforms (emery included), so the row-by-row memcpy in
  // canvas_update_proc's capture step needs no per-pixel conversion.
  state->sky_cache = gbitmap_create_blank(frame.size, GBitmapFormat8Bit);
  state->marker_bitmap = NULL;
  state->marker_bitmap_style = 255; // sentinel: none loaded yet
  state->marker_bitmap_tinted = false;
  state->marker_text_font = NULL;
  state->marker_text_font_loaded_choice = 255;
  layer_set_update_proc(layer, canvas_update_proc);
  return layer;
}

void eclipse_canvas_destroy(Layer *layer) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  if (state->sky_cache) {
    gbitmap_destroy(state->sky_cache);
    state->sky_cache = NULL;
  }
  if (state->marker_bitmap) {
    gbitmap_destroy(state->marker_bitmap);
    state->marker_bitmap = NULL;
  }
  if (state->marker_text_font) {
    fonts_unload_custom_font(state->marker_text_font);
    state->marker_text_font = NULL;
  }
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

// Lightweight per-second nudge: marks the layer dirty (so the OS
// invokes canvas_update_proc), but does NOT force a redraw -- the
// canvas's own once-a-minute throttle inside canvas_update_proc
// decides whether anything actually gets recomputed. Safe to call
// every second without it costing a full redraw every time.
void eclipse_canvas_tick(Layer *layer) {
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

// ---- status text -------------------------------------------------------

static void fmt_countdown(char *buf, size_t buf_len, const char *label, time_t target, time_t now) {
  time_t remaining = target - now;
  if (remaining < 0) remaining = 0;
  int h = (int)(remaining / 3600);
  int m = (int)((remaining % 3600) / 60);
  int s = (int)(remaining % 60);
  if (h > 0) {
    snprintf(buf, buf_len, "%s %dh%02dm", label, h, m);
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

EclipsePhase eclipse_get_status_text(const EclipseData *d, time_t now, char *buf, size_t buf_len) {
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
    fmt_countdown(buf, buf_len, "Starts in", d->c1, now);
    return PHASE_BEFORE_C1;
  }
  if (now >= d->c4) {
    buf[0] = '\0';
    return PHASE_DONE;
  }

  // From here on an eclipse is actively in progress (between C1 and
  // C4) -- prefix whichever phase label applies with the live "% of
  // Sun covered" so both are visible on the one line.
  char phase_buf[24];
  EclipsePhase phase;
  if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now < d->c2) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality in", d->c2, now);
    phase = PHASE_PARTIAL_IN;
  } else if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now >= d->c2 && now < d->c3) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality ends", d->c3, now);
    phase = PHASE_TOTAL;
  } else if (now < d->max_t && (d->type == ECLIPSE_TYPE_PARTIAL || d->c2 == 0)) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Peak in", d->max_t, now);
    phase = PHASE_PARTIAL_IN;
  } else {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Clears in", d->c4, now);
    phase = PHASE_PARTIAL_OUT;
  }

  snprintf(buf, buf_len, "%d%% - %s", interp_mag_pct(d, now), phase_buf);
  return phase;
}
