#include "./feature_render.h"
#include "./feature_icons.h"
#include "./feature_icon_assets.h"

#define CORNER_INSET_PX 2

GColor feature_render_contrasting_outline_color(GColor c) {
  uint8_t r = (c.argb >> 4) & 0x03;
  uint8_t g = (c.argb >> 2) & 0x03;
  uint8_t b = c.argb & 0x03;
  int luma = r * 3 + g * 6 + b;
  return (luma >= 9) ? GColorBlack : GColorWhite;
}

void feature_render_draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                                       GTextOverflowMode overflow, GTextAlignment alignment,
                                       GColor color, uint8_t outline_style) {
  if (outline_style != 0) {
    const GPoint *offsets;
    int offset_count;
    feature_icon_assets_get_outline_offsets(outline_style, &offsets, &offset_count);
    graphics_context_set_text_color(ctx, feature_render_contrasting_outline_color(color));
    for (int i = 0; i < offset_count; i++) {
      GRect shifted = GRect(box.origin.x + offsets[i].x, box.origin.y + offsets[i].y,
                             box.size.w, box.size.h);
      graphics_draw_text(ctx, text, font, shifted, overflow, alignment, NULL);
    }
  }
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, overflow, alignment, NULL);
}

void feature_render_resolve_segment_offsets(FeatureSlot *slot, GFont font, int16_t font_h) {
  int16_t box_w = slot->custom_box ? slot->box_w : CORNER_BOX_W;
  int16_t advance[MAX_RENDER_SEGMENTS];
  int16_t total_w = 0;

  for (int i = 0; i < slot->segment_count; i++) {
    RenderSegment *seg = &slot->segments[i];
    if (seg->is_icon) {
      seg->width = FEATURE_ICON_WIDTH;
      advance[i] = feature_icons_plus_gap_width(seg->icon_kind);
    } else {
      GSize sz = graphics_text_layout_get_content_size(
          seg->text, font, GRect(0, 0, 200, font_h + 10),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
      seg->width = sz.w + 2;
      advance[i] = seg->width + 5;
    }
    total_w += advance[i];
  }

  if (total_w > box_w) {
    if (slot->is_middle) {
      if (slot->is_left) {
        total_w = box_w;
      }
    } else if (!slot->is_left && !slot->is_edge) {
      total_w -= total_w - box_w;
    }
  }

  int16_t start_x = slot->custom_box ? (box_w - total_w) / 2
                    : (slot->center_horizontal ? (box_w - total_w) / 2
                       : (!slot->is_left ? box_w - total_w : 0));
  int16_t x = start_x;
  for (int i = 0; i < slot->segment_count; i++) {
    slot->segments[i].x_offset = x;
    x += advance[i];
  }
}

void feature_render_draw_slot(GContext *ctx, GRect bounds, const FeatureSlot *slot,
                              GFont font, int16_t font_h, int16_t font_offset,
                              uint8_t outline_style, uint8_t weather_icon_style,
                              bool draw_debug) {
  if (!slot->active || slot->segment_count == 0) return;

  int16_t box_w = slot->custom_box ? slot->box_w : CORNER_BOX_W;
  int16_t box_x = slot->custom_box ? bounds.origin.x + slot->box_x
    : (slot->center_horizontal
       ? bounds.origin.x + (bounds.size.w - CORNER_BOX_W) / 2
       : (slot->is_left ? bounds.origin.x + CORNER_INSET_PX
                        : bounds.origin.x + bounds.size.w - CORNER_INSET_PX - CORNER_BOX_W));
  int16_t box_y = slot->center_vertical
    ? bounds.origin.y + (bounds.size.h - CORNER_ROW_H) / 2 + slot->top_offset
    : (slot->is_top ? bounds.origin.y + slot->top_offset - 2
                    : bounds.origin.y + bounds.size.h - CORNER_ROW_H - CORNER_INSET_PX
                      - slot->bottom_shift + 2);

  if (slot->is_middle) {
    if (slot->is_left) box_x += slot->middle_inset;
    else box_x -= slot->middle_inset;
  }

  if (slot->draw_pill) {
    graphics_context_set_fill_color(ctx, slot->pill_bg);
    graphics_fill_rect(ctx, GRect(box_x, box_y, box_w, CORNER_ROW_H),
                       CORNER_ROW_H / 2, GCornersAll);
  }

  uint8_t effective_outline_style = slot->allow_outline ? outline_style : 0;
  for (int i = 0; i < slot->segment_count; i++) {
    const RenderSegment *seg = &slot->segments[i];
    int16_t seg_x = box_x + seg->x_offset;
    if (seg->is_icon) {
      feature_icons_draw_render_icon(ctx, seg->icon_kind, seg->icon_extra, seg->icon_flag,
                                     seg->color, seg->color2, seg_x, box_y, CORNER_ROW_H,
                                     effective_outline_style, weather_icon_style, draw_debug);
    } else {
      int16_t box_h = font_h + font_offset + 2;
      GRect bounding_box = GRect(seg_x, box_y + (CORNER_ROW_H - box_h) / 2 - 1,
                                 seg->width + 2, box_h);
      feature_render_draw_text_outlined(ctx, seg->text, font, bounding_box,
                                        GTextOverflowModeTrailingEllipsis,
                                        GTextAlignmentLeft, seg->color,
                                        effective_outline_style);
      if (draw_debug) {
        graphics_context_set_stroke_width(ctx, 1);
        graphics_context_set_stroke_color(ctx, GColorGreen);
        graphics_draw_rect(ctx, bounding_box);
      }
    }
  }
}
