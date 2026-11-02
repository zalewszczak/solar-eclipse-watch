#pragma once

#include <pebble.h>
#include "../data/eclipse_data.h"
#include "./primitives/primitive_storage.h"

// Shared feature-row geometry used by both layout and rendering.
#define CORNER_ROW_H 24

// Internal primitive-only feature-layer data model shared by layout and drawing

#define FEATURES_MAX_SLOTS 12
#define CORNER_BOX_W 68

typedef struct FeatureSlot {
  bool active;
  bool shake_only;
  uint8_t color_mode;

  bool is_top;
  bool is_left;
  bool is_middle;
  bool is_edge;
  int16_t top_offset;
  int16_t bottom_shift;
  int16_t middle_inset;
  bool center_horizontal;
  bool center_vertical;
  bool allow_outline;
  bool needs_second_refresh;

  // Digital layouts: the slot sits in its own box (the clock area) rather than
  // in a CORNER_BOX_W wide column.
  bool custom_box;
  int16_t box_x;
  int16_t box_w;

  bool draw_pill;
  GColor pill_bg;
  PrimitiveFeature primitives;
} FeatureSlot;

typedef struct {
  EclipseData *data;
  FeatureSlot slots[FEATURES_MAX_SLOTS];
} FeaturesState;

enum {
  SLOT_UPPER_L1 = 0, SLOT_UPPER_L2,
  SLOT_BOTTOM_L1, SLOT_BOTTOM_L2,
  SLOT_LEFT_L1, SLOT_LEFT_L2,
  SLOT_RIGHT_L1, SLOT_RIGHT_L2,
  SLOT_CORNER_TL, SLOT_CORNER_TR, SLOT_CORNER_BL, SLOT_CORNER_BR,
};
