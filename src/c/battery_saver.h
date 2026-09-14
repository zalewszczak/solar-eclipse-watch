#pragma once

#include <pebble.h>
#include <stdbool.h>
#include <time.h>

typedef enum {
  BATTERY_SAVER_AWAKE = 0,
  BATTERY_SAVER_SLEEP = 1,
  BATTERY_SAVER_DEEP_SLEEP = 2,
} BatterySaverPhase;

typedef void (*BatterySaverPhaseChangedHandler)(BatterySaverPhase previous_phase,
                                                  BatterySaverPhase phase,
                                                  void *context);

// Tracks the shake-idle power policy. The module owns policy state only;
// the application decides what visual/timing work each phase suppresses.
void battery_saver_init(bool enabled, time_t active_since,
                        BatterySaverPhaseChangedHandler handler, void *context);
void battery_saver_set_enabled(bool enabled);
void battery_saver_note_activity(time_t now);
void battery_saver_update(time_t now);

BatterySaverPhase battery_saver_phase(void);
