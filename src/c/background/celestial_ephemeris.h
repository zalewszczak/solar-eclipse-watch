#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

#define CELESTIAL_RISE_SET_TRANSITION_S 180

uint16_t celestial_interp_separation_centideg(const EclipseData *data, time_t now);
uint8_t celestial_interp_mag_pct(const EclipseData *data, time_t now);
int16_t celestial_interp_sun_alt_decideg(const EclipseData *data, time_t now);
int16_t celestial_interp_moon_alt_decideg(const EclipseData *data, time_t now);
int16_t celestial_interp_planet_alt_decideg(const EclipseData *data, PlanetId planet, time_t now);
uint16_t celestial_interp_sun_az_decideg(const EclipseData *data, time_t now);
uint16_t celestial_interp_moon_az_decideg(const EclipseData *data, time_t now);
uint16_t celestial_interp_planet_az_decideg(const EclipseData *data, PlanetId planet, time_t now);

uint8_t celestial_count_visible_planets(const EclipseData *data, time_t now);
int celestial_compute_eclipse_phase(const EclipseData *data, time_t now);
bool celestial_compute_iss_visible(const EclipseData *data, time_t now, bool sky_is_dark);
bool celestial_sky_is_dark(const EclipseData *data, time_t now);
