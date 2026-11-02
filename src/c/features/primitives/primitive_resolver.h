#pragma once

// Phase 3 (primitive value transport): given a primitive ID alone -- no
// "content" number, no legacy render segment -- resolves what that primitive
// should currently show. Feature IDs and composition are phone-side concerns;
// this resolver operates on one primitive at a time.
//
// The resolver covers the primitive IDs exposed by the phone compiler and
// custom primitive palette. Unknown IDs are rejected; there is no fallback.
//
// Each primitive resolves its OWN best color (Section 31), independent of
// the phone-side feature selection -- see
// primitive_transport.c for how that composes with the slot's color_mode
// (feature_color_resolve_flat already does the MONO/ACC/PILL/COLOR
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
  // Some primitive fallback cases (sleep/bed-wake time's "N/A" branches) render an
  // always-gray fallback that bypasses color_mode entirely -- not even
  // COLOR mode is the exception to it, unlike every other dynamic color in
  // this codebase. Setting this true tells primitive_transport.c to use
  // dynamic_color as the final color directly, skipping
  // feature_color_resolve_flat()'s MONO/ACC/PILL/COLOR dispatch.
  // False (the default/common case) for everything else.
  bool force_color;
  // Legacy astronomy/composite cases passed main_color (not a graded color)
  // as their COLOR-mode candidate. True = "my dynamic candidate is the
  // slot's main color", resolved in primitive_transport.c where main_color
  // is known.
  bool dynamic_is_main;
  // Compass icon only (compass primitive): in COLOR mode it needs two
  // colors at once -- accent for the north arrow, main for the other three.
  // Every other mode: one flat color for both.
  bool two_tone_accent_main;
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
