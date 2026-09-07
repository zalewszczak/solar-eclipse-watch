#pragma once

#include <pebble.h>

// ---------------------------------------------------------------------------
// Shared sub-pixel fixed-point (Q24.8) coordinate system, and the generic
// polygon/circle fill+stroke rasterizers built on it.
//
// This used to live only in hand_layer.c -- hands need sub-degree angular
// precision since they rotate continuously. Custom markers (background_layer.c)
// rotate around the dial exactly the same way, so this got pulled out into
// its own header rather than staying hand-specific: background_layer.c's
// marker ring now builds each mark's geometry in this same Q24.8 space
// (see point_on_ring_fp()/draw_ring_mark_fp() there) instead of rounding
// each mark's endpoints to whole pixels first and only then computing its
// thickness -- the same class of precision loss compute_hand_geometry_fp's
// use of this system already avoids for hands.
//
// F: this header used to hold every function's full body as `static`, so
// both background_layer.c and hand_layer.c (the only two includers) each
// got their own private copy of anything they used -- fine for genuinely
// tiny one-line accessors, but real rasterizer bodies (fill_circle_fp,
// fill_circle_thin_fp, point_in_convex_polygon_fp, ...) were being emitted
// twice, one full copy per translation unit, for exactly the functions both
// files happen to use. Real function bodies now live in subpixel.c instead
// (one shared copy, declared `extern` below); only trivial `static inline`
// one-liners stay header-only, where duplicating them costs nothing.
// ---------------------------------------------------------------------------

#define SUBPIXEL_BITS 8
#define SUBPIXEL_SCALE (1 << SUBPIXEL_BITS) // 256
#define SUBPIXEL_MASK  (SUBPIXEL_SCALE - 1)
#define SUBPIXEL_HALF  (1 << (SUBPIXEL_BITS - 1)) // 128 (0.5 px)

typedef struct {
  int32_t x; // Fixed-point X coordinate (24.8)
  int32_t y; // Fixed-point Y coordinate (24.8)
} FGPoint;

static inline FGPoint fgpoint_new(int32_t x, int32_t y) {
  return (FGPoint){ .x = x, .y = y };
}

static inline FGPoint fgpoint_from_gpoint(GPoint p) {
  return (FGPoint){ .x = ((int32_t)p.x) << SUBPIXEL_BITS, .y = ((int32_t)p.y) << SUBPIXEL_BITS };
}

// Rounds a fixed-point coordinate to the nearest whole pixel (adds half a
// pixel before truncating) rather than flooring it -- every rasterizer
// below that needs a final integer pixel uses this same convention, so a
// filled shape and a stroked outline of the "same" geometric boundary land
// on the same pixels instead of drifting apart by up to half a pixel (see
// stroke_line_fp()'s comment for why that drift used to be the actual bug).
static inline int16_t fp_round_to_px(int32_t v_fp) {
  return (int16_t)((v_fp + SUBPIXEL_HALF) >> SUBPIXEL_BITS);
}

static inline GPoint fgpoint_to_gpoint(FGPoint fp) {
  return GPoint(fp_round_to_px(fp.x), fp_round_to_px(fp.y));
}

// Shared cap on how many points a caller may pass into
// inset_convex_polygon_fp()/fill_polygon_ring_fp() below -- these two
// need their own fixed-size local (stack) arrays, independent of
// hand_layer.c's own HAND_MAX_POLY_PTS (which must stay <= this).
#define SUBPIXEL_MAX_RING_PTS 16

// 4x4 ordered-dither matrix + ~50% threshold, shared by every dithered
// fill/stroke below -- one copy, in subpixel.c, used directly by both
// background_layer.c and hand_layer.c's own dithering code (not just by
// the rasterizers below), so it stays `extern` rather than file-static.
extern const uint8_t BAYER4[4][4];

// Rounds to the nearest integer rather than truncating toward zero --
// matters most for thin hands (e.g. the second hand's default preset,
// width=1 -> half_w clamped to 1): (half_w * trig_component) /
// TRIG_MAX_RATIO with plain C integer division truncates any fractional
// pixel offset straight to 0 whenever |trig_component| is under
// TRIG_MAX_RATIO, which is EVERY angle except exactly the four
// cardinal ones (0/90/180/270) where one component hits its max
// magnitude exactly -- so a half_w=1 hand's perpendicular width offset
// truncated to (0,0) at all other angles, collapsing its triangle to a
// zero-area shape that doesn't render at all. This showed up as "the
// second hand only draws at right angles". Rounding to nearest instead
// keeps that offset non-zero across the whole sweep (verified
// numerically for all 60 second positions before this was written).
int32_t round_div(int32_t num, int32_t denom);

// Plain integer square root (binary/digit-by-digit method, same
// approach as background_layer.c's own isqrt32) for 64-bit inputs --
// needed here (rather than reusing that 32-bit one) because squared
// sub-pixel (Q24.8) lengths overflow int32 well before the lengths
// themselves get interesting (a ~100px edge is already ~25600 in fp
// units, and 25600^2 alone is > INT32_MAX). Used by
// inset_convex_polygon_fp() below (hollow-thickness ring fills) and
// by hand_layer.c's serpentine style (per-vertex tangent
// normalization) -- both need a real Euclidean length from fp
// dx/dy, not just a comparison.
uint32_t isqrt64_fp(int64_t v);

// Fixed-point convex polygon inclusion test using 64-bit cross-product
bool point_in_convex_polygon_fp(const FGPoint *pts, int n, FGPoint p);

// Sub-pixel solid polygon fill with scanline optimization
void fill_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color);

// Sub-pixel Bayer-dithered polygon fill
void fill_polygon_dithered_fp(GContext *ctx, const FGPoint *pts, int n, GColor color);

// Anti-aliased fills for thin shapes (nominal width under ~3px) --
// point_in_convex_polygon_fp()/the circle math sample exactly ONE point
// per candidate pixel (its center), so whether a given pixel along a
// narrow shape gets drawn at all comes down to whether the shape's true
// edge happens to fall on the near or far side of that single sample
// point -- not how much of the pixel it actually covers. For a wide
// shape that's invisible; for a thin one it's the difference between a
// clean edge and a line that looks thinner than its nominal width, or
// even develops gaps, at certain sub-pixel angles. The fix supersamples
// 3x3 around the usual sample point for a 0-9 coverage count, then
// dithers proportionally to that coverage (see subpixel.c) rather than
// either drawing or skipping a pixel outright. Reserved for genuinely
// thin shapes rather than replacing the plain fills everywhere: 9
// point-in-shape tests per candidate pixel is meaningfully more
// expensive than 1, and a thin shape's own bounding box stays small
// (proportional to its width, however long it runs), which is exactly
// what keeps that extra cost bounded to shapes that were cheap to
// rasterize in the first place.
void fill_polygon_thin_fp(GContext *ctx, const FGPoint *pts, int n, GColor color);
void fill_circle_thin_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color);

// Sub-pixel circle fill (solid & dithered)
void fill_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered);

// Sub-pixel line DDA for precise outer contours -- a genuine 1px stroke,
// used both directly (hollow shapes) and as the building block for
// stroke_polygon_fp()/stroke_circle_fp() below.
void stroke_line_fp(GContext *ctx, FGPoint a, FGPoint b, GColor color, bool dithered);

// A ~50%-Bayer-dithered stroke along the polygon's actual perimeter --
// each consecutive pair of points (including the wrap from the last
// point back to the first), walked pixel by pixel (Bresenham-ish) with
// the same dither test fill_polygon_dithered_fp() uses.
void stroke_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color, bool dithered);

// Same idea as stroke_polygon_fp(), for a round cap or dot -- a
// midpoint-circle walk of just the boundary pixels, each checked against
// the dither test rather than filling the whole disc.
void stroke_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered);

// ---- inline ("hollow thickness") stroke fills ------------------------
//
// stroke_polygon_fp()/stroke_circle_fp() above trace a genuine but
// always-1px perimeter. The "hollow thickness" hand feature (see
// HandConfig.hollow_thickness in hand_layer.h) needs an inline stroke
// of arbitrary width instead -- drawn WITHIN the shape's own outline,
// unlike outline_enabled's perimeter trace which sits OUTSIDE it. The
// two functions below build that by shrinking the shape inward by the
// requested thickness (a standard convex polygon erosion for
// polygons; a plain smaller radius for circles) and filling the ring
// between the original boundary and the shrunk one.

// Shrinks a convex polygon inward by `d_fp` along every edge. Works
// for any convex n-gon (any of this file's callers build their
// points in whichever order was convenient for that particular
// shape), by testing per-edge which of its two perpendiculars points
// toward the polygon's own centroid rather than assuming a fixed
// winding order. Returns false (out_pts left untouched) if d_fp is
// too large for this particular polygon -- offsetting every edge
// inward by more than the shape's own narrowest span inverts the
// winding instead of shrinking it. Callers should fall back to a
// plain solid fill in that case.
bool inset_convex_polygon_fp(const FGPoint *pts, int n, int32_t d_fp, FGPoint *out_pts);

// Fills the ring between a convex polygon and its own d_fp-inset
// (i.e. an inline stroke of thickness d_fp, drawn just inside the
// polygon's boundary) -- or, if the inset failed (thickness too large
// for this shape -- see inset_convex_polygon_fp() above), just fills
// the whole polygon solid instead of leaving it empty.
void fill_polygon_ring_fp(GContext *ctx, const FGPoint *pts, int n, int32_t thickness_fp, GColor color, bool dithered);

// Same idea for a circle -- the ring between radius outer_r_fp and
// outer_r_fp - thickness_fp, or a plain solid disc if that inner
// radius would be <= 0 (thickness covers the whole circle).
void fill_circle_ring_fp(GContext *ctx, FGPoint center, int32_t outer_r_fp, int32_t thickness_fp, GColor color, bool dithered);
