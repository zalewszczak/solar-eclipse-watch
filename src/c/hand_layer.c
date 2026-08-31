#include "hand_layer.h"

// round_div/BAYER4/FGPoint helpers and the fill_polygon_fp()/
// fill_polygon_dithered_fp()/fill_circle_fp()/stroke_line_fp()/
// stroke_polygon_fp()/stroke_circle_fp() rasterizers this file relies on
// all now live in subpixel.h (included via hand_layer.h) -- shared with
// background_layer.c's marker ring. See that header's own top-of-file
// comment for why, and stroke_line_fp()'s comment specifically for the
// rounding/step-count bug that used to make outlines and hollow shapes
// look half a pixel off or drop parts of thin lines.

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
  bool thin = cfg->width < 3; // see subpixel.h's own comment on fill_polygon_thin_fp() for why

  if (dithered) fill_polygon_dithered_fp(ctx, points, n, color);
  else if (cfg->hollow) stroke_polygon_fp(ctx, points, n, color, false);
  else if (thin) fill_polygon_thin_fp(ctx, points, n, color);
  else fill_polygon_fp(ctx, points, n, color);

  if (n == 4 && cfg->style == 0) { // dot style round caps
    if (cfg->hollow && !dithered) {
      stroke_circle_fp(ctx, inner, half_w, color, false);
      stroke_circle_fp(ctx, outer, half_w, color, false);
    } else if (thin && !dithered) {
      fill_circle_thin_fp(ctx, inner, half_w, color);
      fill_circle_thin_fp(ctx, outer, half_w, color);
    } else {
      fill_circle_fp(ctx, inner, half_w, color, dithered);
      fill_circle_fp(ctx, outer, half_w, color, dithered);
    }
  }
}
// A genuine perimeter outline for hand_layer_draw()'s outline_enabled
// pass -- stroke_polygon_fp()/stroke_circle_fp() (a real, clean 1px
// boundary line, whether opaque or dithered -- see subpixel.h for the
// rounding fix that makes that boundary line actually land where it's
// supposed to). Replaces the earlier "draw 4 shifted copies of the
// filled shape" technique, which isn't actually an outline at all -- for
// an opaque hand it merely approximates one by blurring the silhouette
// outward, and for a translucent hand it doesn't work at all: dithering
// 4 offset copies of an already-dithered fill just smears the same
// stipple pattern into a slightly bigger blob, not a clean ring around
// the shape.
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

// Bayer-dithered polygon/circle fills at an arbitrary density, not just
// fill_polygon_dithered_fp()/fill_circle_fp()'s fixed ~50% -- shadows need
// a second, lighter ~25% density (see draw_hand_shadow_once_fp() below),
// and duplicating the scan logic here (rather than changing the shared
// subpixel.h versions or their callers) matches this project's own
// stated convention for small self-contained helpers like this -- see
// subpixel.h's top-of-file comment. `threshold` is compared directly
// against BAYER4's 0-15 values (skip when >= threshold): 8 reproduces
// the original ~50%, 4 gives ~25%.
static void fill_polygon_dithered_level_fp(GContext *ctx, const FGPoint *pts, int n, GColor color, uint8_t threshold) {
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
      if (BAYER4[y & 3][x & 3] >= threshold) continue;
      int32_t sample_x = ((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF;
      if (point_in_convex_polygon_fp(pts, n, fgpoint_new(sample_x, sample_y))) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

static void fill_circle_dithered_level_fp(GContext *ctx, FGPoint center, int32_t radius_fp, GColor color, uint8_t threshold) {
  int16_t min_x = (int16_t)((center.x - radius_fp) >> SUBPIXEL_BITS);
  int16_t max_x = (int16_t)((center.x + radius_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);
  int16_t min_y = (int16_t)((center.y - radius_fp) >> SUBPIXEL_BITS);
  int16_t max_y = (int16_t)((center.y + radius_fp + SUBPIXEL_MASK) >> SUBPIXEL_BITS);

  int64_t r_sq = (int64_t)radius_fp * radius_fp;
  graphics_context_set_fill_color(ctx, color);

  for (int16_t y = min_y; y <= max_y; y++) {
    int64_t dy = (((int32_t)y << SUBPIXEL_BITS) + SUBPIXEL_HALF) - center.y;
    int64_t dy_sq = dy * dy;
    for (int16_t x = min_x; x <= max_x; x++) {
      if (BAYER4[y & 3][x & 3] >= threshold) continue;
      int64_t dx = (((int32_t)x << SUBPIXEL_BITS) + SUBPIXEL_HALF) - center.x;
      if (dx * dx + dy_sq <= r_sq) {
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}

// A drop shadow of the hand's own shape, translated (never rotated
// relative to the hand -- a real shadow's direction is fixed by the
// light source, not by whatever the hand itself currently points at) by
// shadow_distance_px in shadow_angle_deg's direction (a single shared
// angle for all 3 hands -- see hand_layer_draw()'s own comment for why),
// then filled in black -- solid if shadow_translucent_style is off,
// otherwise dithered at ~50%, or ~25% when the hand itself
// (cfg->translucent) is also translucent. Drawn before the outline/fill
// in hand_layer_draw() below, so it always sits underneath both.
static void draw_hand_shadow_once_fp(GContext *ctx, FGPoint center, int32_t angle, const HandConfig *cfg,
                                      bool shadow_translucent_style, uint16_t shadow_angle_deg) {
  if (!cfg->shadow_enabled) return;

  int32_t shadow_native_angle = (int32_t)(((int64_t)shadow_angle_deg * TRIG_MAX_ANGLE) / 360);
  int32_t dist_fp = (int32_t)cfg->shadow_distance_px << SUBPIXEL_BITS;
  int32_t dx = (int32_t)(((int64_t)dist_fp * sin_lookup(shadow_native_angle)) / TRIG_MAX_RATIO);
  int32_t dy = -(int32_t)(((int64_t)dist_fp * cos_lookup(shadow_native_angle)) / TRIG_MAX_RATIO);
  FGPoint shadow_center = fgpoint_new(center.x + dx, center.y + dy);

  FGPoint points[4], inner, outer;
  int32_t half_w;
  int n = compute_hand_geometry_fp(shadow_center, angle, cfg, points, &inner, &outer, &half_w);

  if (!shadow_translucent_style) {
    if (cfg->width < 3) {
      fill_polygon_thin_fp(ctx, points, n, GColorBlack);
      if (n == 4 && cfg->style == 0) {
        fill_circle_thin_fp(ctx, inner, half_w, GColorBlack);
        fill_circle_thin_fp(ctx, outer, half_w, GColorBlack);
      }
    } else {
      fill_polygon_fp(ctx, points, n, GColorBlack);
      if (n == 4 && cfg->style == 0) {
        fill_circle_fp(ctx, inner, half_w, GColorBlack, false);
        fill_circle_fp(ctx, outer, half_w, GColorBlack, false);
      }
    }
    return;
  }

  uint8_t threshold = cfg->translucent ? 4 : 8; // ~25% vs ~50% Bayer density
  fill_polygon_dithered_level_fp(ctx, points, n, GColorBlack, threshold);
  if (n == 4 && cfg->style == 0) {
    fill_circle_dithered_level_fp(ctx, inner, half_w, GColorBlack, threshold);
    fill_circle_dithered_level_fp(ctx, outer, half_w, GColorBlack, threshold);
  }
}

void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color,
                      bool shadow_translucent_style, uint16_t shadow_angle_deg) {
  FGPoint center_fp = fgpoint_from_gpoint(center);

  draw_hand_shadow_once_fp(ctx, center_fp, angle, cfg, shadow_translucent_style, shadow_angle_deg);

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
