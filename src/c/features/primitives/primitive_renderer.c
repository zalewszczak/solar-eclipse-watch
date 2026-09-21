#include "./primitive_renderer.h"
#include "../feature_slot.h"
#include "../feature_render.h"
#include "../feature_icons.h"

static const char *spacer_glyph(PrimitiveKind kind) {
  switch (kind) {
    case PRIMITIVE_KIND_SPACE: return " ";
    case PRIMITIVE_KIND_SLASH: return "/";
    case PRIMITIVE_KIND_DASH: return "-";
    default: return "";
  }
}

static bool is_text_like(PrimitiveKind kind) {
  return kind == PRIMITIVE_KIND_TEXT || kind == PRIMITIVE_KIND_SPACE
      || kind == PRIMITIVE_KIND_SLASH || kind == PRIMITIVE_KIND_DASH;
}

static GRect text_box_for(const PrimitiveInstance *p, int16_t box_x, int16_t box_y,
                          int16_t row_height, int16_t font_h, int16_t font_offset) {
  int16_t seg_x = box_x + p->x_offset;
  int16_t box_h = font_h + font_offset + 2;
  return GRect(seg_x, box_y + (row_height - box_h) / 2 - 1, p->width + 2, box_h);
}

void primitive_renderer_draw_feature(GContext *ctx, int16_t box_x, int16_t box_y, int16_t row_height,
                                     const PrimitiveFeature *feature, const FeatureSlot *slot,
                                     GFont font, int16_t font_h, int16_t font_offset,
                                     uint8_t outline_style, uint8_t icon_style, bool draw_debug) {
  if (!feature || feature->count == 0) return;
  uint8_t effective_outline_style = (slot && slot->allow_outline) ? outline_style : 0;

  // Pass 1 -- outline for every primitive that can be split (Section 13).
  // See primitive_renderer.h for why icon primitives are not split yet.
  for (int i = 0; i < feature->count; i++) {
    const PrimitiveInstance *p = &feature->items[i];
    if (!is_text_like(p->kind)) continue;
    const char *text = (p->kind == PRIMITIVE_KIND_TEXT) ? p->text : spacer_glyph(p->kind);
    GRect box = text_box_for(p, box_x, box_y, row_height, font_h, font_offset);
    feature_render_draw_text_outline_only(ctx, text, font, box, GTextOverflowModeTrailingEllipsis,
                                          GTextAlignmentLeft, p->color, effective_outline_style);
  }

  // Pass 2 -- colored content for every primitive.
  for (int i = 0; i < feature->count; i++) {
    const PrimitiveInstance *p = &feature->items[i];
    int16_t seg_x = box_x + p->x_offset;
    if (p->kind == PRIMITIVE_KIND_ICON) {
      feature_icons_draw_render_icon(ctx, p->icon_kind, p->aux, p->aux_flag, p->color, p->color2,
                                     seg_x, box_y, row_height, effective_outline_style, icon_style, draw_debug);
      continue;
    }
    const char *text = (p->kind == PRIMITIVE_KIND_TEXT) ? p->text : spacer_glyph(p->kind);
    GRect box = text_box_for(p, box_x, box_y, row_height, font_h, font_offset);
    feature_render_draw_text_content_only(ctx, text, font, box, GTextOverflowModeTrailingEllipsis,
                                          GTextAlignmentLeft, p->color);
    if (draw_debug) {
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_stroke_color(ctx, GColorGreen);
      graphics_draw_rect(ctx, box);
    }
  }
}
