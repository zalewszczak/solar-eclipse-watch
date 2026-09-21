#pragma once

// Phase 5: feature layout calculation. Computes each primitive's width and
// x-offset, and the feature's total width, exactly once whenever
// configuration/primitive values change -- never at draw time (Section 11).
//
// This does not know what any primitive *means* -- only its kind (icon vs
// text vs spacer) and, for text/icon, its already-resolved payload -- so it
// stays correct across the eventual Phase 7 move of weather/astronomy
// computation into PKJS without needing any changes itself.

#include <pebble.h>
#include "./primitive_storage.h"

typedef struct FeatureSlot FeatureSlot; // feature_slot.h

// `box_w`/alignment come from the slot's existing layout fields
// (custom_box/box_w, is_left/is_middle/center_horizontal, middle_inset) --
// unchanged from feature_render_resolve_segment_offsets, since Section 17
// keeps "the existing slot/layout model."
void primitive_layout_compute(PrimitiveFeature *feature, const FeatureSlot *slot, GFont font, int16_t font_h);
