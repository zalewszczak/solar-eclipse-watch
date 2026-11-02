#include "./primitive_layout.h"
#include "../feature_slot.h"
#include "../feature_icons.h"

// Lays the primitives of one feature out left to right (spacer kinds
// included) inside the slot's box.

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
      // User-built sequences add no automatic gap after an icon.
      advance[i] = feature->tight ? FEATURE_ICON_WIDTH : feature_icons_plus_gap_width(p->icon_kind);
    } else if ((p->kind == PRIMITIVE_KIND_TEXT || p->kind == PRIMITIVE_KIND_PUNCT) && p->text[0] == '\0') {
      // Empty text (e.g. the AM/PM part of a world clock in 24 h style) takes no room at all.
      p->width = 0;
      advance[i] = 0;
      continue;
    } else {
      const char *text = (p->kind == PRIMITIVE_KIND_TEXT || p->kind == PRIMITIVE_KIND_PUNCT) ? p->text : spacer_glyph(p->kind);
      GSize sz = graphics_text_layout_get_content_size(
          text, font, GRect(0, 0, 200, font_h + 10),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
      p->width = sz.w + 2;
      // Text runs sit directly against their neighbours: spacing comes only
      // from Space elements (so "MON 9/24" is drawn as "MON 9/24"). The one
      // automatic gap, before an icon, is added below.
      advance[i] = sz.w;
      if (p->kind == PRIMITIVE_KIND_SPACE) {
        int16_t min_space = (font_h * 3) / 10;
        if (min_space < 3) min_space = 3;
        if (advance[i] < min_space) advance[i] = min_space;
      }
    }
  }
  if (!feature->tight) {
    // Text followed by an icon keeps the icon gap (icons carry their own
    // trailing gap, see feature_icons_plus_gap_width()).
    for (int i = 0; i < feature->count; i++) {
      if (feature->items[i].kind == PRIMITIVE_KIND_ICON || advance[i] == 0) continue;
      for (int j = i + 1; j < feature->count; j++) {
        if (advance[j] == 0) continue;
        if (feature->items[j].kind == PRIMITIVE_KIND_ICON) advance[i] += 7;
        break;
      }
    }
  }
  // Spacers at the end (before nothing, or before only empty elements) would
  // just push the visible text off centre.
  for (int i = feature->count - 1; i >= 0 && !feature->tight; i--) {
    PrimitiveInstance *p = &feature->items[i];
    if (advance[i] == 0) continue;
    if (p->kind == PRIMITIVE_KIND_SPACE) { advance[i] = 0; continue; }
    break;
  }
  for (int i = 0; i < feature->count; i++) total_w += advance[i];

  if (total_w > box_w) {
    if (slot->is_middle) {
      if (slot->is_left) total_w = box_w;
    } else if (!slot->is_left && !slot->is_edge) {
      total_w -= total_w - box_w;
    }
  }
  feature->total_width = total_w;

  int16_t start_x = (slot->custom_box || slot->center_horizontal) ? (box_w - total_w) / 2
                    : (!slot->is_left ? box_w - total_w : 0);
  int16_t x = start_x;
  for (int i = 0; i < feature->count; i++) {
    feature->items[i].x_offset = x;
    x += advance[i];
  }
}
