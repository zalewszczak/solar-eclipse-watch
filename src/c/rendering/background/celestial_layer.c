#include "./celestial_layer.h"
#include "./celestial_ephemeris.h"
#include "./celestial_bodies.h"
#include "../../features/feature_render.h"
#include <string.h>

#define CELESTIAL_ARROW_W 6


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

// Linear azimuth-to-x mapping the star field and ISS already used --
// the full 0-360 deg compass circle unrolled across the screen's own
// width, so x=0 is due north and x=canvas_w is back around to north
// The Sun, Moon, and planets share the same vertical mapping as the sky.
// fixed horizontal "lanes" instead (screen-center, 2/3-across, and a
// handful of hardcoded per-planet percentages) with only their Y
// reflecting real altitude -- harmless as a stylized layout, but
// inconsistent with stars/ISS already moving with real bearing, and
// not what a "sky view" reads as to someone expecting compass-accurate
// positions. Sun/Moon/planets now share this same mapping.
static int16_t celestial_az_decideg_to_x(uint16_t az_decideg, int16_t canvas_w) {
  return (int16_t)(((int32_t)canvas_w * az_decideg) / 3600);
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
    // sky_now (not now) matches alt above -- keeps X and Y sweeping
    // together during the startup "animate background" substitution.
    uint16_t sun_az = celestial_interp_sun_az_decideg(d, sky_now);
    sun_center = GPoint(celestial_az_decideg_to_x(sun_az, bounds.size.w), sun_y);
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
      uint16_t moon_az = celestial_interp_moon_az_decideg(d, sky_now);
      moon_center = GPoint(celestial_az_decideg_to_x(moon_az, bounds.size.w), moon_y);
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
      uint16_t p_az = celestial_interp_planet_az_decideg(d, (PlanetId)p, sky_now);
      GPoint c = GPoint(celestial_az_decideg_to_x(p_az, bounds.size.w), p_y);
      planet_visible[p] = true; planet_center[p] = c;
      if (!skip_body_paint) {
        if (p == PLANET_SATURN) celestial_bodies_draw_saturn(ctx, c, d->saturn_ring_open_pct);
        else { graphics_context_set_fill_color(ctx, celestial_bodies_planet_color((PlanetId)p)); graphics_fill_circle(ctx, c, CELESTIAL_PLANET_R); }
      }
    }
  }

  bool star_visible[STAR_COUNT] = { false };
  GPoint star_center[STAR_COUNT];
  for (int s = 0; s < STAR_COUNT; s++) star_center[s] = GPoint(0, 0);
  if (d->sky_mode == 2 && d->show_major_stars && !suppress_other_bodies) {
    for (int s = 0; s < STAR_COUNT; s++) {
      if (d->star_alt_decideg[s] <= 0) continue;
      int16_t y = celestial_alt_to_y(d->star_alt_decideg[s], d->sky_scale_max_alt_decideg, bounds.size.h, celestial_bodies_star_radius((uint8_t)s));
      int16_t x = celestial_az_decideg_to_x(d->star_az_decideg[s], bounds.size.w);
      star_visible[s] = true; star_center[s] = GPoint(x, y);
      if (!skip_body_paint) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, star_center[s], celestial_bodies_star_radius((uint8_t)s));
      }
    }
  }

  bool iss_visible = false;
  GPoint iss_center = GPoint(0, 0);
  if (!suppress_other_bodies && d->show_iss && sky_dark_for_bodies && d->iss_alt_deg > 0 && d->iss_computed_at != 0) {
    time_t age = now - d->iss_computed_at;
    if (age >= 0 && age < 900) {
      int16_t y = celestial_alt_to_y(d->iss_alt_deg * 10, d->sky_scale_max_alt_decideg, bounds.size.h, CELESTIAL_ISS_R);
      int16_t x = celestial_az_decideg_to_x(d->iss_az_deg * 10, bounds.size.w);
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

void celestial_draw_label_in_box(GContext *ctx, GRect r, const char *text, uint8_t label_style, GColor main_color) {
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GRect text_box = GRect(r.origin.x, r.origin.y - 2, r.size.w, r.size.h + 2);
  if (label_style == 1) {
    feature_render_draw_text_outlined(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, main_color, 1);
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
  graphics_draw_text(ctx, text, font, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

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
  celestial_draw_label_in_box(ctx, GRect(x, y, w, h), text, label_style, main_color);
}

void celestial_layer_draw_labels(GContext *ctx, GRect bounds, const EclipseData *d,
                                 const CelestialLayerState *state, uint8_t label_style,
                                 GColor main_color) {
  if (state->sun_up) celestial_layer_draw_label(ctx, bounds, state->sun_center, "Sun", label_style, main_color);
  if (state->moon_visible) celestial_layer_draw_label(ctx, bounds, state->moon_center, "Moon", label_style, main_color);
  for (int p = 0; p < PLANET_COUNT; p++) {
    if (!state->planet_visible[p]) continue;
    celestial_bodies_draw_visible_planet(ctx, d, state, (PlanetId)p);
    celestial_layer_draw_label(ctx, bounds, state->planet_center[p], celestial_bodies_planet_name((PlanetId)p), label_style, main_color);
  }
  for (int s = 0; s < STAR_COUNT; s++) {
    if (state->star_visible[s]) celestial_layer_draw_label(ctx, bounds, state->star_center[s], celestial_bodies_star_name((uint8_t)s), label_style, main_color);
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
    if (p == PLANET_SATURN) celestial_bodies_draw_saturn(ctx, state->planet_center[p], d->saturn_ring_open_pct);
    else { graphics_context_set_fill_color(ctx, celestial_bodies_planet_color((PlanetId)p)); graphics_fill_circle(ctx, state->planet_center[p], CELESTIAL_PLANET_R); }
  }
  // Space view's star field -- celestial_layer_update() above already
  // computes state->star_visible[]/star_center[] every frame regardless
  // of skip_body_paint (same as it does for the Sun/Moon/planets), but
  // Repaint these bodies explicitly here so the cached background remains
  // never appeared for the ~2s "animate background on start" sweep and
  // only popped in once bg_anim_mode 1 ended and the normal (non-
  // skip_body_paint) draw path took over. Same plain white fill circle
  // celestial_layer_update() itself uses; no per-star animation of
  // their own to blend since -- unlike the Sun/Moon/planets -- stars
  // don't move with the sweep's altitude/azimuth interpolation at all.
  for (int s = 0; s < STAR_COUNT; s++) {
    if (!state->star_visible[s]) continue;
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, state->star_center[s], celestial_bodies_star_radius((uint8_t)s));
  }
}

