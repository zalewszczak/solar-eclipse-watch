#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

typedef void (*TimeServiceTickHandler)(struct tm *tick_time, TimeUnits units_changed, void *context);

// Owns the Pebble tick subscription and decides whether the watch needs
// second- or minute-resolution ticks. Application code supplies the tick
// callback; this module owns only scheduling/policy, not rendering.
void time_service_init(const EclipseData *data, TimeServiceTickHandler handler, void *context);
void time_service_update(void);
void time_service_deinit(void);

// True when the current subscription is SECOND_UNIT. This is intentionally
// separate from the subscription itself so callers can cheaply gate work
// that only makes sense on a per-second tick.
bool time_service_is_seconds(void);

// Returns whether the display should currently be driven at second
// precision. Battery-saver resting phases always force this false.
bool time_service_live_seconds_now(time_t now);
