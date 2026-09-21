#include "./primitive_layout.h"
#include "../feature_slot.h"
#include "../feature_icons.h"

// Mirrors feature_render_resolve_segment_offsets() (feature_render.c)
// exactly, generalized to PrimitiveInstance and to the three universal
// spacer kinds it never had to handle before (Section 4).

static const char *spacer_glyph(PrimitiveKind kind) {
  switch (kind) {
    case PRIMITIVE_KIND_SPACE: return " ";
    case PRIMITIVE_KIND_SLASH: return "/";
    case PRIMITIVE_KIND_DASH: return "-";
    default: return "";
  }
}

void primitive_layout_compute(PrimitiveFeature *feature, const FeatureSlot *slot, GFont font, int16_t font_h) {
  if (!feature || !slot) return;

  int16_t box_w = slot->custom_box ? slot->box_w : CORNER_BOX_W;
  int16_t advance[PRIMITIVE_FEATURE_MAX];
  int16_t total_w = 0;

  for (int i = 0; i < feature->count; i++) {
    PrimitiveInstance *p = &feature->items[i];
    if (p->kind == PRIMITIVE_KIND_ICON) {
      p->width = FEATURE_ICON_WIDTH;
      advance[i] = feature_icons_plus_gap_width(p->icon_kind);
    } else {
      const char *text = (p->kind == PRIMITIVE_KIND_TEXT) ? p->text : spacer_glyph(p->kind);
      GSize sz = graphics_text_layout_get_content_size(
          text, font, GRect(0, 0, 200, font_h + 10),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
      p->width = sz.w + 2;
      advance[i] = p->width + 5;
    }
    total_w += advance[i];
  }

  if (total_w > box_w) {
    if (slot->is_middle) {
      if (slot->is_left) total_w = box_w;
    } else if (!slot->is_left && !slot->is_edge) {
      total_w -= total_w - box_w;
    }
  }
  feature->total_width = total_w;

  int16_t start_x = slot->custom_box ? (box_w - total_w) / 2
                    : (slot->center_horizontal ? (box_w - total_w) / 2
                       : (!slot->is_left ? box_w - total_w : 0));
  int16_t x = start_x;
  for (int i = 0; i < feature->count; i++) {
    feature->items[i].x_offset = x;
    x += advance[i];
  }
}
