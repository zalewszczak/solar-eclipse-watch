#include <pebble.h>
#include "feature_refresh.h"
#include "features_layer.h"
#include "layout_controller.h"

static AppTimer *s_timer;

static void feature_refresh_timer_callback(void *context) {
  (void)context;

  Layer *features_layer = layout_controller_features_layer();
  if (features_layer) {
    features_layer_refresh_values(features_layer);
  }

  s_timer = app_timer_register(FEATURES_REFRESH_MS, feature_refresh_timer_callback, NULL);
}

void feature_refresh_init(void) {
  s_timer = NULL;
}

void feature_refresh_deinit(void) {
  feature_refresh_stop();
}

void feature_refresh_start(void) {
  if (s_timer) return;
  s_timer = app_timer_register(FEATURES_REFRESH_MS, feature_refresh_timer_callback, NULL);
}

void feature_refresh_stop(void) {
  if (!s_timer) return;
  app_timer_cancel(s_timer);
  s_timer = NULL;
}

bool feature_refresh_is_running(void) {
  return s_timer != NULL;
}
