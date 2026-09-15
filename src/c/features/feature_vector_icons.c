#include "./feature_vector_icons.h"

void feature_vector_icons_battery(GContext *ctx, GPoint top_left, GColor color, int charge) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y, 4, 2), 0, GCornerNone); // nub
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(top_left.x, top_left.y + 2, 8, 12));
  int16_t charge_pixels = (charge == 0) ? 0 : (8 * charge / 100);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y + 4 + (8 - charge_pixels), 4, charge_pixels), 0, GCornerNone); // fill
}



void feature_vector_icons_pressure_trend(GContext *ctx, GPoint top_left, uint8_t trend, GColor color) {
  GPoint center = GPoint(top_left.x + 5, top_left.y + 6);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  if (trend == 1) { // rising
    graphics_draw_line(ctx, GPoint(center.x, center.y + 5), GPoint(center.x, center.y - 5));
    graphics_draw_line(ctx, GPoint(center.x, center.y - 5), GPoint(center.x - 3, center.y - 2));
    graphics_draw_line(ctx, GPoint(center.x, center.y - 5), GPoint(center.x + 3, center.y - 2));
  } else if (trend == 2) { // falling
    graphics_draw_line(ctx, GPoint(center.x, center.y - 5), GPoint(center.x, center.y + 5));
    graphics_draw_line(ctx, GPoint(center.x, center.y + 5), GPoint(center.x - 3, center.y + 2));
    graphics_draw_line(ctx, GPoint(center.x, center.y + 5), GPoint(center.x + 3, center.y + 2));
  } else { // flat
    graphics_draw_line(ctx, GPoint(center.x - 5, center.y), GPoint(center.x + 5, center.y));
  }
}



void feature_vector_icons_wind_direction(GContext *ctx, GPoint top_left, int16_t from_deg, GColor color) {
  GPoint center = GPoint(top_left.x + 6, top_left.y + 6);
  int32_t angle = (int32_t)(((from_deg + 180) % 360) * TRIG_MAX_ANGLE) / 360;
  int16_t len = 6;
  GPoint tip = GPoint(center.x + (len * sin_lookup(angle)) / TRIG_MAX_RATIO,
                       center.y - (len * cos_lookup(angle)) / TRIG_MAX_RATIO);
  GPoint tail = GPoint(center.x - (len * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y + (len * cos_lookup(angle)) / TRIG_MAX_RATIO);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, tail, tip);
  int32_t back_angle1 = angle + (TRIG_MAX_ANGLE * 150) / 360;
  int32_t back_angle2 = angle - (TRIG_MAX_ANGLE * 150) / 360;
  GPoint h1 = GPoint(tip.x + (4 * sin_lookup(back_angle1)) / TRIG_MAX_RATIO, tip.y - (4 * cos_lookup(back_angle1)) / TRIG_MAX_RATIO);
  GPoint h2 = GPoint(tip.x + (4 * sin_lookup(back_angle2)) / TRIG_MAX_RATIO, tip.y - (4 * cos_lookup(back_angle2)) / TRIG_MAX_RATIO);
  graphics_draw_line(ctx, tip, h1);
  graphics_draw_line(ctx, tip, h2);
}



void feature_vector_icons_compass(GContext *ctx, GPoint top_left, int16_t heading_deg,
                               GColor north_color, GColor other_color) {
  GPoint center = GPoint(top_left.x + 6, top_left.y + 6);
  int32_t north_angle = (int32_t)((((360 - (heading_deg % 360)) % 360) * TRIG_MAX_ANGLE)) / 360;

  for (int k = 0; k < 4; k++) {
    int32_t angle = north_angle + (int32_t)((int64_t)k * TRIG_MAX_ANGLE / 4);
    bool is_north = (k == 0);
    int16_t len = is_north ? 6 : 4;
    GColor color = is_north ? north_color : other_color;
    GPoint tip = GPoint(center.x + (len * sin_lookup(angle)) / TRIG_MAX_RATIO,
                         center.y - (len * cos_lookup(angle)) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, center, tip);
    int32_t head_len = is_north ? 3 : 2;
    int32_t back_angle1 = angle + (TRIG_MAX_ANGLE * 150) / 360;
    int32_t back_angle2 = angle - (TRIG_MAX_ANGLE * 150) / 360;
    GPoint h1 = GPoint(tip.x + (head_len * sin_lookup(back_angle1)) / TRIG_MAX_RATIO, tip.y - (head_len * cos_lookup(back_angle1)) / TRIG_MAX_RATIO);
    GPoint h2 = GPoint(tip.x + (head_len * sin_lookup(back_angle2)) / TRIG_MAX_RATIO, tip.y - (head_len * cos_lookup(back_angle2)) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, tip, h1);
    graphics_draw_line(ctx, tip, h2);
  }
}



void feature_vector_icons_compass_sleep(GContext *ctx, GPoint top_left, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  // Big Z, roughly 7x7, upper-left of the icon box.
  GPoint bz[4] = { GPoint(top_left.x, top_left.y), GPoint(top_left.x + 6, top_left.y),
                    GPoint(top_left.x, top_left.y + 6), GPoint(top_left.x + 6, top_left.y + 6) };
  graphics_draw_line(ctx, bz[0], bz[1]);
  graphics_draw_line(ctx, bz[1], bz[2]);
  graphics_draw_line(ctx, bz[2], bz[3]);
  // Small z, roughly 4x4, lower-right, overlapping the big one's tail like a real "Zz" sleep glyph.
  GPoint sz_origin = GPoint(top_left.x + 5, top_left.y + 6);
  GPoint sz[4] = { sz_origin, GPoint(sz_origin.x + 4, sz_origin.y), GPoint(sz_origin.x, sz_origin.y + 4), GPoint(sz_origin.x + 4, sz_origin.y + 4) };
  graphics_draw_line(ctx, sz[0], sz[1]);
  graphics_draw_line(ctx, sz[1], sz[2]);
  graphics_draw_line(ctx, sz[2], sz[3]);
}



void feature_vector_icons_mountain(GContext *ctx, GPoint top_left, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  GPoint peak1[3] = {
    GPoint(top_left.x + 4, top_left.y + 1),
    GPoint(top_left.x, top_left.y + 11),
    GPoint(top_left.x + 9, top_left.y + 11),
  };
  GPathInfo info1 = { .num_points = 3, .points = peak1 };
  GPath *path1 = gpath_create(&info1);
  gpath_draw_filled(ctx, path1);
  gpath_destroy(path1);

  GPoint peak2[3] = {
    GPoint(top_left.x + 11, top_left.y + 4),
    GPoint(top_left.x + 6, top_left.y + 11),
    GPoint(top_left.x + 15, top_left.y + 11),
  };
  GPathInfo info2 = { .num_points = 3, .points = peak2 };
  GPath *path2 = gpath_create(&info2);
  gpath_draw_filled(ctx, path2);
  gpath_destroy(path2);
}

