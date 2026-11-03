#include "./hand_layer.h"
#include "./hand_geometry.h"
#include "../graphics/subpixel.h"

// Hand shapes are built once, then reused for fill, outline, and shadow passes.

static GColor resolve_scheme_color(uint8_t choice, GColor main_color, GColor accent_color, GColor bg_color) {
  if (choice == 1) return accent_color;
  if (choice == 2) return bg_color;
  return main_color; 
}


#define HAND_MAX_POLY_PTS 12


#define HAND_MAX_POLYS 6


#define HAND_MAX_CIRCLES 3


static void draw_hand_shape_from_geometry(GContext *ctx, const HandGeometry *geo, const HandConfig *cfg,
                                           GColor color, bool dithered) {
  int32_t hollow_thickness_fp = (int32_t)cfg->hollow_thickness << SUBPIXEL_BITS;

  for (int i = 0; i < geo->n_polys; i++) {
    const HandPoly *p = &geo->polys[i];
    if (dithered) subpixel_fill_polygon_dithered_fp(ctx, p->pts, p->n, color);
    else if (cfg->hollow) {
      if (cfg->hollow_thickness <= 1) subpixel_stroke_polygon_fp(ctx, p->pts, p->n, color, false);
      else subpixel_fill_polygon_ring_fp(ctx, p->pts, p->n, hollow_thickness_fp, color, false);
    }
    else if (p->thin) subpixel_fill_polygon_thin_fp(ctx, p->pts, p->n, color);
    else subpixel_fill_polygon_fp(ctx, p->pts, p->n, color);
  }
  for (int i = 0; i < geo->n_circles; i++) {
    const HandCircle *c = &geo->circles[i];
    if (cfg->hollow && !dithered) {
      if (cfg->hollow_thickness <= 1) subpixel_stroke_circle_fp(ctx, c->center, c->radius_fp, color, false);
      else subpixel_fill_circle_ring_fp(ctx, c->center, c->radius_fp, hollow_thickness_fp, color, false);
    }
    else if (c->thin && !dithered) subpixel_fill_circle_thin_fp(ctx, c->center, c->radius_fp, color);
    else subpixel_fill_circle_fp(ctx, c->center, c->radius_fp, color, dithered);
  }
}


static void draw_hand_outline_from_geometry(GContext *ctx, const HandGeometry *geo,
                                             GColor color, bool dithered) {
  for (int i = 0; i < geo->n_polys; i++) {
    subpixel_stroke_polygon_fp(ctx, geo->polys[i].pts, geo->polys[i].n, color, dithered);
  }
  for (int i = 0; i < geo->n_circles; i++) {
    subpixel_stroke_circle_fp(ctx, geo->circles[i].center, geo->circles[i].radius_fp, color, dithered);
  }
}


// Fill with a selectable Bayer-4 density; 8 is ~50%, 4 is ~25%.
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
      if (subpixel_point_in_convex_polygon_fp(pts, n, subpixel_fgpoint_new(sample_x, sample_y))) {
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


// Draw the hand shadow using the configured shadow angle and density.
static void draw_hand_shadow_once_fp(GContext *ctx, FGPoint center, int32_t angle, const HandConfig *cfg,
                                      bool shadow_translucent_style, uint16_t shadow_angle_deg) {
  if (!cfg->shadow_enabled) return;

  int32_t shadow_native_angle = (int32_t)(((int64_t)shadow_angle_deg * TRIG_MAX_ANGLE) / 360);
  int32_t dist_fp = (int32_t)cfg->shadow_distance_px << SUBPIXEL_BITS;
  int32_t dx = (int32_t)(((int64_t)dist_fp * sin_lookup(shadow_native_angle)) / TRIG_MAX_RATIO);
  int32_t dy = -(int32_t)(((int64_t)dist_fp * cos_lookup(shadow_native_angle)) / TRIG_MAX_RATIO);
  FGPoint shadow_center = subpixel_fgpoint_new(center.x + dx, center.y + dy);

  HandGeometry geo;
  hand_geometry_compute_fp(shadow_center, angle, cfg, &geo);

  if (!shadow_translucent_style) {
    for (int i = 0; i < geo.n_polys; i++) {
      HandPoly *p = &geo.polys[i];
      if (p->thin) subpixel_fill_polygon_thin_fp(ctx, p->pts, p->n, GColorBlack);
      else subpixel_fill_polygon_fp(ctx, p->pts, p->n, GColorBlack);
    }
    for (int i = 0; i < geo.n_circles; i++) {
      HandCircle *c = &geo.circles[i];
      if (c->thin) subpixel_fill_circle_thin_fp(ctx, c->center, c->radius_fp, GColorBlack);
      else subpixel_fill_circle_fp(ctx, c->center, c->radius_fp, GColorBlack, false);
    }
    return;
  }

  uint8_t threshold = cfg->translucent ? 4 : 8; 
  for (int i = 0; i < geo.n_polys; i++) {
    fill_polygon_dithered_level_fp(ctx, geo.polys[i].pts, geo.polys[i].n, GColorBlack, threshold);
  }
  for (int i = 0; i < geo.n_circles; i++) {
    fill_circle_dithered_level_fp(ctx, geo.circles[i].center, geo.circles[i].radius_fp, GColorBlack, threshold);
  }
}

// Compute geometry once and reuse it for shadow, outline, and fill.
void hand_layer_draw(GContext *ctx, GPoint center, int32_t angle, const HandConfig *cfg,
                      GColor main_color, GColor accent_color, GColor bg_color,
                      bool shadow_translucent_style, uint16_t shadow_angle_deg,
                      uint16_t length_scale_1000) {
  
  
  
  
  
  
  HandConfig scaled_cfg;
  if (length_scale_1000 < 1000) {
    scaled_cfg = *cfg;
    scaled_cfg.length = (uint8_t)(((uint32_t)cfg->length * length_scale_1000) / 1000);
    cfg = &scaled_cfg;
  }

  FGPoint center_fp = subpixel_fgpoint_from_gpoint(center);

  draw_hand_shadow_once_fp(ctx, center_fp, angle, cfg, shadow_translucent_style, shadow_angle_deg);

  
  
  
  
  
  
  
  
  
  HandGeometry geo;
  hand_geometry_compute_fp(center_fp, angle, cfg, &geo);

  GColor hand_color = GColorClear; 
  if (cfg->color != 3) {
    hand_color = resolve_scheme_color(cfg->color, main_color, accent_color, bg_color);
  }

  if (cfg->outline_enabled) {
    
    
    
    
    
    
    
    GColor outline_color = resolve_scheme_color(cfg->outline_color, main_color, accent_color, bg_color);
    draw_hand_outline_from_geometry(ctx, &geo, outline_color, cfg->translucent);
  }

  if (cfg->color != 3) { 
    draw_hand_shape_from_geometry(ctx, &geo, cfg, hand_color, cfg->translucent);
  }
}

void hand_layer_draw_center_circle(GContext *ctx, GPoint center, uint8_t radius, uint8_t color_choice,
                                    GColor main_color, GColor accent_color, GColor bg_color) {
  if (radius == 0) return;
  GColor color = resolve_scheme_color(color_choice, main_color, accent_color, bg_color);
  FGPoint center_fp = subpixel_fgpoint_from_gpoint(center);
  subpixel_fill_circle_fp(ctx, center_fp, (int32_t)radius << SUBPIXEL_BITS, color, false);
}
