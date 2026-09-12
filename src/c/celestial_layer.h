#pragma once

#include <pebble.h>
#include "eclipse_data.h"

#define CELESTIAL_SUN_R_ECLIPSE 30
#define CELESTIAL_SUN_R_NORMAL 20
#define CELESTIAL_MOON_R_NORMAL 16
#define CELESTIAL_PLANET_R 3
#define CELESTIAL_ISS_R 3
#define CELESTIAL_GROUND_H 18
#define CELESTIAL_SKY_TOP_MARGIN 20
#define CELESTIAL_RISE_SET_TRANSITION_S 180

typedef struct {
  GPoint sun_center;
  GPoint moon_center;
  GPoint planet_center[PLANET_COUNT];
  GPoint star_center[STAR_COUNT];
  GPoint iss_center;
  bool sun_up;
  bool moon_visible;
  bool planet_visible[PLANET_COUNT];
  bool star_visible[STAR_COUNT];
  bool iss_visible;
  bool eclipse_moon_active;
  bool fullscreen_sun;
  int16_t sun_r;
  int16_t moon_r;
  GColor sun_fill_color;
} CelestialLayerState;

void celestial_layer_init(CelestialLayerState *state);

int16_t celestial_interp_sun_alt_decideg(const EclipseData *data, time_t now);
int16_t celestial_interp_moon_alt_decideg(const EclipseData *data, time_t now);
int16_t celestial_interp_planet_alt_decideg(const EclipseData *data, PlanetId planet, time_t now);
uint16_t celestial_interp_sun_az_decideg(const EclipseData *data, time_t now);
uint16_t celestial_interp_moon_az_decideg(const EclipseData *data, time_t now);
uint16_t celestial_interp_planet_az_decideg(const EclipseData *data, PlanetId planet, time_t now);
uint16_t celestial_interp_separation_centideg(const EclipseData *data, time_t now);
uint8_t celestial_interp_mag_pct(const EclipseData *data, time_t now);

uint8_t celestial_count_visible_planets(const EclipseData *data, time_t now);
int celestial_compute_eclipse_phase(const EclipseData *data, time_t now);
bool celestial_compute_iss_visible(const EclipseData *data, time_t now, bool sky_is_dark);

bool celestial_sky_is_dark(const EclipseData *data, time_t now);
int16_t celestial_alt_to_y(int16_t altitude_decideg, int16_t scale_max_decideg,
                           int16_t canvas_h, int16_t radius);

void draw_moon_phase(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                    uint8_t phase_pct, bool waxing, GColor lit_color);

// Computes and caches every point-like celestial body used by the sky canvas.
// Normal body painting is optional because Planet Seek and the startup
// animation deliberately repaint the bodies as overlays instead of baking
// their positions into the cached sky bitmap.
void celestial_layer_update(CelestialLayerState *state, GContext *ctx, GRect bounds,
                            const EclipseData *data, time_t now, time_t sky_now,
                            bool skip_body_paint, bool suppress_other_bodies,
                            GColor sun_fill_color, GColor sun_outline_color);

void celestial_layer_draw_bg_anim_planets(GContext *ctx, GRect bounds,
                                          const EclipseData *data,
                                          const CelestialLayerState *state);

void celestial_layer_draw_label(GContext *ctx, GRect bounds, GPoint near, const char *text,
                               uint8_t label_style, GColor main_color);

void celestial_layer_draw_labels(GContext *ctx, GRect bounds, const EclipseData *data,
                                 const CelestialLayerState *state, uint8_t label_style,
                                 GColor main_color);

const char *celestial_planet_name(PlanetId planet);
const char *celestial_star_name(uint8_t star);
void celestial_layer_draw_visible_planet(GContext *ctx, const EclipseData *data,
                                         const CelestialLayerState *state, PlanetId planet);

void celestial_layer_draw_planet_seek(GContext *ctx, GRect bounds,
                                      const EclipseData *data,
                                      const CelestialLayerState *state,
                                      time_t now, int32_t heading_deg,
                                      int32_t blend_t_1000,
                                      uint8_t label_style, GColor main_color);
