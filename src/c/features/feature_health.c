#include "./feature_health.h"

void feature_health_format_duration_hm(char *buf, size_t buf_size, int32_t total_seconds) {
  if (total_seconds < 0) total_seconds = 0;
  int hours = (int)(total_seconds / 3600);
  int minutes = (int)((total_seconds % 3600) / 60);
  snprintf(buf, buf_size, "%dh %dm", hours, minutes);
}

static bool sleep_span_iterator_cb(HealthActivity activity, time_t time_start,
                                   time_t time_end, void *context) {
  (void)activity;
  FeatureSleepSpan *span = (FeatureSleepSpan *)context;
  if (!span->found || time_start < span->earliest_start) {
    span->earliest_start = time_start;
  }
  if (!span->found || time_end > span->latest_end) {
    span->latest_end = time_end;
  }
  span->found = true;
  return true;
}

FeatureSleepSpan feature_health_get_sleep_span(void) {
  FeatureSleepSpan span = { 0, 0, false };
  time_t now = time(NULL);
  time_t day_ago = now - 24 * 3600;

  // HealthActivitySleep is already a single-bit mask value. Keep the
  // full iterator so multiple sleep segments are merged into one span.
  health_service_activities_iterate(
      HealthActivitySleep, day_ago, now, HealthIterationDirectionPast,
      sleep_span_iterator_cb, &span);

  return span;
}

bool feature_health_metric_available(HealthMetric metric) {
  time_t now = time(NULL);
  return (health_service_metric_accessible(metric, now - 86400, now) &
          HealthServiceAccessibilityMaskAvailable) != 0;
}

int feature_health_peek_current_bpm(void) {
  time_t now = time(NULL);
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(
      HealthMetricHeartRateBPM, now, now);
  if (!(mask & HealthServiceAccessibilityMaskAvailable)) return 0;
  return (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
}

HealthValue feature_health_sum_today(HealthMetric metric) {
  return health_service_sum_today(metric);
}
