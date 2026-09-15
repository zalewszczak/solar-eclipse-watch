#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../data/eclipse_data.h"

typedef enum {
  COMMS_CHANGE_NONE           = 0,
  COMMS_CHANGE_CLOCK_FONT     = 1u << 0,
  COMMS_CHANGE_LAYOUT         = 1u << 1,
  COMMS_CHANGE_HANDS          = 1u << 2,
  COMMS_CHANGE_CANVAS         = 1u << 3,
  COMMS_CHANGE_PANEL          = 1u << 4,
  // Raised on every decoded message: features_layer_set_data()
  // recomputes each slot's layout AND value in one pass, so there was
  // never a point in the separate "values only" flag this used to sit
  // next to -- every consumer checked them together.
  COMMS_CHANGE_FEATURES       = 1u << 5,
} CommsChangeFlags;

typedef void (*CommsDataAppliedHandler)(CommsChangeFlags changes, void *context);

void comms_init(EclipseData *data, CommsDataAppliedHandler handler, void *context);
void comms_deinit(void);

// Sends the small watch-to-phone battery-saver state update.
// Returns false if the AppMessage outbox is currently unavailable.
bool comms_send_battery_saver_phase(uint8_t phase);
