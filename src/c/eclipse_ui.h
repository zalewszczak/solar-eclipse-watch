#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// Resolves the active day/night color scheme into concrete Pebble colors.
// The caller supplies the current time because night mode follows the same
// civil-twilight brightness rule used by the sky renderer.
void eclipse_ui_get_active_color_scheme(const EclipseData *data, time_t now,
                                        GColor *background, GColor *text, GColor *accent);

// Reconstructs a Pebble color from the packed 2-bit-per-channel value sent
// by the settings page.
GColor eclipse_ui_color_from_packed(uint8_t packed);

// Returns the next sunrise/sunset event represented by the supplied daily
// data. If today's events have passed, tomorrow's sunrise is used when
// available.
bool eclipse_ui_get_next_sun_event(time_t now, time_t sunrise, time_t sunset,
                                    time_t sunrise_tomorrow, time_t *event_time,
                                    bool *is_sunrise);
