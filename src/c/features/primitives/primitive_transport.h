#pragma once

// Phase 3 (primitive value transport): given the raw primitive-ID sequence
// PKJS sent for a slot (see eclipse_data.h's corner_primitive_ids), builds
// a PrimitiveFeature by resolving each ID directly via primitive_resolver.c
// -- the real thing Phase 3 asks for, as opposed to primitive_bridge.c's
// bridge (which still derives primitives from a legacy "content" number
// computed on-watch). Whichever content ids PKJS has started sending real
// primitive sequences for (see config/primitive-decompose.js's
// PRIMITIVE_TRANSPORT_CONTENT_IDS) now render through this path instead of
// the bridge; everything else still goes through the bridge exactly as
// before -- see primitive_transport.c's own header comment for the exact
// content id list and why it's smaller than "everything Phase 8 already
// decomposed."

#include <pebble.h>
#include "./primitive_storage.h"
#include "../../data/eclipse_data.h"

// `primitive_ids` is a PRIMITIVE_FEATURE_MAX-length array (0/PRIM_ID_NONE-
// terminated); `primitive_aux` is the parallel PRIMITIVE_FEATURE_MAX-length
// array of per-position parameters (currently only meaningful for the
// timezone cluster's zone index -- see primitive_resolver.h; every other
// primitive ignores its own aux byte). `color_mode` is the slot's own
// MONO/ACC/PILL/COLOR setting. Returns false (leaving *out empty) when the
// sequence is empty (PRIM_ID_NONE at index 0 -- this slot hasn't been sent
// one, either because its content isn't transport-enabled yet or the phone
// app is older than this feature) or when any ID in it has no resolver yet
// -- a partially-resolved feature is never shown; the caller falls back to
// primitive_bridge_build() for the whole slot in either case.
bool primitive_transport_try_build(PrimitiveFeature *out, const uint8_t *primitive_ids, const uint8_t *primitive_aux,
                                   const EclipseData *data, time_t now, struct tm *t, uint8_t color_mode,
                                   GColor main_color, GColor accent_color);
