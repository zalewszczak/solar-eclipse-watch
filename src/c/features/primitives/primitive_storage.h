#pragma once

// Watch-side primitive storage (Section 30: "the watch should store the
// final primitive data needed for drawing"). This is what a feature slot's
// primitive sequence looks like once resolved -- no weather/astronomy/health
// semantics live below this layer, only the already-resolved thing to draw.
//
// A PrimitiveInstance stores the data needed by the primitive renderer plus the catalogue identity (`id`) and refresh metadata (`refresh_class`).
// The storage is independent of feature selection and legacy feature-value code.

#include <pebble.h>
#include "./primitive_types.h"
#include "./primitive_ids.h"

#define PRIMITIVE_FEATURE_MAX 10 // Section 23 / acceptance criterion 2: 10 primitives per feature

typedef struct {
  PrimitiveId id;
  PrimitiveKind kind;
  PrimitiveRefreshClass refresh_class;

  // Payload -- exactly one of these is meaningful, selected by `kind`.
  char text[20];        // PRIMITIVE_KIND_TEXT
  uint8_t icon_kind;     // PRIMITIVE_KIND_ICON: existing 1-30 icon-ID space (feature_icons.c)
  int16_t aux;           // icon_extra equivalent: zone/offset index, percent, heading, etc.
  bool aux_flag;         // icon_flag equivalent: day/night, charging, crossed-out, etc.

  // Coloring -- resolved before storage, per Section 31 ("watch owns...
  GColor color;
  GColor color2; // only used where two colors are needed at once (e.g. compass)

  // Layout (Section 30, Phase 5) -- computed by primitive_layout.c, not by
  // the configuration compiler and not at draw time.
  int16_t width;
  int16_t x_offset;
} PrimitiveInstance;

typedef struct {
  uint8_t count;
  bool tight;          // user-built sequence: no automatic gaps, spacing comes only from Space elements
  int16_t total_width; // Section 11: computed once, not recomputed every frame
  PrimitiveInstance items[PRIMITIVE_FEATURE_MAX];
} PrimitiveFeature;
