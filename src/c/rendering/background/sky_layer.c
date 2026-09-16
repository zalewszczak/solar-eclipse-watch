#include "./sky_layer.h"
#include "./celestial_layer.h"
#include "./celestial_ephemeris.h"
#include "../../graphics/subpixel.h"
#include "../render_math.h"

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



// Convert a continuous 0-255 RGB value to Pebble's 2-bit-per-channel
static GColor dither_pixel(SkyRgb c, uint8_t bayer_0_15) {
  return render_math_dither_rgb(c.r, c.g, c.b, bayer_0_15);
}

// The Sun's own disc color, white near the zenith and shifting through
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
      out.r = render_math_lerp8(hi->r, lo->r, num, den);
      out.g = render_math_lerp8(hi->g, lo->g, num, den);
      out.b = render_math_lerp8(hi->b, lo->b, num, den);
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
      top_out->r = render_math_lerp8(hi->top_r, lo->top_r, num, den);
      top_out->g = render_math_lerp8(hi->top_g, lo->top_g, num, den);
      top_out->b = render_math_lerp8(hi->top_b, lo->top_b, num, den);
      hz_out->r = render_math_lerp8(hi->hz_r, lo->hz_r, num, den);
      hz_out->g = render_math_lerp8(hi->hz_g, lo->hz_g, num, den);
      hz_out->b = render_math_lerp8(hi->hz_b, lo->hz_b, num, den);
      return;
    }
  }
  // Shouldn't reach here given the bracketing above; fall back to night.
  top_out->r = last->top_r; top_out->g = last->top_g; top_out->b = last->top_b;
  hz_out->r = last->hz_r; hz_out->g = last->hz_g; hz_out->b = last->hz_b;
}

// Digital top layout only: same "where does the cloud deck's graying
int16_t sky_layer_compute_cloud_band_y_virtual(int16_t virtual_top_y, int16_t virtual_total_h, uint8_t cloud_altitude_pct) {
  (void)virtual_top_y;
  int16_t half_h = virtual_total_h / 2;
  int16_t lower_top = half_h;
  int16_t lower_bottom = virtual_total_h - SKY_GROUND_H - 10;
  if (lower_bottom < lower_top) lower_bottom = lower_top;
  return lower_bottom - (((int32_t)(lower_bottom - lower_top) * cloud_altitude_pct) / 100);
}

// Fills `bounds` with a dithered vertical gradient from `top` down to
void sky_layer_fill_gradient(GContext *ctx, GRect bounds, int16_t virtual_top_y, int16_t virtual_total_h,
                                  SkyRgb top, SkyRgb band, int16_t band_y, SkyRgb hz) {
// virtual_bottom_y, upper_span and the upper-branch lerp position
  int16_t virtual_bottom_y = virtual_total_h - 1;
  if (band_y > virtual_bottom_y) band_y = virtual_bottom_y;
  if (band_y < 0) band_y = 0;
  int16_t upper_span = band_y;
  int16_t lower_span = virtual_bottom_y - band_y;

  for (int16_t y = 0; y < bounds.size.h; y++) {
    int16_t virtual_y = virtual_top_y + y;
    SkyRgb row;
    if (virtual_y <= band_y) {
      row.r = render_math_lerp8(top.r, band.r, virtual_y, upper_span > 0 ? upper_span : 1);
      row.g = render_math_lerp8(top.g, band.g, virtual_y, upper_span > 0 ? upper_span : 1);
      row.b = render_math_lerp8(top.b, band.b, virtual_y, upper_span > 0 ? upper_span : 1);
    } else {
      int16_t rel = virtual_y - band_y;
      row.r = render_math_lerp8(band.r, hz.r, rel, lower_span > 0 ? lower_span : 1);
      row.g = render_math_lerp8(band.g, hz.g, rel, lower_span > 0 ? lower_span : 1);
      row.b = render_math_lerp8(band.b, hz.b, rel, lower_span > 0 ? lower_span : 1);
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

// Digital top layout only: computes the same "what should the plain
static void sky_layer_compute_wash(const EclipseData *d, time_t now, int16_t virtual_top_y, int16_t virtual_total_h,
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
    band_rgb.r = render_math_lerp8(sky_hz_rgb.r, neutral_gray.r, gray_amount, 100);
    band_rgb.g = render_math_lerp8(sky_hz_rgb.g, neutral_gray.g, gray_amount, 100);
    band_rgb.b = render_math_lerp8(sky_hz_rgb.b, neutral_gray.b, gray_amount, 100);
    hz_rgb.r = render_math_lerp8(sky_hz_rgb.r, dark_gray.r, gray_amount, 100);
    hz_rgb.g = render_math_lerp8(sky_hz_rgb.g, dark_gray.g, gray_amount, 100);
    hz_rgb.b = render_math_lerp8(sky_hz_rgb.b, dark_gray.b, gray_amount, 100);
    band_y = sky_layer_compute_cloud_band_y_virtual(virtual_top_y, virtual_total_h, d->cloud_altitude_pct);
  }

  *out_top = sky_top_rgb; *out_band = band_rgb; *out_band_y = band_y; *out_hz = hz_rgb;
}


// ---- Digital top's own gradient-only strip ------------------------------
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
  if (!layer) return NULL;
  TopGradientState *state = (TopGradientState *)layer_get_data(layer);
  state->data = NULL;
  layer_set_update_proc(layer, top_gradient_update_proc);
  return layer;
}

void sky_layer_top_gradient_destroy(Layer *layer) {
  if (layer) layer_destroy(layer);
}

void sky_layer_top_gradient_set_data(Layer *layer, EclipseData *data) {
  TopGradientState *state = (TopGradientState *)layer_get_data(layer);
  state->data = data;
  layer_mark_dirty(layer);
}


// Cheap (no redraw needed) check for whether the sky is currently
bool sky_layer_is_bright(const EclipseData *d, time_t now) {
  if (!d->valid || d->sky_sample_count == 0) return true;
  int16_t alt = celestial_interp_sun_alt_decideg(d, now);
  return alt > -60; // still light through civil twilight
}

