#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"

void hourly_vibration_init(EclipseData *data);
void hourly_vibration_deinit(void);
void hourly_vibration_handle_minute(struct tm *tick_time);
