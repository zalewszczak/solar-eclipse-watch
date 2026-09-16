#include "./hand_geometry.h"
#include "../data/eclipse_data.h"

// Build points from axial distance and perpendicular width in fixed-point space.

static FGPoint point_at_axial_fp(FGPoint center, int32_t sin_v, int32_t cos_v, int32_t axial_fp) {
  int32_t dx = (int32_t)(((int64_t)axial_fp * sin_v) / TRIG_MAX_RATIO);
  int32_t dy = (int32_t)(((int64_t)axial_fp * cos_v) / TRIG_MAX_RATIO);
  return subpixel_fgpoint_new(center.x + dx, center.y - dy);
}

static void perp_offset_fp(int32_t sin_v, int32_t cos_v, int32_t half_w_fp, int32_t *dx_w, int32_t *dy_w) {
  *dx_w = (int32_t)(((int64_t)half_w_fp * cos_v) / TRIG_MAX_RATIO);
  *dy_w = (int32_t)(((int64_t)half_w_fp * sin_v) / TRIG_MAX_RATIO);
}


// Keep every hand at least 0.5 px wide in fixed-point space.
static int32_t half_width_fp(uint8_t width_px) {
  int32_t half_w_fp = ((int32_t)width_px << SUBPIXEL_BITS) / 2;
  if (half_w_fp < (1 << (SUBPIXEL_BITS - 1))) half_w_fp = 1 << (SUBPIXEL_BITS - 1);
  return half_w_fp;
}


// Add a rectangular section, optionally with round caps.
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


// Add a tapered section from a base width to a supplied tip.
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
  int32_t back_fp    = -((int32_t)cfg->back_offset << SUBPIXEL_BITS); 
  int32_t mid_fp      = (int32_t)cfg->middle_offset << SUBPIXEL_BITS;  
  int32_t half_w_fp    = half_width_fp(cfg->width);
  int32_t half_sw_fp    = half_width_fp(cfg->secondary_width);
  bool thin_w  = cfg->width < 3;
  bool thin_sw = cfg->secondary_width < 3;

  switch (cfg->style) {
    case 1: { // Triangle.
      FGPoint outer = point_at_axial_fp(center, sin_v, cos_v, len_fp);
      append_taper_fp(geo, center, sin_v, cos_v, back_fp, half_w_fp, outer, thin_w);
      return;
    }

    case 3: { // Dauphine.
              
              
              
              
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

    case 4: { // Sword.
              
              
              
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
      poly->thin = thin_w; 
      poly->pts[0] = subpixel_fgpoint_new(base.x - dx_w, base.y - dy_w);
      poly->pts[1] = subpixel_fgpoint_new(mid.x - dx_sw, mid.y - dy_sw);
      poly->pts[2] = top;
      poly->pts[3] = subpixel_fgpoint_new(mid.x + dx_sw, mid.y + dy_sw);
      poly->pts[4] = subpixel_fgpoint_new(base.x + dx_w, base.y + dy_w);
      return;
    }

    case 5: { // Pomme.
              
              
      append_capsule_fp(geo, center, sin_v, cos_v, mid_fp, len_fp, half_w_fp, thin_w, true);
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, mid_fp, half_sw_fp, thin_sw, false);
      return;
    }

    case 6: { // Spade.
              
              
              
              
              
              
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, len_fp, half_w_fp, thin_w, true);
      FGPoint tip = point_at_axial_fp(center, sin_v, cos_v, len_fp);
      geo->circles[geo->n_circles++] = (HandCircle){ .center = tip, .radius_fp = half_sw_fp, .thin = thin_sw };
      
      
      
      
      
      
      
      
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

    case 7: { // Arrow.
              
              
              
              
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

    case 8: { // Leaf.
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
      int32_t center_ax = (back_fp + len_fp) / 2;
      int32_t peak_ax = center_ax + mid_fp;
      
      
      
      
      if (peak_ax < back_fp) peak_ax = back_fp;
      if (peak_ax > len_fp) peak_ax = len_fp;
      #define LEAF_HALF_SAMPLES 2 
        
        
        
      FGPoint plus_side[LEAF_HALF_SAMPLES], minus_side[LEAF_HALF_SAMPLES];   
      FGPoint plus_side2[LEAF_HALF_SAMPLES], minus_side2[LEAF_HALF_SAMPLES]; 

      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      bool have_back = peak_ax > back_fp;
      bool have_tip  = peak_ax < len_fp;

      if (have_back) {
        for (int k = 1; k <= LEAF_HALF_SAMPLES; k++) {
          int32_t u_num = k, u_den = LEAF_HALF_SAMPLES + 1; 
          int32_t ax = back_fp + subpixel_round_div((peak_ax - back_fp) * u_num, u_den);
          int32_t angle_half_pi_u = (int32_t)(((int64_t)u_num * (TRIG_MAX_ANGLE / 4)) / u_den); 
          int32_t w = (int32_t)(((int64_t)half_w_fp * sin_lookup(angle_half_pi_u)) / TRIG_MAX_RATIO);
          FGPoint p = point_at_axial_fp(center, sin_v, cos_v, ax);
          int32_t dx, dy; perp_offset_fp(sin_v, cos_v, w, &dx, &dy);
          plus_side[k - 1] = subpixel_fgpoint_new(p.x + dx, p.y + dy);
          minus_side[k - 1] = subpixel_fgpoint_new(p.x - dx, p.y - dy);
        }
      }
      if (have_tip) {
        for (int k = 1; k <= LEAF_HALF_SAMPLES; k++) {
          int32_t u_num = k, u_den = LEAF_HALF_SAMPLES + 1; 
          int32_t ax = peak_ax + subpixel_round_div((len_fp - peak_ax) * u_num, u_den);
          int32_t angle_half_pi_v = (int32_t)(((int64_t)u_num * (TRIG_MAX_ANGLE / 4)) / u_den); 
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

    case 9: { // Syringe.
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
              
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, len_fp, half_w_fp, thin_w, false);

      int32_t corner_ax = len_fp + mid_fp;
      int32_t taper_len_fp = half_w_fp - half_sw_fp;
      if (taper_len_fp < SUBPIXEL_HALF) taper_len_fp = SUBPIXEL_HALF; 
        
        
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

    case 10: { // Serpentine.
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
               
      #define SERP_SEGMENTS 6
      int32_t amp_fp = (half_sw_fp > half_w_fp) ? (half_sw_fp - half_w_fp) : 0;
      int32_t diameter_fp = (int32_t)cfg->middle_offset << SUBPIXEL_BITS;
      if (diameter_fp < 0) diameter_fp = -diameter_fp;
      if (diameter_fp < (4 << SUBPIXEL_BITS)) diameter_fp = 4 << SUBPIXEL_BITS; 
        
        
      int32_t period_fp = diameter_fp * 2;
      int32_t dir_sign = (cfg->middle_offset < 0) ? -1 : 1;
      int32_t span_fp = len_fp - back_fp;

      FGPoint verts[SERP_SEGMENTS + 1];
      for (int i = 0; i <= SERP_SEGMENTS; i++) {
        int32_t s_rel_fp = subpixel_round_div(span_fp * i, SERP_SEGMENTS);
        int32_t ax = back_fp + s_rel_fp;
        int64_t angle_raw = ((int64_t)s_rel_fp * TRIG_MAX_ANGLE) / period_fp;
        int32_t angle = (int32_t)(angle_raw % TRIG_MAX_ANGLE);
        int32_t sin_val = sin_lookup(angle) * dir_sign;
        int32_t dev_fp = (int32_t)(((int64_t)amp_fp * sin_val) / TRIG_MAX_RATIO);
        verts[i] = point_at_axial_fp(center, sin_v, cos_v, ax);
        int32_t dx, dy;
        perp_offset_fp(sin_v, cos_v, dev_fp, &dx, &dy);
        verts[i].x += dx;
        verts[i].y += dy;
      }

      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
      
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

    case 0:  // Round-ended bar.
    case 2:  // Square.
    default: 
      append_capsule_fp(geo, center, sin_v, cos_v, back_fp, len_fp, half_w_fp, thin_w, cfg->style == 0);
      return;
  }
}

