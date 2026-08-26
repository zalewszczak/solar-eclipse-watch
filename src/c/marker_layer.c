#include "marker_layer.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Design notes (read this before touching the geometry below)
//
// "Cached bitmap, blit every tick" -- and NOT a second Pebble Layer:
// Pebble's public SDK has no function that hands you a GContext for an
// arbitrary offscreen GBitmap -- graphics_draw_*() only ever runs inside a
// LayerUpdateProc, against whatever context the system already set up for
// the current frame (confirmed against the Pebble developer docs). So a
// literal "build a Layer once, never redraw it" is not just unnecessary
// here, it wouldn't work: hands_layer_update_proc() (in
// pebble-eclipse-watch.c) already has to repaint hands + markers + date
// together every tick, because it never clears its own background (by
// design, so the sky canvas shows through) -- if the ring weren't
// repainted too, the previous second's hand position would leave a trail
// where it swept over the ring.
//
// So the actual optimization this file provides is: the *geometry* (12
// hour + up to 48 second positions, each with eccentricity-blended inner/
// outer points and a filled capsule/rect rasterized between them) is only
// recomputed when the config or color actually changes. The result is
// rasterized once into a plain GBitmapFormat8Bit buffer via direct pixel
// writes (the same manual technique pebble-eclipse-watch.c already uses
// in tint_marker_bitmap() -- gbitmap_create_blank() needs no GContext).
// Every tick then just does one graphics_draw_bitmap_in_rect() blit,
// exactly like the existing bitmap marker styles (3-7) already do.
//
// Text numerals are the one piece that genuinely can't be cached this
// way -- graphics_draw_text() needs a live GContext, so
// marker_layer_draw_text() runs every tick. It's cheap (at most 12 short
// strings), so this doesn't cost much.
//
// Eccentricity: point_on_ring() blends between a point on a circle of
// radius border_px and a point on a rectangle proportioned to the real
// screen aspect ratio, scaled so its longer half-extent equals border_px.
// eccentricity 0 = pure circle, 100 = pure rectangle. This is an
// approximation (not a true superellipse), chosen because it's cheap
// integer math and looks right at both ends of the slider; the middle
// values are a visual judgment call worth eyeballing on-device.
// ---------------------------------------------------------------------------

typedef enum { CAP_ROUND = 0, CAP_BUTT = 1, CAP_SQUARE = 2 } CapStyle;

static GBitmap *s_ring_bitmap = NULL;
static MarkerRingConfig s_cached_hour;
static MarkerRingConfig s_cached_second;
static GColor s_cached_color;
static bool s_cache_valid = false;

static GFont s_marker_custom_font = NULL;
static uint8_t s_marker_font_loaded_choice = 255; // sentinel: none loaded

// Rough export heights for each font_choice (0-2 system, 3-6 custom),
// used only to size/vertically-center each numeral's text box.
static const uint8_t FONT_HEIGHTS[7] = {14, 16, 20, 12, 12, 14, 19};

// Per-font vertical fine-tune, added to FONT_HEIGHTS when placing the
// text box -- left at 0 deliberately. Tweak these by hand once you can
// see real numerals on-device/in the emulator; index matches font_choice.
static int8_t FONT_Y_OFFSET[7] = {0, 0, 0, 0, 0, 0, 0};

static int16_t mk_min(int16_t a, int16_t b) { return a < b ? a : b; }
static int16_t mk_max(int16_t a, int16_t b) { return a > b ? a : b; }

static int32_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  int32_t x = v, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + v / x) / 2; }
  return x;
}

// Converts a 0-100% "reach" into an actual px distance from center, using
// the SAME half-extent units throughout (both circle and rectangle math in
// point_on_ring() below work in this unit) -- this is what actually keeps
// everything on-screen: 0% = the largest circle that's guaranteed to stay
// fully within the screen at every angle (the shorter half-dimension),
// 100% = the far edge the screen-fitted rectangle reaches along its
// dominant axis (the longer half-dimension). An earlier version of this
// function mixed a half-dimension reach with a full-dimension range and
// the rectangle math independently used a different half-dimension
// constant -- that mismatch could put marks up to 2x too far out,
// including entirely off-screen at high border% + low eccentricity.
// Keeping every quantity in half-extent units from this one place down
// closes that off by construction.
static int16_t marker_reach_px(GRect screen, uint8_t pct) {
  int16_t screen_hw = screen.size.w / 2, screen_hh = screen.size.h / 2;
  int16_t reach_min = mk_min(screen_hw, screen_hh);
  int16_t reach_max = mk_max(screen_hw, screen_hh);
  return reach_min + (int32_t)(reach_max - reach_min) * pct / 100;
}

// Blends a point on a circle of radius marker_reach_px(pct) with a point
// on a screen-proportioned rectangle sized so its longer half-extent
// equals that same reach, by eccentricity_pct (0=circle, 100=rectangle).
// See the design note on marker_reach_px() above for why this stays
// on-screen at eccentricity 100 (the rectangle exactly touches the real
// screen edges at pct=100) but can clip slightly past the left/right (or
// top/bottom) edges in pure-circle mode at high pct -- a circle is
// isotropic, so it can't simultaneously touch both a wider and a taller
// edge without overshooting the narrower one. It never leaves the screen
// ENTIRELY, though: at pct=100 the circle radius equals the longer
// half-dimension, which is still well short of the corner-to-center
// distance, so most of the ring stays visible even in that combination.
static GPoint point_on_ring(GPoint center, GRect screen, int32_t angle,
                             uint8_t pct, uint8_t eccentricity_pct) {
  int16_t screen_hw = screen.size.w / 2, screen_hh = screen.size.h / 2;
  int16_t reach = marker_reach_px(screen, pct);
  int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

  GPoint circle_pt = GPoint(
    center.x + (int32_t)(reach * sin_v) / TRIG_MAX_RATIO,
    center.y - (int32_t)(reach * cos_v) / TRIG_MAX_RATIO);

  if (eccentricity_pct == 0) return circle_pt; // common case, skip the rest

  int16_t reach_max = mk_max(screen_hw, screen_hh); // same constant marker_reach_px() uses
  int32_t rect_hw = ((int32_t)reach * screen_hw) / reach_max;
  int32_t rect_hh = ((int32_t)reach * screen_hh) / reach_max;

  int32_t adx = sin_v < 0 ? -sin_v : sin_v;
  int32_t ady = cos_v < 0 ? -cos_v : cos_v;
  int32_t t_x = (adx == 0) ? INT32_MAX : (rect_hw * TRIG_MAX_RATIO) / adx;
  int32_t t_y = (ady == 0) ? INT32_MAX : (rect_hh * TRIG_MAX_RATIO) / ady;
  int32_t t = t_x < t_y ? t_x : t_y;

  GPoint rect_pt = GPoint(
    center.x + (int32_t)(t * sin_v) / TRIG_MAX_RATIO,
    center.y - (int32_t)(t * cos_v) / TRIG_MAX_RATIO);

  GPoint result;
  result.x = circle_pt.x + ((rect_pt.x - circle_pt.x) * eccentricity_pct) / 100;
  result.y = circle_pt.y + ((rect_pt.y - circle_pt.y) * eccentricity_pct) / 100;
  return result;
}

static int32_t dist_sq(GPoint a, GPoint b) {
  int32_t dx = a.x - b.x, dy = a.y - b.y;
  return dx * dx + dy * dy;
}

static void put_px(uint8_t *data, uint16_t stride, GRect b, int16_t x, int16_t y, uint8_t argb) {
  if (x < 0 || y < 0 || x >= b.size.w || y >= b.size.h) return;
  data[(int32_t)y * stride + x] = argb;
}

// Fills a capsule/rect/line bar from A to B with the given half-thickness
// and cap style, directly into an offscreen GBitmapFormat8Bit buffer.
static void rasterize_bar(uint8_t *data, uint16_t stride, GRect b, GPoint A, GPoint B,
                           int16_t half_thick, CapStyle cap, uint8_t argb) {
//  APP_LOG(APP_LOG_LEVEL_INFO, "rasterizing_bar: A(%d, %d), B(%d, %d)", (int)A.x, (int)A.y, (int)B.x, (int)B.y);
  
  if (half_thick < 1) half_thick = 1;
  int16_t margin = half_thick + 1;
  int16_t minx = mk_max(0, mk_min(A.x, B.x) - margin);
  int16_t maxx = mk_min(b.size.w - 1, mk_max(A.x, B.x) + margin);
  int16_t miny = mk_max(0, mk_min(A.y, B.y) - margin);
  int16_t maxy = mk_min(b.size.h - 1, mk_max(A.y, B.y) + margin);

  int32_t dx = B.x - A.x, dy = B.y - A.y;
  int32_t len_sq = dx * dx + dy * dy;
  int32_t half_sq = (int32_t)half_thick * half_thick;

  if (len_sq == 0) { // degenerate segment -- just a dot/square of half_thick
    for (int16_t y = miny; y <= maxy; y++) {
      for (int16_t x = minx; x <= maxx; x++) {
        GPoint p = GPoint(x, y);
        int32_t d2 = dist_sq(p, A);
        bool in_shape = (cap == CAP_ROUND) ? (d2 <= half_sq)
          : (x >= A.x - half_thick && x <= A.x + half_thick
             && y >= A.y - half_thick && y <= A.y + half_thick);
        if (in_shape) put_px(data, stride, b, x, y, argb);
      }
    }
    return;
  }

  int32_t len = isqrt32(len_sq);

  for (int16_t y = miny; y <= maxy; y++) {
    for (int16_t x = minx; x <= maxx; x++) {
      int32_t px = x - A.x, py = y - A.y;
      int32_t t_num = px * dx + py * dy; // projection onto AB, scaled by len_sq

      if (cap == CAP_ROUND) {
        // Distance to the closest point on the clamped segment -- this
        // alone produces correct round-capped capsule + straight body.
        int32_t t_clamped = t_num < 0 ? 0 : (t_num > len_sq ? len_sq : t_num);
        GPoint closest = GPoint(
          A.x + (int32_t)((int64_t)dx * t_clamped / len_sq),
          A.y + (int32_t)((int64_t)dy * t_clamped / len_sq));
        if (dist_sq(GPoint(x, y), closest) <= half_sq) put_px(data, stride, b, x, y, argb);
        continue;
      }

      int32_t t_lo = 0, t_hi = len_sq;
      if (cap == CAP_SQUARE) { t_lo = -half_thick * len; t_hi = len_sq + half_thick * len; }
      if (t_num < t_lo || t_num > t_hi) continue;

      int64_t s_num = (int64_t)px * dy - (int64_t)py * dx; // perpendicular * len_sq's sqrt
      int64_t perp_sq_times_lensq = s_num * s_num; // == perp_dist^2 * len_sq
      if (perp_sq_times_lensq <= (int64_t)half_sq * len_sq) {
        put_px(data, stride, b, x, y, argb);
      }
    }
  }
}

static void rasterize_ring(uint8_t *data, uint16_t stride, GRect b, GPoint center,
                            const MarkerRingConfig *cfg, int marks, int skip_step) {
  
//  APP_LOG(APP_LOG_LEVEL_INFO, "rasterize_ring: marks:%d, skip_step:%d, stride:%d", marks, skip_step, stride);
  
  if (cfg->thickness == 0) return;
  uint8_t inner_pct = cfg->inner_border_pct, outer_pct = cfg->outer_border_pct;
  if (outer_pct < inner_pct) outer_pct = inner_pct; // defensive, see header

  CapStyle cap = (cfg->style == 0) ? CAP_ROUND : (cfg->style == 2) ? CAP_SQUARE : CAP_BUTT;
  uint8_t argb = 0xC0 | (s_cached_color.argb & 0x3F); // fully opaque, cached ring color
  int16_t half_thick = cfg->thickness / 2;

  for (int i = 0; i < marks; i++) {
    if (skip_step > 0 && i % skip_step == 0) continue; // that slot belongs to the other ring
    int32_t angle = ((int32_t)i * TRIG_MAX_ANGLE) / marks;

    // The mark is drawn directly between its inner and outer border
    // points -- no separate length setting; eccentricity changing how
    // far apart these two points are (more so near the "corners" as the
    // shape bends from circle towards rectangle) is what gives each mark
    // its length.
    GPoint outer_pt = point_on_ring(center, b, angle, outer_pct, cfg->outer_eccentricity);
    GPoint inner_pt = point_on_ring(center, b, angle, inner_pct, cfg->inner_eccentricity);

    rasterize_bar(data, stride, b, outer_pt, inner_pt, half_thick, cap, argb);
  }
}

void marker_layer_ensure_ring_cache(GRect bounds, const MarkerRingConfig *hour_cfg,
                                     const MarkerRingConfig *second_cfg, GColor color) {
  
//  APP_LOG(APP_LOG_LEVEL_INFO, "m_l_e_r_c: hour_cfg(s:%d, t:%d, i_e:%d, o_e:%d, i_b_p:%d, o_b_p:%d)", (int)hour_cfg->style, (int)hour_cfg->thickness, (int)hour_cfg->inner_eccentricity, (int)hour_cfg->outer_eccentricity, (int)hour_cfg->inner_border_pct, (int)hour_cfg->outer_border_pct);
  
  bool same = s_cache_valid && s_ring_bitmap
    && memcmp(&s_cached_hour, hour_cfg, sizeof(MarkerRingConfig)) == 0
    && memcmp(&s_cached_second, second_cfg, sizeof(MarkerRingConfig)) == 0
    && s_cached_color.argb == color.argb;
  if (same) return;
//  APP_LOG(APP_LOG_LEVEL_INFO, "m_l_e_r_c: s_cache_valid true");
  if (s_ring_bitmap) { gbitmap_destroy(s_ring_bitmap); s_ring_bitmap = NULL; }
  s_ring_bitmap = gbitmap_create_blank(bounds.size, GBitmapFormat8Bit);
  if (!s_ring_bitmap) { s_cache_valid = false; return; }
  
//  APP_LOG(APP_LOG_LEVEL_INFO, "m_l_e_r_c: s_ring_bitmap valid");
  uint8_t *data = gbitmap_get_data(s_ring_bitmap);
  uint16_t stride = gbitmap_get_bytes_per_row(s_ring_bitmap);
  GRect b = gbitmap_get_bounds(s_ring_bitmap);
  memset(data, 0, (size_t)stride * b.size.h); // argb 0x00 == fully transparent everywhere
  
//  APP_LOG(APP_LOG_LEVEL_INFO, "m_l_e_r_c: memset done");
  s_cached_hour = *hour_cfg;
  s_cached_second = *second_cfg;
  s_cached_color = color;
  s_cache_valid = true;

  GPoint center = GPoint(b.size.w / 2, b.size.h / 2);
  // Second ring first so the hour ring's marks draw on top at shared
  // 12-o'clock-aligned slots (matches draw_procedural_markers' precedent).
  rasterize_ring(data, stride, b, center, second_cfg, 60, 5);
  rasterize_ring(data, stride, b, center, hour_cfg, 12, 0);
}

void marker_layer_draw_ring(GContext *ctx, GRect bounds) {
  if (!s_ring_bitmap) return;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_ring_bitmap, bounds);
}

static uint32_t marker_text_font_resource_id(uint8_t choice) {
  switch (choice) {
    case 3: return RESOURCE_ID_DIGITALDREAM_FONT_12;
    case 4: return RESOURCE_ID_MINECRAFTER_FONT_12;
    case 5: return RESOURCE_ID_SFPIXELATE_FONT_14;
    case 6: return RESOURCE_ID_MISO_FONT_19;
    default: return 0;
  }
}

static GFont get_marker_text_font(uint8_t choice) {
  if (choice != s_marker_font_loaded_choice) {
    if (s_marker_custom_font) { fonts_unload_custom_font(s_marker_custom_font); s_marker_custom_font = NULL; }
    uint32_t res_id = marker_text_font_resource_id(choice);
    if (res_id != 0) s_marker_custom_font = fonts_load_custom_font(resource_get_handle(res_id));
    s_marker_font_loaded_choice = choice;
  }
  if (s_marker_custom_font) return s_marker_custom_font;
  if (choice == 2) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (choice == 1) return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  return fonts_get_system_font(FONT_KEY_GOTHIC_14); // 0, and fallback for an unrecognized choice
}

void marker_layer_draw_text(GContext *ctx, GRect bounds, const MarkerTextConfig *text_cfg,
                             const MarkerRingConfig *hour_cfg, const MarkerRingConfig *second_cfg,
                             GColor color) {
  if (text_cfg->target == 0) return;
  bool is_hour = (text_cfg->target == 1);
  const MarkerRingConfig *ring = is_hour ? hour_cfg : second_cfg;
  uint16_t mask = is_hour ? text_cfg->hour_mask : text_cfg->second_mask;
  if (mask == 0) return;

  GFont font = get_marker_text_font(text_cfg->font_choice);
  int16_t fh = FONT_HEIGHTS[text_cfg->font_choice] + FONT_Y_OFFSET[text_cfg->font_choice];
  GPoint center = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + bounds.size.h / 2);

  graphics_context_set_text_color(ctx, color);

  for (int i = 0; i < 12; i++) {
    if (!(mask & (1 << i))) continue;
    int32_t angle = ((int32_t)i * TRIG_MAX_ANGLE) / 12;
    GPoint base = point_on_ring(center, bounds, angle, ring->outer_border_pct, ring->outer_eccentricity);

    int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);
    GPoint pos = GPoint(
      base.x + (int32_t)(text_cfg->offset_px * sin_v) / TRIG_MAX_RATIO,
      base.y - (int32_t)(text_cfg->offset_px * cos_v) / TRIG_MAX_RATIO);

    char buf[3];
    int label = is_hour ? (i == 0 ? 12 : i) : (i * 5);
    snprintf(buf, sizeof(buf), "%d", label);

    int16_t box_w = 26, box_h = fh + 4;
    GRect box = GRect(pos.x - box_w / 2, pos.y - box_h / 2, box_w, box_h);
    graphics_draw_text(ctx, buf, font, box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

void marker_layer_deinit(void) {
  if (s_ring_bitmap) { gbitmap_destroy(s_ring_bitmap); s_ring_bitmap = NULL; }
  if (s_marker_custom_font) { fonts_unload_custom_font(s_marker_custom_font); s_marker_custom_font = NULL; }
  s_marker_font_loaded_choice = 255;
  s_cache_valid = false;
}
