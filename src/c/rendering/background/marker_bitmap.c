#include "./marker_bitmap.h"

void marker_bitmap_release(GBitmap **bitmap, uint8_t *bitmap_style, bool *bitmap_tinted) {
  if (*bitmap) {
    gbitmap_destroy(*bitmap);
    *bitmap = NULL;
  }
  *bitmap_style = 255;
  *bitmap_tinted = false;
}

static uint32_t marker_bitmap_resource_id(uint8_t style) {
  switch (style) {
    case 3: return RESOURCE_ID_MODERN_BACKGROUND;
    case 4: return RESOURCE_ID_SHADOW_BACKGROUND;
    case 5: return RESOURCE_ID_TALLY_BACKGROUND;
    case 6: return RESOURCE_ID_BELL_BACKGROUND;
    case 7: return RESOURCE_ID_FANCY_BACKGROUND;
    default: return 0;
  }
}

void marker_bitmap_ensure(GBitmap **bitmap, uint8_t *bitmap_style, bool *bitmap_tinted,
                          GColor *tint_color, bool *tint_transparent, uint8_t style) {
  if (style < 3) {
    if (*bitmap) {
      gbitmap_destroy(*bitmap);
      *bitmap = NULL;
    }
    *bitmap_style = 255;
    *bitmap_tinted = false;
    return;
  }

  if (*bitmap_style == style && *bitmap) return;

  if (*bitmap) {
    gbitmap_destroy(*bitmap);
    *bitmap = NULL;
  }

  uint32_t res_id = marker_bitmap_resource_id(style);
  if (res_id != 0) *bitmap = gbitmap_create_with_resource(res_id);

  *bitmap_style = style;
  *bitmap_tinted = false;
  *tint_color = GColorClear;
  *tint_transparent = false;
}

void marker_bitmap_tint(GBitmap *bitmap, bool *bitmap_tinted, GColor *tint_color,
                        bool *tint_transparent, GColor tint, bool transparent) {
  if (!bitmap) return;
  if (*bitmap_tinted && tint_color->argb == tint.argb &&
      *tint_transparent == transparent) return;

  uint8_t forced_alpha_bits = transparent ? 0x80 : 0xC0;
  GBitmapFormat format = gbitmap_get_format(bitmap);

  if (format == GBitmapFormat1BitPalette || format == GBitmapFormat2BitPalette ||
      format == GBitmapFormat4BitPalette) {
    GColor *palette = gbitmap_get_palette(bitmap);
    if (palette) {
      int count = (format == GBitmapFormat1BitPalette) ? 2 :
                  (format == GBitmapFormat2BitPalette) ? 4 : 16;
      for (int i = 0; i < count; i++) {
        if ((palette[i].argb & 0xC0) == 0) continue;
        GColor new_color;
        new_color.argb = forced_alpha_bits | (tint.argb & 0x3F);
        palette[i] = new_color;
      }
    }
  } else if (format == GBitmapFormat8Bit) {
    uint8_t *data = gbitmap_get_data(bitmap);
    uint16_t stride = gbitmap_get_bytes_per_row(bitmap);
    GRect bounds = gbitmap_get_bounds(bitmap);
    for (int16_t y = 0; y < bounds.size.h; y++) {
      uint8_t *row = data + (int32_t)y * stride;
      for (int16_t x = 0; x < bounds.size.w; x++) {
        if ((row[x] & 0xC0) == 0) continue;
        row[x] = forced_alpha_bits | (tint.argb & 0x3F);
      }
    }
  }

  *bitmap_tinted = true;
  *tint_transparent = transparent;
  *tint_color = tint;
}

void marker_bitmap_draw(GContext *ctx, GBitmap *bitmap, GRect bounds,
                        bool anim_active, int32_t anim_progress_1000) {
  if (!bitmap) return;

  GRect bmp_bounds = gbitmap_get_bounds(bitmap);
  GRect dest = GRect(bounds.origin.x + (bounds.size.w - bmp_bounds.size.w) / 2,
                     bounds.origin.y + (bounds.size.h - bmp_bounds.size.h) / 2,
                     bmp_bounds.size.w, bmp_bounds.size.h);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);

  // Bitmap marker styles currently draw immediately. The animation arguments
  // are retained so the renderer can share one call shape with procedural and
  // text markers if a true bitmap reveal is implemented later.
  (void)anim_active;
  (void)anim_progress_1000;
  graphics_draw_bitmap_in_rect(ctx, bitmap, dest);
}
