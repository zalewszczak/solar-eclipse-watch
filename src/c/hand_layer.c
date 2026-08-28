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
