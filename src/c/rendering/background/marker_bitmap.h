#pragma once

#include <pebble.h>

// Bitmap marker resource management is kept separate from procedural marker
// geometry. The caller owns the state fields and this module owns the bitmap
// resource/tint operations.
void marker_bitmap_release(GBitmap **bitmap, uint8_t *bitmap_style, bool *bitmap_tinted);
void marker_bitmap_ensure(GBitmap **bitmap, uint8_t *bitmap_style, bool *bitmap_tinted,
                          GColor *tint_color, bool *tint_transparent, uint8_t style);
void marker_bitmap_tint(GBitmap *bitmap, bool *bitmap_tinted, GColor *tint_color,
                        bool *tint_transparent, GColor tint, bool transparent);
void marker_bitmap_draw(GContext *ctx, GBitmap *bitmap, GRect bounds,
                        bool anim_active, int32_t anim_progress_1000);
