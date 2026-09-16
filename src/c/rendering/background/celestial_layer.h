#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"

#define CELESTIAL_SUN_R_ECLIPSE 30
#define CELESTIAL_SUN_R_NORMAL 20
#define CELESTIAL_MOON_R_NORMAL 16
#define CELESTIAL_PLANET_R 3
#define CELESTIAL_ISS_R 3
#define CELESTIAL_GROUND_H 18
#define CELESTIAL_SKY_TOP_MARGIN 20
// Minimum on-screen gap kept between the sun's and moon's circles when no
// eclipse is active/scheduled -- enforced regardless of adjustable
// sun/moon size or azimuth-to-x squashing, so a normal day never LOOKS
// like an eclipse is underway.
#define CELESTIAL_MIN_BODY_GAP_PX 4

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

const char *celestial_moon_phase_short_name(uint8_t pct, bool waxing);



void celestial_draw_moon_phase(GContext *ctx, GRect bounds, GPoint center, int16_t radius,
                    uint8_t phase_pct, bool waxing, GColor lit_color);

// Computes and caches every point-like celestial body used by the sky canvas.
void celestial_layer_update(CelestialLayerState *state, GContext *ctx, GRect bounds,
                            const EclipseData *data, time_t now, time_t sky_now,
                            bool skip_body_paint, bool suppress_other_bodies,
                            GColor sun_fill_color, GColor sun_outline_color);

void celestial_layer_draw_bg_anim_planets(GContext *ctx, GRect bounds,
                                          const EclipseData *data,
                                          const CelestialLayerState *state);

void celestial_draw_label_in_box(GContext *ctx, GRect r, const char *text,
                                   uint8_t label_style, GColor main_color);

void celestial_layer_draw_label(GContext *ctx, GRect bounds, GPoint near, const char *text,
                               uint8_t label_style, GColor main_color);

void celestial_layer_draw_labels(GContext *ctx, GRect bounds, const EclipseData *data,
                                 const CelestialLayerState *state, uint8_t label_style,
                                 GColor main_color);

