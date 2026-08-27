#include "hand_layer.h"

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

// Same 4x4 ordered-dither matrix and ~50% threshold pebble-eclipse-watch.c's
// fill_polygon_dithered() uses -- copied rather than shared so this file
// stays self-contained (see the design note in hand_layer.h).
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

// A genuine 1px stroke of the shape's own outline -- distinct from the
// shifted-copy "outline_enabled" underlay in hand_layer_draw() below,
// this is what HandConfig.hollow actually draws instead of a fill.
// Sub-pixel line DDA for precise outer contours
static void stroke_line_fp(GContext *ctx, FGPoint a, FGPoint b, GColor color, bool dithered) {
  int32_t dx = b.x - a.x;
  int32_t dy = b.y - a.y;
  int32_t steps = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) >> SUBPIXEL_BITS;
  if (steps == 0) steps = 1;

  int32_t x_inc = dx / steps;
  int32_t y_inc = dy / steps;
  int32_t cur_x = a.x;
  int32_t cur_y = a.y;

  graphics_context_set_fill_color(ctx, color);
  int16_t last_px = -32768, last_py = -32768;

  for (int i = 0; i <= steps; i++) {
    int16_t px = (int16_t)(cur_x >> SUBPIXEL_BITS);
    int16_t py = (int16_t)(cur_y >> SUBPIXEL_BITS);

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
// A ~50%-Bayer-dithered stroke along the polygon's actual perimeter --
// each consecutive pair of points (including the wrap from the last
// point back to the first), walked pixel by pixel (Bresenham) with the
// same dither test fill_polygon_dithered() uses. This is what makes a
// translucent hand's outline_enabled pass genuinely different from
// just drawing 4 shifted copies of the (already dithered) fill: tracing
// the real boundary gives a clean, consistent-width dithered ring,
// where the shifted-copy technique would just smear the fill's own
// dither pattern into a slightly larger, blurrier blob -- there's no
// actual "outline" in that result, just a bigger dithered fill.
static void stroke_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color, bool dithered) {
  for (int i = 0; i < n; i++) {
    stroke_line_fp(ctx, pts[i], pts[(i + 1) % n], color, dithered);
  }
}

// Same idea as stroke_polygon_dithered(), for the dot style's two
// round caps -- a midpoint-circle walk of just the boundary pixels,
// each checked against the dither test rather than filling the whole
// disc.
static void stroke_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered) {
  int16_t r_px = (int16_t)(radius_fp >> SUBPIXEL_BITS);
  if (r_px < 1) r_px = 1;

  graphics_context_set_fill_color(ctx, color);
  int16_t x = r_px, y = 0, err = 0;
  int16_t cx = (int16_t)(center.x >> SUBPIXEL_BITS);
  int16_t cy = (int16_t)(center.y >> SUBPIXEL_BITS);

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

static GColor resolve_scheme_color(uint8_t choice, GColor main_color, GColor accent_color, GColor bg_color) {
  if (choice == 1) return accent_color;
  if (choice == 2) return bg_color;
  return main_color; // choice 0, and any unrecognized value
}

// Computes the hand shape's actual boundary points (triangle: 3, dot/
// square: 4) plus, for the dot style, the two round-cap centers and
// their shared radius -- shared by the fill/hollow pass
// (draw_hand_shape_once) and the outline pass (draw_hand_outline_once)
// below, so both always agree on exactly where the shape's edge is.
// Returns the point count (3 or 4).
// Computes hand geometry using sub-pixel fixed-point precision
static int compute_hand_geometry_fp(FGPoint center, int32_t angle, const HandConfig *cfg,
                                     FGPoint *points, FGPoint *inner_out, FGPoint *outer_out, int32_t *half_w_out) {
  int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

  int32_t len_fp  = (int32_t)cfg->length << SUBPIXEL_BITS;
  int32_t back_fp = (int32_t)cfg->back_offset << SUBPIXEL_BITS;
  int32_t half_w_fp = ((int32_t)cfg->width << SUBPIXEL_BITS) / 2;
  if (half_w_fp < (1 << (SUBPIXEL_BITS - 1))) {
    half_w_fp = 1 << (SUBPIXEL_BITS - 1); // 0.5px minimum
  }

  int32_t dx_outer = (int32_t)(((int64_t)len_fp * sin_v) / TRIG_MAX_RATIO);
  int32_t dy_outer = (int32_t)(((int64_t)len_fp * cos_v) / TRIG_MAX_RATIO);
  int32_t dx_back  = (int32_t)(((int64_t)back_fp * sin_v) / TRIG_MAX_RATIO);
  int32_t dy_back  = (int32_t)(((int64_t)back_fp * cos_v) / TRIG_MAX_RATIO);

  FGPoint outer = fgpoint_new(center.x + dx_outer, center.y - dy_outer);
  FGPoint inner = fgpoint_new(center.x - dx_back,  center.y + dy_back);

  *inner_out = inner;
  *outer_out = outer;
  *half_w_out = half_w_fp;

  int32_t dx_w = (int32_t)(((int64_t)half_w_fp * cos_v) / TRIG_MAX_RATIO);
  int32_t dy_w = (int32_t)(((int64_t)half_w_fp * sin_v) / TRIG_MAX_RATIO);

  if (cfg->style == 1) { // triangle
    points[0] = fgpoint_new(inner.x - dx_w, inner.y - dy_w);
    points[1] = fgpoint_new(inner.x + dx_w, inner.y + dy_w);
    points[2] = outer;
    return 3;
  }

  // dot / square body
  points[0] = fgpoint_new(inner.x - dx_w, inner.y - dy_w);
  points[1] = fgpoint_new(inner.x + dx_w, inner.y + dy_w);
  points[2] = fgpoint_new(outer.x + dx_w, outer.y + dy_w);
  points[3] = fgpoint_new(outer.x - dx_w, outer.y - dy_w);
  return 4;
}
// Draws just the hand's shape (no outline underlay) in `color`, at
// `center`. `dithered` selects a genuine ~50% stipple fill (see
// fill_polygon_dithered() above) instead of a solid one -- this is
// what HandConfig.translucent actually means now, not the 1px
// stroke-only look an earlier version of this file used.
static void draw_hand_shape_once_fp(GContext *ctx, FGPoint center, int32_t angle, const HandConfig *cfg,
                                     GColor color, bool dithered) {
  FGPoint points[4], inner, outer;
  int32_t half_w;
  int n = compute_hand_geometry_fp(center, angle, cfg, points, &inner, &outer, &half_w);

  if (dithered) fill_polygon_dithered_fp(ctx, points, n, color);
  else if (cfg->hollow) stroke_polygon_fp(ctx, points, n, color, false);
  else fill_polygon_fp(ctx, points, n, color);

  if (n == 4 && cfg->style == 0) { // dot style round caps
    if (cfg->hollow && !dithered) {
      stroke_circle_fp(ctx, inner, half_w, color, false);
      stroke_circle_fp(ctx, outer, half_w, color, false);
    } else {
      fill_circle_fp(ctx, inner, half_w, color, dithered);
      fill_circle_fp(ctx, outer, half_w, color, dithered);
    }
  }
}
// A genuine perimeter outline for hand_layer_draw()'s outline_enabled
// pass -- native gpath_draw_outline()/graphics_draw_circle() (a real,
// clean 1px boundary line) when opaque, the matching dithered stroke
// functions above when the hand is translucent. Replaces the earlier
// "draw 4 shifted copies of the filled shape" technique, which isn't
// actually an outline at all -- for an opaque hand it merely
// approximates one by blurring the silhouette outward, and for a
// translucent hand it doesn't work at all: dithering 4 offset copies
// of an already-dithered fill just smears the same stipple pattern
// into a slightly bigger blob, not a clean ring around the shape.
static void draw_hand_outline_once_fp(GContext *ctx, FGPoint center, int32_t angle, const HandConfig *cfg,
                                       GColor color, bool dithered) {
  FGPoint points[4], inner, outer;
  int32_t half_w;
  int n = compute_hand_geometry_fp(center, angle, cfg, points, &inner, &outer, &half_w);

  stroke_polygon_fp(ctx, points, n, color, dithered);

  if (n == 4 && cfg->style == 0) {
    stroke_circle_fp(ctx, inner, half_w, color, dithered);
    stroke_circle_fp(ctx, outer, half_w, color, dithered);
  }
}

void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color) {
  FGPoint center_fp = fgpoint_from_gpoint(center);

  if (cfg->outline_enabled) {
    // A real perimeter trace now (see draw_hand_outline_once above),
    // dithered too when the hand is translucent, so the outline
    // doesn't look more solid than the fill it's outlining.
    GColor outline_color = resolve_scheme_color(cfg->outline_color, main_color, accent_color, bg_color);
    draw_hand_outline_once_fp(ctx, center_fp, angle, cfg, outline_color, cfg->translucent);
  }

  if (cfg->color != 3) { // 3 = "none" -- skip the fill, outline (if any) still drew above
    GColor hand_color = resolve_scheme_color(cfg->color, main_color, accent_color, bg_color);
    draw_hand_shape_once_fp(ctx, center_fp, angle, cfg, hand_color, cfg->translucent);
  }
}

void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color) {
  if (radius == 0) return;
  GColor color = resolve_scheme_color(color_choice, main_color, accent_color, bg_color);
  FGPoint center_fp = fgpoint_from_gpoint(center);
  fill_circle_fp(ctx, center_fp, (int32_t)radius << SUBPIXEL_BITS, color, false);
}
