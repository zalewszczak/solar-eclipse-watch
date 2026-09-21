#pragma once

// Phase 3 (primitive value transport): given a primitive ID alone -- no
// "content" number, no RenderSegment -- resolves what that primitive
// should currently show. This is the missing piece the legacy content-
// dispatch system never needed: feature_value_*_compute() always went
// content -> (a fixed sequence of) segments in one pass, so no code ever
// had to answer "what does primitive X show right now" in isolation.
//
// Deliberately NOT complete: only the primitive IDs used by the initial
// primitive-transport-enabled content set are covered (see
// primitive_transport.h and IMPLEMENTATION_NOTES.md's Phase 3 update for
// exactly which and why). Returns false for anything else so the caller
// can fall back to the legacy content-based path -- never guesses.
//
// Each primitive resolves its OWN best color (Section 31), independent of
// whatever content id a legacy caller might have built it from -- see
// primitive_transport.c for how that composes with the slot's color_mode
// (feature_value_resolve_flat_color already does the MONO/ACC/PILL/COLOR
// dispatch; this only ever supplies the "COLOR mode" candidate).

#include <pebble.h>
#include "./primitive_types.h"
#include "./primitive_ids.h"
#include "../../data/eclipse_data.h"

typedef struct {
  PrimitiveKind kind;
  char text[20];       // meaningful when kind != PRIMITIVE_KIND_ICON
  uint8_t icon_kind;    // meaningful when kind == PRIMITIVE_KIND_ICON
  int16_t aux;
  bool aux_flag;
  GColor dynamic_color; // this primitive's own candidate for COLOR mode
  // Some legacy cases (sleep/bed-wake time's "N/A" branches) render an
  // always-gray fallback that bypasses color_mode entirely -- not even
  // COLOR mode is the exception to it, unlike every other dynamic color in
  // this codebase. Setting this true tells primitive_transport.c to use
  // dynamic_color as the final color directly, skipping
  // feature_value_resolve_flat_color()'s MONO/ACC/PILL/COLOR dispatch.
  // False (the default/common case) for everything else.
  bool force_color;
} PrimitiveResolved;

// Returns false (leaving *out unspecified) when `id` has no resolver yet.
// `aux_in` is the wire-supplied parameter for primitives that need one --
// currently only the timezone cluster (PRIM_TIMEZONE_ABBR/_TIME/_AMPM),
// where it's the zone index (0-18, feature_timezone_get()'s own index
// space) rather than a raw UTC offset: reusing feature_timezone_get()'s
// existing table means feature_timezone_current_offset_min()'s DST
// handling comes for free, which a bare offset-in-minutes parameter
// could not do correctly across a DST transition. Ignored by every
// primitive that doesn't need it -- pass 0 for those.
bool primitive_resolver_resolve(PrimitiveId id, uint8_t aux_in, const EclipseData *data, time_t now, struct tm *t, PrimitiveResolved *out);
