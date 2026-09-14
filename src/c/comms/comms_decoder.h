#pragma once

#include <pebble.h>
#include "./comms.h"
#include "../data/eclipse_data.h"

// Applies one AppMessage dictionary to EclipseData and returns the set of
// presentation domains whose inputs changed. This module owns wire-format
// decoding; comms.c retains transport, retry, and lifecycle policy.
CommsChangeFlags comms_decoder_apply(DictionaryIterator *iter, EclipseData *data);
