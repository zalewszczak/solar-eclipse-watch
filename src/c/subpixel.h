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
// Every function here is `static` -- each .c file that includes this
// header gets its own private copy, no linker symbols shared across
// translation units. That mirrors this project's existing convention of
// duplicating small self-contained helpers (see hand_layer.c's original
// comment on why it duplicated BAYER4 rather than sharing it) -- this
// header just replaces manual copy-paste duplication with one real source
// of truth, without a new .c file or any build-system changes.
//
// IMPORTANT: any .c file that includes this AND already has its own
// `BAYER4` (background_layer.c did, for cloud/phase-color dithering) must
// remove its own copy -- two `static const BAYER4` definitions in one
// translation unit is a redefinition error, not a silent duplicate. The
// values are identical either way, so nothing about the existing dithering
// elsewhere in that file changes.
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
static int32_t round_div(int32_t num, int32_t denom) {
  if (denom == 0) return 0;
  if (num >= 0) return (num + denom / 2) / denom;
  return -((-num + denom / 2) / denom);
}

// 4x4 ordered-dither matrix + ~50% threshold, shared by every dithered
// fill/stroke below.
static const uint8_t BAYER4[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
};

// Fixed-point convex polygon inclusion test using 64-bit cross-product
static bool point_in_convex_polygon_fp(const FGPoint *pts, int n, FGPoint p) {
  bool has_pos = false, has_neg = false;
  for (int i = 0; i < n; i++) {
    FGPoint a = pts[i];
    FGPoint b = pts[(i + 1) % n];
    int64_t cross = (int64_t)(b.x - a.x) * (p.y - a.y) - (int64_t)(b.y - a.y) * (p.x - a.x);
    if (cross > 0) has_pos = true;
    if (cross < 0) has_neg = true;
    if (has_pos && has_neg) return false;
  }
  return true;
}

// Sub-pixel solid polygon fill with scanline optimization
static void fill_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color) {
  int32_t min_x_fp = pts[0].x, max_x_fp = pts[0].x;
  int32_t min_y_fp = pts[0].y, max_y_fp = pts[0].y;
  for (int i = 1; i < n; i++) {
    if (pts[i].x < min_x_fp) min_x_fp = pts[i].x;
    if (pts[i].x > max_x_fp) max_x_fp = pts[i].x;
    if (pts[i].y < min_y_fp) min_y_fp = pts[i].y;
    if (pts[i].y > max_y_fp) max_y_fp = pts[i].y;
  }

  int16_t min_x = (int16_t)(min_x_fp >> SUBPIXEL_BITS);
  int16_t max_x = (int16_t)((max_x_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);
  int16_t min_y = (int16_t)(min_y_fp >> SUBPIXEL_BITS);
  int16_t max_y = (int16_t)((max_y_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);

  graphics_context_set_fill_color(ctx, color);

  for (int16_t y = min_y; y <= max_y; y++) {
    int16_t span_start = -1;
    int16_t span_len = 0;
    int32_t sample_y = ((int32_t)y << SUBPIXEL_BITS) + SUBPIXEL_HALF;

    for (int16_t x = min_x; x <= max_x; x++) {
      int32_t sample_x = ((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF;
      if (point_in_convex_polygon_fp(pts, n, fgpoint_new(sample_x, sample_y))) {
        if (span_start == -1) {
          span_start = x;
          span_len = 1;
        } else {
          span_len++;
        }
      } else {
        if (span_start != -1) {
          graphics_fill_rect(ctx, GRect(span_start, y, span_len, 1), 0, GCornerNone);
          span_start = -1;
          span_len = 0;
        }
      }
    }
    if (span_start != -1) {
      graphics_fill_rect(ctx, GRect(span_start, y, span_len, 1), 0, GCornerNone);
    }
  }
}

// Sub-pixel Bayer-dithered polygon fill
static void fill_polygon_dithered_fp(GContext *ctx, const FGPoint *pts, int n, GColor color) {
  int32_t min_x_fp = pts[0].x, max_x_fp = pts[0].x;
  int32_t min_y_fp = pts[0].y, max_y_fp = pts[0].y;
  for (int i = 1; i < n; i++) {
    if (pts[i].x < min_x_fp) min_x_fp = pts[i].x;
    if (pts[i].x > max_x_fp) max_x_fp = pts[i].x;
    if (pts[i].y < min_y_fp) min_y_fp = pts[i].y;
    if (pts[i].y > max_y_fp) max_y_fp = pts[i].y;
  }

  int16_t min_x = (int16_t)(min_x_fp >> SUBPIXEL_BITS);
  int16_t max_x = (int16_t)((max_x_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);
  int16_t min_y = (int16_t)(min_y_fp >> SUBPIXEL_BITS);
  int16_t max_y = (int16_t)((max_y_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);

  graphics_context_set_fill_color(ctx, color);

  for (int16_t y = min_y; y <= max_y; y++) {
    int32_t sample_y = ((int32_t)y << SUBPIXEL_BITS) + SUBPIXEL_HALF;
    for (int16_t x = min_x; x <= max_x; x++) {
      if (BAYER4[y & 3][x & 3] >= 8) continue;
      int32_t sample_x = ((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF;
      if (point_in_convex_polygon_fp(pts, n, fgpoint_new(sample_x, sample_y))) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

// Anti-aliased fills for thin shapes (nominal width under ~3px) --
// point_in_convex_polygon_fp()/the circle math above sample exactly
// ONE point per candidate pixel (its center), so whether a given
// pixel along a narrow shape gets drawn at all comes down to whether
// the shape's true edge happens to fall on the near or far side of
// that single sample point -- not how much of the pixel it actually
// covers. For a wide shape that's invisible (plenty of fully-covered
// interior pixels either way); for a thin one it's the difference
// between a clean edge and a line that looks thinner than its nominal
// width, or even develops gaps, at certain sub-pixel angles. This is
// NOT a color/bit-casting issue -- nothing in this file blends colors
// or reads back existing pixels; every pixel is either painted solid
// or left untouched, everywhere, including here. It's a sampling
// resolution problem, and the fix is the same "fake alpha via ordered
// dithering" principle fill_polygon_dithered_fp()'s flat 50% stipple
// above already uses for translucency -- just driven by each pixel's
// actual measured coverage instead of a fixed density.
//
// polygon_coverage9_fp()/circle_coverage9_fp() supersample 3x3 (9
// sub-points spaced a third of a pixel apart) around the usual pixel-
// center sample, giving a coverage count of 0-9 instead of a binary
// hit/miss. fill_polygon_thin_fp()/fill_circle_thin_fp() below then
// draw fully-covered pixels solid and skip fully-empty ones exactly
// like the plain fill functions do, but dither everything in between
// at a density proportional to its own coverage (scaled onto BAYER4's
// 0-15 range) rather than either drawing or skipping it outright --
// that graduated density is what actually reads as a smoothly
// anti-aliased edge despite this display having no real alpha
// blending to fall back on.
//
// Reserved for genuinely thin shapes rather than replacing the plain
// fills everywhere: 9 point-in-shape tests per candidate pixel is
// meaningfully more expensive than 1, and a thin shape's own bounding
// box stays small (proportional to its width, however long it runs),
// which is exactly what keeps that extra cost bounded to shapes that
// were cheap to rasterize in the first place.
static const int32_t SUBPIXEL_AA_OFFSETS3[3] = { -SUBPIXEL_SCALE / 3, 0, SUBPIXEL_SCALE / 3 };

static uint8_t polygon_coverage9_fp(const FGPoint *pts, int n, int32_t center_x, int32_t center_y) {
  uint8_t hits = 0;
  for (int oy = 0; oy < 3; oy++) {
    for (int ox = 0; ox < 3; ox++) {
      FGPoint sample = fgpoint_new(center_x + SUBPIXEL_AA_OFFSETS3[ox], center_y + SUBPIXEL_AA_OFFSETS3[oy]);
      if (point_in_convex_polygon_fp(pts, n, sample)) hits++;
    }
  }
  return hits;
}

static uint8_t circle_coverage9_fp(int32_t center_x, int32_t center_y, int32_t cx, int32_t cy, int64_t r_sq) {
  uint8_t hits = 0;
  for (int oy = 0; oy < 3; oy++) {
    for (int ox = 0; ox < 3; ox++) {
      int64_t dx = (center_x + SUBPIXEL_AA_OFFSETS3[ox]) - cx;
      int64_t dy = (center_y + SUBPIXEL_AA_OFFSETS3[oy]) - cy;
      if (dx * dx + dy * dy <= r_sq) hits++;
    }
  }
  return hits;
}

// coverage (0-9) -> a BAYER4-comparable threshold (0-16): a pixel
// draws when the ordered-dither value at its position is below this,
// same comparison shape the flat-50%-translucency dithered fills use
// (there, the threshold is always a fixed 8).
static inline uint8_t coverage9_to_bayer_threshold(uint8_t coverage) {
  return (uint8_t)(((uint16_t)coverage * 16) / 9);
}

static void fill_polygon_thin_fp(GContext *ctx, const FGPoint *pts, int n, GColor color) {
  int32_t min_x_fp = pts[0].x, max_x_fp = pts[0].x;
  int32_t min_y_fp = pts[0].y, max_y_fp = pts[0].y;
  for (int i = 1; i < n; i++) {
    if (pts[i].x < min_x_fp) min_x_fp = pts[i].x;
    if (pts[i].x > max_x_fp) max_x_fp = pts[i].x;
    if (pts[i].y < min_y_fp) min_y_fp = pts[i].y;
    if (pts[i].y > max_y_fp) max_y_fp = pts[i].y;
  }

  int16_t min_x = (int16_t)(min_x_fp >> SUBPIXEL_BITS);
  int16_t max_x = (int16_t)((max_x_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);
  int16_t min_y = (int16_t)(min_y_fp >> SUBPIXEL_BITS);
  int16_t max_y = (int16_t)((max_y_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);

  graphics_context_set_fill_color(ctx, color);

  for (int16_t y = min_y; y <= max_y; y++) {
    int32_t sample_y = ((int32_t)y << SUBPIXEL_BITS) + SUBPIXEL_HALF;
    for (int16_t x = min_x; x <= max_x; x++) {
      int32_t sample_x = ((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF;
      uint8_t coverage = polygon_coverage9_fp(pts, n, sample_x, sample_y);
      if (coverage == 0) continue;
      if (coverage == 9 || BAYER4[y & 3][x & 3] < coverage9_to_bayer_threshold(coverage)) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

static void fill_circle_thin_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color) {
  int16_t min_x = (int16_t)((center.x - radius_fp) >> SUBPIXEL_BITS);
  int16_t max_x = (int16_t)((center.x + radius_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);
  int16_t min_y = (int16_t)((center.y - radius_fp) >> SUBPIXEL_BITS);
  int16_t max_y = (int16_t)((center.y + radius_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);

  int64_t r_sq = (int64_t)radius_fp * radius_fp;
  graphics_context_set_fill_color(ctx, color);

  for (int16_t y = min_y; y <= max_y; y++) {
    int32_t sample_y = ((int32_t)y << SUBPIXEL_BITS) + SUBPIXEL_HALF;
    for (int16_t x = min_x; x <= max_x; x++) {
      int32_t sample_x = ((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF;
      uint8_t coverage = circle_coverage9_fp(sample_x, sample_y, center.x, center.y, r_sq);
      if (coverage == 0) continue;
      if (coverage == 9 || BAYER4[y & 3][x & 3] < coverage9_to_bayer_threshold(coverage)) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

// Sub-pixel circle fill (solid & dithered)
static void fill_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered) {
  int16_t min_x = (int16_t)((center.x - radius_fp) >> SUBPIXEL_BITS);
  int16_t max_x = (int16_t)((center.x + radius_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);
  int16_t min_y = (int16_t)((center.y - radius_fp) >> SUBPIXEL_BITS);
  int16_t max_y = (int16_t)((center.y + radius_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);

  int64_t r_sq = (int64_t)radius_fp * radius_fp;
  graphics_context_set_fill_color(ctx, color);

  for (int16_t y = min_y; y <= max_y; y++) {
    int16_t span_start = -1;
    int16_t span_len = 0;
    int64_t dy = (((int32_t)y << SUBPIXEL_BITS) + SUBPIXEL_HALF) - center.y;
    int64_t dy_sq = dy * dy;

    for (int16_t x = min_x; x <= max_x; x++) {
      if (dithered && BAYER4[y & 3][x & 3] >= 8) {
        if (span_start != -1) {
          graphics_fill_rect(ctx, GRect(span_start, y, span_len, 1), 0, GCornerNone);
          span_start = -1;
          span_len = 0;
        }
        continue;
      }

      int64_t dx = (((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF) - center.x;
      if (dx * dx + dy_sq <= r_sq) {
        if (dithered) {
          graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
        } else {
          if (span_start == -1) {
            span_start = x;
            span_len = 1;
          } else {
            span_len++;
          }
        }
      } else if (!dithered && span_start != -1) {
        graphics_fill_rect(ctx, GRect(span_start, y, span_len, 1), 0, GCornerNone);
        span_start = -1;
        span_len = 0;
      }
    }
    if (!dithered && span_start != -1) {
      graphics_fill_rect(ctx, GRect(span_start, y, span_len, 1), 0, GCornerNone);
    }
  }
}

// Sub-pixel line DDA for precise outer contours -- a genuine 1px stroke,
// used both directly (hollow shapes) and as the building block for
// stroke_polygon_fp()/stroke_circle_fp() below.
//
// This used to floor `cur_x`/`cur_y` straight to a pixel index (plain
// `>> SUBPIXEL_BITS`, no rounding) and pick the step count by flooring
// the run length too (`max(|dx|,|dy|) >> SUBPIXEL_BITS`). Both were bugs:
//
// - Flooring the per-step pixel is a DIFFERENT rounding convention than
//   every fill function above, which samples at the pixel CENTER (adds
//   SUBPIXEL_HALF before testing). A stroke traced along the exact same
//   geometric edge as a fill therefore landed up to half a pixel away
//   from it -- "the outline looks like it's half a pixel wide/offset".
// - Flooring the step count means the per-step increment along the
//   dominant axis could run up to just-under-2 whole pixels (whenever
//   the run length wasn't an exact multiple of SUBPIXEL_SCALE, which is
//   almost always) instead of never exceeding 1 -- a DDA that advances
//   more than 1px per step along its dominant axis can hop clean over
//   an intermediate pixel and leave a gap. That's "missing parts of the
//   line" -- not an antialiasing/color-precision issue (nothing here
//   blends colors at all; every pixel is either painted solid or left
//   untouched, same point-sampling approach the fill functions use --
//   there's no coverage value to round into the 64-color palette more
//   finely in the first place), just an under-sampled walk along the
//   line's own length.
//
// Fixed by rounding to nearest (fp_round_to_px(), matching the fills)
// and by using a ceiling step count (steps * SUBPIXEL_SCALE always >=
// the actual run length, so the per-step advance is always <= 1px).
static void stroke_line_fp(GContext *ctx, FGPoint a, FGPoint b, GColor color, bool dithered) {
  int32_t dx = b.x - a.x;
  int32_t dy = b.y - a.y;
  int32_t max_len = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  int32_t steps = (max_len + SUBPIXEL_MASK) >> SUBPIXEL_BITS; // ceiling, not floor
  if (steps == 0) steps = 1;

  int32_t x_inc = dx / steps;
  int32_t y_inc = dy / steps;
  int32_t cur_x = a.x;
  int32_t cur_y = a.y;

  graphics_context_set_fill_color(ctx, color);
  int16_t last_px = -32768, last_py = -32768;

  for (int i = 0; i <= steps; i++) {
    int16_t px = fp_round_to_px(cur_x);
    int16_t py = fp_round_to_px(cur_y);

    if (px != last_px || py != last_py) {
      if (!dithered || BAYER4[py & 3][px & 3] < 8) {
        graphics_fill_rect(ctx, GRect(px, py, 1, 1), 0, GCornerNone);
      }
      last_px = px;
      last_py = py;
    }
    cur_x += x_inc;
    cur_y += y_inc;
  }
}

// Same DDA walk as stroke_line_fp() above, but only plots every other
// distinct pixel (a dotted line) and stops early once *budget_px hits
// 0 -- decremented once per distinct pixel, drawn or not, so a
// caller can budget a whole multi-segment path's reveal length across
// several calls to this function and have it stop exactly where the
// budget runs out, mid-segment if need be. Used for "Paths"
// (shake_anim_mode 5)'s own extend/contract animation -- see
// draw_body_paths_overlay() in background_layer.c.
static bool stroke_line_dotted_budget_fp(GContext *ctx, FGPoint a, FGPoint b, GColor color, int32_t *budget_px) {
  if (*budget_px <= 0) return false;

  int32_t dx = b.x - a.x;
  int32_t dy = b.y - a.y;
  int32_t max_len = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  int32_t steps = (max_len + SUBPIXEL_MASK) >> SUBPIXEL_BITS;
  if (steps == 0) steps = 1;

  int32_t x_inc = dx / steps;
  int32_t y_inc = dy / steps;
  int32_t cur_x = a.x;
  int32_t cur_y = a.y;

  graphics_context_set_fill_color(ctx, color);
  int16_t last_px = -32768, last_py = -32768;
  bool dot_on = true; // alternates per distinct pixel -- "every second pixel" per the request

  for (int i = 0; i <= steps; i++) {
    int16_t px = fp_round_to_px(cur_x);
    int16_t py = fp_round_to_px(cur_y);

    if (px != last_px || py != last_py) {
      if (dot_on) {
        graphics_fill_rect(ctx, GRect(px, py, 1, 1), 0, GCornerNone);
      }
      dot_on = !dot_on;
      last_px = px;
      last_py = py;
      (*budget_px)--;
      if (*budget_px <= 0) return true; // budget exhausted mid-segment -- caller stops walking further segments too
    }
    cur_x += x_inc;
    cur_y += y_inc;
  }
  return true;
}

// A ~50%-Bayer-dithered stroke along the polygon's actual perimeter --
// each consecutive pair of points (including the wrap from the last
// point back to the first), walked pixel by pixel (Bresenham-ish) with
// the same dither test fill_polygon_dithered_fp() uses. This is what
// makes a translucent shape's outline genuinely different from just
// drawing 4 shifted copies of the (already dithered) fill: tracing the
// real boundary gives a clean, consistent-width dithered ring, where the
// shifted-copy technique would just smear the fill's own dither pattern
// into a slightly larger, blurrier blob -- there's no actual "outline"
// in that result, just a bigger dithered fill.
static void stroke_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color, bool dithered) {
  for (int i = 0; i < n; i++) {
    stroke_line_fp(ctx, pts[i], pts[(i + 1) % n], color, dithered);
  }
}

// Same idea as stroke_polygon_fp(), for a round cap or dot -- a
// midpoint-circle walk of just the boundary pixels, each checked against
// the dither test rather than filling the whole disc. center/radius are
// rounded to whole pixels once up front (same fp_round_to_px() convention
// as everything else here) rather than per-pixel, since a Bresenham
// circle walk is inherently integer-pixel-based.
static void stroke_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered) {
  int16_t r_px = fp_round_to_px(radius_fp);
  if (r_px < 1) r_px = 1;

  graphics_context_set_fill_color(ctx, color);
  int16_t x = r_px, y = 0, err = 0;
  int16_t cx = fp_round_to_px(center.x);
  int16_t cy = fp_round_to_px(center.y);

  while (x >= y) {
    GPoint pts[8] = {
      GPoint(cx + x, cy + y), GPoint(cx + y, cy + x),
      GPoint(cx - y, cy + x), GPoint(cx - x, cy + y),
      GPoint(cx - x, cy - y), GPoint(cx - y, cy - x),
      GPoint(cx + y, cy - x), GPoint(cx + x, cy - y),
    };
    for (int i = 0; i < 8; i++) {
      if (!dithered || BAYER4[pts[i].y & 3][pts[i].x & 3] < 8) {
        graphics_fill_rect(ctx, GRect(pts[i].x, pts[i].y, 1, 1), 0, GCornerNone);
      }
    }
    y++;
    if (err <= 0) err += 2 * y + 1;
    if (err > 0) { x--; err -= 2 * x + 1; }
  }
}

// ---- gradient variants for the "on shake" outline animation ---------
// Same rasterizers as stroke_line_fp/stroke_polygon_fp/stroke_circle_fp
// above, but instead of one fixed color for the whole stroke, each
// individual pixel's color is looked up from RAINBOW_OUTLINE_LUT based
// on that pixel's own x coordinate -- a genuine gradient across the
// SCREEN, not just a color that changes over time/by item. `shift`
// (updated once per animation frame, not per pixel -- see
// pebble-eclipse-watch.c's shake_anim_timer_callback()) is the entire
// "animation": it just moves where in the table screen_x=0 starts
// reading from, so the strip appears to scroll without recomputing any
// actual color values.
//
// Duplicated here (same small table, same lookup) rather than sharing
// pebble-eclipse-watch.c's copy -- this header is included by both
// hand_layer.c and background_layer.c and doesn't otherwise depend on
// eclipse_data.h/pebble-eclipse-watch.c's own declarations, and this
// project already prefers a small duplicated helper over threading a
// cross-file dependency through a shared low-level header for
// something this self-contained (see this file's own top comment).
#define RAINBOW_LUT_SIZE 24
static const uint8_t RAINBOW_OUTLINE_LUT[RAINBOW_LUT_SIZE] = {
  0xF0, 0xF4, 0xF4, 0xF8, 0xFC, 0xEC, 0xEC, 0xDC, 0xCC, 0xCD, 0xCD, 0xCE,
  0xCF, 0xCB, 0xCB, 0xC7, 0xC3, 0xD3, 0xD3, 0xE3, 0xF3, 0xF2, 0xF2, 0xF1
};

static GColor rainbow_color_at_fp(int16_t screen_x, uint8_t shift) {
  int32_t idx = (screen_x / 3 + shift) % RAINBOW_LUT_SIZE;
  if (idx < 0) idx += RAINBOW_LUT_SIZE;
  GColor c;
  c.argb = RAINBOW_OUTLINE_LUT[idx];
  return c;
}

static void stroke_line_gradient_fp(GContext *ctx, FGPoint a, FGPoint b, uint8_t shift) {
  int32_t dx = b.x - a.x;
  int32_t dy = b.y - a.y;
  int32_t max_len = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  int32_t steps = (max_len + SUBPIXEL_MASK) >> SUBPIXEL_BITS;
  if (steps == 0) steps = 1;

  int32_t x_inc = dx / steps;
  int32_t y_inc = dy / steps;
  int32_t cur_x = a.x;
  int32_t cur_y = a.y;

  int16_t last_px = -32768, last_py = -32768;

  for (int i = 0; i <= steps; i++) {
    int16_t px = fp_round_to_px(cur_x);
    int16_t py = fp_round_to_px(cur_y);

    if (px != last_px || py != last_py) {
      graphics_context_set_fill_color(ctx, rainbow_color_at_fp(px, shift));
      graphics_fill_rect(ctx, GRect(px, py, 1, 1), 0, GCornerNone);
      last_px = px;
      last_py = py;
    }
    cur_x += x_inc;
    cur_y += y_inc;
  }
}

static void stroke_polygon_gradient_fp(GContext *ctx, const FGPoint *pts, int n, uint8_t shift) {
  for (int i = 0; i < n; i++) {
    stroke_line_gradient_fp(ctx, pts[i], pts[(i + 1) % n], shift);
  }
}

static void stroke_circle_gradient_fp(GContext *ctx, FGPoint center, int32_t radius_fp, uint8_t shift) {
  int16_t r_px = fp_round_to_px(radius_fp);
  if (r_px < 1) r_px = 1;

  int16_t x = r_px, y = 0, err = 0;
  int16_t cx = fp_round_to_px(center.x);
  int16_t cy = fp_round_to_px(center.y);

  while (x >= y) {
    GPoint pts[8] = {
      GPoint(cx + x, cy + y), GPoint(cx + y, cy + x),
      GPoint(cx - y, cy + x), GPoint(cx - x, cy + y),
      GPoint(cx - x, cy - y), GPoint(cx - y, cy - x),
      GPoint(cx + y, cy - x), GPoint(cx + x, cy - y),
    };
    for (int i = 0; i < 8; i++) {
      graphics_context_set_fill_color(ctx, rainbow_color_at_fp(pts[i].x, shift));
      graphics_fill_rect(ctx, GRect(pts[i].x, pts[i].y, 1, 1), 0, GCornerNone);
    }
    y++;
    if (err <= 0) err += 2 * y + 1;
    if (err > 0) { x--; err -= 2 * x + 1; }
  }
}
