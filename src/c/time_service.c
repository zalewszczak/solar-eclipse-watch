#include "time_service.h"
#include "eclipse_status.h"

#include "battery_saver.h"
#include "feature_controller.h"
#include "background_layer.h"

// Content ids that display a live seconds value -- see the corresponding
// corner/edge content table in the configuration page.
static const uint8_t SECOND_PRECISION_CONTENT_IDS[] = { 63, 69, 70, 71, 72 };

static const EclipseData *s_data;
static TimeServiceTickHandler s_tick_handler;
static void *s_context;
static bool s_subscribed;
static bool s_unit_is_seconds;

static bool needs_second_precision(void) {
  if (s_data->show_seconds) return true;

  for (size_t i = 0; i < sizeof(SECOND_PRECISION_CONTENT_IDS); i++) {
    if (feature_controller_content_in_use(s_data, SECOND_PRECISION_CONTENT_IDS[i])) return true;
  }

  return false;
}

bool time_service_live_seconds_now(time_t now) {
  if (!s_data) return false;
  if (battery_saver_phase() != BATTERY_SAVER_AWAKE) return false;
  return needs_second_precision() || eclipse_status_is_active(s_data, now);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (s_tick_handler) s_tick_handler(tick_time, units_changed, s_context);
}

void time_service_init(const EclipseData *data, TimeServiceTickHandler handler, void *context) {
  s_data = data;
  s_tick_handler = handler;
  s_context = context;
  s_subscribed = false;
  s_unit_is_seconds = false;
  time_service_update();
}

void time_service_update(void) {
  if (!s_data) return;

  bool need_seconds = time_service_live_seconds_now(time(NULL));
  if (s_subscribed && need_seconds == s_unit_is_seconds) return;

  s_subscribed = true;
  s_unit_is_seconds = need_seconds;
  tick_timer_service_subscribe(need_seconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

void time_service_deinit(void) {
  tick_timer_service_unsubscribe();
  s_data = NULL;
  s_tick_handler = NULL;
  s_context = NULL;
  s_subscribed = false;
  s_unit_is_seconds = false;
}

bool time_service_is_seconds(void) {
  return s_unit_is_seconds;
}
