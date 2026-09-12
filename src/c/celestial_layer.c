#include "celestial_layer.h"
#include "features_layer.h"
#include <string.h>

#define CELESTIAL_ARROW_W 6

static const char *PLANET_NAMES[PLANET_COUNT] = { "Mercury", "Venus", "Mars", "Jupiter", "Saturn" };
static const int16_t PLANET_COLUMN_PCT[PLANET_COUNT] = { 15, 85, 42, 58, 33 };
static const char *STAR_NAMES[STAR_COUNT] = {
  "Sirius", "Canopus", "Arcturus", "Vega", "Capella", "Rigel", "Procyon", "Betelgeuse",
  "Altair", "Aldebaran", "Antares", "Spica", "Pollux", "Fomalhaut", "Deneb", "Regulus"
};
static const uint8_t STAR_RADIUS[STAR_COUNT] = {
  2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

typedef enum { SAMPLE_U8, SAMPLE_U16, SAMPLE_I16 } SampleKind;

static int32_t read_sample(const void *samples, SampleKind kind, int idx) {
  switch (kind) {
    case SAMPLE_U8:  return ((const uint8_t *)samples)[idx];
    case SAMPLE_U16: return ((const uint16_t *)samples)[idx];
    case SAMPLE_I16: return ((const int16_t *)samples)[idx];
  }
  return 0;
}

static bool grid_bracket(int count, time_t start, int32_t interval_s, time_t t,
                         int *idx, int32_t *frac_num, int32_t *frac_den) {
  if (count <= 0) { *idx = 0; return false; }
  if (interval_s <= 0) { *idx = 0; return false; }
  int32_t offset_s = (int32_t)(t - start);
  int32_t idx_f = offset_s / interval_s;
  if (idx_f <= 0) { *idx = 0; return false; }
  if (idx_f >= count - 1) { *idx = count - 1; return false; }
  *idx = (int)idx_f;
  time_t t0 = start + (*idx) * (time_t)interval_s;
  *frac_num = (int32_t)(t - t0);
  *frac_den = interval_s;
  return true;
}

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

static uint16_t interp_grid_az(const uint16_t *samples, int count,
                               time_t start, uint32_t interval_s, time_t t, uint16_t fallback) {
  if (count == 0) return fallback;
  int idx; int32_t frac_num = 0, frac_den = 1;
  bool blend = grid_bracket(count, start, (int32_t)interval_s, t, &idx, &frac_num, &frac_den);
  if (!blend) return samples[idx];
  int32_t diff = (int32_t)samples[idx + 1] - samples[idx];
  if (diff > 1800) diff -= 3600;
  if (diff < -1800) diff += 3600;
  int32_t result = (int32_t)samples[idx] + (diff * frac_num) / frac_den;
  result %= 3600;
  if (result < 0) result += 3600;
  return (uint16_t)result;
}

uint16_t celestial_interp_separation_centideg(const EclipseData *d, time_t t) {
  return (uint16_t)interp_grid(d->sep_samples_centideg, SAMPLE_U16, d->sample_count,
                                d->sample_start, d->sample_interval_s, t, 0);
}

uint8_t celestial_interp_mag_pct(const EclipseData *d, time_t t) {
  return (uint8_t)interp_grid(d->mag_pct_samples, SAMPLE_U8, d->sample_count,
                              d->sample_start, d->sample_interval_s, t, 0);
}

int16_t celestial_interp_sun_alt_decideg(const EclipseData *d, time_t t) {
  return (int16_t)interp_grid(d->sun_alt_decideg, SAMPLE_I16, d->sky_sample_count,
                              d->sky_sample_start, d->sky_sample_interval_s, t, -900);
}

int16_t celestial_interp_moon_alt_decideg(const EclipseData *d, time_t t) {
  return (int16_t)interp_grid(d->moon_alt_decideg, SAMPLE_I16, d->sky_sample_count,
                              d->sky_sample_start, d->sky_sample_interval_s, t, -900);
}

int16_t celestial_interp_planet_alt_decideg(const EclipseData *d, PlanetId planet, time_t t) {
  return (int16_t)interp_grid(d->planet_alt_decideg[planet], SAMPLE_I16, d->sky_sample_count,
                              d->sky_sample_start, d->sky_sample_interval_s, t, -900);
}

uint16_t celestial_interp_sun_az_decideg(const EclipseData *d, time_t t) {
  return interp_grid_az(d->sun_az_decideg, d->sky_sample_count,
                        d->sky_sample_start, d->sky_sample_interval_s, t, 0);
}

uint16_t celestial_interp_moon_az_decideg(const EclipseData *d, time_t t) {
  return interp_grid_az(d->moon_az_decideg, d->sky_sample_count,
                        d->sky_sample_start, d->sky_sample_interval_s, t, 0);
}

uint16_t celestial_interp_planet_az_decideg(const EclipseData *d, PlanetId planet, time_t t) {
  return interp_grid_az(d->planet_az_decideg[planet], d->sky_sample_count,
                        d->sky_sample_start, d->sky_sample_interval_s, t, 0);
}

uint8_t celestial_count_visible_planets(const EclipseData *d, time_t now) {
  if (celestial_interp_sun_alt_decideg(d, now) > -60) return 0;
  uint8_t count = 0;
  for (int p = 0; p < PLANET_COUNT; p++) {
    time_t rise = d->planet_rise[p], set = d->planet_set[p];
    bool up = (rise != 0 && set != 0)
      ? (now >= rise - CELESTIAL_RISE_SET_TRANSITION_S && now <= set + CELESTIAL_RISE_SET_TRANSITION_S)
      : celestial_interp_planet_alt_decideg(d, (PlanetId)p, now) > 0;
    if (up) count++;
  }
  return count;
}

int celestial_compute_eclipse_phase(const EclipseData *d, time_t now) {
  if (!d->has_eclipse) return 0;
  if (now < d->c1) return 1;
  if (now < d->c2) return 2;
  if (now < d->c3) return 3;
  if (now < d->c4) return 4;
  return 5;
}

bool celestial_compute_iss_visible(const EclipseData *d, time_t now, bool sky_is_dark) {
  if (!d->show_iss || !sky_is_dark || d->iss_alt_deg <= 0 || d->iss_computed_at == 0) return false;
  time_t age = now - d->iss_computed_at;
  return age >= 0 && age < 900;
}

bool celestial_sky_is_dark(const EclipseData *d, time_t now) {
  return celestial_interp_sun_alt_decideg(d, now) <= -60;
}

int16_t celestial_alt_to_y(int16_t alt_decideg, int16_t scale_max_decideg, int16_t canvas_h, int16_t radius) {
  int16_t horizon_y = canvas_h - CELESTIAL_GROUND_H;
  int16_t usable = horizon_y - CELESTIAL_SKY_TOP_MARGIN;
  if (scale_max_decideg < 50) scale_max_decideg = 50;
  int32_t y = horizon_y - ((int32_t)alt_decideg * usable) / scale_max_decideg;
  if (alt_decideg >= 0) {
    int16_t max_y_when_up = horizon_y - radius;
    if (y > max_y_when_up) y = max_y_when_up;
    if (y < CELESTIAL_SKY_TOP_MARGIN) y = CELESTIAL_SKY_TOP_MARGIN;
  }
  if (y > canvas_h + 60) y = canvas_h + 60;
  if (y < -60) y = -60;
  return (int16_t)y;
}

static bool body_screen_y(int16_t alt_based_y, time_t rise, time_t set, time_t now,
                          int16_t horizon_y, int16_t radius, int16_t *y_out) {
  int16_t hidden_y = horizon_y + radius;
  bool has_rise = rise != 0, has_set = set != 0;
  if (!has_rise || !has_set) {
    if (alt_based_y > horizon_y) return false;
    *y_out = alt_based_y;
    return true;
  }
  if (now < rise - CELESTIAL_RISE_SET_TRANSITION_S || now > set + CELESTIAL_RISE_SET_TRANSITION_S) return false;
  int32_t span = CELESTIAL_RISE_SET_TRANSITION_S * 2;
  if (now < rise + CELESTIAL_RISE_SET_TRANSITION_S) {
    int32_t progress = (int32_t)(now - (rise - CELESTIAL_RISE_SET_TRANSITION_S));
    if (progress < 0) progress = 0;
    if (progress > span) progress = span;
    *y_out = hidden_y - (int16_t)(((int32_t)(hidden_y - alt_based_y) * progress) / span);
    return true;
  }
  if (now > set - CELESTIAL_RISE_SET_TRANSITION_S) {
    int32_t progress = (int32_t)(now - (set - CELESTIAL_RISE_SET_TRANSITION_S));
    if (progress < 0) progress = 0;
    if (progress > span) progress = span;
    *y_out = alt_based_y + (int16_t)(((int32_t)(hidden_y - alt_based_y) * progress) / span);
    return true;
  }
  *y_out = alt_based_y;
  return true;
}

static uint16_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  uint32_t x = (uint32_t)v, res = 0, bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= res + bit) { x -= res + bit; res = (res >> 1) + bit; }
    else res >>= 1;
    bit >>= 2;
  }
  return (uint16_t)res;
}

static GPoint enforce_min_separation(GPoint a, GPoint b, int32_t min_dist) {
  int32_t dx = b.x - a.x, dy = b.y - a.y;
  int32_t dist = (int32_t)isqrt32(dx * dx + dy * dy);
  if (dist >= min_dist) return b;
  if (dist == 0) return GPoint(a.x + min_dist, a.y);
  return GPoint((int16_t)(a.x + (dx * min_dist) / dist),
                (int16_t)(a.y + (dy * min_dist) / dist));
}

void celestial_draw_moon_phase(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                    uint8_t phase_pct, bool waxing, GColor lit_color) {
  int32_t k100 = phase_pct, side = waxing ? 1 : -1;
  int16_t x0 = center.x - radius, x1 = center.x + radius;
  int16_t y0 = center.y - radius, y1 = center.y + radius;
  for (int16_t y = y0; y <= y1; y++) {
    int16_t dy = y - center.y;
    int32_t term = (int32_t)radius * radius - (int32_t)dy * dy;
    if (term < 0) continue;
    int16_t half_width = (int16_t)isqrt32(term);
    bool gibbous = k100 > 50;
    int32_t a = gibbous ? (radius * (2 * k100 - 100)) / 100 : (radius * (100 - 2 * k100)) / 100;
    int32_t ellipse_w = (a * half_width) / (radius == 0 ? 1 : radius);
    for (int16_t x = x0 + (radius - half_width); x <= x1 - (radius - half_width); x++) {
      if (x < bounds.origin.x || x >= bounds.origin.x + bounds.size.w ||
          y < bounds.origin.y || y >= bounds.origin.y + bounds.size.h) continue;
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

static GColor planet_color(PlanetId p) {
  switch (p) {
    case PLANET_MERCURY: return GColorLightGray;
    case PLANET_VENUS: return GColorWhite;
    case PLANET_MARS: return GColorRed;
    case PLANET_JUPITER: return GColorYellow;
    case PLANET_SATURN: default: return GColorYellow;
  }
}

static void draw_saturn(GContext *ctx, GPoint center, uint8_t ring_open_pct) {
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_circle(ctx, center, CELESTIAL_PLANET_R);
  int16_t ring_span = CELESTIAL_PLANET_R + 4;
  int16_t ring_thickness = 1 + (int16_t)((ring_open_pct * 2) / 100);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(center.x - ring_span, center.y - ring_thickness / 2,
                                ring_span * 2, ring_thickness), 0, GCornerNone);
}

static GPoint moon_offset_px(const EclipseData *d, time_t now, int16_t sun_r, int16_t moon_r) {
  if (d->sample_count == 0) return GPoint(10000, 10000);
  uint16_t sep_now = celestial_interp_separation_centideg(d, now);
  uint16_t sep_ref = d->sep_samples_centideg[0];
  if (sep_ref == 0) sep_ref = 1;
  int32_t max_offset_px = sun_r + moon_r;
  int32_t offset_px = ((int32_t)sep_now * max_offset_px) / sep_ref;
  if (offset_px > max_offset_px) offset_px = max_offset_px;
  if (offset_px < 0) offset_px = 0;
  int32_t dir_deg = d->pos_angle_deg;
  if (now >= d->max_t) dir_deg = (dir_deg + 180) % 360;
  int32_t angle = (dir_deg * TRIG_MAX_ANGLE) / 360;
  return GPoint((offset_px * sin_lookup(angle)) / TRIG_MAX_RATIO,
                -(offset_px * cos_lookup(angle)) / TRIG_MAX_RATIO);
}

// Compact labels used by the small corner/edge Moon-phase slots.
const char *celestial_moon_phase_short_name(uint8_t pct, bool waxing) {
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

void celestial_layer_init(CelestialLayerState *state) {
  memset(state, 0, sizeof(*state));
}

void celestial_layer_update(CelestialLayerState *state, GContext *ctx, GRect bounds,
                            const EclipseData *d, time_t now, time_t sky_now,
                            bool skip_body_paint, bool suppress_other_bodies,
                            GColor sun_fill_color, GColor sun_outline_color) {
  int16_t alt = celestial_interp_sun_alt_decideg(d, sky_now);
  int16_t moon_alt = celestial_interp_moon_alt_decideg(d, sky_now);
  bool eclipse_moon_active = d->has_eclipse && now >= d->c1 && now <= d->c4;
  if (eclipse_moon_active && d->bottom_style != 1 && d->sunset != 0 && now >= d->sunset) eclipse_moon_active = false;
  bool fullscreen_sun = d->bottom_style == 1 && eclipse_moon_active;

  int16_t sun_r = eclipse_moon_active ? CELESTIAL_SUN_R_ECLIPSE :
    (int16_t)(((int32_t)CELESTIAL_SUN_R_NORMAL * (d->sun_moon_size_pct > 0 ? d->sun_moon_size_pct : 100)) / 100);
  if (sun_r < 4) sun_r = 4;
  if (fullscreen_sun) {
    int16_t min_dim = bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h;
    sun_r = (min_dim * 95) / 200;
  }
  int16_t moon_r = eclipse_moon_active
    ? (int16_t)(((int32_t)sun_r * (d->radius_ratio_pct > 0 ? d->radius_ratio_pct : 100)) / 100)
    : (int16_t)(((int32_t)CELESTIAL_MOON_R_NORMAL * (d->sun_moon_size_pct > 0 ? d->sun_moon_size_pct : 100)) / 100);
  if (moon_r < (eclipse_moon_active ? 4 : 3)) moon_r = eclipse_moon_active ? 4 : 3;

  GPoint sun_center;
  bool sun_up;
  if (fullscreen_sun) {
    sun_center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
    sun_up = true;
  } else {
    int16_t sun_alt_y = celestial_alt_to_y(alt, d->sky_scale_max_alt_decideg, bounds.size.h, sun_r);
    int16_t sun_y;
    sun_up = body_screen_y(sun_alt_y, d->sun_rise, d->sun_set, now,
                           bounds.size.h - CELESTIAL_GROUND_H, sun_r, &sun_y);
    sun_center = GPoint(bounds.size.w / 2, sun_y);
  }
  if (sun_up && !skip_body_paint) {
    graphics_context_set_fill_color(ctx, sun_fill_color);
    graphics_fill_circle(ctx, sun_center, sun_r);
    graphics_context_set_stroke_color(ctx, sun_outline_color);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, sun_center, sun_r);
  }

  bool moon_visible = false;
  GPoint moon_center = GPoint(0, 0);
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
  } else if (!eclipse_moon_active) {
    int16_t moon_alt_y = celestial_alt_to_y(moon_alt, d->sky_scale_max_alt_decideg, bounds.size.h, moon_r);
    int16_t moon_y;
    bool moon_up = body_screen_y(moon_alt_y, d->moon_rise, d->moon_set, now,
                                 bounds.size.h - CELESTIAL_GROUND_H, moon_r, &moon_y);
    if (moon_up) {
      moon_center = GPoint((bounds.size.w * 2) / 3, moon_y);
      if (sun_up) moon_center = enforce_min_separation(sun_center, moon_center, (sun_r * 3) / 2);
      moon_visible = true;
      if (!skip_body_paint) celestial_draw_moon_phase(ctx, bounds, moon_center, moon_r, d->moon_phase_pct, d->moon_waxing, GColorWhite);
    }
  }

  bool planet_visible[PLANET_COUNT] = { false };
  GPoint planet_center[PLANET_COUNT];
  for (int p = 0; p < PLANET_COUNT; p++) planet_center[p] = GPoint(0, 0);
  bool sky_dark_for_bodies = celestial_sky_is_dark(d, sky_now) || d->sky_mode == 2;
  if (sky_dark_for_bodies && !suppress_other_bodies) {
    for (int p = 0; p < PLANET_COUNT; p++) {
      int16_t p_alt = celestial_interp_planet_alt_decideg(d, (PlanetId)p, sky_now);
      int16_t p_alt_y = celestial_alt_to_y(p_alt, d->sky_scale_max_alt_decideg, bounds.size.h, CELESTIAL_PLANET_R);
      int16_t p_y;
      if (!body_screen_y(p_alt_y, d->planet_rise[p], d->planet_set[p], now,
                         bounds.size.h - CELESTIAL_GROUND_H, CELESTIAL_PLANET_R, &p_y)) continue;
      GPoint c = GPoint((bounds.size.w * PLANET_COLUMN_PCT[p]) / 100, p_y);
      planet_visible[p] = true; planet_center[p] = c;
      if (!skip_body_paint) {
        if (p == PLANET_SATURN) draw_saturn(ctx, c, d->saturn_ring_open_pct);
        else { graphics_context_set_fill_color(ctx, planet_color((PlanetId)p)); graphics_fill_circle(ctx, c, CELESTIAL_PLANET_R); }
      }
    }
  }

  bool star_visible[STAR_COUNT] = { false };
  GPoint star_center[STAR_COUNT];
  for (int s = 0; s < STAR_COUNT; s++) star_center[s] = GPoint(0, 0);
  if (d->sky_mode == 2 && !suppress_other_bodies) {
    for (int s = 0; s < STAR_COUNT; s++) {
      if (d->star_alt_decideg[s] <= 0) continue;
      int16_t y = celestial_alt_to_y(d->star_alt_decideg[s], d->sky_scale_max_alt_decideg, bounds.size.h, STAR_RADIUS[s]);
      int16_t x = (bounds.size.w * (int32_t)d->star_az_decideg[s]) / 3600;
      star_visible[s] = true; star_center[s] = GPoint(x, y);
      if (!skip_body_paint) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, star_center[s], STAR_RADIUS[s]);
      }
    }
  }

  bool iss_visible = false;
  GPoint iss_center = GPoint(0, 0);
  if (!suppress_other_bodies && d->show_iss && sky_dark_for_bodies && d->iss_alt_deg > 0 && d->iss_computed_at != 0) {
    time_t age = now - d->iss_computed_at;
    if (age >= 0 && age < 900) {
      int16_t y = celestial_alt_to_y(d->iss_alt_deg * 10, d->sky_scale_max_alt_decideg, bounds.size.h, CELESTIAL_ISS_R);
      int16_t x = (bounds.size.w * (int32_t)d->iss_az_deg) / 360;
      iss_visible = true; iss_center = GPoint(x, y);
      if (!skip_body_paint) {
        graphics_context_set_fill_color(ctx, GColorWhite); graphics_fill_circle(ctx, iss_center, CELESTIAL_ISS_R);
        graphics_context_set_stroke_color(ctx, GColorBlack); graphics_context_set_stroke_width(ctx, 1); graphics_draw_circle(ctx, iss_center, CELESTIAL_ISS_R);
      }
    }
  }

  state->sun_center = sun_center; state->sun_up = sun_up; state->sun_r = sun_r;
  state->moon_center = moon_center; state->moon_visible = moon_visible; state->moon_r = moon_r;
  state->eclipse_moon_active = eclipse_moon_active; state->fullscreen_sun = fullscreen_sun;
  state->sun_fill_color = sun_fill_color;
  state->iss_center = iss_center; state->iss_visible = iss_visible;
  for (int p = 0; p < PLANET_COUNT; p++) { state->planet_center[p] = planet_center[p]; state->planet_visible[p] = planet_visible[p]; }
  for (int s = 0; s < STAR_COUNT; s++) { state->star_center[s] = star_center[s]; state->star_visible[s] = star_visible[s]; }
}

const char *celestial_planet_name(PlanetId planet) {
  return PLANET_NAMES[planet];
}

const char *celestial_star_name(uint8_t star) {
  return star < STAR_COUNT ? STAR_NAMES[star] : "Star";
}

void celestial_layer_draw_visible_planet(GContext *ctx, const EclipseData *d,
                                         const CelestialLayerState *state, PlanetId planet) {
  if (planet >= PLANET_COUNT || !state->planet_visible[planet]) return;
  if (planet == PLANET_SATURN) draw_saturn(ctx, state->planet_center[planet], d->saturn_ring_open_pct);
  else {
    graphics_context_set_fill_color(ctx, planet_color(planet));
    graphics_fill_circle(ctx, state->planet_center[planet], CELESTIAL_PLANET_R);
  }
}

static void draw_label_in_box(GContext *ctx, GRect r, const char *text, uint8_t label_style, GColor main_color);

void celestial_layer_draw_label(GContext *ctx, GRect bounds, GPoint near, const char *text,
                                 uint8_t label_style, GColor main_color) {
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

void celestial_layer_draw_labels(GContext *ctx, GRect bounds, const EclipseData *d,
                                 const CelestialLayerState *state, uint8_t label_style,
                                 GColor main_color) {
  if (state->sun_up) celestial_layer_draw_label(ctx, bounds, state->sun_center, "Sun", label_style, main_color);
  if (state->moon_visible) celestial_layer_draw_label(ctx, bounds, state->moon_center, "Moon", label_style, main_color);
  for (int p = 0; p < PLANET_COUNT; p++) {
    if (!state->planet_visible[p]) continue;
    celestial_layer_draw_visible_planet(ctx, d, state, (PlanetId)p);
    celestial_layer_draw_label(ctx, bounds, state->planet_center[p], PLANET_NAMES[p], label_style, main_color);
  }
  for (int s = 0; s < STAR_COUNT; s++) {
    if (state->star_visible[s]) celestial_layer_draw_label(ctx, bounds, state->star_center[s], STAR_NAMES[s], label_style, main_color);
  }
  if (state->iss_visible) celestial_layer_draw_label(ctx, bounds, state->iss_center, "ISS", label_style, main_color);
}

void celestial_layer_draw_bg_anim_planets(GContext *ctx, GRect bounds,
                                          const EclipseData *d,
                                          const CelestialLayerState *state) {
  if (state->sun_up) {
    GColor fill = d->sky_mode == 2 ? GColorFromRGB(255, 190, 60) : state->sun_fill_color;
    graphics_context_set_fill_color(ctx, fill); graphics_fill_circle(ctx, state->sun_center, state->sun_r);
  }
  if (state->moon_visible) celestial_draw_moon_phase(ctx, bounds, state->moon_center, state->moon_r, d->moon_phase_pct, d->moon_waxing, GColorWhite);
  for (int p = 0; p < PLANET_COUNT; p++) {
    if (!state->planet_visible[p]) continue;
    if (p == PLANET_SATURN) draw_saturn(ctx, state->planet_center[p], d->saturn_ring_open_pct);
    else { graphics_context_set_fill_color(ctx, planet_color((PlanetId)p)); graphics_fill_circle(ctx, state->planet_center[p], CELESTIAL_PLANET_R); }
  }
}

static int32_t planet_seek_az_offset_decideg(uint16_t az_decideg, int32_t heading_deg) {
  int32_t diff = (int32_t)az_decideg - heading_deg * 10;
  diff %= 3600;
  if (diff > 1800) diff -= 3600;
  if (diff < -1800) diff += 3600;
  return diff;
}

static void draw_label_in_box(GContext *ctx, GRect r, const char *text, uint8_t label_style, GColor main_color) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GRect text_box = GRect(r.origin.x, r.origin.y - 2, r.size.w, r.size.h + 2);
  if (label_style == 1) {
    features_draw_text_outlined(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, main_color, 1);
    return;
  }
  if (label_style == 2) {
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
  graphics_context_set_fill_color(ctx, GColorBlack); graphics_fill_rect(ctx, r, 2, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void draw_planet_seek_arrow(GContext *ctx, GPoint tip, bool points_left, GColor color) {
  GPoint pts_left[3] = { GPoint(tip.x, tip.y), GPoint(tip.x + CELESTIAL_ARROW_W, tip.y - 4), GPoint(tip.x + CELESTIAL_ARROW_W, tip.y + 4) };
  GPoint pts_right[3] = { GPoint(tip.x, tip.y), GPoint(tip.x - CELESTIAL_ARROW_W, tip.y - 4), GPoint(tip.x - CELESTIAL_ARROW_W, tip.y + 4) };
  GPathInfo info = { .num_points = 3, .points = points_left ? pts_left : pts_right };
  GPath *path = gpath_create(&info); graphics_context_set_fill_color(ctx, color); gpath_draw_filled(ctx, path); gpath_destroy(path);
}

static void draw_planet_seek_edge_label(GContext *ctx, GRect bounds, GPoint arrow_tip, bool pin_right,
                                        const char *text, uint8_t label_style, GColor main_color) {
  int16_t w = 46, h = 14, gap = 2;
  int16_t arrow_base_x = pin_right ? arrow_tip.x - CELESTIAL_ARROW_W : arrow_tip.x + CELESTIAL_ARROW_W;
  int16_t x = pin_right ? arrow_base_x - gap - w : arrow_base_x + gap;
  int16_t y = arrow_tip.y - h / 2;
  if (y < bounds.origin.y) y = bounds.origin.y;
  if (y + h > bounds.origin.y + bounds.size.h) y = bounds.origin.y + bounds.size.h - h;
  draw_label_in_box(ctx, GRect(x, y, w, h), text, label_style, main_color);
}

static void draw_planet_seek_body(GContext *ctx, GRect bounds, const char *name,
                                  uint16_t az_decideg, GPoint normal_center, int16_t radius,
                                  GColor fill_color, int32_t heading_deg, int32_t blend_t_1000,
                                  uint8_t label_style, GColor main_color,
                                  bool is_moon, uint8_t moon_phase_pct, bool moon_waxing) {
  int32_t offset_decideg = planet_seek_az_offset_decideg(az_decideg, heading_deg);
  bool in_fov = offset_decideg >= -450 && offset_decideg <= 450;
  if (in_fov) {
    int32_t compass_x = bounds.origin.x + bounds.size.w / 2 + (int32_t)((int64_t)offset_decideg * bounds.size.w / 900);
    int16_t blended_x = (int16_t)(normal_center.x + (((int32_t)compass_x - normal_center.x) * blend_t_1000) / 1000);
    GPoint pos = GPoint(blended_x, normal_center.y);
    if (pos.x >= bounds.origin.x - radius && pos.x <= bounds.origin.x + bounds.size.w + radius) {
      if (is_moon) celestial_draw_moon_phase(ctx, bounds, pos, radius, moon_phase_pct, moon_waxing, fill_color);
      else { graphics_context_set_fill_color(ctx, fill_color); graphics_fill_circle(ctx, pos, radius); }
      // On-screen case: the body itself is drawn above, but the label was
      // missing here entirely -- only the off-screen/edge-arrow branch
      // below ever called a label draw, so a shake-revealed body that
      // stayed in FOV throughout its Planet-seek animation never got a
      // name label, and one only appeared once the body's blended
      // position crossed off-screen and hit draw_planet_seek_edge_label()
      // instead. Reuse the same near-point label helper the non-Planet-
      // seek path uses (celestial_layer_draw_labels() above); it already
      // clamps itself to bounds.
      celestial_layer_draw_label(ctx, bounds, pos, name, label_style, main_color);
      return;
    }
  }
  bool pin_right = offset_decideg > 0;
  int16_t edge_arrow_x = pin_right ? bounds.origin.x + bounds.size.w - 2 : bounds.origin.x + 2;
  int16_t blended_arrow_x = (int16_t)(normal_center.x + (((int32_t)edge_arrow_x - normal_center.x) * blend_t_1000) / 1000);
  GPoint arrow_tip = GPoint(blended_arrow_x, normal_center.y);
  draw_planet_seek_edge_label(ctx, bounds, arrow_tip, pin_right, name, label_style, main_color);
  draw_planet_seek_arrow(ctx, arrow_tip, !pin_right, main_color);
}

void celestial_layer_draw_planet_seek(GContext *ctx, GRect bounds, const EclipseData *d,
                                      const CelestialLayerState *state, time_t now,
                                      int32_t heading_deg, int32_t blend_t_1000,
                                      uint8_t label_style, GColor main_color) {
  uint8_t pct = d->sun_moon_size_pct > 0 ? d->sun_moon_size_pct : 100;
  int16_t sun_r = (CELESTIAL_SUN_R_NORMAL * pct) / 100; if (sun_r < 4) sun_r = 4;
  int16_t moon_r = (CELESTIAL_MOON_R_NORMAL * pct) / 100; if (moon_r < 4) moon_r = 4;
  if (state->sun_up) {
    GColor sun_fill = d->sky_mode == 2 ? GColorFromRGB(255, 190, 60) : state->sun_fill_color;
    draw_planet_seek_body(ctx, bounds, "Sun", celestial_interp_sun_az_decideg(d, now), state->sun_center, sun_r,
                           sun_fill, heading_deg, blend_t_1000, label_style, main_color, false, 0, false);
  }
  if (state->moon_visible) {
    draw_planet_seek_body(ctx, bounds, "Moon", celestial_interp_moon_az_decideg(d, now), state->moon_center, moon_r,
                           GColorWhite, heading_deg, blend_t_1000, label_style, main_color, true, d->moon_phase_pct, d->moon_waxing);
  }
  for (int p = 0; p < PLANET_COUNT; p++) if (state->planet_visible[p]) {
    draw_planet_seek_body(ctx, bounds, PLANET_NAMES[p], celestial_interp_planet_az_decideg(d, (PlanetId)p, now),
                           state->planet_center[p], CELESTIAL_PLANET_R, planet_color((PlanetId)p), heading_deg,
                           blend_t_1000, label_style, main_color, false, 0, false);
  }
  if (d->sky_mode == 2) {
    for (int s = 0; s < STAR_COUNT; s++) {
      if (!state->star_visible[s]) continue;
      draw_planet_seek_body(ctx, bounds, STAR_NAMES[s], d->star_az_decideg[s], state->star_center[s], STAR_RADIUS[s],
                             GColorWhite, heading_deg, blend_t_1000, label_style, main_color, false, 0, false);
    }
  }
  if (state->iss_visible) {
    draw_planet_seek_body(ctx, bounds, "ISS", (uint16_t)(d->iss_az_deg * 10), state->iss_center,
                           CELESTIAL_ISS_R, GColorWhite, heading_deg, blend_t_1000, label_style, main_color, false, 0, false);
  }
}
