#include "feature_layout.h"
#include "background_layer.h"
#include "marker_layer.h"
#include "feature_rules.h"
#include "font_lookup.h"

#define FEATURE_LAYOUT_CORNER_INSET_PX 2

uint8_t feature_layout_digital_side_mode(uint8_t bottom_style) {
  return feature_layout_is_digital_top_layout(bottom_style) ? bottom_style - 5 : bottom_style;
}
bool feature_layout_is_digital_top_layout(uint8_t bottom_style) {
  return bottom_style >= 5;
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
  bool is_digital_top = feature_layout_is_digital_top_layout(d->bottom_style);
  uint8_t marker_style = d->big_analog_marker_style;
  bool is_bitmap_style = is_analog && marker_style >= 3 && marker_style != 8 && marker_style != 9;

  // Which of the 12 slots actually apply for the current marker/
  // bottom_style is decided entirely on the phone (see
  // computeSlotAvailability()/CORNER_CATEGORIES in config-page.js and
  // the availability check in index.js's dict-building step) -- by the
  // time a content field reaches here, it's already 0 ("None") for any
  // slot that shouldn't show for the current style, so this file just
  // builds every slot from whatever content it was given, unconditionally,
  // and trusts a content of 0 to mean "draws nothing" (already true --
  // see feature_values_compute_slot()'s own content==0 early return)
  // rather than keeping its own separate copy of "which styles support
  // which slots" to decide that upfront.

  // Inner-empty-area margins: procedural presets (0/1/2) and "none" (9)
  // are calculated from that style's own marker-ring geometry via
  // marker_layer_inner_reach() (same marker_layer_point_on_ring() technique
  // custom (8) uses below, just fed a fixed preset instead of a live
  // user config); bitmap styles (3-7) have no ring geometry at all, so
  // they use a fixed per-style table instead, each side independent.
  typedef struct { int16_t top, bottom, left, right; } EdgeMargins;
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
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_UPPER_L2] = (FeatureSlot){
        .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
        .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
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
      .top_offset = 0, .bottom_shift = line1_shift, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_BOTTOM_L2] = (FeatureSlot){
        .active = true, .content = d->bottom_middle_line2_content, .color_mode = d->bottom_middle_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
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
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_left_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_LEFT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = true, .is_edge = false,
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
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_right_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
        .is_top = false, .is_left = false, .is_middle = true, .is_edge = false,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_right_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line2_content),
      };
    }
  }

  // Digital-mode-only equivalent of the 4 blocks above -- the 8 edge
  // slot indices are unused by any analog "show_*" block when
  // !is_analog (none of those ran), so it's safe to repurpose 7 of
  // them here: SLOT_LEFT_L1/L2 + SLOT_UPPER_L1 as the 3-line left
  // column, SLOT_RIGHT_L1/L2 + SLOT_UPPER_L2 as the 3-line right
  // column, SLOT_BOTTOM_L1 as the single bottom feature (SLOT_BOTTOM_L2
  // stays unused). Deliberately reuses the SAME 8 EclipseData fields
  // analog mode's upper/bottom/left/right-middle content uses (see
  // their own dual-purpose comment in eclipse_data.h) rather than a
  // separate set of digital-only fields -- the two modes never run at
  // once, so there's nothing to actually preserve by keeping them
  // apart, and sharing saves both the extra bytes on the watch and the
  // extra AppMessage keys/traffic a second set would cost. Which side
  // columns actually apply for the current bottom_style (0/2/3/4) is
  // decided phone-side same as everything else here -- the content
  // fields already arrive zeroed for whichever side isn't turned on,
  // so both columns are just built unconditionally below.
  if (!is_analog) {
    int16_t clock_x, clock_w;
    feature_layout_digital_clock_area(d->bottom_style, 200, &clock_x, &clock_w);

    // 1 = nearest the clock, 3 = nearest the screen's own outer edge --
    // anchored off whichever edge is adjacent to the clock for the
    // CURRENT layout (the panel's own top for Digital bar, since the
    // clock sits near there with the sky above it; the panel's own
    // bottom for Digital top, mirrored, since the clock sits near
    // THERE with the sky below it instead) via is_top/top_offset vs
    // bottom_shift -- same row_1/2/3_off magnitudes either way, so
    // Digital top's column reads as a literal vertical flip of Digital
    // bar's rather than a separately-tuned layout. Bottom-anchored (not
    // top-anchored off a fixed panel offset) in the Digital bar case so
    // a shrinking screen during a system notification shifts the whole
    // stack up together, same as the corners already do, rather than
    // the top row drifting away from the panel it's meant to sit
    // inside -- Digital top has no such obstruction to react to (system
    // notifications only ever eat into the screen's bottom), so being
    // top-anchored there costs nothing.
    int16_t row1_off = CORNER_ROW_H * 2, row2_off = CORNER_ROW_H, row3_off = 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line1_content),
    };
    state->slots[SLOT_LEFT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line2_content),
    };
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){ // reused: digital left column, row 3 -- reads upper_middle_line1
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line1_content),
    };

    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line1_content),
    };
    state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line2_content),
    };
    state->slots[SLOT_UPPER_L2] = (FeatureSlot){ // reused: digital right column, row 3 -- reads upper_middle_line2
      .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line2_content),
    };

    // Single bottom feature -- reuses bottom_middle_line1 (analog's
    // upper of its own 2-line pair; bottom_middle_line2 has no
    // digital-mode role, 7 slots needed against 8 available fields).
    // Shares clock_x/clock_w with the clock text itself
    // (draw_digital_clock_panel() in pebble-eclipse-watch.c uses the
    // exact same feature_layout_digital_clock_area() call), always centered within
    // that band, anchored to the screen's own outer edge -- the true
    // bottom for Digital bar, the true top for Digital top (row 3's own
    // edge in both cases, per the comment above).
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = 0, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .custom_box = true, .box_x = clock_x, .box_w = clock_w,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line1_content),
    };
  }

  // Corners always just draw whatever d->corner_content[] says, for
  // every marker style including bitmap ones -- defaulting that to
  // "off" for bitmap styles (and offering an "enable corner features"
  // override) is the settings page's job, not this file's.
  //
  // Top corners (TL/TR): anchored to the screen's own top edge for
  // Analog and Digital bar (both have open sky right there), but
  // pulled DOWN by DIGITAL_PANEL_H for Digital top -- that layout's
  // panel sits at the top instead, so its own sky begins
  // DIGITAL_PANEL_H down, and these two need to land at THAT boundary
  // instead of the screen's real top edge (features_layer's frame
  // spans the full screen in every layout -- see apply_layout() -- so
  // without this they'd land inside the transparent panel itself,
  // overlapping the clock). Exact mirror of bottom_corner_shift below,
  // just for the opposite pair of corners.
  int16_t top_corner_shift = is_digital_top ? DIGITAL_PANEL_H : 0;
  state->slots[SLOT_CORNER_TL] = (FeatureSlot){
    .active = true, .content = d->corner_content[0], .color_mode = d->corner_color_mode[0],
    .is_top = true, .is_left = true, .is_middle = false, .is_edge = true,
    .top_offset = FEATURE_LAYOUT_CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[0]),
  };
  state->slots[SLOT_CORNER_TR] = (FeatureSlot){
    .active = true, .content = d->corner_content[1], .color_mode = d->corner_color_mode[1],
    .is_top = true, .is_left = false, .is_middle = false, .is_edge = true,
    .top_offset = FEATURE_LAYOUT_CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[1]),
  };
  // Bottom corners (BL/BR): stay anchored to the SKY's own bottom
  // edge, not the full screen's -- meaningfully different only in
  // Digital bar, where the sky canvas only occupies the screen's top
  // portion and the digital clock's own bottom panel fills the rest.
  // features_layer's own frame spans the FULL screen in every layout
  // (see apply_layout()), so a plain bottom_shift of 0 here would put
  // these two corners down inside the digital panel instead of at the
  // sky's own bottom-left/-right -- adding DIGITAL_PANEL_H's worth of
  // shift pulls them back up to the sky boundary. Analog mode has no
  // separate panel (sky already fills the screen) and Digital top's
  // own sky already reaches all the way to the real screen bottom (its
  // panel is up at the TOP instead -- see top_corner_shift above), so
  // bottom_shift stays 0 for both of those.
  int16_t bottom_corner_shift = (is_analog || is_digital_top) ? 0 : DIGITAL_PANEL_H;
  state->slots[SLOT_CORNER_BL] = (FeatureSlot){
    .active = true, .content = d->corner_content[2], .color_mode = d->corner_color_mode[2],
    .is_top = false, .is_left = true, .is_middle = false, .is_edge = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[2]),
  };
  state->slots[SLOT_CORNER_BR] = (FeatureSlot){
    .active = true, .content = d->corner_content[3], .color_mode = d->corner_color_mode[3],
    .is_top = false, .is_left = false, .is_middle = false, .is_edge = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[3]),
  };
}

