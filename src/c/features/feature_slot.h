#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

// Shared feature-row geometry used by both layout and rendering.
#define CORNER_ROW_H 24

// Internal feature-layer data model shared by the value and layout/drawing
// modules. These types are deliberately not part of features_layer.h: they
// describe the implementation's cached slot representation.

#define FEATURES_MAX_SLOTS 12
#define MAX_RENDER_SEGMENTS 8
#define CORNER_BOX_W 68

typedef struct {
  bool is_icon;
  uint8_t icon_kind;
  int16_t icon_extra;
  bool icon_flag;
  char text[20];
  GColor color;
  GColor color2;
  int16_t x_offset;
  int16_t width;
} RenderSegment;

typedef struct FeatureSlot {
  bool active;
  uint8_t content;
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

  bool custom_box;
  int16_t box_x;
  int16_t box_w;

  bool draw_pill;
  GColor pill_bg;
  int16_t segment_count;
  RenderSegment segments[MAX_RENDER_SEGMENTS];
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
