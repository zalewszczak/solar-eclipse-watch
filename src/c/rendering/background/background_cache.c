#include "./background_cache.h"
#include <string.h>

#define BACKGROUND_CACHE_REFRESH_S 60

void background_cache_init(BackgroundCache *cache, GSize size) {
  cache->bitmap = gbitmap_create_blank(size, GBitmapFormat8Bit);
  cache->last_full_draw = 0;
  cache->force_next_draw = true;
}

void background_cache_deinit(BackgroundCache *cache) {
  if (cache->bitmap) {
    gbitmap_destroy(cache->bitmap);
    cache->bitmap = NULL;
  }
}

void background_cache_invalidate(BackgroundCache *cache) {
  cache->force_next_draw = true;
}

bool background_cache_needs_refresh(const BackgroundCache *cache, time_t now) {
  if (cache->force_next_draw || !cache->bitmap) return true;

  time_t elapsed = now - cache->last_full_draw;
  return elapsed < 0 || elapsed >= BACKGROUND_CACHE_REFRESH_S;
}

bool background_cache_blit(const BackgroundCache *cache, GContext *ctx, GRect bounds) {
  if (!cache->bitmap) return false;
  graphics_draw_bitmap_in_rect(ctx, cache->bitmap, bounds);
  return true;
}

void background_cache_capture(BackgroundCache *cache, GContext *ctx, GRect bounds) {
  if (!cache->bitmap) return;

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;

  uint8_t *src_base = gbitmap_get_data(fb);
  uint16_t src_stride = gbitmap_get_bytes_per_row(fb);
  GRect fb_bounds = gbitmap_get_bounds(fb);
  uint8_t *dst_base = gbitmap_get_data(cache->bitmap);
  uint16_t dst_stride = gbitmap_get_bytes_per_row(cache->bitmap);

  int16_t copy_w = bounds.size.w;
  int16_t copy_h = bounds.size.h;
  if (bounds.origin.x + copy_w > fb_bounds.size.w) {
    copy_w = fb_bounds.size.w - bounds.origin.x;
  }
  if (bounds.origin.y + copy_h > fb_bounds.size.h) {
    copy_h = fb_bounds.size.h - bounds.origin.y;
  }

  if (copy_w > 0 && copy_h > 0) {
    for (int16_t y = 0; y < copy_h; y++) {
      uint8_t *src_row = src_base + (bounds.origin.y + y) * src_stride + bounds.origin.x;
      uint8_t *dst_row = dst_base + y * dst_stride;
      memcpy(dst_row, src_row, copy_w);
    }
  }

  graphics_release_frame_buffer(ctx, fb);
}

void background_cache_commit(BackgroundCache *cache, time_t now) {
  cache->force_next_draw = false;
  cache->last_full_draw = now;
}
