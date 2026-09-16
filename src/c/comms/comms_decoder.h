#pragma once

#include <pebble.h>
#include "./comms.h"
#include "../data/eclipse_data.h"

// Decodes one AppMessage dictionary into EclipseData and reports changed domains.
CommsChangeFlags comms_decoder_apply(DictionaryIterator *iter, EclipseData *data);
