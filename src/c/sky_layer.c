#include "sky_layer.h"
#include "celestial_layer.h"

#define SKY_GROUND_H 18

uint8_t sky_layer_interp_cloud_pct(const EclipseData *d, time_t t) {
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



uint8_t sky_layer_lerp8(uint8_t a, uint8_t b, int32_t num, int32_t den) {
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

static GColor dither_pixel(SkyRgb c, uint8_t bayer_0_15) {
  return GColorFromRGB(dither_channel(c.r, bayer_0_15) * 85,
                       dither_channel(c.g, bayer_0_15) * 85,
                       dither_channel(c.b, bayer_0_15) * 85);
}

// The Sun's own disc color, white near the zenith and shifting through
// yellow/orange to a deep red right at the horizon -- real sunlight
// reddens as it travels through more atmosphere at low altitude
// (Rayleigh scattering strips out blue/green wavelengths first, the
// same physical effect SKY_ANCHORS above already models for the sky
// itself). Same anchor-lerp technique as sky_layer_colors_for_altitude()
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
// of the normal sky -- sky_layer_sun_color_for_altitude() above models how
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

SkyRgb sky_layer_space_sun_color(void) {
  return (SkyRgb){ SUN_COLOR_SPACE_R, SUN_COLOR_SPACE_G, SUN_COLOR_SPACE_B };
}

SkyRgb sky_layer_sun_color_for_altitude(int16_t alt_decideg) {
  SkyRgb out;
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
      out.r = sky_layer_lerp8(hi->r, lo->r, num, den);
      out.g = sky_layer_lerp8(hi->g, lo->g, num, den);
      out.b = sky_layer_lerp8(hi->b, lo->b, num, den);
      return out;
    }
  }
  out.r = last->r; out.g = last->g; out.b = last->b; // shouldn't reach here given the bracketing above
  return out;
}

void sky_layer_colors_for_altitude(int16_t alt_decideg, SkyRgb *top_out, SkyRgb *hz_out) {
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
      top_out->r = sky_layer_lerp8(hi->top_r, lo->top_r, num, den);
      top_out->g = sky_layer_lerp8(hi->top_g, lo->top_g, num, den);
      top_out->b = sky_layer_lerp8(hi->top_b, lo->top_b, num, den);
      hz_out->r = sky_layer_lerp8(hi->hz_r, lo->hz_r, num, den);
      hz_out->g = sky_layer_lerp8(hi->hz_g, lo->hz_g, num, den);
      hz_out->b = sky_layer_lerp8(hi->hz_b, lo->hz_b, num, den);
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
// than any one physical layer -- see sky_layer_fill_gradient()'s own
// comment for why that split exists at all. Returns a value in that
// SAME virtual coordinate space (0 = the conceptual gradient's own
// top), for sky_layer_fill_gradient()'s band_y param, not a screen y.
int16_t sky_layer_compute_cloud_band_y_virtual(int16_t virtual_top_y, int16_t virtual_total_h, uint8_t cloud_altitude_pct) {
  int16_t half_h = virtual_total_h / 2;
  int16_t lower_top = virtual_top_y + half_h;
  int16_t lower_bottom = virtual_top_y + virtual_total_h - SKY_GROUND_H - 10;
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
void sky_layer_fill_gradient(GContext *ctx, GRect bounds, int16_t virtual_top_y, int16_t virtual_total_h,
                                  SkyRgb top, SkyRgb band, int16_t band_y, SkyRgb hz) {
  int16_t virtual_bottom_y = virtual_top_y + virtual_total_h - 1;
  if (band_y > virtual_bottom_y) band_y = virtual_bottom_y;
  if (band_y < virtual_top_y) band_y = virtual_top_y;
  int16_t upper_span = band_y - virtual_top_y;
  int16_t lower_span = virtual_bottom_y - band_y;

  for (int16_t y = 0; y < bounds.size.h; y++) {
    int16_t virtual_y = virtual_top_y + y;
    SkyRgb row;
    if (virtual_y <= band_y) {
      row.r = sky_layer_lerp8(top.r, band.r, y, upper_span > 0 ? upper_span : 1);
      row.g = sky_layer_lerp8(top.g, band.g, y, upper_span > 0 ? upper_span : 1);
      row.b = sky_layer_lerp8(top.b, band.b, y, upper_span > 0 ? upper_span : 1);
    } else {
      int16_t rel = virtual_y - band_y;
      row.r = sky_layer_lerp8(band.r, hz.r, rel, lower_span > 0 ? lower_span : 1);
      row.g = sky_layer_lerp8(band.g, hz.g, rel, lower_span > 0 ? lower_span : 1);
      row.b = sky_layer_lerp8(band.b, hz.b, rel, lower_span > 0 ? lower_span : 1);
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
// sky_layer_top_gradient_*()'s) are the only ones that pass something
// taller -- see canvas_update_proc()'s own local virtual_top_y/
// virtual_total_h for why.

// Digital top layout only: computes the same "what should the plain
// sky wash look like right now" colors canvas_update_proc()'s own
// gradient block below works out inline (sun altitude -> top/horizon
// colors, then weather-driven graying toward a band/horizon color and
// where that graying band sits) -- factored out here so
// sky_layer_top_gradient_*()'s own thin gradient-only strip can call the
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
void sky_layer_compute_wash(const EclipseData *d, time_t now, int16_t virtual_top_y, int16_t virtual_total_h,
                              SkyRgb *out_top, SkyRgb *out_band, int16_t *out_band_y, SkyRgb *out_hz, bool *out_flat_black) {
  *out_flat_black = (d->sky_mode == 2);
  if (*out_flat_black) return;

  int16_t alt = celestial_interp_sun_alt_decideg(d, now);
  SkyRgb sky_top_rgb, sky_hz_rgb;
  sky_layer_colors_for_altitude(alt, &sky_top_rgb, &sky_hz_rgb);

  uint8_t cloud_pct = sky_layer_interp_cloud_pct(d, now);
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

  SkyRgb band_rgb = sky_hz_rgb;
  SkyRgb hz_rgb = sky_hz_rgb; // what actually reaches fill_sky_gradient_ex's horizon row
  int16_t band_y = virtual_top_y + virtual_total_h; // off-canvas by default -- no visible band
  if (gray_amount > 0) {
    SkyRgb neutral_gray = { 115, 117, 120 };
    SkyRgb dark_gray = { 40, 41, 46 };
    band_rgb.r = sky_layer_lerp8(sky_hz_rgb.r, neutral_gray.r, gray_amount, 100);
    band_rgb.g = sky_layer_lerp8(sky_hz_rgb.g, neutral_gray.g, gray_amount, 100);
    band_rgb.b = sky_layer_lerp8(sky_hz_rgb.b, neutral_gray.b, gray_amount, 100);
    hz_rgb.r = sky_layer_lerp8(sky_hz_rgb.r, dark_gray.r, gray_amount, 100);
    hz_rgb.g = sky_layer_lerp8(sky_hz_rgb.g, dark_gray.g, gray_amount, 100);
    hz_rgb.b = sky_layer_lerp8(sky_hz_rgb.b, dark_gray.b, gray_amount, 100);
    band_y = sky_layer_compute_cloud_band_y_virtual(virtual_top_y, virtual_total_h, d->cloud_altitude_pct);
  }

  *out_top = sky_top_rgb; *out_band = band_rgb; *out_band_y = band_y; *out_hz = hz_rgb;
}


// ---- Digital top's own gradient-only strip ------------------------------
// See this pair's own declaration comment in background_layer.h for
// the "why a separate tiny module instead of another eclipse_canvas_
// create() frame" reasoning. Layer-local state is just the EclipseData
// pointer -- no cache, no animation/tick bookkeeping, nothing else this
// needs to remember between redraws (sky_layer_compute_wash() is cheap
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

  SkyRgb top, band, hz;
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
  sky_layer_compute_wash(d, time(NULL), 0, 228, &top, &band, &band_y, &hz, &flat_black);
  if (flat_black) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    return;
  }
  sky_layer_fill_gradient(ctx, bounds, 0, 228, top, band, band_y, hz);
}

Layer *sky_layer_top_gradient_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(TopGradientState));
  TopGradientState *state = (TopGradientState *)layer_get_data(layer);
  state->data = NULL;
  layer_set_update_proc(layer, top_gradient_update_proc);
  return layer;
}

void sky_layer_top_gradient_destroy(Layer *layer) {
  layer_destroy(layer);
}

void sky_layer_top_gradient_set_data(Layer *layer, EclipseData *data) {
  TopGradientState *state = (TopGradientState *)layer_get_data(layer);
  state->data = data;
  layer_mark_dirty(layer);
}


// Cheap (no redraw needed) check for whether the sky is currently
// bright enough that the overlaid countdown label should use dark
// text instead of light -- lets that label stay legible over the
// gradient without needing to redraw the whole canvas every second.
bool sky_layer_is_bright(const EclipseData *d, time_t now) {
  if (!d->valid || d->sky_sample_count == 0) return true;
  int16_t alt = celestial_interp_sun_alt_decideg(d, now);
  return alt > -60; // still light through civil twilight
}

