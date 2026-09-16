#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"

// Continuous RGB representation used while constructing the sky gradient.
typedef struct {
  uint8_t r, g, b;
} SkyRgb;

// Interpolate the transmitted cloud-cover samples at an arbitrary time.
uint8_t sky_layer_interp_cloud_pct(const EclipseData *data, time_t now);

// Compute the atmosphere-dependent Sun disc color.
SkyRgb sky_layer_sun_color_for_altitude(int16_t alt_decideg);

// Fixed Sun color used when the display is not representing an
// atmosphere-dependent view (Space, fullscreen eclipse Sun, etc.).
SkyRgb sky_layer_space_sun_color(void);

// Shared integer helpers used by the background compositor while keeping
// sky-specific math in this module.
int16_t sky_layer_compute_cloud_band_y_virtual(int16_t virtual_top_y,
                                               int16_t virtual_total_h,
                                               uint8_t cloud_altitude_pct);

// Compute the top/zenith and horizon colors for the current Sun altitude.
void sky_layer_colors_for_altitude(int16_t alt_decideg, SkyRgb *top_out, SkyRgb *horizon_out);

// Paint a dithered vertical sky gradient. The virtual coordinates allow the
void sky_layer_fill_gradient(GContext *ctx, GRect bounds,
                             int16_t virtual_top_y, int16_t virtual_total_h,
                             SkyRgb top, SkyRgb band, int16_t band_y, SkyRgb horizon);

// Compute the weather-adjusted sky wash shared by the main canvas and the
// Digital-top gradient strip.


// Cheap daylight/twilight test used by countdown/color policy.
bool sky_layer_is_bright(const EclipseData *data, time_t now);

// Digital-top layout's gradient-only layer.
Layer *sky_layer_top_gradient_create(GRect frame);
void sky_layer_top_gradient_destroy(Layer *layer);
void sky_layer_top_gradient_set_data(Layer *layer, EclipseData *data);
