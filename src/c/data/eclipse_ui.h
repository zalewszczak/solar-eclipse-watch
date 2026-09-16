#pragma once

#include <pebble.h>
#include "./eclipse_data.h"

// Resolves the active day/night color scheme.
void eclipse_ui_get_active_color_scheme(const EclipseData *data, time_t now,
                                        GColor *background, GColor *text, GColor *accent);

// Returns the next sunrise or sunset from the supplied daily times.
bool eclipse_ui_get_next_sun_event(time_t now, time_t sunrise, time_t sunset,
                                    time_t sunrise_tomorrow, time_t *event_time,
                                    bool *is_sunrise);
