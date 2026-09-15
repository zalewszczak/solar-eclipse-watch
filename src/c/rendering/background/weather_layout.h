#pragma once

#include <stdint.h>

#define WEATHER_CLOUD_CLUSTER_SLOTS 4

// Shared horizontal positions and vertical nudges for the cloud clusters.
// Values are percentages / pixel offsets and fit comfortably in int8_t.
extern const int8_t WEATHER_CLOUD_CLUSTER_X_PCT[WEATHER_CLOUD_CLUSTER_SLOTS];
extern const int8_t WEATHER_CLOUD_CLUSTER_Y_OFFSET[WEATHER_CLOUD_CLUSTER_SLOTS];
