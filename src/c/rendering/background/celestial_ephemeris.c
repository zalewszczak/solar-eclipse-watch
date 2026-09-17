#include "./celestial_ephemeris.h"
#include <pebble.h>

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

