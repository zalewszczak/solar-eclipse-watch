#include "./feature_icon_assets.h"

const GPoint FEATURE_ICON_OUTLINE_OFFSETS_THIN[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
const GPoint FEATURE_ICON_OUTLINE_OFFSETS_THICK[12] = {
  {-1, 0}, {1, 0}, {0, -1}, {0, 1},
  {-2, 0}, {2, 0}, {0, -2}, {0, 2},
  {1, 1}, {-1, 1}, {-1, -1}, {1, -1},
};
void feature_icon_assets_get_outline_offsets(uint8_t style, const GPoint **offsets, int *count) {
  if (style >= 2) { *offsets = FEATURE_ICON_OUTLINE_OFFSETS_THICK; *count = 12; }
  else { *offsets = FEATURE_ICON_OUTLINE_OFFSETS_THIN; *count = 4; }
}

void feature_icon_assets_draw_tiny(GContext *ctx, GPoint top_left, const uint8_t *pattern, int rows, int width, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int bytes_per_row = (width + 7) / 8;
  for (int row = 0; row < rows; row++) {
    int16_t y0 = top_left.y + row;
    for (int col = 0; col < width; col++) {
      int byte_index = row * bytes_per_row + col / 8;
      int bit_index = 7 - (col % 8);
      if (pattern[byte_index] & (1 << bit_index)) {
        int16_t x0 = top_left.x + col;
        graphics_fill_rect(ctx, GRect(x0, y0, 1, 1), 0, GCornerNone);
      }
    }
  }
}



// Palette-tints a already-loaded 2-color bitmap to `color` (the lighter of
// its two palette entries becomes transparent, the other becomes `color`)
// and draws it at the given size -- the actual recoloring step behind
// feature_icon_assets_draw_styled()'s simple/hollow tinting, exposed here
// so any other single-resource, single-color bitmap (e.g. Big Digital's
// digit art) can reuse it without a second tinting implementation.
//
// `transparent` forces the ink color's alpha bits the same way
// marker_bitmap_tint() does for bitmap markers (0x80 for its "see the sky
// through it" look, 0xC0 for fully opaque) -- same two-line technique, not
// a second implementation, so callers that want that exact look (Big
// Digital's digit art, via the same CONFIG_BITMAP_MARKER_TRANSPARENT
// setting bitmap markers use) don't add to the binary for it.
void feature_icon_assets_draw_bitmap_tinted_sized(GContext *ctx, GBitmap *bmp, GPoint top_left, GColor color,
                                           int16_t w, int16_t h, bool transparent) {
  GColor *palette = gbitmap_get_palette(bmp);
  if (palette) {
    bool transparent0 = (palette[0].argb & 0xC0) == 0;
    bool transparent1 = (palette[1].argb & 0xC0) == 0;
    int ink;
    if (transparent0 != transparent1) {
      ink = transparent0 ? 1 : 0;
    } else {
      int sum0 = ((palette[0].argb >> 4) & 0x03) + ((palette[0].argb >> 2) & 0x03) + (palette[0].argb & 0x03);
      int sum1 = ((palette[1].argb >> 4) & 0x03) + ((palette[1].argb >> 2) & 0x03) + (palette[1].argb & 0x03);
      ink = (sum0 <= sum1) ? 0 : 1;
    }
    uint8_t forced_alpha_bits = transparent ? 0x80 : 0xC0;
    GColor tinted;
    tinted.argb = forced_alpha_bits | (color.argb & 0x3F);
    palette[ink] = tinted;
    palette[1 - ink] = GColorClear;
  }
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, w, h));
}



static void feature_icon_assets_draw_resource_native_sized(GContext *ctx, GPoint top_left, uint32_t resource_id,
                                                     int16_t w, int16_t h) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, w, h));
  gbitmap_destroy(bmp);
}



uint32_t feature_icon_assets_resolve_styled(const IconResourceSet *set, uint8_t style) {
  switch (style) {
    case 0: return set->simple;
    case 2: return set->fullcolor;
    case 1:
    default: return set->hollow;
  }
}



void feature_icon_assets_draw_styled(GContext *ctx, GPoint top_left, const IconResourceSet *set, uint8_t style,
                                     GColor color, int16_t w, int16_t h) {
  uint32_t resource_id = feature_icon_assets_resolve_styled(set, style);
  if (!resource_id) return;
  if (style == 2) {
    feature_icon_assets_draw_resource_native_sized(ctx, top_left, resource_id, w, h);
  } else {
    GBitmap *bmp = gbitmap_create_with_resource(resource_id);
    if (!bmp) return;
    feature_icon_assets_draw_bitmap_tinted_sized(ctx, bmp, top_left, color, w, h, false);
    gbitmap_destroy(bmp);
  }
}



void feature_icon_assets_draw_styled_with_outline(GContext *ctx, GPoint pos, const IconResourceSet *set, uint8_t style,
                                                   uint8_t outline_style, GColor outline_color, GColor color,
                                                   int16_t w, int16_t h) {
  if (outline_style != 0) {
    // Full color art has no transparent-ink channel to tint, so its outline
    // pass borrows the hollow set's silhouette instead (same trick the old
    // per-style weather switch used).
    uint8_t outline_pick_style = (style == 2) ? 1 : style;
    uint32_t outline_resource = feature_icon_assets_resolve_styled(set, outline_pick_style);
    if (outline_resource) {
      GBitmap *bmp = gbitmap_create_with_resource(outline_resource);
      if (bmp) {
        const GPoint *offs; int offs_n;
        feature_icon_assets_get_outline_offsets(outline_style, &offs, &offs_n);
        for (int i = 0; i < offs_n; i++) {
          GPoint shifted = GPoint(pos.x + offs[i].x, pos.y + offs[i].y);
          feature_icon_assets_draw_bitmap_tinted_sized(ctx, bmp, shifted, outline_color, w, h, false);
        }
        gbitmap_destroy(bmp);
      }
    }
  }
  feature_icon_assets_draw_styled(ctx, pos, set, style, color, w, h);
}



