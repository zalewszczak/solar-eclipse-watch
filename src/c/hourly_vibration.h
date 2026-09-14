#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// Handles the once-per-minute hourly/interval vibration reminder.
// The application still controls tick subscription; this module only owns
// the reminder's schedule and vibration policy.
void hourly_vibration_init(EclipseData *data);
void hourly_vibration_deinit(void);
void hourly_vibration_handle_minute(struct tm *tick_time);
