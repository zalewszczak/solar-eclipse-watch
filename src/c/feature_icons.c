#include "feature_icons.h"
#include "eclipse_ui.h"
#include "features_layer.h"
#include "celestial_layer.h"

#define ICON_WIDTH 16
#define ICON_ROWS 12
#define SUN_TIME_ICON_WIDTH 20
#define SUN_TIME_ICON_ROWS 10

// Debug crosshair used to visualize icon anchor points and geometry.
static void draw_debug_marker_point(GContext *ctx, bool draw_debug, GPoint pos, GColor color) {
  if (!draw_debug) return;
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, color);
  graphics_draw_rect(ctx, GRect(pos.x, pos.y - 15, 1, 30));
  graphics_draw_rect(ctx, GRect(pos.x - 15, pos.y, 30, 1));
}

static const uint8_t PEBBLE_ICON[62]     = { 0x00, 0x02, 0x04, 0x08, 0x00, 0x00, 0x02, 0x04, 0x08, 0x00, 0xFD, 0xFB,
  0xF7, 0xE9, 0xF8, 0x85, 0x0A, 0x14, 0x29, 0x08, 0x85, 0x0A, 0x14, 0x29,
  0x08, 0x85, 0xFA, 0x14, 0x29, 0xF8, 0x85, 0x02, 0x14, 0x29, 0x00, 0xFD,
  0xFB, 0xF7, 0xED, 0xF8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
  0x00, 0x00}; 

static const struct { uint8_t kind; uint32_t resource_id; int16_t x_nudge; } SIMPLE_ICONS[] = {
  { 1,  RESOURCE_ID_ICON_HEART,           12 },
  { 2,  RESOURCE_ID_ICON_FOOT,             6 },
  { 5,  RESOURCE_ID_ICON_UMBRELLA,        10 },
  { 6,  RESOURCE_ID_ICON_DROPLET,         10 },
  { 7,  RESOURCE_ID_ICON_WIND,            10 },
  { 8,  RESOURCE_ID_ICON_GPS_PIN,          6 },
  { 9,  RESOURCE_ID_ICON_EYE,              6 },
  { 10, RESOURCE_ID_ICON_CLOUD,            6 },
  { 18, RESOURCE_ID_ICON_BED_ARROW_IN,     6 },
  { 19, RESOURCE_ID_ICON_BED_ARROW_OUT,    6 },
  { 20, RESOURCE_ID_ICON_BED_CHECK,        6 },
  { 21, RESOURCE_ID_ICON_BED_CLOCK,        6 },
  { 22, RESOURCE_ID_ICON_BED_CHECK_CLOCK,  6 },
  { 23, RESOURCE_ID_ICON_PLANETS,          6 },
  { 24, RESOURCE_ID_ICON_SATURN_RING,      6 },
  { 25, RESOURCE_ID_ICON_ISS,              6 },
  { 26, RESOURCE_ID_ICON_AURORA,           6 },
  { 28, RESOURCE_ID_ICON_REFRESH,          6 },
};


static const GPoint OUTLINE_OFFSETS_THIN[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
static const GPoint OUTLINE_OFFSETS_THICK[12] = {
  {-1, 0}, {1, 0}, {0, -1}, {0, 1},
  {-2, 0}, {2, 0}, {0, -2}, {0, 2},
  {1, 1}, {-1, 1}, {-1, -1}, {1, -1},
};
static void get_icon_outline_offsets(uint8_t style, const GPoint **offsets, int *count) {
  if (style >= 2) { *offsets = OUTLINE_OFFSETS_THICK; *count = 12; }
  else { *offsets = OUTLINE_OFFSETS_THIN; *count = 4; }
}

static void draw_tiny_icon(GContext *ctx, GPoint top_left, const uint8_t *pattern, int rows, int width, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int bytes_per_row = (width + 7) / 8;
  for (int row = 0; row < rows; row++) {
    int16_t y0 = top_left.y + row;
    for (int col = 0; col < width; col++) {
      int byte_index = row * bytes_per_row + col / 8;
      int bit_index = 7 - (col % 8);
      if (pattern[byte_index] & (1 << bit_index)) {
        int16_t x0 = top_left.x + col;
        graphics_fill_rect(ctx, GRect(x0, y0, 1, 1), 0, GCornerNone);
      }
    }
  }
}



static void draw_icon_bitmap_tinted_sized(GContext *ctx, GBitmap *bmp, GPoint top_left, GColor color,
                                           int16_t w, int16_t h) {
  GColor *palette = gbitmap_get_palette(bmp);
  if (palette) {
    bool transparent0 = (palette[0].argb & 0xC0) == 0;
    bool transparent1 = (palette[1].argb & 0xC0) == 0;
    int ink;
    if (transparent0 != transparent1) {
      ink = transparent0 ? 1 : 0;
    } else {
      int sum0 = ((palette[0].argb >> 4) & 0x03) + ((palette[0].argb >> 2) & 0x03) + (palette[0].argb & 0x03);
      int sum1 = ((palette[1].argb >> 4) & 0x03) + ((palette[1].argb >> 2) & 0x03) + (palette[1].argb & 0x03);
      ink = (sum0 <= sum1) ? 0 : 1;
    }
    palette[ink] = color;
    palette[1 - ink] = GColorClear;
  }
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, w, h));
}



static void draw_icon_bitmap_tinted(GContext *ctx, GBitmap *bmp, GPoint top_left, GColor color) {
  draw_icon_bitmap_tinted_sized(ctx, bmp, top_left, color, ICON_WIDTH, ICON_ROWS);
}



static void draw_icon_resource(GContext *ctx, GPoint top_left, uint32_t resource_id, GColor color) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  draw_icon_bitmap_tinted(ctx, bmp, top_left, color);
  gbitmap_destroy(bmp);
}



static void draw_icon_resource_with_outline_sized(GContext *ctx, GPoint pos, uint32_t resource_id,
                                                   uint8_t outline_style, GColor outline_color, GColor color,
                                                   int16_t w, int16_t h) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  if (outline_style != 0) {
    const GPoint *offs; int offs_n;
    get_icon_outline_offsets(outline_style, &offs, &offs_n);
    for (int i = 0; i < offs_n; i++) {
      GPoint shifted = GPoint(pos.x + offs[i].x, pos.y + offs[i].y);
      draw_icon_bitmap_tinted_sized(ctx, bmp, shifted, outline_color, w, h);
    }
  }
  draw_icon_bitmap_tinted_sized(ctx, bmp, pos, color, w, h);
  gbitmap_destroy(bmp);
}



static void draw_icon_resource_with_outline(GContext *ctx, GPoint pos, uint32_t resource_id,
                                             uint8_t outline_style, GColor outline_color, GColor color) {
  draw_icon_resource_with_outline_sized(ctx, pos, resource_id, outline_style, outline_color, color,
                                         ICON_WIDTH, ICON_ROWS);
}



static void draw_icon_resource_native(GContext *ctx, GPoint top_left, uint32_t resource_id) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, ICON_WIDTH, ICON_ROWS));
  gbitmap_destroy(bmp);
}



static void draw_corner_battery_icon(GContext *ctx, GPoint top_left, GColor color, int charge) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y, 4, 2), 0, GCornerNone); // nub
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(top_left.x, top_left.y + 2, 8, 12));
  int16_t charge_pixels = (charge == 0) ? 0 : (8 * charge / 100);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y + 4 + (8 - charge_pixels), 4, charge_pixels), 0, GCornerNone); // fill
}



static void draw_weather_icon_hollow(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_SUN, color);
      return;
    case 1: // partly cloudy
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_PARTLY_CLOUDY, color);
      return;
    case 2: // cloudy / overcast
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_CLOUDY_OVERCAST, color);
      return;
    case 3: // fog
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_FOG, color);
      return;
    case 4: // rain
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_RAIN, color);
      return;
    case 5: // snow
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_SNOW, color);
      return;
    case 6: { // storm
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_STORM, color);
      return;
    }
  }
}



static void draw_weather_icon_simple(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_SUN, color);
      return;
    case 1: // partly cloudy
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_PARTLY_CLOUDY, color);
      return;
    case 2: // cloudy / overcast
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_CLOUDY_OVERCAST, color);
      return;
    case 3: // fog
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_FOG, color);
      return;
    case 4: // rain
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_RAIN, color);
      return;
    case 5: // snow
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_SNOW, color);
      return;
    case 6: { // storm
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_STORM, color);
      return;
    }
  }
}



static void draw_weather_icon_filled(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  (void)color;
  switch (category) {
    case 0: // sunny
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SUN);
      return;
    case 1: // partly cloudy
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_PARTLY_CLOUDY);
      return;
    case 2: // cloudy / overcast
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_CLOUDY_OVERCAST);
      return;
    case 3: // fog
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_FOG);
      return;
    case 4: // rain
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_RAIN);
      return;
    case 5: // snow
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SNOW);
      return;
    case 6: { // storm
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_STORM);
      return;
    }
  }
}



static void draw_weather_icon(GContext *ctx, GPoint top_left, uint8_t category, uint8_t style, GColor color) {
  switch (style) {
    case 0: draw_weather_icon_simple(ctx, top_left, category, color); return;
    case 2: draw_weather_icon_filled(ctx, top_left, category, color); return;
    case 1:
    default: draw_weather_icon_hollow(ctx, top_left, category, color); return;
  }
}



static void draw_pressure_trend_icon(GContext *ctx, GPoint top_left, uint8_t trend, GColor color) {
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



static void draw_wind_direction_icon(GContext *ctx, GPoint top_left, int16_t from_deg, GColor color) {
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



static void draw_compass_icon(GContext *ctx, GPoint top_left, int16_t heading_deg,
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



static void draw_compass_sleep_icon(GContext *ctx, GPoint top_left, GColor color) {
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



static void draw_mountain_icon(GContext *ctx, GPoint top_left, GColor color) {
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



void feature_icons_draw_render_icon(GContext *ctx, uint8_t icon_kind, int16_t icon_extra,
                                    bool icon_flag, GColor color, GColor color2,
                                    int16_t icon_x, int16_t box_y, int16_t row_height,
                                    uint8_t outline_style, uint8_t weather_icon_style, bool draw_debug) {
  GColor outline_color = features_contrasting_outline_color(color);
  bool do_outline = outline_style != 0;
  const GPoint *offs = NULL; int offs_n = 0;
  if (do_outline) get_icon_outline_offsets(outline_style, &offs, &offs_n);
  draw_debug_marker_point(ctx, draw_debug, GPoint(icon_x, box_y), GColorRed);
  for (size_t i = 0; i < sizeof(SIMPLE_ICONS) / sizeof(SIMPLE_ICONS[0]); i++) {
    if (SIMPLE_ICONS[i].kind != icon_kind) continue;
    GPoint pos = GPoint(icon_x - ICON_WIDTH + SIMPLE_ICONS[i].x_nudge, box_y + (row_height - ICON_ROWS) / 2);
    draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
    draw_icon_resource_with_outline(ctx, pos, SIMPLE_ICONS[i].resource_id, outline_style, outline_color, color);
    return;
  }

  switch (icon_kind) {
    case 3: { // battery
      GPoint pos = GPoint(icon_x, box_y + (row_height - 14) / 2);
      GColor c = icon_flag ? GColorGreen : color; // charging -> always green, matching the old special case
      GColor oc = icon_flag ? GColorBlack : outline_color;
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_corner_battery_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), oc, 0);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_corner_battery_icon(ctx, pos, c, icon_extra);
      return;
    }
    case 4: { // moon phase
      int16_t moon_r = 9;
      GPoint center = GPoint(icon_x + moon_r, box_y + row_height / 2);
      GRect clip = GRect(icon_x, box_y, moon_r * 2 + 2, row_height);
      if (do_outline) {
        graphics_context_set_fill_color(ctx, outline_color);
        for (int i = 0; i < offs_n; i++) {
          graphics_fill_circle(ctx, GPoint(center.x + offs[i].x, center.y + offs[i].y), moon_r);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, center, GColorMagenta);
      celestial_draw_moon_phase(ctx, clip, center, moon_r, (uint8_t)icon_extra, icon_flag, color);
      return;
    }
    case 11: { // sunrise/sunset glyph -- now a plain image (see resources/images/
               // icon_sun_time_rise.png / icon_sun_time_set.png), drawn the exact
               // same way every other bitmap corner icon is (draw_icon_resource_
               // with_outline, just at this glyph's own wider/shorter size)
               // instead of being hand-drawn with fill primitives every frame.
      GPoint pos = GPoint(icon_x, box_y + (row_height - SUN_TIME_ICON_ROWS) / 2);
      uint32_t sun_time_resource = icon_flag ? RESOURCE_ID_ICON_SUN_TIME_RISE : RESOURCE_ID_ICON_SUN_TIME_SET;
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_icon_resource_with_outline_sized(ctx, pos, sun_time_resource, outline_style, outline_color, color,
                                             SUN_TIME_ICON_WIDTH, SUN_TIME_ICON_ROWS);
      return;
    }
    case 12: { // Pebble battery logo
      GPoint pos = GPoint(icon_x - 15, box_y + (row_height - 10) / 2);
      GColor c = icon_flag ? GColorGreen : color;
      GColor oc = icon_flag ? GColorBlack : outline_color;
      GPoint p1 = GPoint(pos.x + 3, pos.y + 9);
      GPoint p2 = GPoint(pos.x + 3 + (35 * icon_extra / 100), pos.y + 9);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), PEBBLE_ICON, 10, 40, oc);
        }
        graphics_context_set_stroke_color(ctx, oc);
        graphics_draw_line(ctx, GPoint(p1.x, p1.y + 1), GPoint(p2.x, p2.y + 1));
        graphics_draw_line(ctx, GPoint(p1.x - 1, p1.y), GPoint(p2.x + 1, p2.y));
        graphics_draw_line(ctx, GPoint(p1.x, p1.y - 1), GPoint(p2.x, p2.y - 1));
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_tiny_icon(ctx, pos, PEBBLE_ICON, 10, 40, c);
      graphics_context_set_stroke_color(ctx, c);
      graphics_draw_line(ctx, p1, p2);
      return;
    }
    case 14: { // weather condition icon -- style picked in settings (simple/hollow/full color)
      GPoint pos = GPoint(icon_x, box_y + (row_height - ICON_ROWS) / 2 - 2);
      uint8_t category = (uint8_t)icon_extra;
      // Full color (style 2) draws its outline pass using style 1
      // (hollow)'s own silhouette instead of its real style --
      // draw_weather_icon_filled() ignores whatever color it's given
      // (it's a true-color+alpha image, no tint to apply), so shifting
      // IT around would just stack identical copies of the same
      // multi-color icon instead of a contrasting silhouette behind
      // it. Hollow's outline shape in outline_color gives it a real
      // outline without needing a second baked asset.
      if (do_outline) {
        uint8_t outline_icon_style = (weather_icon_style == 2) ? 1 : weather_icon_style;
        for (int i = 0; i < offs_n; i++) {
          draw_weather_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), category, outline_icon_style, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_weather_icon(ctx, pos, category, weather_icon_style, color);
      return;
    }
    case 15: { // pressure trend chevron
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_pressure_trend_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), (uint8_t)icon_extra, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_pressure_trend_icon(ctx, pos, (uint8_t)icon_extra, color);
      return;
    }
    case 16: { // wind direction arrow
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_wind_direction_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), icon_extra, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_wind_direction_icon(ctx, pos, icon_extra, color);
      return;
    }
    case 17: { // altitude mountain glyph
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_mountain_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_mountain_icon(ctx, pos, color);
      return;
    }
    case 13: { // bluetooth -- own case rather than the generic SIMPLE_ICONS
               // bucket above: that bucket draws each bitmap right-anchored
               // within its own 16px box (icon_x - ICON_WIDTH + x_nudge),
               // which is fine for a bitmap that's always the FIRST/only
               // segment in its slot, but bluetooth routinely follows
               // another segment (battery icon, a "NN%" text) -- and a
               // right-anchored draw there lands up to (ICON_WIDTH -
               // x_nudge) px to the LEFT of icon_x, i.e. back on top of
               // whatever precedes it, cancelling out that segment's own
               // trailing gap entirely. Left-anchored at icon_x instead,
               // matching every other multi-segment-capable icon kind
               // (battery, moon, compass, ...) below.
      GPoint pos = GPoint(icon_x, box_y + (row_height - ICON_ROWS) / 2);
      draw_icon_resource_with_outline(ctx, pos, RESOURCE_ID_ICON_BLUETOOTH, outline_style, outline_color, color);
      return;
    }
    case 27: { // compass -- asleep (Zz glyph) or a live heading rose with a distinct north arrow
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          GPoint shifted = GPoint(pos.x + offs[i].x, pos.y + offs[i].y);
          if (icon_flag) draw_compass_sleep_icon(ctx, shifted, outline_color);
          else draw_compass_icon(ctx, shifted, icon_extra, outline_color, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      if (icon_flag) draw_compass_sleep_icon(ctx, pos, color);
      else draw_compass_icon(ctx, pos, icon_extra, color, color2);
      return;
    }
    case 29: { // Quiet Time -- speaker / crossed-out speaker (icon_flag: true = active/muted).
               // Exported to a real image (see resources/images/icon_quiet_time.png /
               // icon_quiet_time_muted.png) and drawn the exact same way every other
               // bitmap corner icon is (draw_icon_resource_with_outline, standard
               // ICON_WIDTH x ICON_ROWS size) -- was hand-drawn with fill/gpath
               // primitives every frame; a plain resource lookup + the shared tinted-
               // bitmap draw already every other icon_kind here reuses costs
               // meaningfully less code than that custom drawing function did.
      GPoint pos = GPoint(icon_x, box_y + (row_height - ICON_ROWS) / 2);
      uint32_t quiet_time_resource = icon_flag ? RESOURCE_ID_ICON_QUIET_TIME_MUTED : RESOURCE_ID_ICON_QUIET_TIME;
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_icon_resource_with_outline(ctx, pos, quiet_time_resource, outline_style, outline_color, color);
      return;
    }
    case 30: { // Hourly Vibrations -- watch+buzz / crossed-out (icon_flag: true = off/crossed).
               // Same "exported to a real image" treatment as Quiet Time above (see
               // resources/images/icon_hourly_vibe.png / icon_hourly_vibe_off.png).
      GPoint pos = GPoint(icon_x, box_y + (row_height - ICON_ROWS) / 2);
      uint32_t hourly_vibe_resource = icon_flag ? RESOURCE_ID_ICON_HOURLY_VIBE_OFF : RESOURCE_ID_ICON_HOURLY_VIBE;
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_icon_resource_with_outline(ctx, pos, hourly_vibe_resource, outline_style, outline_color, color);
      return;
    }
    default:
      return;
  }
}



uint8_t feature_icons_weather_category(uint8_t weather_condition, uint8_t cloud_pct) {
  switch (weather_condition) {
    case 1: return 3; // fog
    case 2: return 4; // rain
    case 3: return 5; // snow
    case 4: return 6; // storm
    default:
      if (cloud_pct < 20) return 0; // sunny
      if (cloud_pct < 60) return 1; // partly cloudy
      return 2; // cloudy/overcast
  }
}



int16_t feature_icons_plus_gap_width(int icon_kind) { // TODO: These values need to be reviewed for bt, steps, etc.
  switch (icon_kind) {
    case 1: case 2: case 5: case 6: case 7: case 8: case 9: case 10:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 28:
      return 15; // bitmap icons (10px) + 5px gap
    case 29: case 30:
      return 20;
    case 3: return 13; // battery (8px wide outlined body) + 5px gap
    case 4: return 21; // moon (radius 9, so 2*9+2 diameter box) + gap
    case 11: return 22; // sun-time glyph (fixed 20px, drawn via direct primitives) + gap
    case 13: return 15; // bluetooth (own case now -- see its own comment in draw_render_icon; same ~10px bitmap width as the SIMPLE_ICONS bucket) + 5px gap
    case 14: return 20; // weather icon (16-wide box, worst case a bit wider for the sun's rays) + gap
    case 15: return 12; // pressure trend chevron + gap
    case 16: return 14; // wind direction arrow + gap
    case 17: return 20; // mountain icon (16-wide box) + gap
    case 27: return 21; // compass rose (~16px-wide box, same footprint class as moon/mountain) + 5px gap
    default: return 0; // no icon
  }
}


