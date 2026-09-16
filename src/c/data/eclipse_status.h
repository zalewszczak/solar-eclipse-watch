#pragma once

#include <pebble.h>
#include "./eclipse_data.h"

// Returns true during the [C1, C4) eclipse interval.
bool eclipse_status_is_active(const EclipseData *data, time_t now);

// Formats the current eclipse status and returns its phase.
EclipsePhase eclipse_status_get_text(const EclipseData *data, time_t now,
                                     char *buf, size_t buf_len, bool live_seconds);
