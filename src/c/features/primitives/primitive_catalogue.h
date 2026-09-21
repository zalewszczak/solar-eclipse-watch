#pragma once

// The "definitive primitive catalogue" Phase 1 asks for, plus the
// content-ID -> primitive-ID mapping the Phase 2-6 bridge (primitive_bridge.c)
// uses to convert an already-resolved RenderSegment sequence (produced by the
// existing feature_value_*_compute functions) into a primitive sequence.
//
// This is intentionally the *only* place that knows about legacy "content"
// numbers below the bridge; primitive_layout.c and primitive_renderer.c never
// see a content ID, only PrimitiveInstance values (Section 12: the draw path
// must not "resolve feature category").

#include <pebble.h>
#include "./primitive_types.h"
#include "./primitive_ids.h"

typedef struct FeatureSlot FeatureSlot; // feature_slot.h

// Returns the catalogue ID for segment `seg_index` of the given legacy
// `content`, given the already-computed slot (used only to disambiguate
// variable-shaped outputs, e.g. content 4's 1-vs-2-segment split by color
// mode, or content 87-95's "N/A" 2-segment fallback vs the normal 3-segment
// form -- see feature_value_weather_compute). Returns PRIM_ID_NONE if the
// content/segment combination is not recognized (segment count should
// already guard this; primitive_bridge.c treats PRIM_ID_NONE defensively).
PrimitiveId primitive_catalogue_id_for(uint8_t content, int seg_index, const FeatureSlot *slot);

// Where a primitive's value comes from and how often it needs recomputing,
// derived purely from its ID (Sections 2 and 14). Independent of any
// particular content number, so this stays correct once Phase 7 actually
// moves weather/astronomy computation into PKJS -- only the *bridge* changes
// then, not this classification.
PrimitiveSource primitive_catalogue_source_for(PrimitiveId id);
PrimitiveRefreshClass primitive_catalogue_refresh_class_for(PrimitiveId id);
