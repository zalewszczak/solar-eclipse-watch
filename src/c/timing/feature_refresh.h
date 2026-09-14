#pragma once

#include <pebble.h>

// Low-frequency refresh scheduler for feature-slot values.
#define FEATURES_REFRESH_MS 60000

void feature_refresh_init(void);
void feature_refresh_deinit(void);
void feature_refresh_start(void);
void feature_refresh_stop(void);
