#include "subpixel.h"

// See subpixel.h's own comment (item F in the size-reduction pass) for why
// these bodies live here, in one shared .c file, instead of as `static`
// functions duplicated into every header-includer's own translation unit.

// ---- shared tables -----------------------------------------------------

const uint8_t BAYER4[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
};

// Only used internally (coverage9 sampling below), so this one stays
// file-static rather than extern -- nothing outside subpixel.c needs it.
static const int32_t SUBPIXEL_AA_OFFSETS3[3] = { -SUBPIXEL_SCALE / 3, 0, SUBPIXEL_SCALE / 3 };

// ---- small shared helpers -----------------------------------------------

int32_t round_div(int32_t num, int32_t denom) {
  if (denom == 0) return 0;
  if (num >= 0) return (num + denom / 2) / denom;
  return -((-num + denom / 2) / denom);
}

uint32_t isqrt64_fp(int64_t v) {
  if (v <= 0) return 0;
  uint64_t x = (uint64_t)v;
  uint64_t res = 0;
  uint64_t bit = (uint64_t)1 << 62; // highest even power of 4 <= any 64-bit value
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= res + bit) {
      x -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return (uint32_t)res;
}

bool point_in_convex_polygon_fp(const FGPoint *pts, int n, FGPoint p) {
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

// ---- plain fills ---------------------------------------------------------

void fill_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color) {
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

void fill_polygon_dithered_fp(GContext *ctx, const FGPoint *pts, int n, GColor color) {
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

void fill_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered) {
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

// ---- anti-aliased ("thin") fills -- internal coverage helpers, then the fills --

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

static inline uint8_t coverage9_to_bayer_threshold(uint8_t coverage) {
  return (uint8_t)(((uint16_t)coverage * 16) / 9);
}

void fill_polygon_thin_fp(GContext *ctx, const FGPoint *pts, int n, GColor color) {
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

void fill_circle_thin_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color) {
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

// ---- strokes ---------------------------------------------------------

void stroke_line_fp(GContext *ctx, FGPoint a, FGPoint b, GColor color, bool dithered) {
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

void stroke_polygon_fp(GContext *ctx, const FGPoint *pts, int n, GColor color, bool dithered) {
  for (int i = 0; i < n; i++) {
    stroke_line_fp(ctx, pts[i], pts[(i + 1) % n], color, dithered);
  }
}

void stroke_circle_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, bool dithered) {
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

// ---- inline ("hollow thickness") ring fills -----------------------------

bool inset_convex_polygon_fp(const FGPoint *pts, int n, int32_t d_fp, FGPoint *out_pts) {
  if (d_fp <= 0 || n < 3 || n > SUBPIXEL_MAX_RING_PTS) return false;

  FGPoint centroid = fgpoint_new(0, 0);
  for (int i = 0; i < n; i++) { centroid.x += pts[i].x; centroid.y += pts[i].y; }
  centroid.x /= n; centroid.y /= n;

  FGPoint offset_a[SUBPIXEL_MAX_RING_PTS]; // each edge's own offset start point
  int32_t dir_x[SUBPIXEL_MAX_RING_PTS], dir_y[SUBPIXEL_MAX_RING_PTS]; // and direction (B - A)

  for (int i = 0; i < n; i++) {
    FGPoint a = pts[i], b = pts[(i + 1) % n];
    int32_t ex = b.x - a.x, ey = b.y - a.y;
    int64_t len_sq = (int64_t)ex * ex + (int64_t)ey * ey;
    if (len_sq == 0) return false; // degenerate (coincident) edge -- bail to a solid fallback
    int32_t elen = (int32_t)isqrt64_fp(len_sq);

    // two candidate perpendiculars; pick whichever points toward the centroid
    int32_t nx = -ey, ny = ex;
    int32_t mx = a.x + ex / 2, my = a.y + ey / 2; // edge midpoint
    int64_t dot = (int64_t)(centroid.x - mx) * nx + (int64_t)(centroid.y - my) * ny;
    if (dot < 0) { nx = -nx; ny = -ny; }

    int32_t off_x = round_div(nx * d_fp, elen);
    int32_t off_y = round_div(ny * d_fp, elen);
    offset_a[i] = fgpoint_new(a.x + off_x, a.y + off_y);
    dir_x[i] = ex; dir_y[i] = ey;
  }

  for (int i = 0; i < n; i++) {
    int prev = (i - 1 + n) % n;
    int64_t ex = (int64_t)offset_a[i].x - offset_a[prev].x;
    int64_t ey = (int64_t)offset_a[i].y - offset_a[prev].y;
    // line(prev): offset_a[prev] + t*dir[prev]; line(i): offset_a[i] + s*dir[i]
    int64_t det = (int64_t)dir_x[i] * dir_y[prev] - (int64_t)dir_x[prev] * dir_y[i];
    if (det == 0) {
      out_pts[i] = offset_a[i]; // parallel edges -- fall back to the offset edge's own start point
      continue;
    }
    int64_t t_num = (int64_t)dir_x[i] * ey - (int64_t)dir_y[i] * ex;
    out_pts[i] = fgpoint_new(
      offset_a[prev].x + (int32_t)((t_num * dir_x[prev]) / det),
      offset_a[prev].y + (int32_t)((t_num * dir_y[prev]) / det)
    );
  }

  int64_t area_out = 0, area_in = 0;
  for (int i = 0; i < n; i++) {
    FGPoint a = pts[i], b = pts[(i + 1) % n];
    area_out += (int64_t)a.x * b.y - (int64_t)b.x * a.y;
    FGPoint ia = out_pts[i], ib = out_pts[(i + 1) % n];
    area_in += (int64_t)ia.x * ib.y - (int64_t)ib.x * ia.y;
  }
  if ((area_out > 0) != (area_in > 0)) return false;

  return true;
}

void fill_polygon_ring_fp(GContext *ctx, const FGPoint *pts, int n, int32_t thickness_fp, GColor color, bool dithered) {
  FGPoint inner[SUBPIXEL_MAX_RING_PTS];
  bool have_inner = inset_convex_polygon_fp(pts, n, thickness_fp, inner);

  if (!have_inner) {
    if (dithered) fill_polygon_dithered_fp(ctx, pts, n, color);
    else fill_polygon_fp(ctx, pts, n, color);
    return;
  }

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
      if (dithered && BAYER4[y & 3][x & 3] >= 8) continue;
      int32_t sample_x = ((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF;
      FGPoint sample = fgpoint_new(sample_x, sample_y);
      if (point_in_convex_polygon_fp(pts, n, sample) && !point_in_convex_polygon_fp(inner, n, sample)) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

void fill_circle_ring_fp(GContext *ctx, FGPoint center, int32_t outer_r_fp, int32_t thickness_fp, GColor color, bool dithered) {
  int32_t inner_r_fp = outer_r_fp - thickness_fp;
  if (inner_r_fp <= 0) {
    fill_circle_fp(ctx, center, outer_r_fp, color, dithered);
    return;
  }

  int16_t min_x = (int16_t)((center.x - outer_r_fp) >> SUBPIXEL_BITS);
  int16_t max_x = (int16_t)((center.x + outer_r_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);
  int16_t min_y = (int16_t)((center.y - outer_r_fp) >> SUBPIXEL_BITS);
  int16_t max_y = (int16_t)((center.y + outer_r_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);

  int64_t r_out_sq = (int64_t)outer_r_fp * outer_r_fp;
  int64_t r_in_sq = (int64_t)inner_r_fp * inner_r_fp;
  graphics_context_set_fill_color(ctx, color);

  for (int16_t y = min_y; y <= max_y; y++) {
    int64_t dy = (((int32_t)y << SUBPIXEL_BITS) + SUBPIXEL_HALF) - center.y;
    int64_t dy_sq = dy * dy;
    for (int16_t x = min_x; x <= max_x; x++) {
      if (dithered && BAYER4[y & 3][x & 3] >= 8) continue;
      int64_t dx = (((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF) - center.x;
      int64_t d_sq = dx * dx + dy_sq;
      if (d_sq <= r_out_sq && d_sq >= r_in_sq) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}
