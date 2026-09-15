#include "./feature_icons.h"
#include "./feature_icon_assets.h"
#include "./feature_weather_icons.h"
#include "./feature_vector_icons.h"
#include "./feature_render.h"
#include "../data/eclipse_ui.h"
#include "../rendering/background/celestial_layer.h"

#define ICON_WIDTH 16
#define ICON_ROWS 12
#define SUN_TIME_ICON_WIDTH 20
#define SUN_TIME_ICON_ROWS 10

// Optional debug crosshair for icon anchor points and geometry.
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





void feature_icons_draw_render_icon(GContext *ctx, uint8_t icon_kind, int16_t icon_extra,
                                    bool icon_flag, GColor color, GColor color2,
                                    int16_t icon_x, int16_t box_y, int16_t row_height,
                                    uint8_t outline_style, uint8_t weather_icon_style, bool draw_debug) {
  GColor outline_color = feature_render_contrasting_outline_color(color);
  bool do_outline = outline_style != 0;
  const GPoint *offs = NULL; int offs_n = 0;
  if (do_outline) feature_icon_assets_get_outline_offsets(outline_style, &offs, &offs_n);
  draw_debug_marker_point(ctx, draw_debug, GPoint(icon_x, box_y), GColorRed);
  for (size_t i = 0; i < sizeof(SIMPLE_ICONS) / sizeof(SIMPLE_ICONS[0]); i++) {
    if (SIMPLE_ICONS[i].kind != icon_kind) continue;
    GPoint pos = GPoint(icon_x - ICON_WIDTH + SIMPLE_ICONS[i].x_nudge, box_y + (row_height - ICON_ROWS) / 2);
    draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
    feature_icon_assets_draw_resource_with_outline(ctx, pos, SIMPLE_ICONS[i].resource_id, outline_style, outline_color, color);
    return;
  }

  switch (icon_kind) {
    case 3: { // battery
      GPoint pos = GPoint(icon_x, box_y + (row_height - 14) / 2);
      GColor c = icon_flag ? GColorGreen : color; // charging state uses green
      GColor oc = icon_flag ? GColorBlack : outline_color;
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          feature_vector_icons_battery(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), oc, 0);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_vector_icons_battery(ctx, pos, c, icon_extra);
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
               // same way every other bitmap corner icon is (feature_icon_assets_draw_resource_
               // with_outline, just at this glyph's own wider/shorter size)
               // instead of being hand-drawn with fill primitives every frame.
      GPoint pos = GPoint(icon_x, box_y + (row_height - SUN_TIME_ICON_ROWS) / 2);
      uint32_t sun_time_resource = icon_flag ? RESOURCE_ID_ICON_SUN_TIME_RISE : RESOURCE_ID_ICON_SUN_TIME_SET;
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_icon_assets_draw_resource_with_outline_sized(ctx, pos, sun_time_resource, outline_style, outline_color, color,
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
          feature_icon_assets_draw_tiny(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), PEBBLE_ICON, 10, 40, oc);
        }
        graphics_context_set_stroke_color(ctx, oc);
        graphics_draw_line(ctx, GPoint(p1.x, p1.y + 1), GPoint(p2.x, p2.y + 1));
        graphics_draw_line(ctx, GPoint(p1.x - 1, p1.y), GPoint(p2.x + 1, p2.y));
        graphics_draw_line(ctx, GPoint(p1.x, p1.y - 1), GPoint(p2.x, p2.y - 1));
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_icon_assets_draw_tiny(ctx, pos, PEBBLE_ICON, 10, 40, c);
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
          feature_weather_icons_draw(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), category, outline_icon_style, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_weather_icons_draw(ctx, pos, category, weather_icon_style, color);
      return;
    }
    case 15: { // pressure trend chevron
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          feature_vector_icons_pressure_trend(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), (uint8_t)icon_extra, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_vector_icons_pressure_trend(ctx, pos, (uint8_t)icon_extra, color);
      return;
    }
    case 16: { // wind direction arrow
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          feature_vector_icons_wind_direction(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), icon_extra, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_vector_icons_wind_direction(ctx, pos, icon_extra, color);
      return;
    }
    case 17: { // altitude mountain glyph
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          feature_vector_icons_mountain(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_vector_icons_mountain(ctx, pos, color);
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
      feature_icon_assets_draw_resource_with_outline(ctx, pos, RESOURCE_ID_ICON_BLUETOOTH, outline_style, outline_color, color);
      return;
    }
    case 27: { // compass -- asleep (Zz glyph) or a live heading rose with a distinct north arrow
      GPoint pos = GPoint(icon_x, box_y + (row_height - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          GPoint shifted = GPoint(pos.x + offs[i].x, pos.y + offs[i].y);
          if (icon_flag) feature_vector_icons_compass_sleep(ctx, shifted, outline_color);
          else feature_vector_icons_compass(ctx, shifted, icon_extra, outline_color, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      if (icon_flag) feature_vector_icons_compass_sleep(ctx, pos, color);
      else feature_vector_icons_compass(ctx, pos, icon_extra, color, color2);
      return;
    }
    case 29: { // Quiet Time -- speaker / crossed-out speaker (icon_flag: true = active/muted).
               // Exported to a real image (see resources/images/icon_quiet_time.png /
               // icon_quiet_time_muted.png) and drawn the exact same way every other
               // bitmap corner icon is (feature_icon_assets_draw_resource_with_outline, standard
               // ICON_WIDTH x ICON_ROWS size) -- was hand-drawn with fill/gpath
               // primitives every frame; a plain resource lookup + the shared tinted-
               // bitmap draw already every other icon_kind here reuses costs
               // meaningfully less code than that custom drawing function did.
      GPoint pos = GPoint(icon_x, box_y + (row_height - ICON_ROWS) / 2);
      uint32_t quiet_time_resource = icon_flag ? RESOURCE_ID_ICON_QUIET_TIME_MUTED : RESOURCE_ID_ICON_QUIET_TIME;
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_icon_assets_draw_resource_with_outline(ctx, pos, quiet_time_resource, outline_style, outline_color, color);
      return;
    }
    case 30: { // Hourly Vibrations -- watch+buzz / crossed-out (icon_flag: true = off/crossed).
               // Same "exported to a real image" treatment as Quiet Time above (see
               // resources/images/icon_hourly_vibe.png / icon_hourly_vibe_off.png).
      GPoint pos = GPoint(icon_x, box_y + (row_height - ICON_ROWS) / 2);
      uint32_t hourly_vibe_resource = icon_flag ? RESOURCE_ID_ICON_HOURLY_VIBE_OFF : RESOURCE_ID_ICON_HOURLY_VIBE;
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      feature_icon_assets_draw_resource_with_outline(ctx, pos, hourly_vibe_resource, outline_style, outline_color, color);
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



// Widths include the fixed 5px gap after the icon. Keeping this as a compact
// lookup avoids a second large switch in the feature measurement path.
static const uint8_t s_icon_plus_gap_width[31] = {
  [1] = 15, [2] = 15, [3] = 13, [4] = 21,
  [5] = 15, [6] = 15, [7] = 15, [8] = 15, [9] = 15, [10] = 15,
  [11] = 22, [13] = 15, [14] = 20, [15] = 12, [16] = 14, [17] = 20,
  [18] = 15, [19] = 15, [20] = 15, [21] = 15, [22] = 15, [23] = 15,
  [24] = 15, [25] = 15, [26] = 15, [27] = 21, [28] = 15, [29] = 20, [30] = 20,
};

int16_t feature_icons_plus_gap_width(int icon_kind) {
  if ((unsigned)icon_kind >= 31u) return 0;
  return s_icon_plus_gap_width[icon_kind];
}
