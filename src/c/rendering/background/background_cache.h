#pragma once

#include <pebble.h>
#include <time.h>

// Cached framebuffer region used by the background canvas between expensive

typedef struct {
  GBitmap *bitmap;
  time_t last_full_draw;
  bool force_next_draw;
} BackgroundCache;

// Creates the cache bitmap for a canvas of the given size.
void background_cache_init(BackgroundCache *cache, GSize size);

// Releases the cached bitmap.
void background_cache_deinit(BackgroundCache *cache);

// Invalidates the cached frame so the next canvas update must render fully.
void background_cache_invalidate(BackgroundCache *cache);

// Returns true when the cache has been explicitly invalidated or is older
// than the background's normal 60-second refresh interval.
bool background_cache_needs_refresh(const BackgroundCache *cache, time_t now);

// Restores the most recently captured background into the current canvas.
// Returns false if no valid cached frame exists.
bool background_cache_blit(const BackgroundCache *cache, GContext *ctx, GRect bounds);

// Captures the current canvas framebuffer into the cache. Must be called from
// inside an update_proc after the base composition has been drawn.
void background_cache_capture(BackgroundCache *cache, GContext *ctx, GRect bounds);

// Records the successful full-render timestamp and clears the invalidation.
void background_cache_commit(BackgroundCache *cache, time_t now);
