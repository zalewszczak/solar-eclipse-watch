#include "./hand_geometry.h"
#include "../data/eclipse_data.h"

// Place a point along the hand axis and perpendicular to it. Keeping this
// construction in one helper makes each hand style describe its geometry in
// terms of axial distance and lateral width without repeating trigonometry.
static FGPoint point_at_axial_fp(FGPoint center, int32_t sin_v, int32_t cos_v, int32_t axial_fp) {
  int32_t dx = (int32_t)(((int64_t)axial_fp * sin_v) / TRIG_MAX_RATIO);
  int32_t dy = (int32_t)(((int64_t)axial_fp * cos_v) / TRIG_MAX_RATIO);
  return subpixel_fgpoint_new(center.x + dx, center.y - dy);
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
  poly->pts[0] = subpixel_fgpoint_new(inner.x - dx_w, inner.y - dy_w);
  poly->pts[1] = subpixel_fgpoint_new(inner.x + dx_w, inner.y + dy_w);
  poly->pts[2] = subpixel_fgpoint_new(outer.x + dx_w, outer.y + dy_w);
  poly->pts[3] = subpixel_fgpoint_new(outer.x - dx_w, outer.y - dy_w);

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
  poly->pts[0] = subpixel_fgpoint_new(base.x - dx_w, base.y - dy_w);
  poly->pts[1] = subpixel_fgpoint_new(base.x + dx_w, base.y + dy_w);
  poly->pts[2] = tip_point;
}

void hand_geometry_compute_fp(FGPoint center, int32_t angle, const HandConfig *cfg, HandGeometry *geo) {
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
      poly->pts[1] = subpixel_fgpoint_new(mid.x - dx_w, mid.y - dy_w);
      poly->pts[2] = top_tip;
      poly->pts[3] = subpixel_fgpoint_new(mid.x + dx_w, mid.y + dy_w);
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
      poly->pts[0] = subpixel_fgpoint_new(base.x - dx_w, base.y - dy_w);
      poly->pts[1] = subpixel_fgpoint_new(mid.x - dx_sw, mid.y - dy_sw);
      poly->pts[2] = top;
      poly->pts[3] = subpixel_fgpoint_new(mid.x + dx_sw, mid.y + dy_sw);
      poly->pts[4] = subpixel_fgpoint_new(base.x + dx_w, base.y + dy_w);
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
        poly->pts[0] = subpixel_fgpoint_new(tip.x - dx_sw, tip.y - dy_sw);
        poly->pts[1] = subpixel_fgpoint_new(tip.x + dx_sw, tip.y + dy_sw);
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
      poly->pts[0] = subpixel_fgpoint_new(tip.x - dx_sw, tip.y - dy_sw);
      poly->pts[1] = subpixel_fgpoint_new(tip.x + dx_sw, tip.y + dy_sw);
      poly->pts[2] = apex;
      return;
    }

    case 8: { // leaf -- a single smooth convex outline that starts at
              // a point (back_offset), swells to its widest at a
              // configurable peak (middle_offset, measured from the
              // hand's own length-center like every other style's
              // middle_offset), and tapers back down to a point at
              // the tip (length). Each half's width follows a quarter
              // sine -- half_w*sin(pi*u/2) growing into the peak,
              // half_w*cos(pi*u/2) shrinking out of it -- rather than
              // a full raised-cosine ease: sin/cos over a quarter
              // period is concave for its whole span, which is what
              // actually keeps the outline convex (this shape's fill
              // routine requires a convex polygon); a raised-cosine
              // has an inflection partway along each half and
              // produces a self-intersecting, wrongly-filled outline.
              // The trade-off is a sharp (not smoothed-to-zero) slope
              // at the back and tip points -- which reads as the
              // classic pointed leaf tip anyway, with the smooth
              // deceleration the spec describes happening on the
              // approach to the peak (and, mirrored, on the way out
              // of it).
      int32_t center_ax = (back_fp + len_fp) / 2;
      int32_t peak_ax = center_ax + mid_fp;
      // Clamped to the hand's own [back_offset, length] span -- same
      // idea as sword's own mid_ax clamp above -- so an extreme
      // middle_offset can't push the peak past either anchor and
      // invert one flank's ordering into a self-intersecting outline.
      if (peak_ax < back_fp) peak_ax = back_fp;
      if (peak_ax > len_fp) peak_ax = len_fp;
      #define LEAF_HALF_SAMPLES 2 // interior points per half, excluding
        // the shared back/peak/tip anchors -- keeps the whole 12-point
        // outline (see HAND_MAX_POLY_PTS's own comment) inside its cap
        // while still reading as a smooth curve at watch-face scale.
      FGPoint plus_side[LEAF_HALF_SAMPLES], minus_side[LEAF_HALF_SAMPLES];   // back -> peak flank
      FGPoint plus_side2[LEAF_HALF_SAMPLES], minus_side2[LEAF_HALF_SAMPLES]; // peak -> tip flank

      // A sufficiently extreme middle_offset clamps peak_ax onto
      // back_fp or len_fp exactly. Once a
      // flank's own span is zero, every one of its "interior" points
      // above is mathematically supposed to land exactly on the
      // straight edge between that anchor and the peak, but the
      // fixed-point width/trig math doesn't round to EXACTLY zero
      // deviation from that line -- small enough to be invisible but
      // sufficient to flip the polygon non-convex by a few thousandths of a pixel
      // right at that corner (invisible on screen, but still breaks
      // subpixel_point_in_convex_polygon_fp's assumption). Rather than compute
      // those now-redundant points at all, this flank is just omitted
      // outright when degenerate -- back_pt/tip_pt (and that whole
      // flank's samples) drop out, leaving a flat leading/trailing
      // edge directly between peak_plus and peak_minus, which is the
      // correct limiting shape anyway (the taper have zero length
      // left to bulge out over).
      bool have_back = peak_ax > back_fp;
      bool have_tip  = peak_ax < len_fp;

      if (have_back) {
        for (int k = 1; k <= LEAF_HALF_SAMPLES; k++) {
          int32_t u_num = k, u_den = LEAF_HALF_SAMPLES + 1; // u in (0,1)
          int32_t ax = back_fp + subpixel_round_div((peak_ax - back_fp) * u_num, u_den);
          int32_t angle_half_pi_u = u_num * (TRIG_MAX_ANGLE / 4) / u_den; // (pi/2)*u -- u_num <=
            // LEAF_HALF_SAMPLES and TRIG_MAX_ANGLE/4 is 16384, so this product is
            // nowhere near int32's range; the int64 it used to be widened to only
            // risked pulling libgcc's 64-bit divide back in (see subpixel_div64()).
          int32_t w = (int32_t)(((int64_t)half_w_fp * sin_lookup(angle_half_pi_u)) / TRIG_MAX_RATIO);
          FGPoint p = point_at_axial_fp(center, sin_v, cos_v, ax);
          int32_t dx, dy; perp_offset_fp(sin_v, cos_v, w, &dx, &dy);
          plus_side[k - 1] = subpixel_fgpoint_new(p.x + dx, p.y + dy);
          minus_side[k - 1] = subpixel_fgpoint_new(p.x - dx, p.y - dy);
        }
      }
      if (have_tip) {
        for (int k = 1; k <= LEAF_HALF_SAMPLES; k++) {
          int32_t u_num = k, u_den = LEAF_HALF_SAMPLES + 1; // v in (0,1), peak->tip
          int32_t ax = peak_ax + subpixel_round_div((len_fp - peak_ax) * u_num, u_den);
          int32_t angle_half_pi_v = u_num * (TRIG_MAX_ANGLE / 4) / u_den; // (pi/2)*v -- u_num <=
            // LEAF_HALF_SAMPLES and TRIG_MAX_ANGLE/4 is 16384, so this product is
            // nowhere near int32's range; the int64 it used to be widened to only
            // risked pulling libgcc's 64-bit divide back in (see subpixel_div64()).
          int32_t w = (int32_t)(((int64_t)half_w_fp * cos_lookup(angle_half_pi_v)) / TRIG_MAX_RATIO);
          FGPoint p = point_at_axial_fp(center, sin_v, cos_v, ax);
          int32_t dx, dy; perp_offset_fp(sin_v, cos_v, w, &dx, &dy);
          plus_side2[k - 1] = subpixel_fgpoint_new(p.x + dx, p.y + dy);
          minus_side2[k - 1] = subpixel_fgpoint_new(p.x - dx, p.y - dy);
        }
      }

      FGPoint peak_plus, peak_minus;
      {
        FGPoint p = point_at_axial_fp(center, sin_v, cos_v, peak_ax);
        int32_t dx, dy; perp_offset_fp(sin_v, cos_v, half_w_fp, &dx, &dy);
        peak_plus = subpixel_fgpoint_new(p.x + dx, p.y + dy);
        peak_minus = subpixel_fgpoint_new(p.x - dx, p.y - dy);
      }

      HandPoly *poly = &geo->polys[geo->n_polys++];
      poly->thin = thin_w;
      int n = 0;
      if (have_back) {
        poly->pts[n++] = point_at_axial_fp(center, sin_v, cos_v, back_fp);
        for (int k = 0; k < LEAF_HALF_SAMPLES; k++) poly->pts[n++] = plus_side[k];
      }
      poly->pts[n++] = peak_plus;
      if (have_tip) {
        for (int k = 0; k < LEAF_HALF_SAMPLES; k++) poly->pts[n++] = plus_side2[k];
        poly->pts[n++] = point_at_axial_fp(center, sin_v, cos_v, len_fp);
        for (int k = LEAF_HALF_SAMPLES - 1; k >= 0; k--) poly->pts[n++] = minus_side2[k];
      }
      poly->pts[n++] = peak_minus;
      if (have_back) {
        for (int k = LEAF_HALF_SAMPLES - 1; k >= 0; k--) poly->pts[n++] = minus_side[k];
      }
      poly->n = n;
      #undef LEAF_HALF_SAMPLES
      return;
    }

    case 9: { // syringe -- a flat-ended "barrel" (back_offset..length,
              // regular width, same rectangle style 2/square uses)
              // plus a separate needle: a trapezoid tapering from the
              // barrel's own width down to secondary_width at a fixed
              // 45 degree angle, so its own axial span is whatever
              // (half_w - half_sw) requires rather than being directly
              // user-editable. middle_offset positions the needle's
              // WIDE corner -- where it meets the barrel's width, i.e.
              // where the taper actually starts -- measured from the
              // tip (`length`), same "signed distance along the axis"
              // convention back_offset already uses relative to
              // center: positive pushes that corner (and the whole
              // needle) out past the barrel's own tip ("sticks out
              // from the top", detached from the barrel if the needle
              // doesn't reach back to it -- the same kind of gap a
              // negative back_offset already makes elsewhere in this
              // file); negative pulls the corner backward INTO the
              // barrel, so the needle emerges from partway along the
              // base instead of flush with its end ("sticks out from
              // the base").
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, len_fp, half_w_fp, thin_w, false);

      int32_t corner_ax = len_fp + mid_fp;
      int32_t taper_len_fp = half_w_fp - half_sw_fp;
      if (taper_len_fp < SUBPIXEL_HALF) taper_len_fp = SUBPIXEL_HALF; // guard against
        // secondary_width >= width collapsing the taper to a zero/
        // negative-length degenerate poly
      int32_t tip_ax = corner_ax + taper_len_fp;

      FGPoint corner = point_at_axial_fp(center, sin_v, cos_v, corner_ax);
      FGPoint tip     = point_at_axial_fp(center, sin_v, cos_v, tip_ax);
      int32_t dx_w, dy_w, dx_sw, dy_sw;
      perp_offset_fp(sin_v, cos_v, half_w_fp, &dx_w, &dy_w);
      perp_offset_fp(sin_v, cos_v, half_sw_fp, &dx_sw, &dy_sw);

      HandPoly *poly = &geo->polys[geo->n_polys++];
      poly->n = 4;
      poly->thin = thin_w;
      poly->pts[0] = subpixel_fgpoint_new(corner.x - dx_w, corner.y - dy_w);
      poly->pts[1] = subpixel_fgpoint_new(corner.x + dx_w, corner.y + dy_w);
      poly->pts[2] = subpixel_fgpoint_new(tip.x + dx_sw, tip.y + dy_sw);
      poly->pts[3] = subpixel_fgpoint_new(tip.x - dx_sw, tip.y - dy_sw);
      return;
    }

    case 10: { // serpentine -- a squiggly stroked path from the
               // pivot, approximated as SERP_SEGMENTS straight quads
               // along a sampled sine centerline (not true circular
               // arcs -- see below) so it still fits this file's
               // convex-polygon-only fill/outline/shadow machinery.
               // `length`/`back_offset` set the STRAIGHT-LINE span the
               // path covers (its axial start/end), same meaning as
               // every other style; the path itself winds within that
               // span rather than running along it directly.
               //
               // middle_offset doubles as both the requested curvature
               // ("circle of Npx width" -> a full up-and-down wave
               // spans roughly 2*N px of that axial span) and, via its
               // sign, which way the FIRST bend turns (positive =
               // counter-clockwise per the spec, negative = clockwise)
               // -- the same dual magnitude+sign use back_offset
               // already gets elsewhere in HandConfig.
               //
               // secondary_width is the envelope the squiggle is
               // allowed to occupy (only takes effect once it exceeds
               // width, per spec); the centerline's own peak deviation
               // is half that envelope minus half the line's own
               // width, so the stroke's OUTER edge -- not its
               // centerline -- is what actually reaches the envelope
               // boundary.
               //
               // This is a fixed small number of straight segments on
               // a SAMPLED sine, not a geometrically exact arc --
               // keeps the cost (and HandGeometry's own size) bounded
               // regardless of `length`, at the cost of not being
               // exact circular arcs. Not visually distinguishable
               // from true arcs at watch-face scale/resolution.
      #define SERP_SEGMENTS 6
      int32_t amp_fp = (half_sw_fp > half_w_fp) ? (half_sw_fp - half_w_fp) : 0;
      int32_t diameter_fp = (int32_t)cfg->middle_offset << SUBPIXEL_BITS;
      if (diameter_fp < 0) diameter_fp = -diameter_fp;
      if (diameter_fp < (4 << SUBPIXEL_BITS)) diameter_fp = 4 << SUBPIXEL_BITS; // guard
        // against a near-0 period aliasing into meaningless high-
        // frequency noise (and against dividing by ~0 below)
      int32_t period_fp = diameter_fp * 2;
      int32_t dir_sign = (cfg->middle_offset < 0) ? -1 : 1;
      int32_t span_fp = len_fp - back_fp;

      FGPoint verts[SERP_SEGMENTS + 1];
      for (int i = 0; i <= SERP_SEGMENTS; i++) {
        int32_t s_rel_fp = subpixel_round_div(span_fp * i, SERP_SEGMENTS);
        int32_t ax = back_fp + s_rel_fp;
        // period_fp is a runtime value, so this goes through
        // subpixel_div64() rather than a plain int64 `/` -- see its comment
        // in subpixel.h. period_fp is floored at 8<<SUBPIXEL_BITS by the
        // guard above, so the quotient is well inside int32 either way.
        int32_t angle_raw = subpixel_div64((int64_t)s_rel_fp * TRIG_MAX_ANGLE, period_fp);
        int32_t angle = angle_raw % TRIG_MAX_ANGLE;
        int32_t sin_val = sin_lookup(angle) * dir_sign;
        int32_t dev_fp = (int32_t)(((int64_t)amp_fp * sin_val) / TRIG_MAX_RATIO);
        verts[i] = point_at_axial_fp(center, sin_v, cos_v, ax);
        int32_t dx, dy;
        perp_offset_fp(sin_v, cos_v, dev_fp, &dx, &dy);
        verts[i].x += dx;
        verts[i].y += dy;
      }

      // Each segment quad uses ITS OWN straight-line tangent for its
      // perpendicular offset (like a straight capsule between its two
      // vertices) rather than a bisector shared with its neighbors.
      // A shared-bisector offset (this file's earlier approach) keeps
      // the ribbon perfectly seamless at each joint, but at a sharp
      // enough bend -- short segments, a tight requested curvature, a
      // thin line -- the bisector direction can diverge enough from
      // either segment's own direction that the resulting quad's
      // edges cross (a real, if rare, self-intersecting "bowtie" -- not
      // just fixed-point rounding noise). Computing each quad
      // independently off its own segment guarantees a valid convex
      // quad every time (same guarantee append_capsule_fp's rectangle
      // already relies on for a single straight segment); the only
      // cost is a small seam -- a hair's-width sliver of a gap or
      // overlap -- at each joint, invisible at watch-face scale with
      // SERP_SEGMENTS this low.
      for (int i = 0; i < SERP_SEGMENTS; i++) {
        FGPoint a = verts[i], b = verts[i + 1];
        int32_t tx = b.x - a.x, ty = b.y - a.y;
        int64_t len_sq = (int64_t)tx * tx + (int64_t)ty * ty;
        int32_t ox, oy;
        if (len_sq == 0) {
          perp_offset_fp(sin_v, cos_v, half_w_fp, &ox, &oy);
        } else {
          int32_t tlen = (int32_t)subpixel_isqrt64_fp(len_sq);
          ox = subpixel_round_div(-ty * half_w_fp, tlen);
          oy = subpixel_round_div(tx * half_w_fp, tlen);
        }
        HandPoly *poly = &geo->polys[geo->n_polys++];
        poly->n = 4;
        poly->thin = thin_w;
        poly->pts[0] = subpixel_fgpoint_new(a.x - ox, a.y - oy);
        poly->pts[1] = subpixel_fgpoint_new(a.x + ox, a.y + oy);
        poly->pts[2] = subpixel_fgpoint_new(b.x + ox, b.y + oy);
        poly->pts[3] = subpixel_fgpoint_new(b.x - ox, b.y - oy);
      }
      #undef SERP_SEGMENTS
      return;
    }

    case 0:  // dot -- round-capped body
    case 2:  // square -- flat-capped body (same rectangle, no circles)
    default: // any unrecognized style value falls back to style 2's plain body
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, len_fp, half_w_fp, thin_w, cfg->style == 0);
      return;
  }
}

