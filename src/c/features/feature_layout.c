#include "./feature_layout.h"
#include "../rendering/background/background_layer.h"
#include "../rendering/background/marker_layer.h"
#include "../fonts/font_lookup.h"
#include "./primitives/primitive_catalogue.h"

#define FEATURE_LAYOUT_CORNER_INSET_PX 2

static bool primitive_sequence_enabled(const uint8_t ids[10]) { return ids[0] != PRIM_ID_NONE; }
static bool primitive_sequence_needs_second_refresh(const uint8_t ids[10]) {
  for (int i = 0; i < 10 && ids[i] != PRIM_ID_NONE; i++)
    if (primitive_catalogue_refresh_class_for((PrimitiveId)ids[i]) == PRIMITIVE_REFRESH_SECOND) return true;
  return false;
}

uint8_t feature_layout_digital_side_mode(uint8_t bottom_style) {
  return feature_layout_is_digital_top_layout(bottom_style) ? bottom_style - 5 : bottom_style;
}
bool feature_layout_is_digital_top_layout(uint8_t bottom_style) {
  return bottom_style >= 5 && bottom_style != BOTTOM_STYLE_BIG_DIGITAL && bottom_style != BOTTOM_STYLE_GRID;
}

void feature_layout_digital_clock_area(uint8_t bottom_style, int16_t screen_w, int16_t *out_x, int16_t *out_w) {
  uint8_t side = feature_layout_digital_side_mode(bottom_style);
  if (side == 2) { // right side only -- shift left
    *out_x = 0;
    *out_w = screen_w - CORNER_BOX_W;
  } else if (side == 3) { // left side only -- shift right
    *out_x = CORNER_BOX_W;
    *out_w = screen_w - CORNER_BOX_W;
  } else { // 0 (no sides) or 4 (both sides) -- centered, full width
    *out_x = 0;
    *out_w = screen_w;
  }
}

void feature_layout_recompute(FeaturesState *state) {
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) state->slots[i].active = false;

  EclipseData *d = state->data;
  if (!d) return;

  bool is_analog = d->bottom_style == 1;
  bool is_big_digital = d->bottom_style == BOTTOM_STYLE_BIG_DIGITAL;
  bool is_grid = d->bottom_style == BOTTOM_STYLE_GRID;
  bool is_digital_top = feature_layout_is_digital_top_layout(d->bottom_style);
  uint8_t marker_style = d->big_analog_marker_style;
  bool marker_is_bitmap = marker_style >= 3 && marker_style <= 7;

  // Inner-empty-area margins: fixed per bitmap style (3-7); every other
  // style (the custom hour/second rings, which is also what the phone
  // sends for the Minimal/Braun/Swiss/Classy/None presets) is computed
  // from those rings + marker text below.
  typedef struct { int8_t top, bottom, left, right; } EdgeMargins;
  static const EdgeMargins BITMAP_STYLE_MARGINS[5] = {
    { 34, 30, 40, 35 }, // 3: Modern
    { 37, 33, 38, 33 }, // 4: Shadow
    { 44, 40, 25, 15 }, // 5: Tally
    { 46, 42, 45, 45 }, // 6: Bell
    { 44, 40, 30, 20 }, // 7: Fancy
  };
  int16_t dyn_upper_offset = 24, dyn_bottom_shift = 20, dyn_left_inset = 0, dyn_right_inset = 0;
  if (is_analog && marker_is_bitmap) {
    const EdgeMargins *m = &BITMAP_STYLE_MARGINS[marker_style - 3];
    dyn_upper_offset = m->top; dyn_bottom_shift = m->bottom; dyn_left_inset = m->left; dyn_right_inset = m->right;
  } else if (!marker_is_bitmap) {
    GRect screen = GRect(0, 0, 200, 228);
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    uint8_t pct = d->custom_hour_marker.thickness != 0 ? d->custom_hour_marker.inner_border_pct : 100;
    uint8_t ecc = d->custom_hour_marker.thickness != 0 ? d->custom_hour_marker.inner_eccentricity : 100;
    GPoint top_pt = marker_layer_point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = marker_layer_point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    int16_t margin = 2;
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_reach = left_pt.x + margin, right_reach = screen.size.w - right_pt.x + margin;
    if (left_reach > dyn_left_inset) dyn_left_inset = left_reach;
    if (right_reach > dyn_right_inset) dyn_right_inset = right_reach;
  }

  if (is_analog) {
    bool has_line2 = primitive_sequence_enabled(d->edge_line_primitive_ids[1]);
    int16_t line1_offset = has_line2 ? dyn_upper_offset : dyn_upper_offset + CORNER_ROW_H / 2;
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[0]), .color_mode = d->edge_line_color_mode[0],
      .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[0]),
    };
    if (has_line2) {
      state->slots[SLOT_UPPER_L2] = (FeatureSlot){
        .active = primitive_sequence_enabled(d->edge_line_primitive_ids[1]), .color_mode = d->edge_line_color_mode[1],
        .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
        .top_offset = dyn_upper_offset + CORNER_ROW_H, .bottom_shift = 0, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[1]),
      };
    }

    has_line2 = primitive_sequence_enabled(d->edge_line_primitive_ids[3]);
    int16_t line1_shift = has_line2 ? dyn_bottom_shift + CORNER_ROW_H : dyn_bottom_shift + CORNER_ROW_H / 2;
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[2]), .color_mode = d->edge_line_color_mode[2],
      .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = 0, .bottom_shift = line1_shift, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[2]),
    };
    if (has_line2) {
      state->slots[SLOT_BOTTOM_L2] = (FeatureSlot){
        .active = primitive_sequence_enabled(d->edge_line_primitive_ids[3]), .color_mode = d->edge_line_color_mode[3],
        .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
        .top_offset = 0, .bottom_shift = dyn_bottom_shift, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[3]),
      };
    }

    has_line2 = primitive_sequence_enabled(d->edge_line_primitive_ids[5]);
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[4]), .color_mode = d->edge_line_color_mode[4],
      .is_top = false, .is_left = true, .is_middle = true, .is_edge = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_left_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[4]),
    };
    if (has_line2) {
      state->slots[SLOT_LEFT_L2] = (FeatureSlot){
        .active = primitive_sequence_enabled(d->edge_line_primitive_ids[5]), .color_mode = d->edge_line_color_mode[5],
        .is_top = false, .is_left = true, .is_middle = true, .is_edge = false,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_left_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[5]),
      };
    }

    has_line2 = primitive_sequence_enabled(d->edge_line_primitive_ids[7]);
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[6]), .color_mode = d->edge_line_color_mode[6],
      .is_top = false, .is_left = false, .is_middle = true, .is_edge = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_right_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[6]),
    };
    if (has_line2) {
      state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
        .active = primitive_sequence_enabled(d->edge_line_primitive_ids[7]), .color_mode = d->edge_line_color_mode[7],
        .is_top = false, .is_left = false, .is_middle = true, .is_edge = false,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_right_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[7]),
      };
    }
  }

  // Big Digital: corners (set unconditionally below) plus exactly one
  // top-center and one bottom-center feature -- no sides, no seconds.
  // The two center features are pulled inward by one full feature row
  // (24px), leaving a deliberate gap from the screen edge. Grid has no
  // center features at all, so its corresponding slots stay inactive.
  if (is_big_digital) {
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[0]), .color_mode = d->edge_line_color_mode[0],
      .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = CORNER_ROW_H, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[0]),
    };
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[2]), .color_mode = d->edge_line_color_mode[2],
      .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = 0, .bottom_shift = CORNER_ROW_H, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[2]),
    };
  }

  // Digital-mode-only equivalent of the 4 blocks above -- the 8 edge
  if (!is_analog && !is_big_digital && !is_grid) {
    int16_t clock_x, clock_w;
    feature_layout_digital_clock_area(d->bottom_style, 200, &clock_x, &clock_w);

    // 1 = nearest the clock, 3 = nearest the screen's own outer edge
    int16_t row1_off = CORNER_ROW_H * 2, row2_off = CORNER_ROW_H, row3_off = 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[4]), .color_mode = d->edge_line_color_mode[4],
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[4]),
    };
    state->slots[SLOT_LEFT_L2] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[5]), .color_mode = d->edge_line_color_mode[5],
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[5]),
    };
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){ // reused: digital left column, row 3 -- reads upper_middle_line1
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[0]), .color_mode = d->edge_line_color_mode[0],
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[0]),
    };

    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[6]), .color_mode = d->edge_line_color_mode[6],
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[6]),
    };
    state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[7]), .color_mode = d->edge_line_color_mode[7],
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[7]),
    };
    state->slots[SLOT_UPPER_L2] = (FeatureSlot){ // reused: digital right column, row 3 -- reads upper_middle_line2
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[1]), .color_mode = d->edge_line_color_mode[1],
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[1]),
    };

    // Single bottom feature -- reuses bottom_middle_line1 (analog's
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = primitive_sequence_enabled(d->edge_line_primitive_ids[2]), .color_mode = d->edge_line_color_mode[2],
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = 0, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .custom_box = true, .box_x = clock_x, .box_w = clock_w,
      .needs_second_refresh = primitive_sequence_needs_second_refresh(d->edge_line_primitive_ids[2]),
    };
  }

  // Corners always just draw whatever d->corner_primitive_ids[] says, for
  int16_t top_corner_shift = is_digital_top ? DIGITAL_PANEL_H : 0;
  state->slots[SLOT_CORNER_TL] = (FeatureSlot){
    .active = primitive_sequence_enabled(d->corner_primitive_ids[0]), .color_mode = d->corner_color_mode[0],
    .is_top = true, .is_left = true, .is_middle = false, .is_edge = true,
    .top_offset = FEATURE_LAYOUT_CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = primitive_sequence_needs_second_refresh(d->corner_primitive_ids[0]),
  };
  state->slots[SLOT_CORNER_TR] = (FeatureSlot){
    .active = primitive_sequence_enabled(d->corner_primitive_ids[1]), .color_mode = d->corner_color_mode[1],
    .is_top = true, .is_left = false, .is_middle = false, .is_edge = true,
    .top_offset = FEATURE_LAYOUT_CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = primitive_sequence_needs_second_refresh(d->corner_primitive_ids[1]),
  };
  // Bottom corners (BL/BR): stay anchored to the SKY's own bottom
  int16_t bottom_corner_shift = (is_analog || is_digital_top || is_big_digital || is_grid) ? 0 : DIGITAL_PANEL_H;
  state->slots[SLOT_CORNER_BL] = (FeatureSlot){
    .active = primitive_sequence_enabled(d->corner_primitive_ids[2]), .color_mode = d->corner_color_mode[2],
    .is_top = false, .is_left = true, .is_middle = false, .is_edge = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = primitive_sequence_needs_second_refresh(d->corner_primitive_ids[2]),
  };
  state->slots[SLOT_CORNER_BR] = (FeatureSlot){
    .active = primitive_sequence_enabled(d->corner_primitive_ids[3]), .color_mode = d->corner_color_mode[3],
    .is_top = false, .is_left = false, .is_middle = false, .is_edge = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = primitive_sequence_needs_second_refresh(d->corner_primitive_ids[3]),
  };
}

