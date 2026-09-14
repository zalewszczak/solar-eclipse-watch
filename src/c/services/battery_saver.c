#include "battery_saver.h"

#define BATTERY_SAVER_SLEEP_SECONDS ((time_t)2 * 60 * 60)
#define BATTERY_SAVER_DEEP_SLEEP_SECONDS ((time_t)4 * 60 * 60)

static bool s_enabled;
static time_t s_last_activity;
static BatterySaverPhase s_phase = BATTERY_SAVER_AWAKE;
static BatterySaverPhaseChangedHandler s_phase_changed;
static void *s_phase_changed_context;

static BatterySaverPhase compute_phase(time_t now) {
  if (!s_enabled) return BATTERY_SAVER_AWAKE;

  time_t idle_seconds = now - s_last_activity;
  if (idle_seconds >= BATTERY_SAVER_DEEP_SLEEP_SECONDS) return BATTERY_SAVER_DEEP_SLEEP;
  if (idle_seconds >= BATTERY_SAVER_SLEEP_SECONDS) return BATTERY_SAVER_SLEEP;
  return BATTERY_SAVER_AWAKE;
}

void battery_saver_init(bool enabled, time_t active_since,
                        BatterySaverPhaseChangedHandler handler, void *context) {
  s_enabled = enabled;
  s_last_activity = active_since;
  s_phase = BATTERY_SAVER_AWAKE;
  s_phase_changed = handler;
  s_phase_changed_context = context;
}

void battery_saver_set_enabled(bool enabled) {
  s_enabled = enabled;
}

void battery_saver_note_activity(time_t now) {
  s_last_activity = now;
}

void battery_saver_update(time_t now) {
  BatterySaverPhase new_phase = compute_phase(now);
  if (new_phase == s_phase) return;

  BatterySaverPhase previous_phase = s_phase;
  s_phase = new_phase;
  if (s_phase_changed) s_phase_changed(previous_phase, new_phase, s_phase_changed_context);
}

BatterySaverPhase battery_saver_phase(void) {
  return s_phase;
}

