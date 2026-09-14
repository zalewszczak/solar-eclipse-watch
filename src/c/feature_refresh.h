#pragma once

#include <pebble.h>

// Owns the low-frequency refresh timer used for feature values that do not
// need a genuinely live SECOND_UNIT tick. The scheduler deliberately asks
// the layout controller for the current feature layer each time, so layout
// changes do not leave a stale layer pointer behind.
void feature_refresh_init(void);
void feature_refresh_deinit(void);
void feature_refresh_start(void);
void feature_refresh_stop(void);
