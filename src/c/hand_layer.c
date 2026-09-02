#include "hand_layer.h"
#include "eclipse_data.h" // for shake_gradient_active() -- see draw_hand_outline_once_fp()'s own comment on why
#include "features_layer.h" // for contrasting_outline_color() -- see hand_layer_draw()'s own comment on why. Safe as a .c-file-only include (not added to hand_layer.h itself) -- features_layer.h -> eclipse_data.h -> hand_layer.h would otherwise be a real circular header include.

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

// ---- hand shape geometry ---------------------------------------------
//
// Every hand shape (existing dot/triangle/square, plus dauphine/sword/
// spade/arrow/pomme below) is built from a small, fixed set of convex
// primitives -- 1-2 polygons (3-5 points each) and 0-2 circles --
// packed into a HandGeometry. The fill/outline/shadow passes below all
// just iterate whatever's in here, so a new style only ever needs to
// change compute_hand_geometry_fp(), never the three drawing passes
// themselves. (Circles matter for round caps like the dot style's
// pivot/tip caps or the pomme's rounded thick section, and for the
// spade's droplet tip -- everything else is a plain filled polygon.)
//
// Every polygon here MUST be convex: fill_polygon_fp() (and the
// dithered/thin variants) all test membership via
// point_in_convex_polygon_fp(), same as before this file supported
// more than one shape. Any style whose real shape isn't convex on its
// own (pomme's thick+thin sections; spade/arrow's line-or-triangle
// base plus a separate tip ornament) is decomposed into 2 separate
// convex polygons instead of one -- see each style's own comment
// below for its exact point layout.
#define HAND_MAX_POLY_PTS 5
#define HAND_MAX_POLYS 2
// 3, not 2 -- style 6/spade needs its rounded-line base's own 2 round
// caps (from append_capsule_fp(..., round_caps=true)) PLUS its own
// separate droplet-tip circle, 3 circles total in the one hand. Every
// other style needs at most 2 (the round-capped base styles) or 0.
// This used to be 2, silently overflowing HandGeometry.circles[] by
// one entry on every single Spade redraw (regardless of preset --
// unconditional, not something a particular slider combination
// triggered) and corrupting whatever stack memory followed it, which
// is what actually crashed the watch -- not a divide-by-zero (ARM's
// integer divide instructions just return 0 on divide-by-zero, they
// don't trap), a genuine out-of-bounds write.
#define HAND_MAX_CIRCLES 3

typedef struct {
  FGPoint pts[HAND_MAX_POLY_PTS];
  int n;     // 0 = unused
  bool thin; // true when this polygon's own half-width is < 1.5px -- see
              // subpixel.h's fill_polygon_thin_fp() comment for why that
              // needs a different fill routine than a normal-width shape.
} HandPoly;

typedef struct {
  FGPoint center;
  int32_t radius_fp;
  bool thin;
} HandCircle;

typedef struct {
  HandPoly polys[HAND_MAX_POLYS];
  int n_polys;
  HandCircle circles[HAND_MAX_CIRCLES];
  int n_circles;
} HandGeometry;

// A point at signed distance `axial_fp` along the hand's own axis from
// `center` -- positive moves toward the tip (same direction `length`
// extends in), negative moves toward the back (same direction a
// positive back_offset extends in). Every style below places its
// points by combining this with perp_offset_fp() (the sideways/width
// component) -- together they're the same dx/dy math
// compute_hand_geometry_fp() always used, just factored out so each
// style's point layout reads as "how far along, how far to the side"
// instead of repeating the sin/cos arithmetic per point.
static FGPoint point_at_axial_fp(FGPoint center, int32_t sin_v, int32_t cos_v, int32_t axial_fp) {
  int32_t dx = (int32_t)(((int64_t)axial_fp * sin_v) / TRIG_MAX_RATIO);
  int32_t dy = (int32_t)(((int64_t)axial_fp * cos_v) / TRIG_MAX_RATIO);
  return fgpoint_new(center.x + dx, center.y - dy);
}

static void perp_offset_fp(int32_t sin_v, int32_t cos_v, int32_t half_w_fp, int32_t *dx_w, int32_t *dy_w) {
  *dx_w = (int32_t)(((int64_t)half_w_fp * cos_v) / TRIG_MAX_RATIO);
  *dy_w = (int32_t)(((int64_t)half_w_fp * sin_v) / TRIG_MAX_RATIO);
}

// half-width in sub-pixel units, floored at 0.5px same as the original
// single-shape version of this file always did (a literal 0px width
// would make every fill/stroke routine below degenerate).
static int32_t half_width_fp(uint8_t width_px) {
  int32_t half_w_fp = ((int32_t)width_px << SUBPIXEL_BITS) / 2;
  if (half_w_fp < (1 << (SUBPIXEL_BITS - 1))) half_w_fp = 1 << (SUBPIXEL_BITS - 1);
  return half_w_fp;
}

// Appends the standard 4-point "base rectangle" (inner-left, inner-
// right, outer-right, outer-left, in that consistent winding order --
// same order the original dot/square body always used) spanning
// `inner_axial_fp` to `outer_axial_fp` at half-width `half_w_fp` into
// `poly`, and optionally (round_caps) two matching circles into `geo`
// -- shared by styles 0/2 (the whole hand), 5/pomme (its thick
// section) and 6/spade (its "rounded line" base).
static void append_capsule_fp(HandGeometry *geo, FGPoint center, int32_t sin_v, int32_t cos_v,
                               int32_t inner_axial_fp, int32_t outer_axial_fp, int32_t half_w_fp,
                               bool thin, bool round_caps) {
  FGPoint inner = point_at_axial_fp(center, sin_v, cos_v, inner_axial_fp);
  FGPoint outer = point_at_axial_fp(center, sin_v, cos_v, outer_axial_fp);
  int32_t dx_w, dy_w;
  perp_offset_fp(sin_v, cos_v, half_w_fp, &dx_w, &dy_w);

  HandPoly *poly = &geo->polys[geo->n_polys++];
  poly->n = 4;
  poly->thin = thin;
  poly->pts[0] = fgpoint_new(inner.x - dx_w, inner.y - dy_w);
  poly->pts[1] = fgpoint_new(inner.x + dx_w, inner.y + dy_w);
  poly->pts[2] = fgpoint_new(outer.x + dx_w, outer.y + dy_w);
  poly->pts[3] = fgpoint_new(outer.x - dx_w, outer.y - dy_w);

  if (round_caps) {
    geo->circles[geo->n_circles++] = (HandCircle){ .center = inner, .radius_fp = half_w_fp, .thin = thin };
    geo->circles[geo->n_circles++] = (HandCircle){ .center = outer, .radius_fp = half_w_fp, .thin = thin };
  }
}

// Appends a 3-point triangle: two points at `base_axial_fp` spread
// half_w_fp apart, tapering to `tip_point` (an already-computed point,
// not just an axial distance, so callers can pass either a plain
// point_at_axial_fp() result -- style 1/triangle's own tip, style
// 7/arrow's base -- or a point built some other way).
static void append_taper_fp(HandGeometry *geo, FGPoint center, int32_t sin_v, int32_t cos_v,
                             int32_t base_axial_fp, int32_t half_w_fp, FGPoint tip_point, bool thin) {
  FGPoint base = point_at_axial_fp(center, sin_v, cos_v, base_axial_fp);
  int32_t dx_w, dy_w;
  perp_offset_fp(sin_v, cos_v, half_w_fp, &dx_w, &dy_w);

  HandPoly *poly = &geo->polys[geo->n_polys++];
  poly->n = 3;
  poly->thin = thin;
  poly->pts[0] = fgpoint_new(base.x - dx_w, base.y - dy_w);
  poly->pts[1] = fgpoint_new(base.x + dx_w, base.y + dy_w);
  poly->pts[2] = tip_point;
}

static void compute_hand_geometry_fp(FGPoint center, int32_t angle, const HandConfig *cfg, HandGeometry *geo) {
  geo->n_polys = 0;
  geo->n_circles = 0;

  int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

  int32_t len_fp    = (int32_t)cfg->length << SUBPIXEL_BITS;
  int32_t back_fp    = -((int32_t)cfg->back_offset << SUBPIXEL_BITS); // axial (negative = toward back)
  int32_t mid_fp      = (int32_t)cfg->middle_offset << SUBPIXEL_BITS;  // axial (positive = toward tip)
  int32_t half_w_fp    = half_width_fp(cfg->width);
  int32_t half_sw_fp    = half_width_fp(cfg->secondary_width);
  bool thin_w  = cfg->width < 3;
  bool thin_sw = cfg->secondary_width < 3;

  switch (cfg->style) {
    case 1: { // triangle -- tapers to a single point at the tip
      FGPoint outer = point_at_axial_fp(center, sin_v, cos_v, len_fp);
      append_taper_fp(geo, center, sin_v, cos_v, back_fp, half_w_fp, outer, thin_w);
      return;
    }

    case 3: { // dauphine -- 4-point kite: pointy back, two side points
              // near the pivot (at middle_offset), pointy tip. "obey
              // back_offset, but never let the back point end up
              // nearer than middle_offset" -- literal max() of the two,
              // per the request.
      int8_t effective_back = (cfg->back_offset < cfg->middle_offset) ? cfg->middle_offset : cfg->back_offset;
      int32_t back_ax = -((int32_t)effective_back << SUBPIXEL_BITS);
      FGPoint back_tip = point_at_axial_fp(center, sin_v, cos_v, back_ax);
      FGPoint top_tip  = point_at_axial_fp(center, sin_v, cos_v, len_fp);
      FGPoint mid       = point_at_axial_fp(center, sin_v, cos_v, mid_fp);
      int32_t dx_w, dy_w;
      perp_offset_fp(sin_v, cos_v, half_w_fp, &dx_w, &dy_w);

      HandPoly *poly = &geo->polys[geo->n_polys++];
      poly->n = 4;
      poly->thin = thin_w;
      poly->pts[0] = back_tip;
      poly->pts[1] = fgpoint_new(mid.x - dx_w, mid.y - dy_w);
      poly->pts[2] = top_tip;
      poly->pts[3] = fgpoint_new(mid.x + dx_w, mid.y + dy_w);
      return;
    }

    case 4: { // sword -- 5-point pentagon: base (back_offset, regular
              // width), tapering out to a wider mid-bulge (middle_offset,
              // clamped to the hand's own length; secondary_width),
              // tapering back in to a single tip point (length).
      int32_t mid_ax = mid_fp;
      if (mid_ax > len_fp) mid_ax = len_fp;
      FGPoint top = point_at_axial_fp(center, sin_v, cos_v, len_fp);
      FGPoint base = point_at_axial_fp(center, sin_v, cos_v, back_fp);
      FGPoint mid   = point_at_axial_fp(center, sin_v, cos_v, mid_ax);
      int32_t dx_w, dy_w, dx_sw, dy_sw;
      perp_offset_fp(sin_v, cos_v, half_w_fp, &dx_w, &dy_w);
      perp_offset_fp(sin_v, cos_v, half_sw_fp, &dx_sw, &dy_sw);

      HandPoly *poly = &geo->polys[geo->n_polys++];
      poly->n = 5;
      poly->thin = thin_w; // dominant/base width -- matches the other multi-width styles' convention
      poly->pts[0] = fgpoint_new(base.x - dx_w, base.y - dy_w);
      poly->pts[1] = fgpoint_new(mid.x - dx_sw, mid.y - dy_sw);
      poly->pts[2] = top;
      poly->pts[3] = fgpoint_new(mid.x + dx_sw, mid.y + dy_sw);
      poly->pts[4] = fgpoint_new(base.x + dx_w, base.y + dy_w);
      return;
    }

    case 5: { // pomme -- a rounded thick section (middle_offset..length,
              // regular width, round-capped for that "apple" look) on a
              // thinner plain tail (back_offset..middle_offset, secondary_width).
      append_capsule_fp(geo, center, sin_v, cos_v, mid_fp, len_fp, half_w_fp, thin_w, true);
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, mid_fp, half_sw_fp, thin_sw, false);
      return;
    }

    case 6: { // spade -- the existing "rounded line" body (back_offset..
              // length, regular width, round-capped, i.e. exactly style
              // 0's own shape) plus a droplet tip ornament: a circle
              // (secondary_width) sitting at the very tip, topped with a
              // triangular point once it's requested to extend past that
              // circle -- otherwise just the circle alone, which already
              // looks like a rounded droplet top.
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, len_fp, half_w_fp, thin_w, true);
      FGPoint tip = point_at_axial_fp(center, sin_v, cos_v, len_fp);
      geo->circles[geo->n_circles++] = (HandCircle){ .center = tip, .radius_fp = half_sw_fp, .thin = thin_sw };
      // middle_offset is measured from the hand's own length-center --
      // the midpoint between back_offset and length -- not from the
      // tip, so middle_offset=0 lands exactly on that center rather
      // than right at the circle. The triangular point only actually
      // draws once it would extend past the circle (i.e. past the
      // tip) -- any closer than that and it'd be entirely hidden
      // inside/behind the circle anyway, so there's nothing to gain
      // by drawing it.
      int32_t center_axial_fp = (len_fp + back_fp) / 2;
      int32_t apex_axial_fp = center_axial_fp + mid_fp;
      if (apex_axial_fp > len_fp) {
        FGPoint apex = point_at_axial_fp(center, sin_v, cos_v, apex_axial_fp);
        int32_t dx_sw, dy_sw;
        perp_offset_fp(sin_v, cos_v, half_sw_fp, &dx_sw, &dy_sw);
        HandPoly *poly = &geo->polys[geo->n_polys++];
        poly->n = 3;
        poly->thin = thin_sw;
        poly->pts[0] = fgpoint_new(tip.x - dx_sw, tip.y - dy_sw);
        poly->pts[1] = fgpoint_new(tip.x + dx_sw, tip.y + dy_sw);
        poly->pts[2] = apex;
      }
      return;
    }

    case 7: { // arrow -- the existing triangle body (back_offset..length,
              // regular width, tapering to a point -- exactly style 1's
              // own shape) plus a small tip-pointer triangle beyond it:
              // base spread by secondary_width right at the tip, apex
              // middle_offset further out.
      FGPoint tip = point_at_axial_fp(center, sin_v, cos_v, len_fp);
      append_taper_fp(geo, center, sin_v, cos_v, back_fp, half_w_fp, tip, thin_w);
      FGPoint apex = point_at_axial_fp(center, sin_v, cos_v, len_fp + mid_fp);
      int32_t dx_sw, dy_sw;
      perp_offset_fp(sin_v, cos_v, half_sw_fp, &dx_sw, &dy_sw);
      HandPoly *poly = &geo->polys[geo->n_polys++];
      poly->n = 3;
      poly->thin = thin_sw;
      poly->pts[0] = fgpoint_new(tip.x - dx_sw, tip.y - dy_sw);
      poly->pts[1] = fgpoint_new(tip.x + dx_sw, tip.y + dy_sw);
      poly->pts[2] = apex;
      return;
    }

    case 0:  // dot -- round-capped body
    case 2:  // square -- flat-capped body (same rectangle, no circles)
    default: // any unrecognized style value falls back to style 2's plain body
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, len_fp, half_w_fp, thin_w, cfg->style == 0);
      return;
  }
}

// Draws just the hand's shape (no outline underlay) in `color`, at
// `center`. `dithered` selects a genuine ~50% stipple fill (see
// fill_polygon_dithered() above) instead of a solid one -- this is
// what HandConfig.translucent actually means now, not the 1px
// stroke-only look an earlier version of this file used.
static void draw_hand_shape_once_fp(GContext *ctx, FGPoint center, int32_t angle, const HandConfig *cfg,
                                     GColor color, bool dithered) {
  HandGeometry geo;
  compute_hand_geometry_fp(center, angle, cfg, &geo);

  for (int i = 0; i < geo.n_polys; i++) {
    HandPoly *p = &geo.polys[i];
    if (dithered) fill_polygon_dithered_fp(ctx, p->pts, p->n, color);
    else if (cfg->hollow) stroke_polygon_fp(ctx, p->pts, p->n, color, false);
    else if (p->thin) fill_polygon_thin_fp(ctx, p->pts, p->n, color);
    else fill_polygon_fp(ctx, p->pts, p->n, color);
  }
  for (int i = 0; i < geo.n_circles; i++) {
    HandCircle *c = &geo.circles[i];
    if (cfg->hollow && !dithered) stroke_circle_fp(ctx, c->center, c->radius_fp, color, false);
    else if (c->thin && !dithered) fill_circle_thin_fp(ctx, c->center, c->radius_fp, color);
    else fill_circle_fp(ctx, c->center, c->radius_fp, color, dithered);
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
  HandGeometry geo;
  compute_hand_geometry_fp(center, angle, cfg, &geo);

  // "On shake" gradient mode: a true per-pixel screen-space sweep
  // instead of one fixed color for the whole outline -- see
  // subpixel.h's stroke_*_gradient_fp() functions and their own
  // comment for how. Only for the non-dithered case: a translucent
  // hand's dithered outline already has its own density logic, and
  // combining "which pixels get skipped for translucency" with "what
  // color the ones that survive should be" is more than this is worth
  // -- a translucent hand's outline just doesn't gradient-shift.
  uint8_t shake_shift;
  if (!dithered && shake_gradient_active(&shake_shift)) {
    for (int i = 0; i < geo.n_polys; i++) {
      stroke_polygon_gradient_fp(ctx, geo.polys[i].pts, geo.polys[i].n, shake_shift);
    }
    for (int i = 0; i < geo.n_circles; i++) {
      stroke_circle_gradient_fp(ctx, geo.circles[i].center, geo.circles[i].radius_fp, shake_shift);
    }
    return;
  }

  for (int i = 0; i < geo.n_polys; i++) {
    stroke_polygon_fp(ctx, geo.polys[i].pts, geo.polys[i].n, color, dithered);
  }
  for (int i = 0; i < geo.n_circles; i++) {
    stroke_circle_fp(ctx, geo.circles[i].center, geo.circles[i].radius_fp, color, dithered);
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

  HandGeometry geo;
  compute_hand_geometry_fp(shadow_center, angle, cfg, &geo);

  if (!shadow_translucent_style) {
    for (int i = 0; i < geo.n_polys; i++) {
      HandPoly *p = &geo.polys[i];
      if (p->thin) fill_polygon_thin_fp(ctx, p->pts, p->n, GColorBlack);
      else fill_polygon_fp(ctx, p->pts, p->n, GColorBlack);
    }
    for (int i = 0; i < geo.n_circles; i++) {
      HandCircle *c = &geo.circles[i];
      if (c->thin) fill_circle_thin_fp(ctx, c->center, c->radius_fp, GColorBlack);
      else fill_circle_fp(ctx, c->center, c->radius_fp, GColorBlack, false);
    }
    return;
  }

  uint8_t threshold = cfg->translucent ? 4 : 8; // ~25% vs ~50% Bayer density
  for (int i = 0; i < geo.n_polys; i++) {
    fill_polygon_dithered_level_fp(ctx, geo.polys[i].pts, geo.polys[i].n, GColorBlack, threshold);
  }
  for (int i = 0; i < geo.n_circles; i++) {
    fill_circle_dithered_level_fp(ctx, geo.circles[i].center, geo.circles[i].radius_fp, GColorBlack, threshold);
  }
}

void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color,
                      bool shadow_translucent_style, uint16_t shadow_angle_deg,
                      uint16_t length_scale_1000) {
  // Scaled-length copy for the startup animation's "grows out from a
  // center dot" phase -- everything below just keeps using `cfg` as
  // before, now possibly pointing at this shrunk copy instead of the
  // caller's real one. Width/back_offset/outline/shadow all stay at
  // their real configured size regardless -- only the hand's own
  // length grows in.
  HandConfig scaled_cfg;
  if (length_scale_1000 < 1000) {
    scaled_cfg = *cfg;
    scaled_cfg.length = (uint8_t)(((uint32_t)cfg->length * length_scale_1000) / 1000);
    cfg = &scaled_cfg;
  }

  FGPoint center_fp = fgpoint_from_gpoint(center);

  draw_hand_shadow_once_fp(ctx, center_fp, angle, cfg, shadow_translucent_style, shadow_angle_deg);

  // "Contrast style: Shadow" (hand style presets only -- see
  // hard_shadow's own comment in hand_layer.h) -- a fixed, always-
  // solid, always-1px-right-and-down black copy of the hand shape,
  // drawn before everything else so the real hand/outline paints over
  // most of it, leaving just that 1px offset showing on two edges.
  // Reuses draw_hand_shape_once_fp() directly rather than the outline
  // stroke machinery -- this is a shifted silhouette, not a traced
  // perimeter.
  if (cfg->hard_shadow) {
    FGPoint shadow_center = fgpoint_new(center_fp.x + SUBPIXEL_SCALE, center_fp.y + SUBPIXEL_SCALE);
    draw_hand_shape_once_fp(ctx, shadow_center, angle, cfg, GColorBlack, false);
  }

  GColor hand_color = GColorClear; // resolved below if actually needed (cfg->color != 3 or outline_auto_contrast)
  bool have_hand_color = false;
  if (cfg->color != 3 || cfg->outline_auto_contrast) {
    hand_color = resolve_scheme_color(cfg->color, main_color, accent_color, bg_color);
    have_hand_color = true;
  }

  if (cfg->outline_enabled) {
    // A real perimeter trace now (see draw_hand_outline_once above),
    // dithered too when the hand is translucent, so the outline
    // doesn't look more solid than the fill it's outlining. The "on
    // shake" gradient effect (if active) is applied inside
    // draw_hand_outline_once_fp() itself now, per pixel -- see its own
    // comment -- rather than resolved to one flat color up here.
    GColor outline_color = cfg->outline_auto_contrast && have_hand_color
      ? contrasting_outline_color(hand_color)
      : resolve_scheme_color(cfg->outline_color, main_color, accent_color, bg_color);
    draw_hand_outline_once_fp(ctx, center_fp, angle, cfg, outline_color, cfg->translucent);
  }

  if (cfg->color != 3) { // 3 = "none" -- skip the fill, outline (if any) still drew above
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
