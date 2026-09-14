#pragma once

#include "eclipse_data.h"

// Coordinates the application-level consequences of battery-saver phase
// changes. battery_saver.c owns the policy/state; this controller owns the
// services that must react to that state.
void battery_saver_controller_init(EclipseData *data);
void battery_saver_controller_deinit(void);
void battery_saver_controller_update(void);
void battery_saver_controller_apply_enabled(bool enabled);
