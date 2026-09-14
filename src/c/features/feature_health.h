#pragma once

#include <pebble.h>
#include <stddef.h>
#include <stdint.h>

// Small facade around Pebble HealthService used by the feature-value layer.
// Keeping HealthService calls here makes features_layer.c independent of the
// SDK's health iteration/accessibility details while preserving the existing
// on-watch behavior.

typedef struct {
  time_t earliest_start;
  time_t latest_end;
  bool found;
} FeatureSleepSpan;

void feature_health_format_duration_hm(char *buf, size_t buf_size, int32_t total_seconds);
FeatureSleepSpan feature_health_get_sleep_span(void);
bool feature_health_metric_available(HealthMetric metric);
int feature_health_peek_current_bpm(void);
HealthValue feature_health_sum_today(HealthMetric metric);
