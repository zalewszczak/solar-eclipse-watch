#pragma once

#include <pebble.h>

#define FEATURES_REFRESH_MS 60000 // Refresh feature values once per minute.

void feature_refresh_init(void);
void feature_refresh_deinit(void);
void feature_refresh_start(void);
void feature_refresh_stop(void);
