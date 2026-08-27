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

static bool point_in_convex_polygon(GPoint *pts, int n, GPoint p) {
  bool has_pos = false, has_neg = false;
  for (int i = 0; i < n; i++) {
    GPoint a = pts[i];
    GPoint b = pts[(i + 1) % n];
    int32_t cross = (int32_t)(b.x - a.x) * (p.y - a.y) - (int32_t)(b.y - a.y) * (p.x - a.x);
    if (cross > 0) has_pos = true;
    if (cross < 0) has_neg = true;
    if (has_pos && has_neg) return false;
  }
  return true;
}

static void fill_polygon_dithered(GContext *ctx, GPoint *pts, int n, GColor color) {
  int16_t min_x = pts[0].x, max_x = pts[0].x, min_y = pts[0].y, max_y = pts[0].y;
  for (int i = 1; i < n; i++) {
    if (pts[i].x < min_x) min_x = pts[i].x;
    if (pts[i].x > max_x) max_x = pts[i].x;
    if (pts[i].y < min_y) min_y = pts[i].y;
    if (pts[i].y > max_y) max_y = pts[i].y;
  }
  graphics_context_set_fill_color(ctx, color);
  for (int16_t y = min_y; y <= max_y; y++) {
    for (int16_t x = min_x; x <= max_x; x++) {
      if (BAYER4[y & 3][x & 3] >= 8) continue; // ~50% threshold
      if (!point_in_convex_polygon(pts, n, GPoint(x, y))) continue;
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }
}

static void fill_circle_dithered(GContext *ctx, GPoint center, int16_t radius, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  for (int16_t y = center.y - radius; y <= center.y + radius; y++) {
    for (int16_t x = center.x - radius; x <= center.x + radius; x++) {
      if (BAYER4[y & 3][x & 3] >= 8) continue;
      int32_t dx = x - center.x, dy = y - center.y;
      if (dx * dx + dy * dy > (int32_t)radius * radius) continue;
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }
}

static void fill_path(GContext *ctx, GPoint *points, int num_points, GColor color) {
  GPathInfo info = { .num_points = num_points, .points = points };
  GPath *path = gpath_create(&info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// A genuine 1px stroke of the shape's own outline -- distinct from the
// shifted-copy "outline_enabled" underlay in hand_layer_draw() below,
// this is what HandConfig.hollow actually draws instead of a fill.
static void stroke_path(GContext *ctx, GPoint *points, int num_points, GColor color) {
  GPathInfo info = { .num_points = num_points, .points = points };
  GPath *path = gpath_create(&info);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  gpath_draw_outline(ctx, path);
  gpath_destroy(path);
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
static void stroke_polygon_dithered(GContext *ctx, GPoint *points, int n, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  for (int i = 0; i < n; i++) {
    GPoint a = points[i], b = points[(i + 1) % n];
    int32_t dx = (b.x >= a.x) ? (b.x - a.x) : (a.x - b.x);
    int32_t dy = -((b.y >= a.y) ? (b.y - a.y) : (a.y - b.y));
    int32_t sx = (a.x < b.x) ? 1 : -1;
    int32_t sy = (a.y < b.y) ? 1 : -1;
    int32_t err = dx + dy;
    int32_t x = a.x, y = a.y;
    while (true) {
      if (BAYER4[y & 3][x & 3] < 8) { // ~50% threshold, same as fill_polygon_dithered
        graphics_fill_rect(ctx, GRect((int16_t)x, (int16_t)y, 1, 1), 0, GCornerNone);
      }
      if (x == b.x && y == b.y) break;
      int32_t e2 = 2 * err;
      if (e2 >= dy) { err += dy; x += sx; }
      if (e2 <= dx) { err += dx; y += sy; }
    }
  }
}

// Same idea as stroke_polygon_dithered(), for the dot style's two
// round caps -- a midpoint-circle walk of just the boundary pixels,
// each checked against the dither test rather than filling the whole
// disc.
static void stroke_circle_dithered(GContext *ctx, GPoint center, int16_t radius, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int16_t x = radius, y = 0, err = 0;
  while (x >= y) {
    GPoint pts[8] = {
      GPoint(center.x + x, center.y + y), GPoint(center.x + y, center.y + x),
      GPoint(center.x - y, center.y + x), GPoint(center.x - x, center.y + y),
      GPoint(center.x - x, center.y - y), GPoint(center.x - y, center.y - x),
      GPoint(center.x + y, center.y - x), GPoint(center.x + x, center.y - y),
    };
    for (int i = 0; i < 8; i++) {
      if (BAYER4[pts[i].y & 3][pts[i].x & 3] < 8) {
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
static int compute_hand_geometry(GPoint center, int32_t angle, const HandConfig *cfg,
                                  GPoint *points, GPoint *inner_out, GPoint *outer_out, int16_t *half_w_out) {
  int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

  // Outward direction (sin_v, -cos_v) and its perpendicular (cos_v, sin_v),
  // same rotation convention used throughout this project (0 = up,
  // clockwise). back_offset is measured against the outward direction, so
  // subtracting it moves the inner point backward (positive) or forward
  // past center (negative) -- see the header comment on back_offset.
  GPoint outer = GPoint(
    center.x + round_div(cfg->length * sin_v, TRIG_MAX_RATIO),
    center.y - round_div(cfg->length * cos_v, TRIG_MAX_RATIO));
  GPoint inner = GPoint(
    center.x - round_div(cfg->back_offset * sin_v, TRIG_MAX_RATIO),
    center.y + round_div(cfg->back_offset * cos_v, TRIG_MAX_RATIO));

  int16_t half_w = cfg->width / 2;
  if (half_w < 1) half_w = 1;
  *inner_out = inner; *outer_out = outer; *half_w_out = half_w;

  if (cfg->style == 1) { // triangle: full width at inner (base), tapering to a point at outer (tip)
    points[0] = GPoint(inner.x - round_div(half_w * cos_v, TRIG_MAX_RATIO),
                        inner.y - round_div(half_w * sin_v, TRIG_MAX_RATIO));
    points[1] = GPoint(inner.x + round_div(half_w * cos_v, TRIG_MAX_RATIO),
                        inner.y + round_div(half_w * sin_v, TRIG_MAX_RATIO));
    points[2] = outer;
    return 3;
  }

  // dot/square: a straight rectangular body between inner and outer.
  points[0] = GPoint(inner.x - round_div(half_w * cos_v, TRIG_MAX_RATIO),
                      inner.y - round_div(half_w * sin_v, TRIG_MAX_RATIO));
  points[1] = GPoint(inner.x + round_div(half_w * cos_v, TRIG_MAX_RATIO),
                      inner.y + round_div(half_w * sin_v, TRIG_MAX_RATIO));
  points[2] = GPoint(outer.x + round_div(half_w * cos_v, TRIG_MAX_RATIO),
                      outer.y + round_div(half_w * sin_v, TRIG_MAX_RATIO));
  points[3] = GPoint(outer.x - round_div(half_w * cos_v, TRIG_MAX_RATIO),
                      outer.y - round_div(half_w * sin_v, TRIG_MAX_RATIO));
  return 4;
}

// Draws just the hand's shape (no outline underlay) in `color`, at
// `center`. `dithered` selects a genuine ~50% stipple fill (see
// fill_polygon_dithered() above) instead of a solid one -- this is
// what HandConfig.translucent actually means now, not the 1px
// stroke-only look an earlier version of this file used.
static void draw_hand_shape_once(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                                  GColor color, bool dithered) {
  GPoint points[4], inner, outer;
  int16_t half_w;
  int n = compute_hand_geometry(center, angle, cfg, points, &inner, &outer, &half_w);

  if (dithered) fill_polygon_dithered(ctx, points, n, color);
  else if (cfg->hollow) stroke_path(ctx, points, n, color);
  else fill_path(ctx, points, n, color);

  if (n == 4 && cfg->style == 0) { // dot: round off both true ends
    if (dithered) {
      fill_circle_dithered(ctx, inner, half_w, color);
      fill_circle_dithered(ctx, outer, half_w, color);
    } else if (cfg->hollow) {
      graphics_context_set_stroke_color(ctx, color);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_circle(ctx, inner, half_w);
      graphics_draw_circle(ctx, outer, half_w);
    } else {
      graphics_context_set_fill_color(ctx, color);
      graphics_fill_circle(ctx, inner, half_w);
      graphics_fill_circle(ctx, outer, half_w);
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
static void draw_hand_outline_once(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                                    GColor color, bool dithered) {
  GPoint points[4], inner, outer;
  int16_t half_w;
  int n = compute_hand_geometry(center, angle, cfg, points, &inner, &outer, &half_w);

  if (dithered) stroke_polygon_dithered(ctx, points, n, color);
  else stroke_path(ctx, points, n, color);

  if (n == 4 && cfg->style == 0) { // dot: outline both round caps too
    if (dithered) {
      stroke_circle_dithered(ctx, inner, half_w, color);
      stroke_circle_dithered(ctx, outer, half_w, color);
    } else {
      graphics_context_set_stroke_color(ctx, color);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_circle(ctx, inner, half_w);
      graphics_draw_circle(ctx, outer, half_w);
    }
  }
}

void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color) {
  if (cfg->outline_enabled) {
    // A real perimeter trace now (see draw_hand_outline_once above),
    // dithered too when the hand is translucent, so the outline
    // doesn't look more solid than the fill it's outlining.
    GColor outline_color = resolve_scheme_color(cfg->outline_color, main_color, accent_color, bg_color);
    draw_hand_outline_once(ctx, center, angle, cfg, outline_color, cfg->translucent);
  }

  if (cfg->color != 3) { // 3 = "none" -- skip the fill, outline (if any) still drew above
    GColor hand_color = resolve_scheme_color(cfg->color, main_color, accent_color, bg_color);
    draw_hand_shape_once(ctx, center, angle, cfg, hand_color, cfg->translucent);
  }
}

void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color) {
  if (radius == 0) return;
  GColor color = resolve_scheme_color(color_choice, main_color, accent_color, bg_color);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, center, radius);
}
