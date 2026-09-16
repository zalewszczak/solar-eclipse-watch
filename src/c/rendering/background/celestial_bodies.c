#include "./celestial_bodies.h"
#include "./celestial_layer.h"
#include "./celestial_ephemeris.h"
#include "../../features/feature_render.h"
#include <string.h>

#define CELESTIAL_ARROW_W 6

static const char PLANET_NAMES[] = "Mercury\0Venus\0Mars\0Jupiter\0Saturn\0";
static const uint8_t PLANET_NAME_OFFSETS[PLANET_COUNT] = { 0, 8, 14, 19, 27 };
static const char STAR_NAMES[] =
  "Sirius\0Canopus\0Arcturus\0Vega\0Capella\0Rigel\0Procyon\0Betelgeuse\0"
  "Altair\0Aldebaran\0Antares\0Spica\0Pollux\0Fomalhaut\0Deneb\0Regulus\0";
// Computed once from STAR_NAMES itself (see star_name_offsets() below)
// rather than hand-maintained: a hardcoded offset table silently goes
// stale the moment a name in STAR_NAMES is edited without recomputing
// every offset after it by hand, which is exactly what happened here
// (most of these offsets pointed one-or-two bytes short, into the
// previous star's null terminator, so those stars rendered blank).
static uint8_t s_star_name_offsets[STAR_COUNT];
static bool s_star_name_offsets_ready = false;

static const uint8_t *star_name_offsets(void) {
  if (!s_star_name_offsets_ready) {
    uint8_t off = 0;
    for (int i = 0; i < STAR_COUNT; i++) {
      s_star_name_offsets[i] = off;
      off = (uint8_t)(off + strlen(STAR_NAMES + off) + 1);
    }
    s_star_name_offsets_ready = true;
  }
  return s_star_name_offsets;
}
GColor celestial_bodies_planet_color(PlanetId p) {
  switch (p) {
    case PLANET_MERCURY: return GColorLightGray;
    case PLANET_VENUS: return GColorWhite;
    case PLANET_MARS: return GColorRed;
    case PLANET_JUPITER: return GColorYellow;
    case PLANET_SATURN: default: return GColorYellow;
  }
}

static int32_t planet_seek_ease_out_1000(int32_t t) {
  int32_t inv = 1000 - t;
  int64_t inv3 = ((int64_t)inv * inv * inv) / 1000000;
  int32_t result = 1000 - (int32_t)inv3;
  return result > 1000 ? 1000 : result;
}

int32_t celestial_bodies_planet_seek_eased_t_1000(uint16_t elapsed_ms, const EclipseData *data) {
  uint32_t duration_ms = (uint32_t)(data->shake_label_seconds > 0 ? data->shake_label_seconds : 3) * 1000;
  if (elapsed_ms < 500) {
    return planet_seek_ease_out_1000(((int32_t)elapsed_ms * 1000) / 500);
  }

  uint32_t ease_out_window_ms = duration_ms > 2000 ? 1000 : duration_ms / 2;
  if (ease_out_window_ms > 0 && elapsed_ms > duration_ms - ease_out_window_ms) {
    int32_t remaining = (int32_t)duration_ms - (int32_t)elapsed_ms;
    if (remaining < 0) remaining = 0;
    return planet_seek_ease_out_1000((remaining * 1000) / (int32_t)ease_out_window_ms);
  }

  return 1000;
}

void celestial_bodies_draw_saturn(GContext *ctx, GPoint center, uint8_t ring_open_pct) {
  graphics_context_set_fill_color(ctx, GColorYellow);
  graphics_fill_circle(ctx, center, CELESTIAL_PLANET_R);
  int16_t ring_span = CELESTIAL_PLANET_R + 4;
  int16_t ring_thickness = 1 + (int16_t)((ring_open_pct * 2) / 100);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(center.x - ring_span, center.y - ring_thickness / 2,
                                ring_span * 2, ring_thickness), 0, GCornerNone);
}

const char *celestial_bodies_planet_name(PlanetId planet) {
  return planet < PLANET_COUNT ? PLANET_NAMES + PLANET_NAME_OFFSETS[planet] : "Planet";
}

const char *celestial_bodies_star_name(uint8_t star) {
  return star < STAR_COUNT ? STAR_NAMES + star_name_offsets()[star] : "Star";
}

void celestial_bodies_draw_visible_planet(GContext *ctx, const EclipseData *d,
                                         const CelestialLayerState *state, PlanetId planet) {
  if (planet >= PLANET_COUNT || !state->planet_visible[planet]) return;
  if (planet == PLANET_SATURN) celestial_bodies_draw_saturn(ctx, state->planet_center[planet], d->saturn_ring_open_pct);
  else {
    graphics_context_set_fill_color(ctx, celestial_bodies_planet_color(planet));
    graphics_fill_circle(ctx, state->planet_center[planet], CELESTIAL_PLANET_R);
  }
}

uint8_t celestial_bodies_star_radius(uint8_t star) {
  return star < STAR_COUNT ? (star < 7 ? 2 : 1) : 1;
}

static int32_t planet_seek_az_offset_decideg(uint16_t az_decideg, int32_t heading_deg) {
  int32_t diff = (int32_t)az_decideg - heading_deg * 10;
  diff %= 3600;
  if (diff > 1800) diff -= 3600;
  if (diff < -1800) diff += 3600;
  return diff;
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
  celestial_draw_label_in_box(ctx, GRect(x, y, w, h), text, label_style, main_color);
}

static void draw_planet_seek_body(GContext *ctx, GRect bounds, const char *name,
                                  uint16_t az_decideg, GPoint normal_center, int16_t radius,
                                  GColor fill_color, int32_t heading_deg, int32_t blend_t_1000,
                                  uint8_t label_style, GColor main_color,
                                  bool is_moon, uint8_t moon_phase_pct, bool moon_waxing,
                                  bool is_saturn, uint8_t saturn_ring_open_pct) {
  int32_t offset_decideg = planet_seek_az_offset_decideg(az_decideg, heading_deg);
// Deliberately NOT gated on a raw (pre-blend) "is this within the
  int32_t compass_x = bounds.origin.x + bounds.size.w / 2 + (int32_t)((int64_t)offset_decideg * bounds.size.w / 900);
  int16_t blended_x = (int16_t)(normal_center.x + (((int32_t)compass_x - normal_center.x) * blend_t_1000) / 1000);
  GPoint pos = GPoint(blended_x, normal_center.y);
  if (pos.x >= bounds.origin.x - radius && pos.x <= bounds.origin.x + bounds.size.w + radius) {
// Every other body-drawing path in this file (celestial_layer_update(),
    if (is_moon) celestial_draw_moon_phase(ctx, bounds, pos, radius, moon_phase_pct, moon_waxing, fill_color);
    else if (is_saturn) celestial_bodies_draw_saturn(ctx, pos, saturn_ring_open_pct);
    else { graphics_context_set_fill_color(ctx, fill_color); graphics_fill_circle(ctx, pos, radius); }
// On-screen case: the body itself is drawn above, but the label was
    celestial_layer_draw_label(ctx, bounds, pos, name, label_style, main_color);
    return;
  }
  bool pin_right = pos.x > bounds.origin.x + bounds.size.w / 2;
// Clamp the SAME blended `pos.x` used for the on-screen check above,
  int16_t clamped_x = pin_right ? bounds.origin.x + bounds.size.w - 2 : bounds.origin.x + 2;
  GPoint arrow_tip = GPoint(clamped_x, normal_center.y);
  draw_planet_seek_edge_label(ctx, bounds, arrow_tip, pin_right, name, label_style, main_color);
  draw_planet_seek_arrow(ctx, arrow_tip, !pin_right, main_color);
}

void celestial_bodies_draw_planet_seek(GContext *ctx, GRect bounds, const EclipseData *d,
                                      const CelestialLayerState *state, time_t now,
                                      int32_t heading_deg, int32_t blend_t_1000,
                                      uint8_t label_style, GColor main_color,
                                      uint16_t elapsed_ms) {
  uint8_t pct = d->sun_moon_size_pct > 0 ? d->sun_moon_size_pct : 100;
  int16_t sun_r = (CELESTIAL_SUN_R_NORMAL * pct) / 100; if (sun_r < 4) sun_r = 4;
  int16_t moon_r = (CELESTIAL_MOON_R_NORMAL * pct) / 100; if (moon_r < 4) moon_r = 4;
  if (state->sun_up) {
    GColor sun_fill = d->sky_mode == 2 ? GColorFromRGB(255, 190, 60) : state->sun_fill_color;
    draw_planet_seek_body(ctx, bounds, "Sun", celestial_interp_sun_az_decideg(d, now), state->sun_center, sun_r,
                           sun_fill, heading_deg, blend_t_1000, label_style, main_color, false, 0, false, false, 0);
  }
  if (state->moon_visible) {
    draw_planet_seek_body(ctx, bounds, "Moon", celestial_interp_moon_az_decideg(d, now), state->moon_center, moon_r,
                           GColorWhite, heading_deg, blend_t_1000, label_style, main_color, true, d->moon_phase_pct, d->moon_waxing, false, 0);
  }
  for (int p = 0; p < PLANET_COUNT; p++) if (state->planet_visible[p]) {
    draw_planet_seek_body(ctx, bounds, celestial_bodies_planet_name((PlanetId)p), celestial_interp_planet_az_decideg(d, (PlanetId)p, now),
                           state->planet_center[p], CELESTIAL_PLANET_R, celestial_bodies_planet_color((PlanetId)p), heading_deg,
                           blend_t_1000, label_style, main_color, false, 0, false,
                           p == PLANET_SATURN, d->saturn_ring_open_pct);
  }
  if (d->sky_mode == 2 && d->show_major_stars) {
    for (int s = 0; s < STAR_COUNT; s++) {
      if (!state->star_visible[s]) continue;
      draw_planet_seek_body(ctx, bounds, celestial_bodies_star_name((uint8_t)s), d->star_az_decideg[s], state->star_center[s], celestial_bodies_star_radius((uint8_t)s),
                             GColorWhite, heading_deg, blend_t_1000, label_style, main_color, false, 0, false, false, 0);
    }
  }
  // Overhead objects: ISS (when show_iss is on) and nearby flights (when
  // show_flights is on) unified into one dynamic, cyan-blinking-point list
  // -- see comms_maybe_request_flights() for how/when it gets (re)fetched.
  // This replaces the old ISS-only draw here; the ambient, always-on ISS
  // dot drawn on the regular (non-shake) sky canvas is untouched and still
  // uses its own state->iss_visible/state->iss_center. Blinks by skipping
  // the whole draw (body + label) for half of every second; no "normal"
  // on-canvas position exists for these (they're shake-only), so they
  // always seek in from the center of the compass view rather than
  // blending from one.
  if ((d->show_flights || d->show_iss) && ((elapsed_ms / 500) % 2) == 0) {
    GPoint center_pt = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + bounds.size.h / 2);
    for (int i = 0; i < d->overhead_object_count && i < MAX_OVERHEAD_OBJECTS; i++) {
      const OverheadObject *obj = &d->overhead_objects[i];
      if (obj->alt_deg <= 0) continue;
      draw_planet_seek_body(ctx, bounds, obj->is_iss ? "ISS" : "Flight",
                             (uint16_t)(obj->az_deg * 10), center_pt, 2, GColorCyan,
                             heading_deg, blend_t_1000, label_style, GColorCyan,
                             false, 0, false, false, 0);
    }
  }
}
