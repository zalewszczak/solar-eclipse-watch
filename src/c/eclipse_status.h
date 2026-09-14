#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// Returns true from first contact up to (but not including) last contact.
// This is the canonical definition of an eclipse being actively in progress.
bool eclipse_status_is_active(const EclipseData *data, time_t now);

// Builds the eclipse status/countdown text and returns its current phase.
EclipsePhase eclipse_status_get_text(const EclipseData *data, time_t now,
                                     char *buf, size_t buf_len, bool live_seconds);
