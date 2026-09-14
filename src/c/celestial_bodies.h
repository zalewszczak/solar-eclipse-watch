#pragma once

#include <pebble.h>
#include "celestial_layer.h"
#include "eclipse_data.h"

GColor celestial_bodies_planet_color(PlanetId planet);
void celestial_bodies_draw_saturn(GContext *ctx, GPoint center, uint8_t ring_open_pct);
const char *celestial_bodies_planet_name(PlanetId planet);
const char *celestial_bodies_star_name(uint8_t star);
uint8_t celestial_bodies_star_radius(uint8_t star);
void celestial_bodies_draw_visible_planet(GContext *ctx, const EclipseData *data,
                                          const CelestialLayerState *state, PlanetId planet);
void celestial_bodies_draw_planet_seek(GContext *ctx, GRect bounds, const EclipseData *data,
                                       const CelestialLayerState *state, time_t now,
                                       int32_t heading_deg, int32_t blend_t_1000,
                                       uint8_t label_style, GColor main_color);
