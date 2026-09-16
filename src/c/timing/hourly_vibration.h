#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"

// Fired once per flash toggle (every second, for the flash's whole
// duration) so the caller can mark whatever layer is showing the
// clock -- hands or digital panel -- dirty; see hands_controller.h's
// own HandsControllerInvalidateHandler for the same pattern.
typedef void (*HourlyVibrationFlashHandler)(void *context);

void hourly_vibration_init(EclipseData *data, HourlyVibrationFlashHandler flash_handler, void *flash_context);
void hourly_vibration_deinit(void);
void hourly_vibration_handle_minute(struct tm *tick_time);

// A 10-second, colors-inverted-every-1s flash of the time display
// that accompanies each vibration actually firing (not just each
// scheduled check) -- see hourly_vibration_handle_minute()'s call to
// it. hourly_vibration_flash_is_inverted() only means anything while
// hourly_vibration_flash_is_active() is true.
bool hourly_vibration_flash_is_active(void);
bool hourly_vibration_flash_is_inverted(void);
// True: flash the analog hour hand (this firing is on a 60-minute
// schedule). False: flash the minute hand instead. Digital layouts
// flash the clock font in both cases and can ignore this.
bool hourly_vibration_flash_targets_hour_hand(void);

