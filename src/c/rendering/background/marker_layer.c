#include "./marker_layer.h"
#include "../render_math.h"
#include "./marker_bitmap.h"
#include "./marker_text.h"
#include <string.h>
#include <limits.h>

// Marker renderer


static inline int32_t marker_div_round(int32_t num, int32_t den) {
  if (den == 0) return 0;
  if ((num ^ den) >= 0) return (num + den / 2) / den;
  return (num - den / 2) / den;
}


static int16_t marker_min(int16_t a, int16_t b) { return a < b ? a : b; }
static int16_t marker_max(int16_t a, int16_t b) { return a > b ? a : b; }

// Convert marker reach from 0-100% into Q24.8 screen-relative distance.
static int32_t marker_reach_fp(GRect screen, uint8_t pct) {
  int16_t reach_min = marker_min(screen.size.w / 4, screen.size.h / 4);
  int16_t reach_max = marker_max(screen.size.w / 2, screen.size.h / 2);
  int32_t reach_min_fp = (int32_t)reach_min << SUBPIXEL_BITS;
  int32_t reach_max_fp = (int32_t)reach_max << SUBPIXEL_BITS;
  return reach_min_fp + marker_div_round((reach_max_fp - reach_min_fp) * pct, 100);
}

// Blend circular and screen-proportioned ring geometry according to the
static FGPoint marker_layer_point_on_ring_fp(FGPoint center, GRect screen, int32_t sin_v, int32_t cos_v,
                                 uint8_t pct, uint8_t eccentricity_pct) {
  int16_t screen_hw = screen.size.w / 2, screen_hh = screen.size.h / 2;
  int32_t reach_fp = marker_reach_fp(screen, pct);

  FGPoint circle_pt = subpixel_fgpoint_new(
    center.x + (int32_t)(((int64_t)reach_fp * sin_v) / TRIG_MAX_RATIO),
    center.y - (int32_t)(((int64_t)reach_fp * cos_v) / TRIG_MAX_RATIO));

  if (eccentricity_pct == 0) return circle_pt;

  int16_t reach_max = marker_max(screen_hw, screen_hh);
  int32_t rect_hw_fp = (int32_t)(((int64_t)reach_fp * screen_hw) / reach_max);
  int32_t rect_hh_fp = (int32_t)(((int64_t)reach_fp * screen_hh) / reach_max);

  int32_t adx = sin_v < 0 ? -sin_v : sin_v;
  int32_t ady = cos_v < 0 ? -cos_v : cos_v;
  int32_t t_x = (adx == 0) ? INT32_MAX : (int32_t)(((int64_t)rect_hw_fp * TRIG_MAX_RATIO) / adx);
  int32_t t_y = (ady == 0) ? INT32_MAX : (int32_t)(((int64_t)rect_hh_fp * TRIG_MAX_RATIO) / ady);
  int32_t t = t_x < t_y ? t_x : t_y;

  FGPoint rect_pt = subpixel_fgpoint_new(
    center.x + (int32_t)(((int64_t)t * sin_v) / TRIG_MAX_RATIO),
    center.y - (int32_t)(((int64_t)t * cos_v) / TRIG_MAX_RATIO));

  FGPoint result;
  result.x = circle_pt.x + (int32_t)(((int64_t)(rect_pt.x - circle_pt.x) * eccentricity_pct) / 100);
  result.y = circle_pt.y + (int32_t)(((int64_t)(rect_pt.y - circle_pt.y) * eccentricity_pct) / 100);
  return result;
}

// Thin GPoint-returning wrapper for marker text positioning, which
GPoint marker_layer_point_on_ring(GPoint center, GRect screen, int32_t angle,
                      uint8_t pct, uint8_t eccentricity_pct) {
  int32_t norm_angle = angle & 0xFFFF; // mask to prevent trig table lookup overflow/underflow
  int32_t sin_v = sin_lookup(norm_angle), cos_v = cos_lookup(norm_angle);
  FGPoint center_fp = subpixel_fgpoint_from_gpoint(center);
  return subpixel_fgpoint_to_gpoint(marker_layer_point_on_ring_fp(center_fp, screen, sin_v, cos_v, pct, eccentricity_pct));
}

// Draws one mark as a straight quad from inner to outer, with the
static void draw_ring_mark_fp(GContext *ctx, FGPoint inner, FGPoint outer, int32_t sin_v, int32_t cos_v,
                               int32_t inner_half_thick_fp, int32_t outer_half_thick_fp,
                               uint8_t style, GColor color, bool translucent,
                               uint8_t inner_thickness_px, uint8_t outer_thickness_px) {
  bool thin = inner_thickness_px < 3 || outer_thickness_px < 3;
  if (inner.x == outer.x && inner.y == outer.y) {
// Degenerate zero-length mark (inner/outer border reach configured
    if (thin && !translucent) subpixel_fill_circle_thin_fp(ctx, inner, outer_half_thick_fp, color);
    else subpixel_fill_circle_fp(ctx, inner, outer_half_thick_fp, color, translucent);
    return;
  }

  int32_t inner_dx_w = (int32_t)(((int64_t)inner_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
  int32_t inner_dy_w = (int32_t)(((int64_t)inner_half_thick_fp * sin_v) / TRIG_MAX_RATIO);
  int32_t outer_dx_w = (int32_t)(((int64_t)outer_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
  int32_t outer_dy_w = (int32_t)(((int64_t)outer_half_thick_fp * sin_v) / TRIG_MAX_RATIO);

  FGPoint a = inner, b = outer;
  if (style == 2 || style == 4) { // square/tapered caps -- extend each end along the same
                                    // radial direction outer sits on, by that end's own thickness
    int32_t inner_ex = (int32_t)(((int64_t)inner_half_thick_fp * sin_v) / TRIG_MAX_RATIO);
    int32_t inner_ey = (int32_t)(((int64_t)inner_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
    int32_t outer_ex = (int32_t)(((int64_t)outer_half_thick_fp * sin_v) / TRIG_MAX_RATIO);
    int32_t outer_ey = (int32_t)(((int64_t)outer_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
    a = subpixel_fgpoint_new(inner.x - inner_ex, inner.y + inner_ey);
    b = subpixel_fgpoint_new(outer.x + outer_ex, outer.y - outer_ey);
  }

  FGPoint points[4] = {
    subpixel_fgpoint_new(a.x - inner_dx_w, a.y - inner_dy_w), subpixel_fgpoint_new(a.x + inner_dx_w, a.y + inner_dy_w),
    subpixel_fgpoint_new(b.x + outer_dx_w, b.y + outer_dy_w), subpixel_fgpoint_new(b.x - outer_dx_w, b.y - outer_dy_w),
  };

  if (translucent) {
    subpixel_fill_polygon_dithered_fp(ctx, points, 4, color);
  } else if (thin) {
    subpixel_fill_polygon_thin_fp(ctx, points, 4, color);
  } else {
    subpixel_fill_polygon_fp(ctx, points, 4, color);
  }

  if (style == 0) { // dot caps -- each end's own circle, at that end's own thickness
    if (thin && !translucent) {
      subpixel_fill_circle_thin_fp(ctx, inner, inner_half_thick_fp, color);
      subpixel_fill_circle_thin_fp(ctx, outer, outer_half_thick_fp, color);
    } else {
      subpixel_fill_circle_fp(ctx, inner, inner_half_thick_fp, color, translucent);
      subpixel_fill_circle_fp(ctx, outer, outer_half_thick_fp, color, translucent);
    }
  }
}

// Resolves a MarkerRingConfig's own color choice against the active
static GColor marker_ring_color(uint8_t choice, GColor main_color, GColor accent_color, GColor bg_color) {
  switch (choice) {
    case 1: return accent_color;
    case 2: return bg_color;
    case 0: default: return main_color;
  }
}

// "Animate background on start": each mark staggers in starting from
#define MARKER_ANIM_STAGGER_1000 600 // marks' own starts spread across the first 60% of the duration

static int32_t marker_ease_out_1000(int32_t t) {
  int32_t inv = 1000 - t;
  int64_t inv3 = ((int64_t)inv * inv * inv) / 1000000;
  int32_t r = 1000 - (int32_t)inv3;
  return (r > 1000) ? 1000 : r;
}

int32_t marker_layer_ease_in_1000(int32_t t) {
  int64_t t64 = t;
  int32_t r = (int32_t)((t64 * t64 * t64) / 1000000);
  return (r > 1000) ? 1000 : r;
}

int32_t marker_anim_mark_progress_1000_raw(int mark_index, int marks, int32_t overall_progress_1000) {
  int32_t start_frac = ((int32_t)mark_index * MARKER_ANIM_STAGGER_1000) / marks;
  int32_t duration_frac = 1000 - start_frac;
  if (duration_frac <= 0) return 1000;
  int32_t local = ((overall_progress_1000 - start_frac) * 1000) / duration_frac;
  if (local < 0) local = 0;
  if (local > 1000) local = 1000;
  return local;
}

static void draw_marker_ring(GContext *ctx, GPoint center, GRect screen, const MarkerRingConfig *cfg,
                              int marks, int skip_step, GColor main_color, GColor accent_color, GColor bg_color,
                              bool anim_active, int32_t anim_overall_progress_1000, uint8_t inner_thickness) {
  if (cfg->thickness == 0) return;
  GColor color = marker_ring_color(cfg->color, main_color, accent_color, bg_color);
  uint8_t inner_pct = cfg->inner_border_pct, outer_pct = cfg->outer_border_pct;
  if (outer_pct < inner_pct) outer_pct = inner_pct;

// Same 0.5px-minimum floor hand_geometry_compute_fp() uses for a
  int32_t outer_half_thick_fp = ((int32_t)cfg->thickness << SUBPIXEL_BITS) / 2;
  if (outer_half_thick_fp < SUBPIXEL_HALF) outer_half_thick_fp = SUBPIXEL_HALF;

// Only style 4 (tapered) actually uses inner_thickness -- every other
  int32_t inner_half_thick_fp = outer_half_thick_fp;
  uint8_t inner_thickness_px = cfg->thickness;
  if (cfg->style == 4) {
    inner_half_thick_fp = ((int32_t)inner_thickness << SUBPIXEL_BITS) / 2;
    if (inner_half_thick_fp < SUBPIXEL_HALF) inner_half_thick_fp = SUBPIXEL_HALF;
    inner_thickness_px = inner_thickness;
  }

  FGPoint center_fp = subpixel_fgpoint_from_gpoint(center);

  for (int i = 0; i < marks; i++) {
    if (skip_step > 0 && i % skip_step == 0) continue;

    // Strictly mask with 0xFFFF to fix missing rotated markers
    int32_t angle = (((int32_t)i * TRIG_MAX_ANGLE) / marks) & 0xFFFF;
    int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

    uint8_t use_inner_pct = inner_pct, use_outer_pct = outer_pct;
    uint8_t use_inner_ecc = cfg->inner_eccentricity, use_outer_ecc = cfg->outer_eccentricity;
    if (anim_active) {
      int32_t p = marker_ease_out_1000(marker_anim_mark_progress_1000_raw(i, marks, anim_overall_progress_1000));
      use_inner_pct = (uint8_t)render_math_lerp8(100, inner_pct, p, 1000);
      use_outer_pct = (uint8_t)render_math_lerp8(100, outer_pct, p, 1000);
      use_inner_ecc = (uint8_t)render_math_lerp8(100, cfg->inner_eccentricity, p, 1000);
      use_outer_ecc = (uint8_t)render_math_lerp8(100, cfg->outer_eccentricity, p, 1000);
    }

    FGPoint outer_fp = marker_layer_point_on_ring_fp(center_fp, screen, sin_v, cos_v, use_outer_pct, use_outer_ecc);
    FGPoint inner_fp = marker_layer_point_on_ring_fp(center_fp, screen, sin_v, cos_v, use_inner_pct, use_inner_ecc);
    draw_ring_mark_fp(ctx, inner_fp, outer_fp, sin_v, cos_v, inner_half_thick_fp, outer_half_thick_fp,
                       cfg->style, color, cfg->translucent, inner_thickness_px, cfg->thickness);
  }
}

void marker_layer_inner_reach(uint8_t marker_style, uint8_t *out_pct, uint8_t *out_eccentricity) {
  *out_pct = 65;
  *out_eccentricity = 0;
}

// Marker text's own font is resolved via font_lookup_resolve()

// Converts 1-59 (our only actual range: hour labels 1-12, second labels

// Draws whichever marker style is active (procedural preset, custom, or
void marker_layer_init(MarkerLayerState *state) {
  state->bitmap = NULL;
  state->bitmap_style = 255;
  state->bitmap_tinted = false;
  state->bitmap_tint_color = GColorClear;
  state->bitmap_tint_transparent = false;
  state->text_font_slot = FONT_SLOT_EMPTY;
}

void marker_layer_deinit(MarkerLayerState *state) {
  marker_bitmap_release(&state->bitmap, &state->bitmap_style, &state->bitmap_tinted);
  font_lookup_release(&state->text_font_slot);
}

void marker_layer_draw(GContext *ctx, MarkerLayerState *state, GPoint center, GRect screen,
                              const EclipseData *d, GColor main_color, GColor accent_color, GColor bg_color,
                              bool anim_active, int32_t anim_progress_1000, bool draw_debug) {
  uint8_t marker_style = d->big_analog_marker_style;

  bool is_bitmap_style = marker_style >= 3 && marker_style != 8;

  marker_bitmap_ensure(&state->bitmap, &state->bitmap_style, &state->bitmap_tinted,
                         &state->bitmap_tint_color, &state->bitmap_tint_transparent, marker_style);

  if (is_bitmap_style) {
    marker_bitmap_tint(state->bitmap, &state->bitmap_tinted, &state->bitmap_tint_color,
                       &state->bitmap_tint_transparent, main_color, d->bitmap_marker_transparent);
    marker_bitmap_draw(ctx, state->bitmap, screen, anim_active, anim_progress_1000);
    return;
  }

  const MarkerRingConfig *hour_cfg, *second_cfg;
  MarkerRingConfig hour_preset, second_preset;
  uint8_t hour_inner_thickness, second_inner_thickness;
  hour_cfg = &d->custom_hour_marker;
  second_cfg = &d->custom_second_marker;
  hour_inner_thickness = d->custom_hour_marker_inner_thickness;
  second_inner_thickness = d->custom_second_marker_inner_thickness;

// Second ring first so the hour ring's marks draw on top at shared
  draw_marker_ring(ctx, center, screen, second_cfg, 60, 5, main_color, accent_color, bg_color, false, 0, second_inner_thickness);
  draw_marker_ring(ctx, center, screen, hour_cfg, 12, 0, main_color, accent_color, bg_color, anim_active, anim_progress_1000, hour_inner_thickness);

  marker_text_draw(ctx, center, screen, &state->text_font_slot, &d->marker_text, hour_cfg, second_cfg, main_color, anim_active, anim_progress_1000, draw_debug);
}


