#pragma once

// Phase 3/4 bridge: turns a feature slot's already-resolved RenderSegment
// sequence (built by the legacy feature_value_*_compute functions -- left
// untouched by this pass, see IMPLEMENTATION_NOTES.md) into the primitive
// representation the new storage/layout/render pipeline understands.
//
// This is deliberately *not* where Phase 7 (moving weather/astronomy
// computation into PKJS) happens. Once that lands, this bridge is what gets
// replaced by "read the primitive's value out of the AppMessage payload";
// primitive_layout.c and primitive_renderer.c do not change at all, because
// they never look at content IDs in the first place (Section 12).

#include <pebble.h>
#include "./primitive_storage.h"

typedef struct FeatureSlot FeatureSlot; // feature_slot.h

// Populates `out` from `slot->segments[0..segment_count)` and `slot->content`,
// via the primitive_catalogue lookup. Safe to call with segment_count == 0
// (produces an empty PrimitiveFeature).
void primitive_bridge_build(PrimitiveFeature *out, const FeatureSlot *slot);
