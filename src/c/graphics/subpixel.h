#pragma once

#include <pebble.h>

// Shared Q24.8 geometry and rasterizers for hands, markers, and background shapes.

#define SUBPIXEL_BITS 8
#define SUBPIXEL_SCALE (1 << SUBPIXEL_BITS) // 256
#define SUBPIXEL_MASK  (SUBPIXEL_SCALE - 1)
#define SUBPIXEL_HALF  (1 << (SUBPIXEL_BITS - 1)) // 128 (0.5 px)

typedef struct {
  int32_t x; // Q24.8 X coordinate.
  int32_t y; // Q24.8 Y coordinate.
} FGPoint;

static inline FGPoint subpixel_fgpoint_new(int32_t x, int32_t y) {
  return (FGPoint){ .x = x, .y = y };
}

static inline FGPoint subpixel_fgpoint_from_gpoint(GPoint p) {
  return (FGPoint){ .x = ((int32_t)p.x) << SUBPIXEL_BITS, .y = ((int32_t)p.y) << SUBPIXEL_BITS };
}

// Round Q24.8 coordinates to the nearest pixel.
static inline int16_t subpixel_fp_round_to_px(int32_t v_fp) {
  return (int16_t)((v_fp + SUBPIXEL_HALF) >> SUBPIXEL_BITS);
}

static inline GPoint subpixel_fgpoint_to_gpoint(FGPoint fp) {
  return GPoint(subpixel_fp_round_to_px(fp.x), subpixel_fp_round_to_px(fp.y));
}

// Maximum polygon size supported by ring/inset helpers.
#define SUBPIXEL_MAX_RING_PTS 16

// Shared 4x4 Bayer matrix used by dithered rendering.
extern const uint8_t BAYER4[4][4];

// Divide with rounding; avoids zero-width offsets in thin geometry.
int32_t subpixel_round_div(int32_t num, int32_t denom);

// Integer square root for 64-bit squared Q24.8 lengths.
uint32_t subpixel_isqrt64_fp(int64_t v);

// Convex polygon inclusion test using 64-bit cross-products.
bool subpixel_point_in_convex_polygon_fp(const FGPoint *pts, int n, FGPoint p);

// Solid Q24.8 polygon fill with scanline optimization.
void subpixel_fill_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color);

// Bayer-dithered Q24.8 polygon fill.
void subpixel_fill_polygon_dithered_fp(GContext *ctx, const FGPoint *pts, int n, GColor color);

// 3x3-supersampled fills for thin shapes.
void subpixel_fill_polygon_thin_fp(GContext *ctx, const FGPoint *pts, int n, GColor color);
void subpixel_fill_circle_thin_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color);

// Q24.8 circle fill.
void subpixel_fill_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered);

// 1-pixel Q24.8 line stroke.
void subpixel_stroke_line_fp(GContext *ctx, FGPoint a, FGPoint b, GColor color, bool dithered);

// Stroke the polygon perimeter with optional Bayer dithering.
void subpixel_stroke_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color, bool dithered);

// Stroke a circle boundary with optional Bayer dithering.
void subpixel_stroke_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered);

// ---- inline stroke fills ------------------------------------------------

// Inset a convex polygon by d_fp; returns false if the inset is invalid.
bool subpixel_inset_convex_polygon_fp(const FGPoint *pts, int n, int32_t d_fp, FGPoint *out_pts);

// Fill an inward ring; fall back to a solid fill if the inset fails.
void subpixel_fill_polygon_ring_fp(GContext *ctx, const FGPoint *pts, int n, int32_t thickness_fp, GColor color, bool dithered);

// Fill an inward ring around a circle.
void subpixel_fill_circle_ring_fp(GContext *ctx, FGPoint center, int32_t outer_r_fp, int32_t thickness_fp, GColor color, bool dithered);
