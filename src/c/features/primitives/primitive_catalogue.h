#pragma once

// Primitive metadata only. Feature selection and feature/content IDs remain
// entirely on the phone and are not represented in the watch-side catalogue.

#include <pebble.h>
#include "./primitive_types.h"
#include "./primitive_ids.h"

PrimitiveSource primitive_catalogue_source_for(PrimitiveId id);
PrimitiveRefreshClass primitive_catalogue_refresh_class_for(PrimitiveId id);

// True for primitives whose value comes from the weather service (and its
// legends/units). While the weather service reports a persistent error these
// are replaced by a red "ERR n" (see primitive_transport.c). Aviation/ISS/
// aurora/air-quality/altitude values have their own error handling.
bool primitive_catalogue_is_weather_derived(PrimitiveId id);
