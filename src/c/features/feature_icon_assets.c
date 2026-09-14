#include "./feature_icon_assets.h"

#define ICON_WIDTH 16
#define ICON_ROWS 12

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



void feature_icon_assets_draw_bitmap_tinted_sized(GContext *ctx, GBitmap *bmp, GPoint top_left, GColor color,
                                           int16_t w, int16_t h) {
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
    palette[ink] = color;
    palette[1 - ink] = GColorClear;
  }
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, w, h));
}



void feature_icon_assets_draw_bitmap_tinted(GContext *ctx, GBitmap *bmp, GPoint top_left, GColor color) {
  feature_icon_assets_draw_bitmap_tinted_sized(ctx, bmp, top_left, color, ICON_WIDTH, ICON_ROWS);
}



void feature_icon_assets_draw_resource(GContext *ctx, GPoint top_left, uint32_t resource_id, GColor color) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  feature_icon_assets_draw_bitmap_tinted(ctx, bmp, top_left, color);
  gbitmap_destroy(bmp);
}



void feature_icon_assets_draw_resource_with_outline_sized(GContext *ctx, GPoint pos, uint32_t resource_id,
                                                   uint8_t outline_style, GColor outline_color, GColor color,
                                                   int16_t w, int16_t h) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  if (outline_style != 0) {
    const GPoint *offs; int offs_n;
    feature_icon_assets_get_outline_offsets(outline_style, &offs, &offs_n);
    for (int i = 0; i < offs_n; i++) {
      GPoint shifted = GPoint(pos.x + offs[i].x, pos.y + offs[i].y);
      feature_icon_assets_draw_bitmap_tinted_sized(ctx, bmp, shifted, outline_color, w, h);
    }
  }
  feature_icon_assets_draw_bitmap_tinted_sized(ctx, bmp, pos, color, w, h);
  gbitmap_destroy(bmp);
}



void feature_icon_assets_draw_resource_with_outline(GContext *ctx, GPoint pos, uint32_t resource_id,
                                             uint8_t outline_style, GColor outline_color, GColor color) {
  feature_icon_assets_draw_resource_with_outline_sized(ctx, pos, resource_id, outline_style, outline_color, color,
                                         ICON_WIDTH, ICON_ROWS);
}



void feature_icon_assets_draw_resource_native(GContext *ctx, GPoint top_left, uint32_t resource_id) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, ICON_WIDTH, ICON_ROWS));
  gbitmap_destroy(bmp);
}



