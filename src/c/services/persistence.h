#pragma once

#include "../domain/eclipse_data.h"

// Loads the complete persisted EclipseData snapshot. If any chunk is
// missing or has the wrong size, the destination is left zeroed/invalid.
bool persistence_load(EclipseData *data);

// Saves EclipseData in Pebble-sized chunks.
bool persistence_save(const EclipseData *data);
