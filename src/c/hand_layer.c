#include "hand_layer.h"

static const GPoint OUTLINE_OFFSETS[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

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

static GColor resolve_scheme_color(uint8_t choice, GColor main_color, GColor accent_color, GColor bg_color) {
  if (choice == 1) return accent_color;
  if (choice == 2) return bg_color;
  return main_color; // choice 0, and any unrecognized value
}

// Draws just the hand's shape (no outline underlay) in `color`, at
// `center` (which the outline underlay pass in hand_layer_draw() offsets
// by 1px in each direction). `dithered` selects a genuine ~50% stipple
// fill (see fill_polygon_dithered() above) instead of a solid one --
// this is what HandConfig.translucent actually means now, not the 1px
// stroke-only look an earlier version of this file used.
static void draw_hand_shape_once(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                                  GColor color, bool dithered) {
  int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

  // Outward direction (sin_v, -cos_v) and its perpendicular (cos_v, sin_v),
  // same rotation convention used throughout this project (0 = up,
  // clockwise). back_offset is measured against the outward direction, so
  // subtracting it moves the inner point backward (positive) or forward
  // past center (negative) -- see the header comment on back_offset.
  GPoint outer = GPoint(
    center.x + (int32_t)(cfg->length * sin_v) / TRIG_MAX_RATIO,
    center.y - (int32_t)(cfg->length * cos_v) / TRIG_MAX_RATIO);
  GPoint inner = GPoint(
    center.x - (int32_t)(cfg->back_offset * sin_v) / TRIG_MAX_RATIO,
    center.y + (int32_t)(cfg->back_offset * cos_v) / TRIG_MAX_RATIO);

  int16_t half_w = cfg->width / 2;
  if (half_w < 1) half_w = 1;

  if (cfg->style == 1) { // triangle: full width at inner (base), tapering to a point at outer (tip)
    GPoint points[3];
    points[0] = GPoint(inner.x - (int32_t)(half_w * cos_v) / TRIG_MAX_RATIO,
                        inner.y - (int32_t)(half_w * sin_v) / TRIG_MAX_RATIO);
    points[1] = GPoint(inner.x + (int32_t)(half_w * cos_v) / TRIG_MAX_RATIO,
                        inner.y + (int32_t)(half_w * sin_v) / TRIG_MAX_RATIO);
    points[2] = outer;
    if (dithered) fill_polygon_dithered(ctx, points, 3, color);
    else if (cfg->hollow) stroke_path(ctx, points, 3, color);
    else fill_path(ctx, points, 3, color);
    return;
  }

  // dot/square: a straight rectangular body between inner and outer.
  GPoint points[4];
  points[0] = GPoint(inner.x - (int32_t)(half_w * cos_v) / TRIG_MAX_RATIO,
                      inner.y - (int32_t)(half_w * sin_v) / TRIG_MAX_RATIO);
  points[1] = GPoint(inner.x + (int32_t)(half_w * cos_v) / TRIG_MAX_RATIO,
                      inner.y + (int32_t)(half_w * sin_v) / TRIG_MAX_RATIO);
  points[2] = GPoint(outer.x + (int32_t)(half_w * cos_v) / TRIG_MAX_RATIO,
                      outer.y + (int32_t)(half_w * sin_v) / TRIG_MAX_RATIO);
  points[3] = GPoint(outer.x - (int32_t)(half_w * cos_v) / TRIG_MAX_RATIO,
                      outer.y - (int32_t)(half_w * sin_v) / TRIG_MAX_RATIO);
  if (dithered) fill_polygon_dithered(ctx, points, 4, color);
  else if (cfg->hollow) stroke_path(ctx, points, 4, color);
  else fill_path(ctx, points, 4, color);

  if (cfg->style == 0) { // dot: round off both true ends with filled circles
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

void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color) {
  if (cfg->outline_enabled) {
    // 4x 1px-shifted copies underneath, same technique the original
    // draw_big_hand_outlined() used -- dithered too when the hand is
    // translucent, so the outline doesn't look more solid than the fill.
    GColor outline_color = resolve_scheme_color(cfg->outline_color, main_color, accent_color, bg_color);
    for (int i = 0; i < 4; i++) {
      GPoint shifted = GPoint(center.x + OUTLINE_OFFSETS[i].x, center.y + OUTLINE_OFFSETS[i].y);
      draw_hand_shape_once(ctx, shifted, angle, cfg, outline_color, cfg->translucent);
    }
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
