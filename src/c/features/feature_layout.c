#include "./feature_layout.h"
#include "../rendering/background/background_layer.h"
#include "../rendering/background/marker_layer.h"
#include "./feature_rules.h"
#include "../fonts/font_lookup.h"

#define FEATURE_LAYOUT_CORNER_INSET_PX 2

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
  bool is_bitmap_style = is_analog && marker_style >= 3 && marker_style != 8 && marker_style != 9;

  // Which of the 12 slots actually apply for the current marker/

  // Inner-empty-area margins: procedural presets (0/1/2) and "none" (9)
  typedef struct { int8_t top, bottom, left, right; } EdgeMargins;
  static const EdgeMargins BITMAP_STYLE_MARGINS[5] = {
    { 34, 30, 40, 35 }, // 3: Modern
    { 37, 33, 38, 33 }, // 4: Shadow
    { 44, 40, 25, 15 }, // 5: Tally
    { 46, 42, 45, 45 }, // 6: Bell
    { 44, 40, 30, 20 }, // 7: Fancy
  };
  int16_t dyn_upper_offset = 24, dyn_bottom_shift = 20, dyn_left_inset = 0, dyn_right_inset = 0;
  if (is_bitmap_style && marker_style >= 3 && marker_style <= 7) {
    const EdgeMargins *m = &BITMAP_STYLE_MARGINS[marker_style - 3];
    dyn_upper_offset = m->top; dyn_bottom_shift = m->bottom; dyn_left_inset = m->left; dyn_right_inset = m->right;
  } else if (marker_style == 8) {
    GRect screen = GRect(0, 0, 200, 228);
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    uint8_t pct = d->custom_hour_marker.thickness != 0 ? d->custom_hour_marker.inner_border_pct : 100;
    uint8_t ecc = d->custom_hour_marker.thickness != 0 ? d->custom_hour_marker.inner_eccentricity : 100;
    uint8_t h_offset = 0;
    if (d->marker_text.target != 0) {
      if (d->marker_text.target == 1) { // hours
        int16_t marker_pct = d->custom_hour_marker.thickness == 0 ? 100 + d->marker_text.offset_px : d->custom_hour_marker.inner_border_pct + d->marker_text.offset_px;
        if(marker_pct>100) {
          marker_pct = 100;
        } else if (marker_pct < 0) {
          marker_pct = 0;
        }
        pct = marker_pct;
        ecc = d->custom_hour_marker.thickness == 0 ? 100 : d->custom_hour_marker.inner_eccentricity;
      } else { // seconds
        int16_t marker_pct = d->custom_second_marker.thickness == 0 ? 100 + d->marker_text.offset_px : d->custom_second_marker.inner_border_pct + d->marker_text.offset_px;
        if(marker_pct>100) {
          marker_pct = 100;
        } else if (marker_pct < 0) {
          marker_pct = 0;
        }
        pct = marker_pct;
        ecc = d->custom_second_marker.thickness == 0 ? 100 : d->custom_second_marker.inner_eccentricity;
      }
      h_offset = (font_lookup_height(d->marker_text.font_choice) + font_lookup_y_offset(d->marker_text.font_choice)) / 2;
    }
    GPoint top_pt = marker_layer_point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = marker_layer_point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    top_pt.y += h_offset;
    right_pt.x -= h_offset; // close enough
    bottom_pt.y -= h_offset;
    left_pt.x += h_offset;
    int16_t margin = 2;
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_reach = left_pt.x + margin, right_reach = screen.size.w - right_pt.x + margin;
    if (left_reach > dyn_left_inset) dyn_left_inset = left_reach;
    if (right_reach > dyn_right_inset) dyn_right_inset = right_reach;
  } else if (marker_style <= 2 || marker_style == 9) {
    uint8_t pct, ecc;
    marker_layer_inner_reach(marker_style, &pct, &ecc);
    GRect screen = GRect(0, 0, 200, 228);
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    GPoint top_pt = marker_layer_point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = marker_layer_point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    int16_t margin = 4;
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_reach = left_pt.x + margin, right_reach = screen.size.w - right_pt.x + margin;
    if (left_reach > dyn_left_inset) dyn_left_inset = left_reach;
    if (right_reach > dyn_right_inset) dyn_right_inset = right_reach;
  }

  if (is_analog) {
    bool has_line2 = d->upper_middle_line2_content != 0;
    int16_t line1_offset = has_line2 ? dyn_upper_offset : dyn_upper_offset + CORNER_ROW_H / 2;
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
      .use_primitive_pipeline = true,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_UPPER_L2] = (FeatureSlot){
        .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
        .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
        .use_primitive_pipeline = true,
        .top_offset = dyn_upper_offset + CORNER_ROW_H, .bottom_shift = 0, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line2_content),
      };
    }

    has_line2 = d->bottom_middle_line2_content != 0;
    int16_t line1_shift = has_line2 ? dyn_bottom_shift + CORNER_ROW_H : dyn_bottom_shift + CORNER_ROW_H / 2;
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
      .use_primitive_pipeline = true,
      .top_offset = 0, .bottom_shift = line1_shift, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_BOTTOM_L2] = (FeatureSlot){
        .active = true, .content = d->bottom_middle_line2_content, .color_mode = d->bottom_middle_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
        .use_primitive_pipeline = true,
        .top_offset = 0, .bottom_shift = dyn_bottom_shift, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line2_content),
      };
    }

    has_line2 = d->middle_left_line2_content != 0;
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = true, .is_edge = false,
      .use_primitive_pipeline = true,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_left_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_LEFT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = true, .is_edge = false,
        .use_primitive_pipeline = true,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_left_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line2_content),
      };
    }

    has_line2 = d->middle_right_line2_content != 0;
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = false, .is_left = false, .is_middle = true, .is_edge = false,
      .use_primitive_pipeline = true,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_right_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
        .is_top = false, .is_left = false, .is_middle = true, .is_edge = false,
        .use_primitive_pipeline = true,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_right_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line2_content),
      };
    }
  }

  // Big Digital and Grid: corners (set unconditionally below) plus
  // exactly one top-center and one bottom-center feature -- no sides, no
  // seconds. Reuses upper_middle_line1/bottom_middle_line1 exactly like
  // analog and digital top/bar already do for their own single-line
  // features, just centered above/below the clock face instead of
  // stacked in a column. Identical for both layouts, hence one branch.
  if (is_big_digital || is_grid) {
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
      .use_primitive_pipeline = true,
      .top_offset = FEATURE_LAYOUT_CORNER_INSET_PX, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line1_content),
    };
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
      .use_primitive_pipeline = true,
      .top_offset = 0, .bottom_shift = FEATURE_LAYOUT_CORNER_INSET_PX, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line1_content),
    };
  }

  // Digital-mode-only equivalent of the 4 blocks above -- the 8 edge
  if (!is_analog && !is_big_digital && !is_grid) {
    int16_t clock_x, clock_w;
    feature_layout_digital_clock_area(d->bottom_style, 200, &clock_x, &clock_w);

    // 1 = nearest the clock, 3 = nearest the screen's own outer edge
    int16_t row1_off = CORNER_ROW_H * 2, row2_off = CORNER_ROW_H, row3_off = 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .use_primitive_pipeline = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line1_content),
    };
    state->slots[SLOT_LEFT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .use_primitive_pipeline = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line2_content),
    };
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){ // reused: digital left column, row 3 -- reads upper_middle_line1
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .use_primitive_pipeline = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line1_content),
    };

    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .use_primitive_pipeline = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line1_content),
    };
    state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .use_primitive_pipeline = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line2_content),
    };
    state->slots[SLOT_UPPER_L2] = (FeatureSlot){ // reused: digital right column, row 3 -- reads upper_middle_line2
      .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .use_primitive_pipeline = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line2_content),
    };

    // Single bottom feature -- reuses bottom_middle_line1 (analog's
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = false,
      .use_primitive_pipeline = true,
      .top_offset = 0, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .custom_box = true, .box_x = clock_x, .box_w = clock_w,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line1_content),
    };
  }

  // Corners always just draw whatever d->corner_content[] says, for
  int16_t top_corner_shift = is_digital_top ? DIGITAL_PANEL_H : 0;
  state->slots[SLOT_CORNER_TL] = (FeatureSlot){
    .active = true, .content = d->corner_content[0], .color_mode = d->corner_color_mode[0],
    .is_top = true, .is_left = true, .is_middle = false, .is_edge = true,
    .use_primitive_pipeline = true,
    .top_offset = FEATURE_LAYOUT_CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[0]),
  };
  state->slots[SLOT_CORNER_TR] = (FeatureSlot){
    .active = true, .content = d->corner_content[1], .color_mode = d->corner_color_mode[1],
    .is_top = true, .is_left = false, .is_middle = false, .is_edge = true,
    .use_primitive_pipeline = true,
    .top_offset = FEATURE_LAYOUT_CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[1]),
  };
  // Bottom corners (BL/BR): stay anchored to the SKY's own bottom
  int16_t bottom_corner_shift = (is_analog || is_digital_top || is_big_digital || is_grid) ? 0 : DIGITAL_PANEL_H;
  state->slots[SLOT_CORNER_BL] = (FeatureSlot){
    .active = true, .content = d->corner_content[2], .color_mode = d->corner_color_mode[2],
    .is_top = false, .is_left = true, .is_middle = false, .is_edge = true,
    .use_primitive_pipeline = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[2]),
  };
  state->slots[SLOT_CORNER_BR] = (FeatureSlot){
    .active = true, .content = d->corner_content[3], .color_mode = d->corner_color_mode[3],
    .is_top = false, .is_left = false, .is_middle = false, .is_edge = true,
    .use_primitive_pipeline = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[3]),
  };
}

