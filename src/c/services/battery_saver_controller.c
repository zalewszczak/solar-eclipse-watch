#include <pebble.h>
#include "battery_saver_controller.h"
#include "battery_saver.h"
#include "../comms/comms.h"
#include "feature_refresh.h"
#include "time_service.h"

static EclipseData *s_data;
static uint8_t s_last_sent_phase = 0xFF;

static void sync_phase_to_phone(void) {
  uint8_t phase = (uint8_t)battery_saver_phase();
  if (phase == s_last_sent_phase) return;
  if (comms_send_battery_saver_phase(phase)) s_last_sent_phase = phase;
}

static void phase_changed(BatterySaverPhase previous_phase,
                          BatterySaverPhase new_phase,
                          void *context) {
  (void)context;

  bool was_resting = previous_phase != BATTERY_SAVER_AWAKE;
  bool now_resting = new_phase != BATTERY_SAVER_AWAKE;

  // battery_saver_update() has already committed the new phase. Re-evaluate
  // tick granularity immediately so the service follows the new policy.
  time_service_update();

  if (now_resting && !was_resting) {
    feature_refresh_stop();
  } else if (!now_resting && was_resting) {
    feature_refresh_start();
  }

  sync_phase_to_phone();
}

void battery_saver_controller_init(EclipseData *data) {
  s_data = data;
  s_last_sent_phase = 0xFF;
  battery_saver_init(s_data->battery_saver_enabled, time(NULL), phase_changed, NULL);
}

void battery_saver_controller_deinit(void) {
  s_data = NULL;
  s_last_sent_phase = 0xFF;
}

void battery_saver_controller_update(void) {
  if (!s_data) return;
  battery_saver_update(time(NULL));
  sync_phase_to_phone();
}

void battery_saver_controller_apply_enabled(bool enabled) {
  if (!s_data) return;
  battery_saver_set_enabled(enabled);
  battery_saver_update(time(NULL));
  sync_phase_to_phone();
}
