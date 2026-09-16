#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"

typedef void (*TimeServiceTickHandler)(struct tm *tick_time, TimeUnits units_changed, void *context);

// Owns the tick subscription and selects minute or second resolution.

void time_service_init(const EclipseData *data, TimeServiceTickHandler handler, void *context);
void time_service_update(void);
void time_service_deinit(void);

bool time_service_is_seconds(void);

bool time_service_live_seconds_now(time_t now);
