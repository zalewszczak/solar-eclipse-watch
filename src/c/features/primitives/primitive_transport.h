#pragma once

// Build a resolved primitive sequence from the primitive-only wire payload.
// There is intentionally no legacy content fallback: an empty sequence means
// the slot is disabled, and an invalid/unresolvable sequence is rejected.

#include <pebble.h>
#include "./primitive_storage.h"
#include "../../data/eclipse_data.h"

bool primitive_transport_try_build(PrimitiveFeature *out, const uint8_t *primitive_ids,
                                   const uint8_t *primitive_aux, const EclipseData *data,
                                   time_t now, struct tm *t, uint8_t color_mode,
                                   GColor main_color, GColor accent_color);
