#include "background_layer.h"
#include "subpixel.h"
#include "features_layer.h"
#include "marker_layer.h"
#include "weather_layer.h"
#include "celestial_layer.h"

// ---------------------------------------------------------------------------
// Marker rendering lives in marker_layer.c. The background canvas owns the
// cached sky and decides WHEN markers need to be painted; marker_layer owns
// the geometry, text, bitmap resources, and marker animation itself.
// ---------------------------------------------------------------------------

// Height of the black "horizon" strip at the bottom of the canvas --
// also where altitude 0 lines up, so the sun/moon visibly sink behind
// it as they set (it's drawn last, on top, so it naturally clips
// whatever's behind it once a disc's center passes below this line).
// Shared sky-canvas geometry. Celestial body positioning uses the same
// horizon and top-margin values through celestial_layer.h.
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
  int last_eclipse_phase;   // see celestial_compute_eclipse_phase(): forces an immediate redraw
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

  CelestialLayerState celestial;

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

// Same idea again, for the full-day cloud-cover samples (0-100 %).
static uint8_t interp_cloud_pct(const EclipseData *d, time_t t) {
  if (d->sky_sample_count == 0) return 0;
  if (d->sky_sample_interval_s == 0) return d->cloud_pct_samples[0];
  int32_t offset = (int32_t)(t - d->sky_sample_start);
  int32_t idx = offset / (int32_t)d->sky_sample_interval_s;
  if (idx <= 0) return d->cloud_pct_samples[0];
  if (idx >= d->sky_sample_count - 1) return d->cloud_pct_samples[d->sky_sample_count - 1];
  time_t t0 = d->sky_sample_start + idx * (time_t)d->sky_sample_interval_s;
  int32_t frac = (int32_t)(t - t0);
  int32_t a = d->cloud_pct_samples[idx], b = d->cloud_pct_samples[idx + 1];
  return (uint8_t)(a + ((b - a) * frac) / (int32_t)d->sky_sample_interval_s);
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

// Convert a continuous 0-255 RGB value to Pebble's 2-bit-per-channel
// palette using the shared 4x4 Bayer matrix. The weather renderer has
// its own private copy because it owns atmospheric dithering; the sky
// gradient needs the same operation locally now that weather rendering
// is a separate translation unit.
static uint8_t dither_channel(uint8_t continuous_255, uint8_t bayer_0_15) {
  int32_t scaled = (int32_t)continuous_255 * 3;
  int32_t level = scaled / 255;
  int32_t rem = scaled - level * 255;
  int32_t threshold = (bayer_0_15 * 255) / 16;
  if (rem > threshold && level < 3) level++;
  return (uint8_t)level;
}

static GColor dither_pixel(RGB8 c, uint8_t bayer_0_15) {
  return GColorFromRGB(dither_channel(c.r, bayer_0_15) * 85,
                       dither_channel(c.g, bayer_0_15) * 85,
                       dither_channel(c.b, bayer_0_15) * 85);
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

// Digital top layout only: same "where does the cloud deck's graying
// kick in" math, just computed against
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

  int16_t alt = celestial_interp_sun_alt_decideg(d, now);
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

static int32_t shake_anim_eased_t_1000(uint16_t elapsed, const EclipseData *d) {
  uint32_t duration_ms = (uint32_t)(d->shake_label_seconds > 0 ? d->shake_label_seconds : 3) * 1000;
  if (elapsed < 500) return bg_anim_ease_out_1000(((int32_t)elapsed * 1000) / 500);
  if (duration_ms > 500 && elapsed > duration_ms - 500) {
    int32_t remaining = (int32_t)duration_ms - (int32_t)elapsed;
    if (remaining < 0) remaining = 0;
    return bg_anim_ease_out_1000((remaining * 1000) / 500);
  }
  return 1000;
}

static void draw_bg_anim_markers_overlay(GContext *ctx, CanvasState *state, const EclipseData *d,
                                         GRect bounds, time_t now) {
  if (d->bottom_style != 1) return;
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
  int32_t progress_1000 = ((int32_t)state->bg_anim_elapsed_ms * 1000) / BG_ANIM_MS;
  if (progress_1000 > 1000) progress_1000 = 1000;
  marker_layer_draw(ctx, &state->markers, center, bounds, d, main_color, accent_color, bg, true, progress_1000, d->draw_debug);
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
  int16_t alt = celestial_interp_sun_alt_decideg(d, sky_now);
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
  // Planet seek (see its own celestial.sun_center comment above) and now
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

  int current_phase = celestial_compute_eclipse_phase(d, now);
  bool current_iss_visible = celestial_compute_iss_visible(d, now, sky_dark_for_bodies);
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
      celestial_layer_draw_planet_seek(ctx, bounds, d, &state->celestial, now,
                                       state->planet_seek_heading_deg,
                                       shake_anim_eased_t_1000(state->planet_seek_elapsed_ms, d),
                                       d->label_style, main_color);
    }
    if (state->bg_anim_active && d->bg_anim_mode == 1) {
      celestial_layer_draw_bg_anim_planets(ctx, bounds, d, &state->celestial);
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

  GColor sun_fill_color;
  GColor sun_outline_color;
  bool fullscreen_sun = (d->bottom_style == 1) && d->has_eclipse && now >= d->c1 && now <= d->c4;
  RGB8 sun_rgb = fullscreen_sun
    ? (RGB8){ SUN_COLOR_SPACE_R, SUN_COLOR_SPACE_G, SUN_COLOR_SPACE_B }
    : sun_color_for_altitude(alt);
  sun_fill_color = GColorFromRGB(sun_rgb.r, sun_rgb.g, sun_rgb.b);
  sun_outline_color = GColorFromRGB((uint8_t)((uint16_t)sun_rgb.r * 55 / 100),
                                    (uint8_t)((uint16_t)sun_rgb.g * 55 / 100),
                                    (uint8_t)((uint16_t)sun_rgb.b * 55 / 100));
  bool suppress_other_bodies_for_eclipse = d->sky_mode == 2 && eclipse_is_active(d, now);
  celestial_layer_update(&state->celestial, ctx, bounds, d, now, sky_now,
                         skip_body_paint, suppress_other_bodies_for_eclipse,
                         sun_fill_color, sun_outline_color);

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
  // reuses every frame -- see celestial.sun_center's own comment above).
  if (weather_enabled && !state->planet_seek_active) {
    RGB8 cached_sun_rgb = fullscreen_sun
      ? (RGB8){ SUN_COLOR_SPACE_R, SUN_COLOR_SPACE_G, SUN_COLOR_SPACE_B }
      : sun_color_for_altitude(alt);
    weather_layer_draw_clouds(ctx, bounds, cloud_pct, d->cloud_altitude_pct, d->vis_score_pct, stormy,
                              state->celestial.sun_center, state->celestial.sun_up, flash_currently_active, alt,
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
      celestial_layer_draw_labels(ctx, bounds, d, &state->celestial, d->label_style, label_main_color);
    }
    if (meteors_visible) celestial_layer_draw_label(ctx, bounds, meteor_label_point,
                                     d->meteor_shower_name[0] != '\0' ? d->meteor_shower_name : "Meteors",
                                     d->label_style, label_main_color);
    if (aurora_visible) {
      static char aurora_label_buf[16];
      snprintf(aurora_label_buf, sizeof(aurora_label_buf), "Aurora Kp %d.%d", d->aurora_kp_x10 / 10, d->aurora_kp_x10 % 10);
      celestial_layer_draw_label(ctx, bounds, aurora_label_point, aurora_label_buf, d->label_style, label_main_color);
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
    celestial_layer_draw_planet_seek(ctx, bounds, d, &state->celestial, now,
                                       state->planet_seek_heading_deg,
                                       shake_anim_eased_t_1000(state->planet_seek_elapsed_ms, d),
                                       d->label_style, main_color);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 2) {
    draw_bg_anim_markers_overlay(ctx, state, d, bounds, now);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 1) {
    celestial_layer_draw_bg_anim_planets(ctx, bounds, d, &state->celestial);
  }
}

Layer *eclipse_canvas_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(CanvasState));
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->data = NULL;
  state->show_labels = false;
  state->last_full_draw = 0;
  state->force_next_draw = true; // always draw the first time
  state->last_eclipse_phase = -1; // sentinel: guaranteed to differ from celestial_compute_eclipse_phase()'s 0-5
  state->last_eclipse_max = 0;
  state->max_vibrated = false;
  state->last_iss_visible = false;
  celestial_layer_init(&state->celestial);
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
  int16_t alt = celestial_interp_sun_alt_decideg(d, now);
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

  snprintf(buf, buf_len, "%d%% - %s", celestial_interp_mag_pct(d, now), phase_buf);
  return phase;
}
