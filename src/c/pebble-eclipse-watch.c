#include <pebble.h>
#include "eclipse_data.h"
#include "eclipse_layer.h"

// EclipseData is well past Pebble's 256-byte-per-key persist limit
// (PERSIST_DATA_MAX_LENGTH), so it's split across several keys here
// rather than written as one blob -- see save_data()/load_data().
#define PERSIST_KEY_DATA_BASE 1
#define PERSIST_CHUNK_SIZE 200
#define PERSIST_CHUNK_COUNT 5 // 1000 bytes of capacity; struct size is checked at compile
                                // time below (persist_capacity_check) -- see that comment
                                // if it ever fails to compile after adding fields.

static Window *s_window;
static Layer *s_countdown_layer; // custom-drawn (not TextLayer) so it can draw the 1px outline
static GColor s_countdown_text_color;
static Layer *s_canvas_layer;
static Layer *s_bottom_layer; // digital/analog mode only
static Layer *s_hands_layer;  // big-analogue mode only
static Layer *s_corners_layer; // always present -- overlays the sky canvas's own bounds
static uint8_t s_current_layout_style = 255; // sentinel: forces initial layout setup

// True while the shake-to-reveal ground bar is up (mirrors
// eclipse_layer.c's own private show_labels state, tracked
// separately here since main.c already owns the shake timer and the
// corners layer needs this to shift its bottom corners up out of the
// bar's way).
static bool s_labels_visible = false;

// Defined near window_load below; forward-declared here since
// inbox_received_handler (which needs to call it on a mode switch)
// appears earlier in this file.
static void apply_layout(void);
static void apply_clock_font(void);

static char s_countdown_buf[40];

static EclipseData s_data;

// Declare a file-scope variable
static GFont clock_font;
static bool clock_font_custom = false;
static GFont small_font;
static bool small_font_custom = false;

// static declarations:
static int get_small_font_height_offset(void);
static int get_clock_font_height_offset(void);
static bool use_small_seconds_for_digital_clock(void);

// ---- color schemes ------------------------------------------------------

// Built at runtime via GColorFromRGB rather than named palette
// constants -- guaranteed valid regardless of exact Pebble color name
// availability, same pattern already used safely for the sky
// gradient in eclipse_layer.c.
static void get_color_scheme(uint8_t id, GColor *bg, GColor *text, GColor *accent) {
  switch (id) {
    case 1: // White on Black
      *bg = GColorBlack; *text = GColorWhite; *accent = GColorWhite;
      break;
    case 2: // Red on Black
      *bg = GColorBlack; *text = GColorFromRGB(255, 0, 0); *accent = GColorFromRGB(255, 0, 0);
      break;
    case 3: // White on Dark Blue
      *bg = GColorFromRGB(0, 0, 60); *text = GColorWhite; *accent = GColorWhite;
      break;
    case 4: // Yellow on Dark Blue
      *bg = GColorFromRGB(0, 0, 60); *text = GColorYellow; *accent = GColorYellow;
      break;
    case 5: // White on Black, Red accent
      *bg = GColorBlack; *text = GColorWhite; *accent = GColorFromRGB(255, 0, 0);
      break;
    case 6: // Black on White, Dark Red accent
      *bg = GColorWhite; *text = GColorBlack; *accent = GColorFromRGB(139, 0, 0);
      break;
    case 7: // Black on White, Dark Blue accent
      *bg = GColorWhite; *text = GColorBlack; *accent = GColorFromRGB(0, 0, 139);
      break;
    case 8: // Red on Black, White accent
      *bg = GColorBlack; *text = GColorFromRGB(255, 0, 0); *accent = GColorWhite;
      break;
    case 9: // Red on White, Orange accent
      *bg = GColorWhite; *text = GColorFromRGB(255, 0, 0); *accent = GColorFromRGB(255, 140, 0);
      break;
    case 11: // Brown on Green, Orange accent (10 is reserved for "custom", see resolve_color_scheme)
      *bg = GColorFromRGB(34, 139, 34); *text = GColorFromRGB(139, 69, 19); *accent = GColorFromRGB(255, 140, 0);
      break;
    case 0: // Black on White (default)
    default:
      *bg = GColorWhite; *text = GColorBlack; *accent = GColorBlack;
      break;
  }
}

// A GColor is just a packed byte (2 bits each of alpha/r/g/b) under
// the hood -- reconstructing one from a raw byte the settings page
// sent is exactly how the "pick any of the 64 real display colors"
// picker round-trips: the phone sends back whichever of the 64 the
// user tapped, packed the same way, and this just re-wraps it.
static GColor gcolor_from_packed(uint8_t packed) {
  GColor c;
  c.argb = packed;
  return c;
}

// scheme_id 10 means "use the three custom_* packed colors instead
// of a preset"; anything else falls through to get_color_scheme().
// Used for both the day and (independently) the night scheme, so it
// takes its inputs explicitly rather than reading s_data directly.
static void resolve_color_scheme(uint8_t scheme_id, uint8_t custom_bg, uint8_t custom_text, uint8_t custom_accent,
                                   GColor *bg, GColor *text, GColor *accent) {
  if (scheme_id == 10) {
    *bg = gcolor_from_packed(custom_bg);
    *text = gcolor_from_packed(custom_text);
    *accent = gcolor_from_packed(custom_accent);
  } else {
    get_color_scheme(scheme_id, bg, text, accent);
  }
}

// Picks the day or night scheme based on the Sun's altitude (reusing
// eclipse_sky_is_bright()'s existing civil-twilight threshold rather
// than a second definition of "night") -- falls back to the day
// scheme entirely if the user hasn't turned on a separate night one.
static void get_active_color_scheme(time_t now, GColor *bg, GColor *text, GColor *accent) {
  bool night = s_data.night_scheme_enabled && !eclipse_sky_is_bright(&s_data, now);
  if (night) {
    resolve_color_scheme(s_data.night_color_scheme, s_data.night_custom_bg,
                          s_data.night_custom_text, s_data.night_custom_accent, bg, text, accent);
  } else {
    resolve_color_scheme(s_data.color_scheme, s_data.custom_bg,
                          s_data.custom_text, s_data.custom_accent, bg, text, accent);
  }
}

// ---- analog clock styling ------------------------------------------------

// A minimal 3x5-pixel digit font, drawn procedurally rather than
// loaded as a resource -- "super tiny" is the whole point, smaller
// than any real system font renders legibly. Each row is 3 bits
// (left to right); only the digits needed for a 12/3/6/9 dial are
// used, but the full 0-9 set costs nothing extra to keep complete.
static const uint8_t TINY_DIGITS[10][5] = {
  { 0x7, 0x5, 0x5, 0x5, 0x7 }, // 0
  { 0x2, 0x6, 0x2, 0x2, 0x7 }, // 1
  { 0x7, 0x1, 0x7, 0x4, 0x7 }, // 2
  { 0x7, 0x1, 0x7, 0x1, 0x7 }, // 3
  { 0x5, 0x5, 0x7, 0x1, 0x1 }, // 4
  { 0x7, 0x4, 0x7, 0x1, 0x7 }, // 5
  { 0x7, 0x4, 0x7, 0x5, 0x7 }, // 6
  { 0x7, 0x1, 0x1, 0x1, 0x1 }, // 7
  { 0x7, 0x5, 0x7, 0x5, 0x7 }, // 8
  { 0x7, 0x5, 0x7, 0x1, 0x7 }, // 9
};

static void draw_tiny_digit(GContext *ctx, GPoint top_left, int digit, GColor color, int16_t px) {
  if (digit < 0 || digit > 9) return;
  graphics_context_set_fill_color(ctx, color);
  for (int row = 0; row < 5; row++) {
    uint8_t bits = TINY_DIGITS[digit][row];
    for (int col = 0; col < 3; col++) {
      if (bits & (1 << (2 - col))) {
        graphics_fill_rect(ctx, GRect(top_left.x + col * px, top_left.y + row * px, px, px), 0, GCornerNone);
      }
    }
  }
}

// Centers a (1- or 2-digit) number on `center` using the tiny font.
static void draw_tiny_number(GContext *ctx, GPoint center, int number, GColor color, int16_t px) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", number);
  int len = (int)strlen(buf);
  int16_t digit_w = 3 * px + px; // 3px-wide digit + 1px gap
  int16_t total_w = (int16_t)(len * digit_w - px);
  GPoint start = GPoint(center.x - total_w / 2, center.y - (5 * px) / 2);
  for (int i = 0; i < len; i++) {
    draw_tiny_digit(ctx, GPoint(start.x + i * digit_w, start.y), buf[i] - '0', color, px);
  }
}

// Small tick marks at each hour position -- slightly longer at
// 12/3/6/9 for a bit of visual structure without needing numerals.
static void draw_hour_markers(GContext *ctx, GPoint center, int16_t r, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  for (int h = 0; h < 12; h++) {
    int32_t angle = (h * TRIG_MAX_ANGLE) / 12;
    int16_t outer = r;
    int16_t inner = (h % 3 == 0) ? r - 5 : r - 3;
    GPoint p1 = GPoint(center.x + (outer * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y - (outer * cos_lookup(angle)) / TRIG_MAX_RATIO);
    GPoint p2 = GPoint(center.x + (inner * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y - (inner * cos_lookup(angle)) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, p1, p2);
  }
}

// ---- sunrise/sunset readout ------------------------------------------

// Whichever of today's sunrise/sunset is still ahead of `now`. Only
// covers *today's* two contacts (that's all the phone sends), so once
// today's sunset has passed there's nothing valid to show until the
// next refresh rolls the data over to a new day -- callers fall back
// to the week number in that case rather than showing stale or
// invented data.
static bool get_next_sun_event(time_t now, time_t sun_rise, time_t sun_set, time_t *event_time, bool *is_sunrise) {
  if (sun_rise != 0 && now < sun_rise) {
    *event_time = sun_rise;
    *is_sunrise = true;
    return true;
  }
  if (sun_set != 0 && now < sun_set) {
    *event_time = sun_set;
    *is_sunrise = false;
    return true;
  }
  return false;
}

// A compact "sunrise/sunset" glyph -- an arrow (up for rise, down for
// set) next to a horizon-sun icon (a circle with its bottom half
// covered by the background color, sitting on a short line), both
// built from plain fill primitives rather than a font glyph that may
// not exist in the built-in charset. Returns the total width drawn,
// so the caller can place the time text right after it.
static int16_t draw_sun_time_icon(GContext *ctx, GPoint top_left, bool is_sunrise, GColor color, GColor bg) {
  graphics_context_set_fill_color(ctx, color);
  int16_t ax = top_left.x, ay = top_left.y;
  for (int16_t row = 0; row < 5; row++) {
    int16_t width = is_sunrise ? (row + 1) : (5 - row);
    int16_t row_y = is_sunrise ? (ay + (4 - row)) : (ay + row);
    graphics_fill_rect(ctx, GRect(ax + (5 - width) / 2, row_y, width, 1), 0, GCornerNone);
  }

  GPoint sun_center = GPoint(ax + 13, ay + 4);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, sun_center, 4);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, GRect(sun_center.x - 5, sun_center.y, 10, 5), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(sun_center.x - 6, sun_center.y, 12, 1), 0, GCornerNone);

  return 20; // total icon width, arrow + horizon-sun glyph
}

// ---- big-analogue mode: fullscreen hands over the sky layer --------------

// Same 4x4 ordered (Bayer) dither matrix as eclipse_layer.c's sky
// gradient/clouds, duplicated here rather than shared across the two
// translation units (small, and each file already keeps its own
// self-contained drawing helpers).
static const uint8_t BAYER4[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
};

// True if p is inside the convex polygon defined by pts (any winding
// direction) -- a point is inside a convex polygon exactly when it's
// on the same side of every edge, which a set of cross-product signs
// answers directly without needing a separate "is this polygon wound
// clockwise or counterclockwise" check first.
static bool point_in_convex_polygon(GPoint *pts, int n, GPoint p) {
  bool has_pos = false, has_neg = false;
  for (int i = 0; i < n; i++) {
    GPoint a = pts[i];
    GPoint b = pts[(i + 1) % n];
    int32_t cross = (int32_t)(b.x - a.x) * (p.y - a.y) - (int32_t)(b.y - a.y) * (p.x - a.x);
    if (cross > 0) has_pos = true;
    if (cross < 0) has_neg = true;
    if (has_pos && has_neg) return false;
  }
  return true;
}

// Fills a convex polygon with a genuine ~50% Bayer-dithered stipple
// of `color`, pixel by pixel -- half the pixels (in the same ordered
// pattern used for the sky/clouds elsewhere, not randomly) get the
// hand's color, the other half are left completely untouched. Since
// Pebble's basic fills have no alpha blending for arbitrary shapes,
// this is how "50% transparent" becomes a real per-pixel compositing
// effect rather than a hollow-outline approximation: whatever the
// sky canvas drew underneath shows through evenly across the whole
// hand, not just around its edges.
static void fill_polygon_dithered(GContext *ctx, GPoint *pts, int n, GColor color) {
  int16_t min_x = pts[0].x, max_x = pts[0].x, min_y = pts[0].y, max_y = pts[0].y;
  for (int i = 1; i < n; i++) {
    if (pts[i].x < min_x) min_x = pts[i].x;
    if (pts[i].x > max_x) max_x = pts[i].x;
    if (pts[i].y < min_y) min_y = pts[i].y;
    if (pts[i].y > max_y) max_y = pts[i].y;
  }
  graphics_context_set_fill_color(ctx, color);
  for (int16_t y = min_y; y <= max_y; y++) {
    for (int16_t x = min_x; x <= max_x; x++) {
      if (BAYER4[y & 3][x & 3] >= 8) continue; // ~50% threshold
      GPoint p = GPoint(x, y);
      if (!point_in_convex_polygon(pts, n, p)) continue;
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }
}

// True if p is within half_width of the line segment a-b -- used for
// the rounded/classic hand style's dithered-transparency case, since
// that shape isn't a simple polygon the way the other 3 styles are.
static bool point_in_capsule(GPoint p, GPoint a, GPoint b, int16_t half_width) {
  int32_t abx = b.x - a.x, aby = b.y - a.y;
  int32_t apx = p.x - a.x, apy = p.y - a.y;
  int32_t ab_len_sq = abx * abx + aby * aby;
  int32_t t = ab_len_sq > 0 ? ((apx * abx + apy * aby) * 1000) / ab_len_sq : 0;
  if (t < 0) t = 0;
  if (t > 1000) t = 1000;
  int32_t closest_x = a.x + (abx * t) / 1000;
  int32_t closest_y = a.y + (aby * t) / 1000;
  int32_t dx = p.x - closest_x, dy = p.y - closest_y;
  int32_t dist_sq = dx * dx + dy * dy;
  return dist_sq <= (int32_t)half_width * half_width;
}

// Same ~50% Bayer stipple as fill_polygon_dithered, but for a
// rounded-cap line ("capsule") shape instead of a polygon.
static void fill_capsule_dithered(GContext *ctx, GPoint a, GPoint b, int16_t half_width, GColor color) {
  int16_t min_x = (a.x < b.x ? a.x : b.x) - half_width;
  int16_t max_x = (a.x > b.x ? a.x : b.x) + half_width;
  int16_t min_y = (a.y < b.y ? a.y : b.y) - half_width;
  int16_t max_y = (a.y > b.y ? a.y : b.y) + half_width;
  graphics_context_set_fill_color(ctx, color);
  for (int16_t y = min_y; y <= max_y; y++) {
    for (int16_t x = min_x; x <= max_x; x++) {
      if (BAYER4[y & 3][x & 3] >= 8) continue; // ~50% threshold
      if (!point_in_capsule(GPoint(x, y), a, b, half_width)) continue;
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }
}

// Draws one hour/minute hand at `angle` (TRIG_MAX_ANGLE units) and
// `length` px, in one of four styles. Point arrays are computed
// fresh each call (rather than a persistent GPath rotated in place,
// the more common Pebble pattern) since `length` depends on the
// current screen size -- this keeps the hand correctly proportioned
// on any display without needing to rebuild a cached path whenever
// that size is known, which only matters once per launch anyway.
//
// The modern style is always hollow (outline-only -- gpath_draw_outline()
// genuinely leaves the interior untouched) when hands aren't
// transparent. Transparency, when on, always means a real ~50%
// dithered blend of the hand's full normal silhouette instead --
// including for modern, which trades its hollow center for the
// dithered fill in that case, since piling a 50% stipple on top of
// an already-hollow 1px outline wouldn't read as "half-transparent"
// at all.

// Shared by every outline implementation in this file (text, icons,
// hands): draw once shifted in each cardinal direction with a
// contrasting color, then once more normally on top. Cheap and
// guarantees contrast against any background without needing
// per-pixel edge detection.
static const GPoint OUTLINE_OFFSETS[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

// White for dark colors, black for bright ones -- the outline has to
// contrast with the text/icon's OWN color to do its job (a dark
// outline on dark text is invisible regardless of what's behind it),
// not with the scheme's background color, which is what this used to
// (incorrectly) use.
static GColor contrasting_outline_color(GColor c) {
  uint8_t r = (c.argb >> 4) & 0x03;
  uint8_t g = (c.argb >> 2) & 0x03;
  uint8_t b = c.argb & 0x03;
  int luma = r * 3 + g * 6 + b; // approximates 0.3/0.6/0.1 luma weights, out of 30
  return (luma >= 15) ? GColorBlack : GColorWhite;
}

static void draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                                GTextOverflowMode overflow, GTextAlignment alignment,
                                GColor color, bool outline_enabled) {
  if (outline_enabled) {
    graphics_context_set_text_color(ctx, contrasting_outline_color(color));
    for (int i = 0; i < 4; i++) {
      GRect shifted = GRect(box.origin.x + OUTLINE_OFFSETS[i].x, box.origin.y + OUTLINE_OFFSETS[i].y,
                             box.size.w, box.size.h);
      graphics_draw_text(ctx, text, font, shifted, overflow, alignment, NULL);
    }
  }
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, overflow, alignment, NULL);
}

static void draw_big_hand(GContext *ctx, GPoint center, int32_t angle, int16_t length,
                           uint8_t style, GColor color, bool transparent) {
  if (style == 3) {
    // Rounded/classic: a thick line with rounded caps at both ends,
    // like the default Pebble watchface's hands -- drawn directly
    // (line + circle caps) rather than through the polygon/GPath path
    // the other 3 styles use, since a capsule isn't a simple polygon.
    int16_t half_width = length / 14;
    if (half_width < 2) half_width = 2;
    if (half_width > 6) half_width = 6;
    GPoint tip = GPoint(center.x + ((int32_t)length * sin_lookup(angle)) / TRIG_MAX_RATIO,
                         center.y - ((int32_t)length * cos_lookup(angle)) / TRIG_MAX_RATIO);
    if (transparent) {
      fill_capsule_dithered(ctx, center, tip, half_width, color);
    } else {
      graphics_context_set_stroke_color(ctx, color);
      graphics_context_set_stroke_width(ctx, half_width * 2);
      graphics_draw_line(ctx, center, tip);
      graphics_context_set_fill_color(ctx, color);
      graphics_fill_circle(ctx, tip, half_width);
      graphics_fill_circle(ctx, center, half_width);
    }
    return;
  }

  GPoint points[4];
  int num_points;

  if (style == 1) { // square: constant-width rectangle
    int16_t hw = length / 12;
    if (hw < 3) hw = 3;
    points[0] = GPoint(-hw, 6);
    points[1] = GPoint(-hw, -length);
    points[2] = GPoint(hw, -length);
    points[3] = GPoint(hw, 6);
    num_points = 4;
  } else if (style == 2) { // modern: narrow rounded rectangle, hollow unless transparent (see above)
    int16_t hw = length / 16;
    if (hw < 2) hw = 2;
    points[0] = GPoint(-hw, 8);
    points[1] = GPoint(-hw, -length);
    points[2] = GPoint(hw, -length);
    points[3] = GPoint(hw, 8);
    num_points = 4;
  } else { // pointy: triangle, wide base narrowing to a tip
    int16_t hw = length / 8;
    if (hw < 4) hw = 4;
    points[0] = GPoint(-hw, 6);
    points[1] = GPoint(hw, 6);
    points[2] = GPoint(0, -length);
    num_points = 3;
  }

  if (transparent) {
    // Rotate/translate the shape's points into screen space by hand
    // (rather than via GPath, which has no dithered-fill option) and
    // stipple it in place. Matches the same clockwise-from-12
    // TRIG_MAX_ANGLE rotation convention gpath_rotate_to() uses
    // internally, so a hand doesn't visually jump when this toggle
    // flips.
    GPoint screen_pts[4];
    for (int i = 0; i < num_points; i++) {
      int32_t rx = ((int32_t)points[i].x * cos_lookup(angle) - (int32_t)points[i].y * sin_lookup(angle)) / TRIG_MAX_RATIO;
      int32_t ry = ((int32_t)points[i].x * sin_lookup(angle) + (int32_t)points[i].y * cos_lookup(angle)) / TRIG_MAX_RATIO;
      screen_pts[i] = GPoint(center.x + rx, center.y + ry);
    }
    fill_polygon_dithered(ctx, screen_pts, num_points, color);
    return;
  }

  GPathInfo info;
  info.num_points = num_points;
  info.points = points;
  GPath *path = gpath_create(&info);
  gpath_rotate_to(path, angle);
  gpath_move_to(path, center);

  if (style == 2) {
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, path);
  } else {
    graphics_context_set_fill_color(ctx, color);
    gpath_draw_filled(ctx, path);
  }

  gpath_destroy(path);
}

// Draws the same hand shape 4x with a 1px-shifted center in a
// contrasting color first (only when not in transparent-hands mode --
// an outline under a dithered translucent fill would look wrong),
// then the real hand on top -- reuses draw_big_hand itself for the
// outline passes rather than duplicating its per-style rendering.
static void draw_big_hand_outlined(GContext *ctx, GPoint center, int32_t angle, int16_t length,
                                    uint8_t style, GColor color, bool transparent,
                                    bool outline_enabled) {
  if (outline_enabled && !transparent) {
    GColor outline_color = contrasting_outline_color(color);
    for (int i = 0; i < 4; i++) {
      GPoint shifted = GPoint(center.x + OUTLINE_OFFSETS[i].x, center.y + OUTLINE_OFFSETS[i].y);
      draw_big_hand(ctx, shifted, angle, length, style, outline_color, false);
    }
  }
  draw_big_hand(ctx, center, angle, length, style, color, transparent);
}

// ---- big-analogue marker styles (procedural + bitmap) --------------------

// Loaded on demand and cached (unloaded/reloaded only when the style
// actually changes) rather than every frame -- gbitmap_create_with_resource
// isn't something to call every second.
static GBitmap *s_marker_bitmap = NULL;
static uint8_t s_marker_bitmap_style = 255; // sentinel: none loaded yet
static bool s_marker_bitmap_tinted = false;   // has tint_marker_bitmap run since the last (re)load?
static GColor s_marker_bitmap_tint_color;      // which color it was last tinted to
static bool s_marker_bitmap_tint_transparent;  // and whether that tint was the transparent variant

// Custom font for corner/edge feature text and the big-analog date --
// a single slot since only one custom font can be selected at a time,
// swapped (or unloaded entirely, for "default") whenever the setting
// changes. Same lazy load/unload lifecycle as apply_clock_font() uses
// for the main clock typeface.
static GFont s_corner_custom_font = NULL;
static uint8_t s_corner_custom_font_loaded_choice = 255; // 255 = nothing loaded yet

static uint32_t marker_style_resource_id(uint8_t style) {
  switch (style) {
    case 3: return RESOURCE_ID_MODERN_BACKGROUND;
    case 4: return RESOURCE_ID_SWISS_BACKGROUND;
    case 5: return RESOURCE_ID_TALLY_BACKGROUND;
    case 6: return RESOURCE_ID_BELL_BACKGROUND;
    case 7: return RESOURCE_ID_BROWN_BACKGROUND;
    default: return 0;
  }
}

static void ensure_marker_bitmap_loaded(uint8_t style) {
  if (style < 3) {
    if (s_marker_bitmap) {
      gbitmap_destroy(s_marker_bitmap);
      s_marker_bitmap = NULL;
    }
    s_marker_bitmap_style = 255;
    s_marker_bitmap_tinted = false;
    return;
  }
  if (s_marker_bitmap_style == style && s_marker_bitmap) return; // already the right one
  if (s_marker_bitmap) {
    gbitmap_destroy(s_marker_bitmap);
    s_marker_bitmap = NULL;
  }
  uint32_t res_id = marker_style_resource_id(style);
  if (res_id != 0) {
    s_marker_bitmap = gbitmap_create_with_resource(res_id);
  }
  s_marker_bitmap_style = style;
  s_marker_bitmap_tinted = false; // freshly loaded, still in its original exported colors
}

static uint32_t corner_custom_font_resource_id(uint8_t choice) {
  switch (choice) {
    case 1: return RESOURCE_ID_DIGITALDREAM_FONT_12;
    case 2: return RESOURCE_ID_MINECRAFTER_FONT_12;
    case 3: return RESOURCE_ID_SFPIXELATE_FONT_14;
    case 4: return RESOURCE_ID_MISO_FONT_19;
    case 5: return RESOURCE_ID_BEBAS_FONT_20;
    default: return 0; // 0 = default -- no custom font, corner_font_size applies instead
  }
}

static void ensure_corner_custom_font(uint8_t choice) {
  if (choice == s_corner_custom_font_loaded_choice) return; // already correct
  if (s_corner_custom_font) {
    fonts_unload_custom_font(s_corner_custom_font);
    s_corner_custom_font = NULL;
  }
  uint32_t res_id = corner_custom_font_resource_id(choice);
  if (res_id != 0) {
    s_corner_custom_font = fonts_load_custom_font(resource_get_handle(res_id));
  }
  s_corner_custom_font_loaded_choice = choice;
}

// Resolves to whichever font corner/edge text and the big-analog date
// should currently draw with -- the custom font if one's selected
// (ensure_corner_custom_font() must have already been called this
// cycle so it's actually loaded), otherwise a system font sized per
// corner_font_size.
static GFont get_corner_font(void) {
  if (s_corner_custom_font) return s_corner_custom_font;
  if (s_data.corner_font_size == 0) return fonts_get_system_font(FONT_KEY_GOTHIC_14);
  if (s_data.corner_font_size == 2) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (s_data.corner_font_size == 3) return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); // 1 = medium, also the fallback
}

// A rough height estimate for whatever get_corner_font() currently
// resolves to, used only for sizing/centering text boxes -- doesn't
// need to be pixel-exact, just enough to keep each option's text
// reasonably positioned rather than clipped or badly off-center.
static int16_t corner_font_height_estimate(void) {
  if (s_data.corner_custom_font == 1) return 12;
  if (s_data.corner_custom_font == 2) return 12;
  if (s_data.corner_custom_font == 3) return 14;
  if (s_data.corner_custom_font == 4) return 19;
  if (s_data.corner_custom_font == 5) return 24;
  if (s_data.corner_font_size == 0) return 14;
  if (s_data.corner_font_size == 2) return 24;
  if (s_data.corner_font_size == 3) return 32;
  return 18;
}

// Recolors s_marker_bitmap to tint_color, once, in place -- not a
// per-frame operation. Every non-fully-transparent pixel (or palette
// entry) has its RGB replaced with tint_color's RGB. Its alpha is
// NOT left as originally exported: this follows the same "hands
// transparent" setting the procedural hand styles use, forcing full
// opacity when it's off (regardless of whatever partial alpha the
// source PNG's antialiased edges happen to carry -- otherwise a
// source that isn't fully opaque everywhere renders translucent no
// matter what this setting says) or a consistent partial alpha when
// it's on. Re-running with a different tint_color and/or transparent
// setting is safe and correct any number of times, since each run
// fully replaces both RGB and alpha rather than blending onto
// whatever's already there.
//
// Two cases, matching how Pebble's build tooling can represent a
// mask PNG like this:
//   - Palettized (1/2/4-bit palette): gbitmap_get_palette() gives
//     direct access to the (tiny) color table -- rewrite each entry.
//     This is the standard, well-documented technique for recoloring
//     Pebble icons/masks at runtime.
//   - GBitmapFormat8Bit: each pixel stores its own GColor.argb byte
//     directly (no palette) -- rewrite every non-transparent pixel's
//     byte via gbitmap_get_data()/get_bytes_per_row(), the same safe
//     technique already used for the sky canvas's cache bitmap.
// Any other format is left untinted (drawn with its own original
// colors via the plain GCompOpSet path) rather than risking a wrong
// guess about its layout.
static void tint_marker_bitmap(GColor tint_color, bool transparent) {
  if (!s_marker_bitmap) return;
  if (s_marker_bitmap_tinted && s_marker_bitmap_tint_color.argb == tint_color.argb
      && s_marker_bitmap_tint_transparent == transparent) return;

  uint8_t forced_alpha_bits = transparent ? 0x80 : 0xC0; // alpha 2 (~67%) or 3 (opaque)

  GBitmapFormat format = gbitmap_get_format(s_marker_bitmap);
  if (format == GBitmapFormat1BitPalette || format == GBitmapFormat2BitPalette || format == GBitmapFormat4BitPalette) {
    GColor *palette = gbitmap_get_palette(s_marker_bitmap);
    if (palette) {
      int count = (format == GBitmapFormat1BitPalette) ? 2 : (format == GBitmapFormat2BitPalette) ? 4 : 16;
      for (int i = 0; i < count; i++) {
        if ((palette[i].argb & 0xC0) == 0) continue; // fully transparent entry -- leave it alone
        GColor new_color;
        new_color.argb = forced_alpha_bits | (tint_color.argb & 0x3F);
        palette[i] = new_color;
      }
    }
  } else if (format == GBitmapFormat8Bit) {
    uint8_t *data = gbitmap_get_data(s_marker_bitmap);
    uint16_t stride = gbitmap_get_bytes_per_row(s_marker_bitmap);
    GRect b = gbitmap_get_bounds(s_marker_bitmap);
    for (int16_t y = 0; y < b.size.h; y++) {
      uint8_t *row = data + (int32_t)y * stride;
      for (int16_t x = 0; x < b.size.w; x++) {
        if ((row[x] & 0xC0) == 0) continue; // fully transparent pixel -- leave it alone
        row[x] = forced_alpha_bits | (tint_color.argb & 0x3F);
      }
    }
  }
  // Any other format: left as-is, drawn with its original colors.

  s_marker_bitmap_tinted = true;
  s_marker_bitmap_tint_transparent = transparent;
  s_marker_bitmap_tint_color = tint_color;
}

// Draws `mask` at its own native size, centered within `bounds`,
// using Pebble's standard bitmap transparency technique (GCompOpSet).
// Whatever recoloring is needed happens once beforehand in
// tint_marker_bitmap(), not here -- this stays the same simple,
// confirmed-working draw call every frame.
static void draw_marker_bitmap(GContext *ctx, GBitmap *mask, GRect bounds) {
  if (!mask) return;
  GRect bmp_bounds = gbitmap_get_bounds(mask);
  GRect dest = GRect(bounds.origin.x + (bounds.size.w - bmp_bounds.size.w) / 2,
                      bounds.origin.y + (bounds.size.h - bmp_bounds.size.h) / 2,
                      bmp_bounds.size.w, bmp_bounds.size.h);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, mask, dest);
}

// Procedural hour/second markers for the 3 non-bitmap styles.
// style: 0=minimal (hour only, thin), 1=small (hour+second, hour
// longer), 2=big (hour thick, second thin+short).
static void draw_procedural_markers(GContext *ctx, GPoint center, int16_t radius, uint8_t style, GColor color) {
  bool show_second = (style != 0);
  int16_t hour_outer = radius + 5;
  int16_t hour_inner_major, hour_inner_minor, hour_width;
  int16_t sec_outer = radius + 2, sec_inner = radius, sec_width = 1;

  if (style == 1) { // small
    hour_inner_major = radius - 6; hour_inner_minor = radius - 4; hour_width = 1;
  } else if (style == 2) { // big
    hour_outer = radius + 6;
    hour_inner_major = radius - 9; hour_inner_minor = radius - 6; hour_width = 3;
  } else { // minimal (style == 0, or any unexpected value)
    hour_inner_major = radius - 4; hour_inner_minor = radius - 2; hour_width = 1;
  }

  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, hour_width);
  for (int h = 0; h < 12; h++) {
    int32_t angle = (h * TRIG_MAX_ANGLE) / 12;
    int16_t inner = (h % 3 == 0) ? hour_inner_major : hour_inner_minor;
    GPoint p1 = GPoint(center.x + (hour_outer * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y - (hour_outer * cos_lookup(angle)) / TRIG_MAX_RATIO);
    GPoint p2 = GPoint(center.x + (inner * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y - (inner * cos_lookup(angle)) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, p1, p2);
  }

  if (show_second) {
    graphics_context_set_stroke_width(ctx, sec_width);
    for (int s = 0; s < 60; s++) {
      if (s % 5 == 0) continue; // an hour marker already covers this position
      int32_t angle = (s * TRIG_MAX_ANGLE) / 60;
      GPoint p1 = GPoint(center.x + (sec_outer * sin_lookup(angle)) / TRIG_MAX_RATIO,
                          center.y - (sec_outer * cos_lookup(angle)) / TRIG_MAX_RATIO);
      GPoint p2 = GPoint(center.x + (sec_inner * sin_lookup(angle)) / TRIG_MAX_RATIO,
                          center.y - (sec_inner * cos_lookup(angle)) / TRIG_MAX_RATIO);
      graphics_draw_line(ctx, p1, p2);
    }
  }
}

// The always-on-top overlay for big-analogue mode: hour/minute/second
// hands, optional edge tick markers, and an optional date readout
// behind the hands. Deliberately never fills its own background --
// left untouched, whatever the sky canvas underneath already drew
// shows straight through, the same "transparent overlay" pattern the
// countdown label already uses successfully elsewhere in this file.
static void hands_layer_update_proc(Layer *layer, GContext *ctx) {
  // Unobstructed (not full) bounds -- this layer has no background
  // fill of its own to worry about leaving gaps in (it's a pure
  // overlay on top of the sky canvas), so everything here can just
  // reposition/resize to fit whatever's actually visible right now,
  // shrinking gracefully when Timeline Quick View is showing.
  GRect bounds = layer_get_unobstructed_bounds(layer);
  GPoint center = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + bounds.size.h / 2);
  int16_t radius = ((bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h) / 2) - 8;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  GColor bg, main_color, accent_color;
  get_active_color_scheme(now, &bg, &main_color, &accent_color);

  uint8_t marker_style = s_data.big_analog_marker_style;
  bool is_bitmap_style = marker_style >= 3 && marker_style != 8;
  ensure_marker_bitmap_loaded(marker_style);
  ensure_corner_custom_font(s_data.corner_custom_font);

  if (is_bitmap_style) {
    tint_marker_bitmap(main_color, s_data.big_analog_hands_transparent);
    draw_marker_bitmap(ctx, s_marker_bitmap, bounds);
  } else if (marker_style == 8) {
    // Custom marker system -- ring geometry is cached (see marker_layer.c),
    // only the (cheap) text-numeral pass runs live every tick.
    marker_layer_ensure_ring_cache(bounds, &s_data.custom_hour_marker,
                                    &s_data.custom_second_marker, main_color);
    marker_layer_draw_ring(ctx, bounds);
    marker_layer_draw_text(ctx, bounds, &s_data.marker_text,
                            &s_data.custom_hour_marker, &s_data.custom_second_marker, main_color);
  } else {
    draw_procedural_markers(ctx, center, radius, marker_style, main_color);
  }

  int32_t hour_angle = (((t->tm_hour % 12) * 60 + t->tm_min) * TRIG_MAX_ANGLE) / (12 * 60);
  int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE) / 60;

  if (s_data.big_analog_hand_style == 4) {
    // Custom hand system -- each hand fully independent, see hand_layer.h.
    // Transparency is per-hand now (HandConfig.translucent), not the
    // global big_analog_hands_transparent checkbox.
    hand_layer_draw(ctx, center, hour_angle, &s_data.hand_hour, main_color, accent_color, bg);
    hand_layer_draw(ctx, center, min_angle, &s_data.hand_minute, main_color, accent_color, bg);
    if (s_data.show_seconds) {
      int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;
      hand_layer_draw(ctx, center, sec_angle, &s_data.hand_second, main_color, accent_color, bg);
    }
    hand_layer_draw_center_circle(ctx, center, s_data.center_circle_radius, s_data.center_circle_color,
                                   main_color, accent_color, bg);
  } else {
    // Rounded/classic style: the hour hand takes the accent color
    // (matching the default Pebble watchface's look), minute hand
    // stays main color. Every other style uses main color for both.
    GColor hour_hand_color = (s_data.big_analog_hand_style == 3) ? accent_color : main_color;
    draw_big_hand_outlined(ctx, center, hour_angle, (radius * 55) / 100,
                            s_data.big_analog_hand_style, hour_hand_color, s_data.big_analog_hands_transparent,
                            s_data.outline_enabled);
    draw_big_hand_outlined(ctx, center, min_angle, (radius * 85) / 100,
                            s_data.big_analog_hand_style, main_color, s_data.big_analog_hands_transparent,
                            s_data.outline_enabled);

    if (s_data.show_seconds) {
      int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;
      int16_t sec_len = (radius * 92) / 100;
      GPoint sec_end = GPoint(center.x + (sec_len * sin_lookup(sec_angle)) / TRIG_MAX_RATIO,
                               center.y - (sec_len * cos_lookup(sec_angle)) / TRIG_MAX_RATIO);
      graphics_context_set_stroke_color(ctx, accent_color);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_line(ctx, center, sec_end);
    }

    graphics_context_set_fill_color(ctx, main_color);
    graphics_fill_circle(ctx, center, 3);
  }
}

// ---- corners overlay -------------------------------------------------

// Procedural icon bitmaps (1 bit per pixel, MSB = left column) --
// ~50% bigger than the original 5-wide versions (now 7 wide), same
// style as the tiny digit font elsewhere in this app.
static const uint8_t ICON_WIDTH = 16;
static const uint8_t ICON_ROWS = 12;

//static const uint8_t HEART_ICON[6] = { 0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08 };
static const uint8_t HEART_ICON[24] = { 0x00, 0x00, 0x1C, 0x38, 0x3E, 0x7C, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE,
  0x3F, 0xFC, 0x1F, 0xF8, 0x0F, 0xF0, 0x07, 0xE0, 0x03, 0xC0, 0x01, 0x80 };
static const uint8_t FOOT_ICON[13]  = { 0x3C, 0x3C, 0x3C, 0x18, 0x7E, 0xFF, 0xDB, 0xD8, 0x1E, 0x3F, 0x73, 0xE3, 0xC3 };
static const uint8_t UMBRELLA_ICON[24] = { 0x01, 0x80, 0x0F, 0xF0, 0x3F, 0xFC, 0xFF, 0xFF, 0x01, 0x80, 0x01, 0x80,
  0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xC0, 0x00, 0xC0 };
static const uint8_t DROPLET_ICON[24]  = { 0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0, 0x0E, 0x70, 0x0C, 0x30, 0x18, 0x18,
  0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0C, 0x30, 0x07, 0xE0, 0x03, 0xC0 };
static const uint8_t WIND_ICON[24]     = { 0x06, 0x00, 0x07, 0xF8, 0x03, 0xF8, 0x00, 0x00, 0x60, 0x00, 0x7F, 0xFC,
  0x3F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x07, 0xF8, 0x0F, 0xF8, 0x0C, 0x00 };
static const uint8_t GPS_PIN_ICON[24] = { 0x01, 0x80, 0x01, 0x80, 0x03, 0xC0, 0x04, 0x20, 0x08, 0x10, 0x39, 0x9C,
  0x39, 0x9C, 0x08, 0x10, 0x04, 0x20, 0x03, 0xC0, 0x01, 0x80, 0x01, 0x80 };
static const uint8_t EYE_ICON[24]     = { 0x00, 0x00, 0x00, 0x00, 0x07, 0xE0, 0x18, 0x18, 0x63, 0xC6, 0x82, 0x41,
  0x82, 0x41, 0x63, 0xC6, 0x18, 0x18, 0x07, 0xE0, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t PEBBLE_ICON[62]     = { 0x00, 0x02, 0x04, 0x08, 0x00, 0x00, 0x02, 0x04, 0x08, 0x00, 0xFD, 0xFB,
  0xF7, 0xE9, 0xF8, 0x85, 0x0A, 0x14, 0x29, 0x08, 0x85, 0x0A, 0x14, 0x29,
  0x08, 0x85, 0xFA, 0x14, 0x29, 0xF8, 0x85, 0x02, 0x14, 0x29, 0x00, 0xFD,
  0xFB, 0xF7, 0xED, 0xF8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
  0x00, 0x00}; // 40 x 10px
//static const uint8_t CLOUD_ICON[5]   = { 0x30, 0x7A, 0xFF, 0xFF, 0x7E };
static const uint8_t CLOUD_ICON[24]   = { 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x22, 0xE0, 0x41, 0x10, 0x82, 0x0C,
  0x82, 0x16, 0x80, 0x01, 0x40, 0x01, 0x20, 0x02, 0x1F, 0xFC, 0x00, 0x00 };

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

// A small battery glyph (outline + nub), drawn with primitives rather
// than a bitmap pattern since it's naturally an outline+fill shape,
// not a solid silhouette like the heart/foot icons.
static void draw_corner_battery_icon(GContext *ctx, GPoint top_left, GColor color, int charge) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y, 4, 2), 0, GCornerNone); // nub
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(top_left.x, top_left.y + 2, 8, 12));
  int16_t charge_pixels = (charge == 0) ? 0 : (8 * charge / 100);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y + 4 + (8 - charge_pixels), 4, charge_pixels), 0, GCornerNone); // fill
}

// 7-stop gradient: turquoise (cold/low end) -> light blue -> green ->
// yellow -> orange -> red -> violet (hot/high end). Used for both
// temperature (-10..40C) and UV index (1..13) by passing different
// min/max, per the brief's request for "more colors" than a simple
// 2-stop blend.
static GColor seven_stop_gradient(int32_t value, int32_t min_v, int32_t max_v) {
  static const int16_t STOPS[7][3] = {
    {  64, 224, 208 },  // turquoise
    { 173, 216, 230 },  // light blue
    {   0, 200,   0 },  // green
    { 255, 220,   0 },  // yellow
    { 255, 140,   0 },  // orange
    { 220,  20,  20 },  // red
    { 148,   0, 211 },  // violet
  };
  if (max_v <= min_v || value <= min_v) return GColorFromRGB(STOPS[0][0], STOPS[0][1], STOPS[0][2]);
  if (value >= max_v) return GColorFromRGB(STOPS[6][0], STOPS[6][1], STOPS[6][2]);

  int32_t pos_x6000 = ((value - min_v) * 6000) / (max_v - min_v); // 0..6000 across 6 segments
  int seg = (int)(pos_x6000 / 1000);
  if (seg > 5) seg = 5;
  int32_t seg_frac = pos_x6000 - (int32_t)seg * 1000; // 0..1000 within the segment

  int16_t r = STOPS[seg][0] + (int16_t)(((STOPS[seg + 1][0] - STOPS[seg][0]) * seg_frac) / 1000);
  int16_t g = STOPS[seg][1] + (int16_t)(((STOPS[seg + 1][1] - STOPS[seg][1]) * seg_frac) / 1000);
  int16_t b = STOPS[seg][2] + (int16_t)(((STOPS[seg + 1][2] - STOPS[seg][2]) * seg_frac) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// Simple white (low) -> turquoise (high) gradient, used for humidity,
// wind, and rain chance -- these don't need the full 7-stop range,
// just "more of this = more teal".
static GColor white_to_turquoise_gradient(int32_t value, int32_t min_v, int32_t max_v) {
  if (max_v <= min_v) return GColorWhite;
  int32_t clamped = value < min_v ? min_v : (value > max_v ? max_v : value);
  int32_t frac1000 = ((clamped - min_v) * 1000) / (max_v - min_v);
  int16_t r = 255 - (int16_t)(((255 - 64) * frac1000) / 1000);
  int16_t g = 255 - (int16_t)(((255 - 224) * frac1000) / 1000);
  int16_t b = 255 - (int16_t)(((255 - 208) * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// Red (0%) -> green (100%+). Shared by "steps today"/"step goal %"
// (percent of daily goal) and "battery" (percent charged) -- same
// red-is-low, green-is-high convention makes sense for both.
static GColor red_green_gradient(uint8_t pct) {
  if (pct >= 100) return GColorFromRGB(0, 200, 0);
  int32_t frac1000 = ((int32_t)pct * 1000) / 100;
  int16_t r = 220 - (int16_t)((220 * frac1000) / 1000);
  int16_t g = (int16_t)((200 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, 0);
}

// The four weather-family color gradients "current conditions" (and
// nothing else) uses, each scaled by that condition's own intensity
// rather than a single flat color -- clearer sky/heavier rain/etc.
// reads as a visibly different shade, not just a different icon.
#define OVERCAST_CLOUD_THRESHOLD 40 // cloud_pct at/above this reads as "overcast" rather than "sunny"

// Clear/sunny: white fading toward a warm golden-white as skies get
// clearer (lower cloud_pct).
static GColor sunny_yellow_white_gradient(uint8_t cloud_pct) {
  uint8_t clamped = cloud_pct > OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct;
  int32_t frac1000 = ((int32_t)(OVERCAST_CLOUD_THRESHOLD - clamped) * 1000) / OVERCAST_CLOUD_THRESHOLD;
  int16_t b = 255 - (int16_t)((85 * frac1000) / 1000);
  return GColorFromRGB(255, 255, (uint8_t)b);
}

// Overcast/fog: light gray darkening toward a heavier gray as cloud
// cover thickens.
static GColor overcast_gray_gradient(uint8_t cloud_pct) {
  uint8_t clamped = cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct;
  int32_t frac1000 = ((int32_t)(clamped - OVERCAST_CLOUD_THRESHOLD) * 1000) / (100 - OVERCAST_CLOUD_THRESHOLD);
  int16_t v = 200 - (int16_t)((115 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// Snow: white gaining a faint blue-white cast as it gets heavier
// (denser cloud cover generally means heavier snowfall).
static GColor snow_white_gradient(uint8_t cloud_pct) {
  int32_t frac1000 = ((int32_t)cloud_pct * 1000) / 100;
  int16_t rg = 255 - (int16_t)((85 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)rg, (uint8_t)rg, 255);
}

// weather_condition: 0=clear/cloudy (cloud_pct alone decides sunny vs
// overcast), 1=fog (treated like overcast), 2=rain (reuses the
// existing white->turquoise gradient, driven by rain chance),
// 3=snow, 4=thunderstorm -- an extreme-weather warning that
// overrides everything else with a flat bright red regardless of any
// other value.
static GColor weather_condition_color(uint8_t condition, uint8_t cloud_pct, uint8_t rain_chance_pct) {
  if (condition == 4) return GColorFromRGB(255, 0, 0);
  if (condition == 3) return snow_white_gradient(cloud_pct);
  if (condition == 2) return white_to_turquoise_gradient(rain_chance_pct, 0, 100);
  if (condition == 1) return overcast_gray_gradient(cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct);
  return (cloud_pct < OVERCAST_CLOUD_THRESHOLD) ? sunny_yellow_white_gradient(cloud_pct) : overcast_gray_gradient(cloud_pct);
}

// temp_unit: 0=Celsius (input is already Celsius, passed through),
// 1=Fahrenheit, 2=Kelvin (whole-degree precision throughout this app,
// so +273 rather than +273.15 -- the .15 essentially never changes
// the rounded result at this precision).
static int16_t convert_temp(int16_t celsius, uint8_t temp_unit) {
  if (temp_unit == 1) return (int16_t)((celsius * 9) / 5 + 32);
  if (temp_unit == 2) return (int16_t)(celsius + 273);
  return celsius;
}
static const char *temp_unit_suffix(uint8_t temp_unit) {
  if (temp_unit == 1) return "F";
  if (temp_unit == 2) return "K";
  return "C";
}

// wind_speed_unit: 0=km/h (input is already km/h, passed through),
// 1=mph, 2=m/s, 3=knots.
static int16_t convert_wind(int16_t kmh, uint8_t wind_speed_unit) {
  if (wind_speed_unit == 1) return (int16_t)((kmh * 621) / 1000);  // mph
  if (wind_speed_unit == 2) return (int16_t)((kmh * 1000) / 3600); // m/s
  if (wind_speed_unit == 3) return (int16_t)((kmh * 540) / 1000);  // knots
  return kmh;
}

#define CORNER_BOX_W 68
#define CORNER_ROW_H 24

// Combined width of an icon plus its gap before the text that
// follows it, per icon_kind -- used to position the icon+text group
// as a unit for alignment (see draw_corner_item).
static int16_t icon_plus_gap_width(int icon_kind) { // TODO: This might not be neccessary anymore
  switch (icon_kind) {
    case 1: case 2: case 5: case 6: case 7: case 8: case 9: case 10:
      return 11; // bitmap icons (7-wide at 140% scale) + gap
    case 3: return 10; // battery + gap
    case 4: return 21; // moon (radius 9, so 2*9+2 diameter box) + gap
    case 11: return 22; // sun-time glyph (fixed 20px, drawn via direct primitives) + gap
    default: return 0; // no icon
  }
}

// Renders one corner's chosen content type in one of the four color
// modes. The icon+text group is measured and positioned as a unit:
// left-anchored slots (TL/BL/middle-left) keep it flush left, right-
// anchored slots (TR/BR/middle-right) flush it against the box's own
// right edge (which is itself already anchored near the screen edge)
// so short content doesn't leave an empty gap before the edge, and
// center-anchored slots (upper/bottom-middle) center it within the
// box. The icon (when present) always precedes the text in reading
// order regardless of alignment -- only the whole group's position
// changes, not the icon/text order within it.
static void draw_corner_item(GContext *ctx, GRect bounds, uint8_t content, uint8_t color_mode,
                              GColor main_color, GColor accent_color, GColor bg_color,
                              bool is_top, bool is_left, bool is_middle, int16_t top_offset, int16_t bottom_shift,
                              bool center_horizontal, bool center_vertical) {
  if (content == 0) return;

  char buf[24];
  int icon_kind = 0; // 0=none, 1=heart, 2=foot, 3=battery, 4=moon phase, 5=umbrella, 6=droplet,
                       // 7=wind, 8=GPS pin, 9=eye, 10=clouds, 11=sunrise/sunset
  bool icon_is_sunrise = false; // only meaningful when icon_kind == 11
  GColor dynamic_color = main_color;

  switch (content) {
    case 1: { // heart rate -- red if a recent reading is available, gray otherwise
      icon_kind = 1;
      int bpm = 0;
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
      }
      if (bpm > 0) {
        snprintf(buf, sizeof(buf), "%d", bpm);
        dynamic_color = GColorFromRGB(220, 20, 20);
      } else {
        snprintf(buf, sizeof(buf), "--");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 2: { // steps today
      icon_kind = 2;
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      snprintf(buf, sizeof(buf), "%d", (int)steps);
      uint16_t goal = s_data.daily_step_goal > 0 ? s_data.daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 100) pct = 100;
      dynamic_color = red_green_gradient((uint8_t)pct);
      break;
    }
    case 3: { // step goal %
      icon_kind = 2;
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      uint16_t goal = s_data.daily_step_goal > 0 ? s_data.daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 999) pct = 999;
      snprintf(buf, sizeof(buf), "%d%%", (int)pct);
      dynamic_color = red_green_gradient((uint8_t)(pct > 100 ? 100 : pct));
      break;
    }
    case 4: { // high/low temperature -- same readout the fixed bottom-left corner used to show
      int16_t hi = convert_temp(s_data.temp_high_c, s_data.temp_unit);
      int16_t lo = convert_temp(s_data.temp_low_c, s_data.temp_unit);
      snprintf(buf, sizeof(buf), "H%d L%d", hi, lo);
      // Only used for the mono/accent/translucent color modes, which
      // still share one color across the combined "H.. L.." string --
      // dynamic mode splits high and low into their own separately
      // gradient-colored segments instead (see the special case near
      // the final text draw below), so this particular value goes
      // unused in that combination.
      dynamic_color = seven_stop_gradient(s_data.temp_high_c, -10, 40);
      break;
    }
    case 5: { // current conditions -- same readout the fixed bottom-right corner used to show
      int16_t temp = convert_temp(s_data.weather_temp_c, s_data.temp_unit);
      snprintf(buf, sizeof(buf), "%d%s %s", temp, temp_unit_suffix(s_data.temp_unit),
               short_condition_text(s_data.weather_condition, s_data.cloud_cover_pct));
      dynamic_color = s_data.valid
        ? weather_condition_color(s_data.weather_condition, s_data.cloud_cover_pct, s_data.rain_chance_pct)
        : bg_color;
      break;
    }
    case 6: { // UV index
      uint8_t uv = s_data.uv_index_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      dynamic_color = seven_stop_gradient(uv, 1, 13);
      break;
    }
    case 7: { // rain chance
      icon_kind = 5;
      snprintf(buf, sizeof(buf), "%d%%", s_data.rain_chance_pct);
      dynamic_color = white_to_turquoise_gradient(s_data.rain_chance_pct, 0, 100);
      break;
    }
    case 8: { // humidity
      icon_kind = 6;
      snprintf(buf, sizeof(buf), "%d%%", s_data.humidity_pct);
      dynamic_color = white_to_turquoise_gradient(s_data.humidity_pct, 0, 100);
      break;
    }
    case 9: { // wind
      icon_kind = 7;
      snprintf(buf, sizeof(buf), "%d", convert_wind(s_data.wind_speed_kmh, s_data.wind_speed_unit));
      dynamic_color = white_to_turquoise_gradient(s_data.wind_speed_kmh, 0, 60);
      break;
    }
    case 10: { // battery
      icon_kind = 3;
      BatteryChargeState bs = battery_state_service_peek();
      if (bs.is_charging) {
        snprintf(buf, sizeof(buf), "%d%%+", bs.charge_percent);
      } else {
        snprintf(buf, sizeof(buf), "%d%%", bs.charge_percent);
      }
      dynamic_color = red_green_gradient((uint8_t)bs.charge_percent);
      break;
    }
    case 11: { // Moon phase -- icon + short name
      icon_kind = 4;
      snprintf(buf, sizeof(buf), "%s", moon_phase_short_name(s_data.moon_phase_pct, s_data.moon_waxing));
      dynamic_color = GColorWhite; // no numeric dimension to grade on; white is the Moon's natural color
      break;
    }
    case 12: { // short date -- no icon, e.g. "Mon 15"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      char day_buf[4], mday_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      snprintf(mday_buf, sizeof(mday_buf), "%d", t->tm_mday);
      snprintf(buf, sizeof(buf), "%s %s", day_buf, mday_buf);
      dynamic_color = main_color; // no natural "value" to grade on
      break;
    }
    case 13: { // location name
      icon_kind = 8;
      snprintf(buf, sizeof(buf), "%s", s_data.location_name[0] != '\0' ? s_data.location_name : "Unknown");
      dynamic_color = main_color; // no natural "value" to grade on
      break;
    }
    case 14: { // visibility ("chance you'll actually see it" score)
      icon_kind = 9;
      snprintf(buf, sizeof(buf), "%d%%", s_data.vis_score_pct);
      dynamic_color = red_green_gradient(s_data.vis_score_pct);
      break;
    }
    case 15: { // cloud cover
      icon_kind = 10;
      snprintf(buf, sizeof(buf), "%d%%", s_data.cloud_cover_pct);
      dynamic_color = overcast_gray_gradient(s_data.cloud_cover_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : s_data.cloud_cover_pct);
      break;
    }
    case 16: { // sunrise/sunset -- same event/icon as the digital/analog info panel's row
      icon_kind = 11;
      time_t now = time(NULL);
      time_t sun_event_time = 0;
      if (get_next_sun_event(now, s_data.sun_rise, s_data.sun_set, &sun_event_time, &icon_is_sunrise)) {
        struct tm *event_t = localtime(&sun_event_time);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", event_t);
      } else {
        snprintf(buf, sizeof(buf), "--:--");
      }
      dynamic_color = main_color; // no natural "value" to grade on
      break;
    }
    case 17: { // pebble battery logo
      icon_kind = 12;
      BatteryChargeState bs = battery_state_service_peek();
      dynamic_color = red_green_gradient((uint8_t)bs.charge_percent);
      break;
    }
    case 18: { // digital time
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
      dynamic_color = seven_stop_gradient((int32_t)(t->tm_hour*60+t->tm_min), 0, 1440);
      break;
    }
    case 19: { // week number
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "WK %V", t);
      dynamic_color = seven_stop_gradient((int32_t)(t->tm_yday), 0, 360);
      break;
    }
    default:
      return;
  }

  GColor color;
  bool translucent = false;
  switch (color_mode) {
    case 1: color = accent_color; break;
    case 2: color = accent_color; translucent = true; break;
    case 3: color = dynamic_color; break;
    case 0:
    default: color = main_color; break;
  }

  int16_t box_x = center_horizontal
    ? bounds.origin.x + (bounds.size.w - CORNER_BOX_W) / 2
    : (is_left ? bounds.origin.x + 2 : bounds.origin.x + bounds.size.w - 2 - CORNER_BOX_W);
  int16_t box_y = center_vertical
    ? bounds.origin.y + (bounds.size.h - CORNER_ROW_H) / 2
    : (is_top ? bounds.origin.y + top_offset
              : bounds.origin.y + bounds.size.h - CORNER_ROW_H - 2 - bottom_shift);
  
  if (is_middle) {
    if (is_left) {
      box_x += 30;
    } else {
      box_x -= 30;
    }
  }

  if (translucent) {
    // A dithered highlight plate behind the icon+text -- gives
    // "translucent accent" a real visual difference from "solid
    // accent" without needing per-glyph dithering (system fonts
    // can't be inspected pixel-by-pixel the way the procedural tiny
    // digit/icon bitmaps above can).
    GPoint plate_pts[4] = {
      GPoint(box_x, box_y), GPoint(box_x + CORNER_BOX_W, box_y),
      GPoint(box_x + CORNER_BOX_W, box_y + CORNER_ROW_H), GPoint(box_x, box_y + CORNER_ROW_H)
    };
    fill_polygon_dithered(ctx, plate_pts, 4, color);
  }

  GFont font = get_corner_font();
  int16_t font_h = corner_font_height_estimate();
  GSize text_size = graphics_text_layout_get_content_size(buf, font, GRect(0, 0, 200, font_h + 10),
                                                            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t icon_gap_w = icon_plus_gap_width(icon_kind);
  int16_t group_w = icon_gap_w + text_size.w;
  if (group_w > CORNER_BOX_W) group_w = CORNER_BOX_W;

  int16_t group_x;
  if (center_horizontal) {
    group_x = box_x + (CORNER_BOX_W - group_w) / 2;
  } else if (!is_left) {
    group_x = box_x + CORNER_BOX_W - group_w;
  } else {
    group_x = box_x;
  }
  int16_t icon_x = group_x;
  int16_t text_x = group_x + icon_gap_w;

  bool do_icon_outline = s_data.outline_enabled && color_mode != 2;
  GColor icon_outline_color = contrasting_outline_color(color);
  switch (icon_kind) {
    case 1: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          HEART_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, HEART_ICON, ICON_ROWS, ICON_WIDTH, color);
      break;
    }
    case 2: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 11) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          FOOT_ICON, 13, 8, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, FOOT_ICON, 13, 8, color);
      break;
    }
    case 3: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 14) / 2);
      BatteryChargeState bs = battery_state_service_peek();
      
      if (bs.is_charging) {
        color = GColorGreen;
        icon_outline_color = GColorBlack;
      }
      
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_corner_battery_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), icon_outline_color, 0);
        }
      }
      
      draw_corner_battery_icon(ctx, pos, color, bs.charge_percent);
      break;
    }
    case 4: {
      int16_t moon_r = 9;
      GPoint moon_center = GPoint(icon_x + moon_r, box_y + CORNER_ROW_H / 2);
      GRect moon_clip = GRect(icon_x, box_y, moon_r * 2 + 2, CORNER_ROW_H);
      if (do_icon_outline) {
        // A simplified bounding-circle outline rather than 4 extra
        // full phase-shaded passes -- draw_moon_phase's lit/dark
        // split is a per-pixel loop, so replicating it 4x would cost
        // meaningfully more for a detail (the outline's silhouette
        // exactly following the phase terminator) that isn't visible
        // at this size anyway.
        graphics_context_set_fill_color(ctx, icon_outline_color);
        for (int i = 0; i < 4; i++) {
          GPoint shifted = GPoint(moon_center.x + OUTLINE_OFFSETS[i].x, moon_center.y + OUTLINE_OFFSETS[i].y);
          graphics_fill_circle(ctx, shifted, moon_r);
        }
      }
      draw_moon_phase(ctx, moon_clip, moon_center, moon_r, s_data.moon_phase_pct, s_data.moon_waxing, color);
      break;
    }
    case 5: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+10, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          UMBRELLA_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, UMBRELLA_ICON, ICON_ROWS, ICON_WIDTH, color);
      break;
    }
    case 6: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+10, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          DROPLET_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, DROPLET_ICON, ICON_ROWS, ICON_WIDTH, color);
      break;
    }
    case 7: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+10, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          WIND_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, WIND_ICON, ICON_ROWS, ICON_WIDTH, color);
      break;
    }
    case 8: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          GPS_PIN_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, GPS_PIN_ICON, ICON_ROWS, ICON_WIDTH, color);
      break;
    }
    case 9: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          EYE_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, EYE_ICON, ICON_ROWS, ICON_WIDTH, color);
      break;
    }
    case 10: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          CLOUD_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, CLOUD_ICON, ICON_ROWS, ICON_WIDTH, color);
      break;
    }
    case 11: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 9) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_sun_time_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                              icon_is_sunrise, icon_outline_color, bg_color);
        }
      }
      draw_sun_time_icon(ctx, pos, icon_is_sunrise, color, bg_color);
      break;
    }
    case 12: {
      GPoint pos = GPoint(icon_x - 15, box_y + (CORNER_ROW_H - 10) / 2);
      BatteryChargeState bs = battery_state_service_peek();
      graphics_context_set_stroke_width(ctx, 1);
      GPoint p1 = GPoint(pos.x + 3, pos.y + 9);
      GPoint p2 = GPoint(pos.x + 3 + (35 * bs.charge_percent / 100), pos.y + 9);
      
      if (bs.is_charging) {
        color = GColorGreen;
        icon_outline_color = GColorBlack;
      }
      
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          PEBBLE_ICON, 10, 40, icon_outline_color);
        }
        graphics_context_set_stroke_color(ctx, icon_outline_color);
        graphics_draw_line(ctx, GPoint(p1.x , p1.y + 1), GPoint(p2.x , p2.y + 1));
        graphics_draw_line(ctx, GPoint(p1.x - 1, p1.y), GPoint(p2.x + 1, p2.y));
        graphics_draw_line(ctx, GPoint(p1.x , p1.y - 1), GPoint(p2.x , p2.y - 1));
      }
      
      draw_tiny_icon(ctx, pos, PEBBLE_ICON, 10, 40, color);
      
      graphics_context_set_stroke_color(ctx, color);
      graphics_draw_line(ctx, p1, p2);
      break;
    }
    default:
      break;
  }

  if (content == 4 && color_mode == 3) {
    // Min/max temperature each get their own gradient color rather
    // than sharing one -- split the remaining box width in half and
    // draw "H.." / "L.." as two independently-colored segments
    // instead of the generic single-color path below.
    int16_t hi = convert_temp(s_data.temp_high_c, s_data.temp_unit);
    int16_t lo = convert_temp(s_data.temp_low_c, s_data.temp_unit);
    char hi_buf[8], lo_buf[8];
    snprintf(hi_buf, sizeof(hi_buf), "H%d", hi);
    snprintf(lo_buf, sizeof(lo_buf), "L%d", lo);
    GColor hi_color = seven_stop_gradient(s_data.temp_high_c, -10, 40);
    GColor lo_color = seven_stop_gradient(s_data.temp_low_c, -10, 40);
    int16_t half_w = (CORNER_BOX_W - (text_x - box_x)) / 2;
    draw_text_outlined(ctx, hi_buf, font,
                        GRect(text_x, box_y + (CORNER_ROW_H - font_h) / 2, half_w, font_h + 2),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                        hi_color, s_data.outline_enabled);
    draw_text_outlined(ctx, lo_buf, font,
                        GRect(text_x + half_w, box_y + (CORNER_ROW_H - font_h) / 2, half_w, font_h + 2),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                        lo_color, s_data.outline_enabled);
    return;
  }

  if (content != 17) {
    draw_text_outlined(ctx, buf, font,
                       GRect(text_x, box_y + (CORNER_ROW_H - font_h) / 2, text_size.w + 2, font_h + 2),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                       color, s_data.outline_enabled);
  }
}

// The four-corners overlay. Deliberately a separate layer with its
// own independent refresh timer (see corners_timer_callback) rather
// than being drawn as part of the sky canvas or tied to its redraw
// cycle -- per the brief, updating this (e.g. for a fresh heart rate
// reading) should never force the much more expensive sky canvas to
// redraw too. Like the hands layer, never fills its own background,
// so the sky canvas underneath shows through everywhere except where
// content is actually drawn.
static void corners_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_unobstructed_bounds(layer);
  time_t now = time(NULL);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(now, &bg, &main_color, &accent_color);
  ensure_corner_custom_font(s_data.corner_custom_font);

  bool is_big_analog = s_data.bottom_style == 2;
  uint8_t marker_style = s_data.big_analog_marker_style;
  bool is_bitmap_style = is_big_analog && marker_style >= 3 && marker_style != 8;

  // Which of the 4 edge-middle slots (upper/bottom/left/right-middle)
  // does the current mode/style actually support? Digital/analog
  // modes use none of them. Big-analogue procedural styles (<3) have
  // no artwork to work around, so all 4 are available alongside the
  // corners. Big-analogue bitmap styles (>=3) are limited to
  // whichever slots that specific mask graphic's design has room for,
  // and always suppress the 4 corners (the mask already fills most
  // of the screen either way).
  bool show_upper = false, show_bottom = false, show_left = false, show_right = false;
  if (is_big_analog) {
    if (marker_style < 3 || marker_style == 8) {
      // Procedural styles, including custom (8): no fixed artwork to work
      // around, so all 4 corners stay available same as styles 0-2.
      show_upper = show_bottom = show_left = show_right = true;
    } else {
      switch (marker_style) {
        case 3: case 4: case 6: // Modern, Shadow, Bell -- only top middle and bottom middle
          show_upper = show_bottom = true;
          break;
        case 5: case 7: // tally, brown -- all inside
          show_upper = show_bottom = show_left = show_right = true;
          break;
        default: // swiss, bell -- only the top has clean space
          show_upper = true;
          break;
      }
    }
  }

  // Upper-middle sits further down (34px, was 4px) than the old
  // single-slot version -- too close to the top edge on the modern
  // mask's actual artwork. Bottom-middle mirrors that same ~30px
  // margin up from the bottom edge. Each is now a 2-line pair: when
  // line 2 has no content, line 1 shifts toward the vertical center
  // of where the pair would have sat, rather than staying pinned at
  // the "top line of two" position with an empty gap below/above it.
  if (show_upper) {
    bool has_line2 = s_data.upper_middle_line2_content != 0;
    int16_t line1_offset = has_line2 ? 44 : 44 + CORNER_ROW_H / 2;
    draw_corner_item(ctx, bounds, s_data.upper_middle_line1_content, s_data.upper_middle_line1_color_mode,
                      main_color, accent_color, bg, true, true, false, line1_offset, 0, true, false);
    if (has_line2) {
      draw_corner_item(ctx, bounds, s_data.upper_middle_line2_content, s_data.upper_middle_line2_color_mode,
                        main_color, accent_color, bg, true, true, false, 44 + CORNER_ROW_H, 0, true, false);
    }
  }
  if (show_bottom) {
    bool has_line2 = s_data.bottom_middle_line2_content != 0;
    int16_t line1_shift = has_line2 ? 40 + CORNER_ROW_H : 40 + CORNER_ROW_H / 2;
    draw_corner_item(ctx, bounds, s_data.bottom_middle_line1_content, s_data.bottom_middle_line1_color_mode,
                      main_color, accent_color, bg, false, true, false, 0, line1_shift, true, false);
    if (has_line2) {
      draw_corner_item(ctx, bounds, s_data.bottom_middle_line2_content, s_data.bottom_middle_line2_color_mode,
                        main_color, accent_color, bg, false, true, false, 0, 40, true, false);
    }
  }
  if (show_left) {
    draw_corner_item(ctx, bounds, s_data.middle_left_content, s_data.middle_left_color_mode,
                      main_color, accent_color, bg, false, true, true, 0, 0, false, true);
  }
  if (show_right) {
    draw_corner_item(ctx, bounds, s_data.middle_right_content, s_data.middle_right_color_mode,
                      main_color, accent_color, bg, false, false, true, 0, 0, false, true);
  }

  if (is_bitmap_style) return; // corners fully replaced by the slots above

  // Bottom corners shift up out of the way when the shake-revealed
  // ground bar is showing -- but that bar is itself suppressed in
  // analog mode (bottom_style == 1), where it'd be redundant with the
  // persistent info panel, so there's nothing to shift up for there.
  int16_t bottom_shift = (s_labels_visible && s_data.bottom_style != 1) ? 18 : 0;

  draw_corner_item(ctx, bounds, s_data.corner_content[0], s_data.corner_color_mode[0],
                    main_color, accent_color, bg, true, true, false, 2, 0, false, false);
  draw_corner_item(ctx, bounds, s_data.corner_content[1], s_data.corner_color_mode[1],
                    main_color, accent_color, bg, true, false, false, 2, 0, false, false);
  draw_corner_item(ctx, bounds, s_data.corner_content[2], s_data.corner_color_mode[2],
                    main_color, accent_color, bg, false, true, false, 0, bottom_shift, false, false);
  draw_corner_item(ctx, bounds, s_data.corner_content[3], s_data.corner_color_mode[3],
                    main_color, accent_color, bg, false, false, false, 0, bottom_shift, false, false);
}

// ---- rendering ---------------------------------------------------------

// The bottom third of the face: either the classic big-time-plus-date
// layout, or (bottom_style == 1) a split view with an analog clock on
// the left and four lines of text on the right. Both obey the color
// scheme and the seconds-visibility setting. Redrawn every second
// when seconds are shown; otherwise still cheap enough (no astronomy,
// just text/line drawing) not to bother throttling separately from
// the sky canvas above it.
static void bottom_canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  GColor bg, text_color, accent_color;
  get_active_color_scheme(now, &bg, &text_color, &accent_color);

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  char time_buf[10];
  if (s_data.show_seconds && !use_small_seconds_for_digital_clock()) {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S", t);
  } else {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  }
  char sec_buf[4];
  snprintf(sec_buf, sizeof(sec_buf), "%d\n%d",
           t->tm_sec / 10,
           t->tm_sec % 10);

  if (s_data.bottom_style == 1) {
    // ---- analog: clock on the left half, 4 lines of text on the right ----
    int16_t half_w = bounds.size.w / 2;
    GPoint clock_center = GPoint(bounds.origin.x + half_w / 2, bounds.origin.y + bounds.size.h / 2);
    int16_t clock_r = (bounds.size.h / 2) - 6;
    if (clock_r > half_w / 2 - 6) clock_r = half_w / 2 - 6;

    // Face style: 0=solid circle, 1=hour markers, 2=both, 3=tiny
    // procedural 12/3/6/9 numerals instead of any circle/markers.
    if (s_data.analog_style == 0 || s_data.analog_style == 2) {
      graphics_context_set_stroke_color(ctx, text_color);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_circle(ctx, clock_center, clock_r);
    }
    if (s_data.analog_style == 1 || s_data.analog_style == 2) {
      draw_hour_markers(ctx, clock_center, clock_r, text_color);
    }
    if (s_data.analog_style == 3) {
      int16_t px = 1; // "super tiny" -- 3x5px digits at 1px/dot
      int16_t label_r = clock_r - 7;
      draw_tiny_number(ctx, GPoint(clock_center.x, clock_center.y - label_r), 12, text_color, px);
      draw_tiny_number(ctx, GPoint(clock_center.x + label_r, clock_center.y), 3, text_color, px);
      draw_tiny_number(ctx, GPoint(clock_center.x, clock_center.y + label_r), 6, text_color, px);
      draw_tiny_number(ctx, GPoint(clock_center.x - label_r, clock_center.y), 9, text_color, px);
    }

    int32_t hour_angle = (((t->tm_hour % 12) * 60 + t->tm_min) * TRIG_MAX_ANGLE) / (12 * 60);
    int16_t hour_len = (clock_r * 55) / 100;
    GPoint hour_end = GPoint(
      clock_center.x + (hour_len * sin_lookup(hour_angle)) / TRIG_MAX_RATIO,
      clock_center.y - (hour_len * cos_lookup(hour_angle)) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, text_color);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, clock_center, hour_end);

    int32_t min_angle = (t->tm_min * TRIG_MAX_ANGLE) / 60;
    int16_t min_len = (clock_r * 80) / 100;
    GPoint min_end = GPoint(
      clock_center.x + (min_len * sin_lookup(min_angle)) / TRIG_MAX_RATIO,
      clock_center.y - (min_len * cos_lookup(min_angle)) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, clock_center, min_end);

    if (s_data.show_seconds) {
      int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;
      int16_t sec_len = (clock_r * 88) / 100;
      GPoint sec_end = GPoint(
        clock_center.x + (sec_len * sin_lookup(sec_angle)) / TRIG_MAX_RATIO,
        clock_center.y - (sec_len * cos_lookup(sec_angle)) / TRIG_MAX_RATIO);
      graphics_context_set_stroke_color(ctx, accent_color);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_line(ctx, clock_center, sec_end);
    }

    graphics_context_set_fill_color(ctx, text_color);
    graphics_fill_circle(ctx, clock_center, 3);

    char date_line[24];
    strftime(date_line, sizeof(date_line), "%a %b %d", t);
    char week_line[16];
    strftime(week_line, sizeof(week_line), "Week %V", t);
    char weather_line[40];
    snprintf(weather_line, sizeof(weather_line), "Clds %d%% Vis %d%%",
             s_data.cloud_cover_pct, s_data.vis_score_pct);
    const char *location_line = s_data.location_name[0] != '\0' ? s_data.location_name : "Unknown location";

    // Computed after the strftime()s above, since localtime() returns
    // a pointer to a shared static buffer -- calling it again here
    // for the sun event would otherwise clobber `t` before those
    // date/week strings got built from it.
    time_t sun_event_time = 0;
    bool sun_event_is_rise = false;
    bool show_sun_row = s_data.show_sun_time &&
      get_next_sun_event(now, s_data.sun_rise, s_data.sun_set, &sun_event_time, &sun_event_is_rise);
    char sun_time_buf[8] = "";
    if (show_sun_row) {
      struct tm *event_t = localtime(&sun_event_time);
      strftime(sun_time_buf, sizeof(sun_time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", event_t);
    }

    const char *lines[4] = { weather_line, location_line, date_line, week_line };
    int16_t line_h = bounds.size.h / 4;
    graphics_context_set_text_color(ctx, text_color);
    for (int i = 0; i < 4; i++) {
      int16_t row_y = bounds.origin.y + i * line_h;
      if (i == 3 && show_sun_row) {
        int16_t icon_x = bounds.origin.x + half_w + 4;
        int16_t icon_y = row_y + (line_h - 10) / 2;
        int16_t icon_w = draw_sun_time_icon(ctx, GPoint(icon_x, icon_y), sun_event_is_rise, text_color, bg);
        graphics_context_set_text_color(ctx, text_color);
        graphics_draw_text(ctx, sun_time_buf, small_font,
                            GRect(icon_x + icon_w + 4, row_y + get_small_font_height_offset(), half_w - 8 - icon_w - 4, line_h),
                            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      } else {
        graphics_draw_text(ctx, lines[i], small_font,
                            GRect(bounds.origin.x + half_w + 4, row_y + get_small_font_height_offset(), half_w - 8, line_h),
                            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      }
    }
  } else {
    // ---- digital: big time, small date/week below ----
    graphics_context_set_text_color(ctx, text_color);
    if (s_data.show_seconds && use_small_seconds_for_digital_clock()) {
      graphics_draw_text(ctx, time_buf, clock_font,
                          GRect(bounds.origin.x, bounds.origin.y + get_clock_font_height_offset(), bounds.size.w - 20, 60),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      graphics_context_set_text_color(ctx, accent_color);
      graphics_draw_text(ctx, sec_buf, small_font,
                         GRect(bounds.origin.x + bounds.size.w - 22, bounds.origin.y + 15 + get_small_font_height_offset() * 2, 20, 50),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    } else {
      graphics_draw_text(ctx, time_buf, clock_font,
                          GRect(bounds.origin.x, bounds.origin.y + get_clock_font_height_offset(), bounds.size.w, 60),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    char main_buf[32];
    strftime(main_buf, sizeof(main_buf), "%a %b %d", t);

    // Computed before any second localtime() call below, since that
    // returns a pointer to a shared static buffer and would otherwise
    // clobber `t` before main_buf got built from it.
    time_t sun_event_time = 0;
    bool sun_event_is_rise = false;
    bool show_sun_row = s_data.show_sun_time &&
      get_next_sun_event(now, s_data.sun_rise, s_data.sun_set, &sun_event_time, &sun_event_is_rise);

    graphics_context_set_text_color(ctx, text_color);
    if (show_sun_row) {
      char sun_time_buf[8];
      struct tm *event_t = localtime(&sun_event_time);
      strftime(sun_time_buf, sizeof(sun_time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", event_t);

      int16_t left_w = (bounds.size.w * 55) / 100;
      graphics_draw_text(ctx, main_buf, small_font,
                          GRect(bounds.origin.x, bounds.origin.y + 60 + get_small_font_height_offset(), left_w, 16),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
      int16_t icon_x = bounds.origin.x + left_w + 6;
      int16_t icon_y = bounds.origin.y + 60 + 5;
      int16_t icon_w = draw_sun_time_icon(ctx, GPoint(icon_x, icon_y), sun_event_is_rise, text_color, bg);
      graphics_context_set_text_color(ctx, text_color);
      graphics_draw_text(ctx, sun_time_buf, small_font,
                          GRect(icon_x + icon_w + 3, bounds.origin.y + 60 + get_small_font_height_offset(), bounds.size.w - (icon_x + icon_w + 3), 16),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    } else {
      char week_buf[8];
      strftime(week_buf, sizeof(week_buf), "%V", t);
      char date_buf[40];
      snprintf(date_buf, sizeof(date_buf), "%s  -  Wk%s", main_buf, week_buf);
      graphics_draw_text(ctx, date_buf, small_font,
                          GRect(bounds.origin.x, bounds.origin.y + 60 + get_small_font_height_offset(), bounds.size.w, 16),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  }
}

// The countdown/status label used to be a plain TextLayer, but that
// has no way to draw a custom outline, so it's a plain Layer with its
// own update_proc instead -- reads whatever refresh_status_and_maybe_
// canvas() last stored in s_countdown_buf/s_countdown_text_color
// rather than taking them as parameters, since layer update_procs
// have a fixed signature.
static void countdown_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  draw_text_outlined(ctx, s_countdown_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), bounds,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                      s_countdown_text_color, s_data.outline_enabled);
}

static void refresh_status_and_maybe_canvas(bool force_canvas) {
  time_t now = time(NULL);
  eclipse_get_status_text(&s_data, now, s_countdown_buf, sizeof(s_countdown_buf));
  // The countdown label overlays the sky canvas transparently, so its
  // own contrast needs to track the sky brightness underneath it --
  // this check is cheap (no drawing), so it's fine to do every second.
  s_countdown_text_color = eclipse_sky_is_bright(&s_data, now) ? GColorBlack : GColorWhite;
  // Hidden entirely (not just left blank) when there's confirmed to
  // be no eclipse today -- this field is purely about eclipse phases
  // now (see eclipse_get_status_text), so there's nothing for it to
  // show in that case. Error/loading states still display normally,
  // since those aren't "no eclipse," they're "don't know yet."
  layer_set_hidden(s_countdown_layer, s_data.valid && !s_data.has_eclipse);
  layer_mark_dirty(s_countdown_layer);

  // The bottom canvas (time/date, or the analog clock) is cheap
  // enough (no astronomy) to just redraw every second directly. Only
  // exists in digital/analog mode -- big-analogue mode has no bottom
  // bar, and redraws its hands layer every second instead, for the
  // same reason (cheap, no astronomy, ticks smoothly).
  if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  if (s_hands_layer) layer_mark_dirty(s_hands_layer);

  // New data (or an explicit force, e.g. right after window_load)
  // always redraws immediately; otherwise just nudge the canvas every
  // second and let its own internal once-a-minute throttle (tracked
  // inside canvas_update_proc itself) decide whether that actually
  // costs a redraw -- this is the single biggest lever on battery
  // life for this watchface, and now holds regardless of what
  // triggers the nudge, not just this call site's discipline.
  if (force_canvas) {
    eclipse_canvas_set_data(s_canvas_layer, &s_data);
  } else {
    eclipse_canvas_tick(s_canvas_layer);
  }
}

// ---- persistence --------------------------------------------------------

// Build-time guard: if EclipseData ever outgrows the chunk capacity
// above again (the same kind of growth that caused the original
// persistence bug -- the struct reached 776 bytes against Pebble's
// 256-byte-per-key limit before this was caught), this deliberately
// fails to compile instead of silently truncating what gets saved.
typedef char persist_capacity_check[
  (PERSIST_CHUNK_SIZE * PERSIST_CHUNK_COUNT >= (int)sizeof(EclipseData)) ? 1 : -1];

static void save_data(void) {
  const uint8_t *bytes = (const uint8_t *)&s_data;
  size_t remaining = sizeof(s_data);
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t chunk_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    persist_write_data(PERSIST_KEY_DATA_BASE + i, bytes + (size_t)i * PERSIST_CHUNK_SIZE, chunk_len);
    remaining -= chunk_len;
  }
}

// Only trusts a reload when every chunk key that a full, same-version
// save would have produced is present and reports exactly the size
// expected for its position -- an old struct layout (different total
// size, so a different chunk count/sizing) or a save that was
// interrupted partway through won't pass this and the app stays at
// the zeroed (invalid) state instead, which correctly shows "Waiting
// for phone..." rather than loading a partial/misaligned struct. See
// the comment on EclipseData's growth history for why this matters:
// a mismatched reload doesn't just leave new fields at zero, it can
// misread old bytes as entirely different fields.
static void load_data(void) {
  memset(&s_data, 0, sizeof(s_data));
  size_t total = sizeof(s_data);
  size_t remaining = total;
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t expect_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    if (!persist_exists(PERSIST_KEY_DATA_BASE + i) || persist_get_size(PERSIST_KEY_DATA_BASE + i) != (int)expect_len) {
      return;
    }
    remaining -= expect_len;
  }
  uint8_t *bytes = (uint8_t *)&s_data;
  remaining = total;
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t chunk_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    persist_read_data(PERSIST_KEY_DATA_BASE + i, bytes + (size_t)i * PERSIST_CHUNK_SIZE, chunk_len);
    remaining -= chunk_len;
  }
}

// ---- AppMessage ---------------------------------------------------------

static void request_update(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
  app_message_outbox_send();
}

static bool use_small_seconds_for_digital_clock() {
  switch (s_data.clock_font) {
    case 2: // RESOURCE_ID_SFPIXELATE_FONT_48
    case 3: // RESOURCE_ID_RADIOLAND_FONT_48
    case 4: // RESOURCE_ID_MINISYSTEM_FONT_48
    case 5: // RESOURCE_ID_MINECRAFTER_FONT_48
    case 6: // RESOURCE_ID_KITCHENPOLICE_FONT_48
    case 12: // RESOURCE_ID_AUDIOWIDE_FONT_48 // good
    case 14: // RESOURCE_ID_KOMIKAHB_FONT_48 // great one
      return true;
    case 1: //RESOURCE_ID_CLOCKFORGE_FONT_48
    case 7: // RESOURCE_ID_DSDIGIB_FONT_48
    case 8: // RESOURCE_ID_DISTGRG_FONT_48
    case 9: // RESOURCE_ID_DIMITRI_FONT_48
    case 10: // RESOURCE_ID_DIGITALDREAM_FONT_48
    case 11: // RESOURCE_ID_BLACKOUT_FONT_48 // i like this
    case 13: // RESOURCE_ID_FORMATION_FONT_48 // looks ok
    case 15: // RESOURCE_ID_MISO_FONT_48 // good one
    case 16: // RESOURCE_ID_PRICEDOWN_FONT_48 // gta vibes, good one
    case 17: // FONT_KEY_ROBOTO_BOLD_SUBSET_49 // roboto
    case 18: // FONT_KEY_BITHAM_42_LIGHT // bitham light
    case 19: // FONT_KEY_BITHAM_42_BOLD // bitham bold
    case 20: // RESOURCE_ID_BEBAS_FONT_48 // tally font
    default: // FONT_KEY_LECO_42_NUMBERS
      return false;
  }
}

static int get_small_font_height_offset() {
  switch (s_data.clock_font) {
    case 1: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return 0;
    case 2: // FONT_KEY_GOTHIC_14
      return 0;
    case 3: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return -2;
    case 4: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return -2;
    case 5: // RESOURCE_ID_MINECRAFTER_FONT_12
      return 0;
    case 6: // FONT_KEY_GOTHIC_14
      return 0;
    case 7: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return -2;
    case 8: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 9: // FONT_KEY_GOTHIC_14
      return -2;
    case 10: // RESOURCE_ID_DIGITALDREAM_FONT_12
      return 0;
    case 11: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 12: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 13: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    case 14: // RESOURCE_ID_MISO_FONT_19
      return -4;
    case 15: // RESOURCE_ID_MISO_FONT_19
      return -4;
    case 16: // RESOURCE_ID_MINECRAFTER_FONT_12
      return -2;
    case 17: // FONT_KEY_ROBOTO_CONDENSED_21
      return -2;
    case 18: // FONT_KEY_GOTHIC_14
      return 0;
    case 19: // FONT_KEY_GOTHIC_14
      return 0;
    case 20: // RESOURCE_ID_BEBAS_FONT_20
      return -6;
    default: // FONT_KEY_GOTHIC_14
      return 0;
  }
}

static int get_clock_font_height_offset() {
  switch (s_data.clock_font) {
    case 5: // RESOURCE_ID_MINECRAFTER_FONT_48
      return 4;
    case 8: // RESOURCE_ID_DISTGRG_FONT_48
      return -6;
    case 9: // RESOURCE_ID_DIMITRI_FONT_48
    case 11: // RESOURCE_ID_BLACKOUT_FONT_48 // i like this
    case 12: // RESOURCE_ID_AUDIOWIDE_FONT_48 // good
    case 13: // RESOURCE_ID_FORMATION_FONT_48 // looks ok
    case 17: // FONT_KEY_ROBOTO_BOLD_SUBSET_49 // roboto
    case 10: // RESOURCE_ID_DIGITALDREAM_FONT_48
      return -2;
    case 15: // RESOURCE_ID_MISO_FONT_48 // good one
    case 7: // RESOURCE_ID_DSDIGIB_FONT_48
      return -4;
    case 1: //RESOURCE_ID_CLOCKFORGE_FONT_48
    case 2: // RESOURCE_ID_SFPIXELATE_FONT_48
    case 3: // RESOURCE_ID_RADIOLAND_FONT_48
    case 4: // RESOURCE_ID_MINISYSTEM_FONT_48
    case 6: // RESOURCE_ID_KITCHENPOLICE_FONT_48
    case 14: // RESOURCE_ID_KOMIKAHB_FONT_48 // great one
    case 16: // RESOURCE_ID_PRICEDOWN_FONT_48 // gta vibes, good one
    case 18: // FONT_KEY_BITHAM_42_LIGHT // bitham light
    case 19: // FONT_KEY_BITHAM_42_BOLD // bitham bold
    case 20: // RESOURCE_ID_BEBAS_FONT_48 // tally font
    default: // FONT_KEY_LECO_42_NUMBERS
      return 0;
  }
}

static void apply_clock_font(void) {
  if (clock_font_custom) {
    fonts_unload_custom_font(clock_font);
  }
  if (small_font_custom) {
    fonts_unload_custom_font(small_font);
  }
  
  if (s_data.bottom_style == 1) { // small analog + 4 data rows
    clock_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 1) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_CLOCKFORGE_FONT_48)); // looks kinda good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 2) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_SFPIXELATE_FONT_48)); // looks good
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = true;
    small_font_custom = false;
  } else if (s_data.clock_font == 3) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_RADIOLAND_FONT_48)); // looks good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 4) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINISYSTEM_FONT_48)); // looks kinda interesting
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 5) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_48)); // weird, but ok
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 6) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_KITCHENPOLICE_FONT_48)); // good
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = true;
    small_font_custom = false;
  } else if (s_data.clock_font == 7) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DSDIGIB_FONT_48)); // very good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 8) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DISTGRG_FONT_48)); // good, star wars wibes
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 9) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIMITRI_FONT_48)); // good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 10) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_48)); // wideee but good!
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DIGITALDREAM_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 11) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BLACKOUT_FONT_48)); // i like this
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 12) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_AUDIOWIDE_FONT_48)); // good
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 13) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FORMATION_FONT_48)); // looks ok
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 14) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_KOMIKAHB_FONT_48)); // great one
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MISO_FONT_19));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 15) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MISO_FONT_48)); // good one
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MISO_FONT_19));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 16) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_PRICEDOWN_FONT_48)); // gta vibes, good one
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_MINECRAFTER_FONT_12));
    clock_font_custom = true;
    small_font_custom = true;
  } else if (s_data.clock_font == 17) {
    clock_font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49); // roboto
    small_font = fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 18) {
    clock_font = fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT); // bitham light
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 19) {
    clock_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD); // bitham bold
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  } else if (s_data.clock_font == 20) {
    clock_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_48)); // tally font
    small_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_BEBAS_FONT_20));
    clock_font_custom = true;
    small_font_custom = true;
  } else {
    clock_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    clock_font_custom = false;
    small_font_custom = false;
  }

  if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_DATA_VALID))) {
    s_data.valid = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ERROR_CODE))) {
    s_data.error_code = t->value->uint8;
  }
  // Parsed (and applied) before the early-return below so a
  // font-only settings update still takes effect even if the watch
  // hasn't received a valid eclipse payload yet.
  if ((t = dict_find(iter, MESSAGE_KEY_CLOCK_FONT))) {
    s_data.clock_font = t->value->uint8;
    apply_clock_font();
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TEMP_UNIT))) {
    s_data.temp_unit = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_SPEED_UNIT))) {
    s_data.wind_speed_unit = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_SECONDS))) {
    s_data.show_seconds = t->value->uint8 != 0;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_SCHEME))) {
    s_data.color_scheme = t->value->uint8;
    if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_STYLE))) {
    s_data.bottom_style = t->value->uint8;
    apply_layout(); // may need to tear down/rebuild layers entirely -- see its own comment
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ANALOG_STYLE))) {
    s_data.analog_style = t->value->uint8;
    if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_MOON_SIZE_PCT))) {
    s_data.sun_moon_size_pct = t->value->uint8;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_RENDER_STYLE))) {
    s_data.cloud_render_style = t->value->uint8;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHAKE_LABEL_SECONDS))) {
    s_data.shake_label_seconds = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_INFO_BAR_MODE))) {
    s_data.bottom_info_bar_mode = t->value->uint8;
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_VIBRATE_ON_PHASE_CHANGE))) {
    s_data.vibrate_on_phase_change = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_OUTLINE_ENABLED))) {
    s_data.outline_enabled = t->value->uint8 != 0;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_FONT_SIZE))) {
    s_data.corner_font_size = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_CUSTOM_FONT))) {
    s_data.corner_custom_font = t->value->uint8;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_HAND_STYLE))) {
    s_data.big_analog_hand_style = t->value->uint8;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_HANDS_TRANSPARENT))) {
    s_data.big_analog_hands_transparent = t->value->uint8 != 0;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  }
  // Custom hand system (big_analog_hand_style == 4) -- see hand_layer.h.
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_STYLE))) s_data.hand_hour.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_WIDTH))) s_data.hand_hour.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_LENGTH))) s_data.hand_hour.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_BACK_OFFSET))) s_data.hand_hour.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_COLOR))) s_data.hand_hour.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_OUTLINE_ENABLED))) s_data.hand_hour.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_OUTLINE_COLOR))) s_data.hand_hour.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_HOUR_TRANSLUCENT))) s_data.hand_hour.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_STYLE))) s_data.hand_minute.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_WIDTH))) s_data.hand_minute.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_LENGTH))) s_data.hand_minute.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_BACK_OFFSET))) s_data.hand_minute.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_COLOR))) s_data.hand_minute.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_OUTLINE_ENABLED))) s_data.hand_minute.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_OUTLINE_COLOR))) s_data.hand_minute.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_MIN_TRANSLUCENT))) s_data.hand_minute.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_STYLE))) s_data.hand_second.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_WIDTH))) s_data.hand_second.width = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_LENGTH))) s_data.hand_second.length = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_BACK_OFFSET))) s_data.hand_second.back_offset = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_COLOR))) s_data.hand_second.color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_OUTLINE_ENABLED))) s_data.hand_second.outline_enabled = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_OUTLINE_COLOR))) s_data.hand_second.outline_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HAND_SEC_TRANSLUCENT))) s_data.hand_second.translucent = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_CENTER_CIRCLE_RADIUS))) s_data.center_circle_radius = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CENTER_CIRCLE_COLOR))) s_data.center_circle_color = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_BIG_ANALOG_MARKER_STYLE))) {
    s_data.big_analog_marker_style = t->value->uint8;
    if (s_hands_layer) layer_mark_dirty(s_hands_layer);
    if (s_corners_layer) layer_mark_dirty(s_corners_layer); // bitmap styles disable the 4 corners
  }
  // Custom marker system (big_analog_marker_style == 8) -- see marker_layer.h.
  // Any of these changing invalidates the cached ring bitmap automatically
  // next redraw (marker_layer_ensure_ring_cache() compares configs itself),
  // so all this needs to do is copy the values in and mark the layer dirty.
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_STYLE))) s_data.custom_hour_marker.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_THICKNESS))) s_data.custom_hour_marker.thickness = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_INNER_ECC))) s_data.custom_hour_marker.inner_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_OUTER_ECC))) s_data.custom_hour_marker.outer_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_INNER_BORDER))) s_data.custom_hour_marker.inner_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_HOUR_OUTER_BORDER))) s_data.custom_hour_marker.outer_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_STYLE))) s_data.custom_second_marker.style = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_THICKNESS))) s_data.custom_second_marker.thickness = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_INNER_ECC))) s_data.custom_second_marker.inner_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_OUTER_ECC))) s_data.custom_second_marker.outer_eccentricity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_INNER_BORDER))) s_data.custom_second_marker.inner_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SEC_OUTER_BORDER))) s_data.custom_second_marker.outer_border_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_TARGET))) s_data.marker_text.target = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_FONT))) s_data.marker_text.font_choice = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_OFFSET))) s_data.marker_text.offset_px = (int8_t)t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_HOUR_MASK))) s_data.marker_text.hour_mask = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_SEC_MASK))) s_data.marker_text.second_mask = t->value->uint16;
  // (no explicit layer_mark_dirty here -- refresh_status_and_maybe_canvas()
  // below already unconditionally marks s_hands_layer dirty every inbox batch)
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE1_CONTENT))) {
    s_data.upper_middle_line1_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE1_COLOR_MODE))) {
    s_data.upper_middle_line1_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE2_CONTENT))) {
    s_data.upper_middle_line2_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_UPPER_MIDDLE_LINE2_COLOR_MODE))) {
    s_data.upper_middle_line2_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE1_CONTENT))) {
    s_data.bottom_middle_line1_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE1_COLOR_MODE))) {
    s_data.bottom_middle_line1_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE2_CONTENT))) {
    s_data.bottom_middle_line2_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BOTTOM_MIDDLE_LINE2_COLOR_MODE))) {
    s_data.bottom_middle_line2_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_CONTENT))) {
    s_data.middle_left_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_COLOR_MODE))) {
    s_data.middle_left_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_CONTENT))) {
    s_data.middle_right_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_COLOR_MODE))) {
    s_data.middle_right_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_SUN_TIME))) {
    s_data.show_sun_time = t->value->uint8 != 0;
    if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_ISS))) {
    s_data.show_iss = t->value->uint8 != 0;
    if (s_canvas_layer) eclipse_canvas_set_data(s_canvas_layer, &s_data); // force immediately, not just mark dirty -- the canvas throttles plain redraws internally
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_CONTENT))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) s_data.corner_content[i] = raw[i];
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CORNER_COLOR_MODE))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) s_data.corner_color_mode[i] = raw[i];
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DAILY_STEP_GOAL))) {
    s_data.daily_step_goal = t->value->uint16;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_BG))) s_data.custom_bg = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_TEXT))) s_data.custom_text = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_ACCENT))) s_data.custom_accent = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_SCHEME_ENABLED))) {
    s_data.night_scheme_enabled = t->value->uint8 != 0;
    if (s_bottom_layer) layer_mark_dirty(s_bottom_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_COLOR_SCHEME))) s_data.night_color_scheme = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_CUSTOM_BG))) s_data.night_custom_bg = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_CUSTOM_TEXT))) s_data.night_custom_text = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_CUSTOM_ACCENT))) s_data.night_custom_accent = t->value->uint8;
  if (!s_data.valid) {
    save_data();
    refresh_status_and_maybe_canvas(true);
    return;
  }
  s_data.error_code = 0;

  if ((t = dict_find(iter, MESSAGE_KEY_C1_TIME))) s_data.c1 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_C2_TIME))) s_data.c2 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MAX_TIME))) s_data.max_t = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_C3_TIME))) s_data.c3 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_C4_TIME))) s_data.c4 = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SUNSET_TIME))) s_data.sunset = (time_t)t->value->int32;

  if ((t = dict_find(iter, MESSAGE_KEY_MAGNITUDE))) s_data.magnitude_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_ECLIPSE_TYPE))) {
    s_data.type = (EclipseType)t->value->uint8;
    s_data.has_eclipse = s_data.type != ECLIPSE_TYPE_NONE;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_POS_ANGLE))) s_data.pos_angle_deg = t->value->int16;

  if ((t = dict_find(iter, MESSAGE_KEY_SAMPLE_START))) s_data.sample_start = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SAMPLE_INTERVAL))) s_data.sample_interval_s = t->value->uint32;
  if ((t = dict_find(iter, MESSAGE_KEY_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    s_data.sample_count = count > MAX_SEP_SAMPLES ? MAX_SEP_SAMPLES : count;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SEP_SAMPLES))) {
    // Sent as a byte blob of uint16 (little-endian) values.
    uint16_t *samples = (uint16_t *)t->value->data;
    int n = t->length / sizeof(uint16_t);
    if (n > MAX_SEP_SAMPLES) n = MAX_SEP_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.sep_samples_centideg[i] = samples[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MAG_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > MAX_SEP_SAMPLES) n = MAX_SEP_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.mag_pct_samples[i] = raw[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_RADIUS_RATIO_PCT))) s_data.radius_ratio_pct = t->value->uint8;

  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_COVER))) s_data.cloud_cover_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_VIS_SCORE))) s_data.vis_score_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_SOURCES))) s_data.weather_sources = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_CONDITION))) s_data.weather_condition = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_C))) s_data.weather_temp_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_HIGH_C))) s_data.temp_high_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_LOW_C))) s_data.temp_low_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_UV_INDEX_X10))) s_data.uv_index_x10 = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_RAIN_CHANCE_PCT))) s_data.rain_chance_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_HUMIDITY_PCT))) s_data.humidity_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_SPEED_KMH))) s_data.wind_speed_kmh = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_LOCATION_NAME))) {
    strncpy(s_data.location_name, t->value->cstring, sizeof(s_data.location_name) - 1);
    s_data.location_name[sizeof(s_data.location_name) - 1] = '\0';
  }

  // Full-day sky background: sun altitude + cloud cover samples,
  // used to render the gradient and dithered clouds behind the sun.
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SAMPLE_START))) s_data.sky_sample_start = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SAMPLE_INTERVAL))) s_data.sky_sample_interval_s = t->value->uint32;
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SAMPLE_COUNT))) {
    uint8_t count = t->value->uint8;
    s_data.sky_sample_count = count > MAX_SKY_SAMPLES ? MAX_SKY_SAMPLES : count;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_ALT_SAMPLES))) {
    // Byte blob of int16 (little-endian) tenths-of-a-degree values.
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      uint16_t u = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
      s_data.sun_alt_decideg[i] = (int16_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      s_data.cloud_pct_samples[i] = raw[i];
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CLOUD_ALTITUDE_PCT))) s_data.cloud_altitude_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_ALT_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 2;
    if (n > MAX_SKY_SAMPLES) n = MAX_SKY_SAMPLES;
    for (int i = 0; i < n; i++) {
      uint16_t u = (uint16_t)raw[i * 2] | ((uint16_t)raw[i * 2 + 1] << 8);
      s_data.moon_alt_decideg[i] = (int16_t)u;
    }
  }
  // Packed as PLANET_COUNT rows of int16 (little-endian) samples,
  // concatenated in PlanetId order (see eclipse_data.h) -- one
  // message key instead of five near-duplicate ones.
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_ALT_SAMPLES))) {
    uint8_t *raw = t->value->data;
    int total_int16 = t->length / 2;
    int per_planet = total_int16 / PLANET_COUNT;
    if (per_planet > MAX_SKY_SAMPLES) per_planet = MAX_SKY_SAMPLES;
    for (int p = 0; p < PLANET_COUNT; p++) {
      for (int i = 0; i < per_planet; i++) {
        int src_idx = p * (total_int16 / PLANET_COUNT) + i;
        uint16_t u = (uint16_t)raw[src_idx * 2] | ((uint16_t)raw[src_idx * 2 + 1] << 8);
        s_data.planet_alt_decideg[p][i] = (int16_t)u;
      }
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_RISE))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 4;
    if (n > PLANET_COUNT) n = PLANET_COUNT;
    for (int p = 0; p < n; p++) {
      uint32_t u = (uint32_t)raw[p * 4] | ((uint32_t)raw[p * 4 + 1] << 8) |
                   ((uint32_t)raw[p * 4 + 2] << 16) | ((uint32_t)raw[p * 4 + 3] << 24);
      s_data.planet_rise[p] = (time_t)(int32_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_PLANET_SET))) {
    uint8_t *raw = t->value->data;
    int n = t->length / 4;
    if (n > PLANET_COUNT) n = PLANET_COUNT;
    for (int p = 0; p < n; p++) {
      uint32_t u = (uint32_t)raw[p * 4] | ((uint32_t)raw[p * 4 + 1] << 8) |
                   ((uint32_t)raw[p * 4 + 2] << 16) | ((uint32_t)raw[p * 4 + 3] << 24);
      s_data.planet_set[p] = (time_t)(int32_t)u;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SATURN_RING_OPEN_PCT))) s_data.saturn_ring_open_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_SKY_SCALE_MAX_ALT))) s_data.sky_scale_max_alt_decideg = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_PHASE_PCT))) s_data.moon_phase_pct = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_WAXING))) s_data.moon_waxing = t->value->uint8 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_RISE))) s_data.sun_rise = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_SET))) s_data.sun_set = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_RISE))) s_data.moon_rise = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_MOON_SET))) s_data.moon_set = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_METEOR_INTENSITY))) s_data.meteor_intensity = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_METEOR_SHOWER_NAME))) {
    strncpy(s_data.meteor_shower_name, t->value->cstring, sizeof(s_data.meteor_shower_name) - 1);
    s_data.meteor_shower_name[sizeof(s_data.meteor_shower_name) - 1] = '\0';
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_ALT))) s_data.iss_alt_deg = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_AZ))) s_data.iss_az_deg = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_ISS_COMPUTED_AT))) s_data.iss_computed_at = (time_t)t->value->int32;

  save_data();
  refresh_status_and_maybe_canvas(true);
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ---- tick + click ---------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh_status_and_maybe_canvas(false);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_update();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// ---- shake-to-reveal labels ------------------------------------------------

static AppTimer *s_label_timer = NULL;

static void hide_labels_callback(void *data) {
  s_label_timer = NULL;
  s_labels_visible = false;
  eclipse_canvas_set_show_labels(s_canvas_layer, false);
  if (s_corners_layer) layer_mark_dirty(s_corners_layer);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  eclipse_canvas_set_show_labels(s_canvas_layer, true);
  s_labels_visible = true;
  if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  uint8_t seconds = s_data.shake_label_seconds > 0 ? s_data.shake_label_seconds : 3;
  uint32_t reveal_ms = (uint32_t)seconds * 1000;
  if (s_label_timer) {
    app_timer_reschedule(s_label_timer, reveal_ms);
  } else {
    s_label_timer = app_timer_register(reveal_ms, hide_labels_callback, NULL);
  }
}

// ---- corners overlay's own independent refresh cycle ----------------------

// A separate, shorter cadence than the sky canvas's once-a-minute
// throttle -- health data (heart rate especially) is worth checking
// more often, and redrawing four small icons/numbers is cheap enough
// that doing it this often doesn't meaningfully affect battery life,
// as long as it never touches the much more expensive sky canvas.
#define CORNERS_REFRESH_MS 5000
static AppTimer *s_corners_timer = NULL;

static void corners_timer_callback(void *data) {
  if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  s_corners_timer = app_timer_register(CORNERS_REFRESH_MS, corners_timer_callback, NULL);
}

// ---- window lifecycle ----------------------------------------------------

// Creates the right set of layers for the current bottom_style, tearing
// down and rebuilding whatever's there if the style actually changed.
// Needed (rather than just resizing existing layers) because the sky
// canvas's cache bitmap is sized once at creation and Pebble has no
// API to resize a layer's internal state afterward -- a mode switch
// that changes the canvas's height means destroying and recreating
// it, not just adjusting its frame. Idempotent: safe to call after
// every settings update even when the style didn't change, since it
// no-ops in that case.
// Reacts to Timeline Quick View (or any future system overlay using
// this same API) appearing/disappearing at the bottom of the screen.
// Digital/analog mode's bottom panel shrinks from the bottom so it
// never extends past the obstruction; big-analogue mode has no
// separate bottom bar, so its sky canvas itself shrinks the same
// way, and its hands layer repositions itself around whatever's
// actually visible (see hands_layer_update_proc, which reads
// layer_get_unobstructed_bounds() directly).
static void unobstructed_change_handler(AnimationProgress progress, void *context) {
  if (!s_window) return;
  Layer *root = window_get_root_layer(s_window);
  GRect unobstructed = layer_get_unobstructed_bounds(root);

  if (s_data.bottom_style != 2 && s_bottom_layer) {
    GRect frame = layer_get_frame(s_bottom_layer);
    int16_t top = frame.origin.y; // fixed at 152 by apply_layout()
    int16_t new_h = unobstructed.size.h - top;
    if (new_h < 0) new_h = 0;
    if (frame.size.h != new_h) {
      frame.size.h = new_h;
      layer_set_frame(s_bottom_layer, frame);
    }
    layer_mark_dirty(s_bottom_layer);
  }

  if (s_data.bottom_style == 2 && s_canvas_layer) {
    GRect frame = layer_get_frame(s_canvas_layer);
    if (frame.size.h != unobstructed.size.h) {
      frame.size.h = unobstructed.size.h;
      layer_set_frame(s_canvas_layer, frame);
      // Force an immediate full redraw at the new size rather than
      // leaving the cached bitmap sized for the old frame -- the
      // canvas's own throttle would otherwise just blit that stale
      // cache back until its next scheduled minute.
      eclipse_canvas_set_data(s_canvas_layer, &s_data);
    }
  }

  if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  if (s_corners_layer) layer_mark_dirty(s_corners_layer);
}

static UnobstructedAreaHandlers s_unobstructed_handlers = {
  .change = unobstructed_change_handler
};

static void apply_layout(void) {
  Layer *root = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(root);
  uint8_t style = s_data.bottom_style;

  if (style == s_current_layout_style && s_canvas_layer != NULL) {
    return;
  }
  s_current_layout_style = style;

  if (s_canvas_layer) {
    eclipse_canvas_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
  if (s_bottom_layer) {
    layer_destroy(s_bottom_layer);
    s_bottom_layer = NULL;
  }
  if (s_hands_layer) {
    layer_destroy(s_hands_layer);
    s_hands_layer = NULL;
  }
  if (s_corners_layer) {
    layer_destroy(s_corners_layer);
    s_corners_layer = NULL;
  }

  if (style == 2) {
    // Big analogue: sky canvas fills the whole screen; hands render
    // in their own always-on-top transparent layer; no bottom bar.
    s_canvas_layer = eclipse_canvas_create(GRect(0, 0, bounds.size.w, bounds.size.h));
    layer_add_child(root, s_canvas_layer);
    s_hands_layer = layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
    layer_set_update_proc(s_hands_layer, hands_layer_update_proc);
    layer_add_child(root, s_hands_layer);
  } else {
    // Digital / analog: sky canvas keeps its original top-2/3
    // proportions; bottom third is the digital time or the analog
    // clock + 4-line info panel.
    s_canvas_layer = eclipse_canvas_create(GRect(0, 0, bounds.size.w, 152));
    layer_add_child(root, s_canvas_layer);
    s_bottom_layer = layer_create(GRect(0, 152, bounds.size.w, bounds.size.h - 152));
    layer_set_update_proc(s_bottom_layer, bottom_canvas_update_proc);
    layer_add_child(root, s_bottom_layer);
    apply_clock_font();
  }

  // Overlays exactly the sky canvas's own bounds -- reuses its
  // just-created frame rather than duplicating the size logic above,
  // so it always matches regardless of mode. Added after the canvas
  // (and, in big-analogue mode, after the hands) so it draws on top
  // of both, per the brief's "on top of the eclipse layer".
  s_corners_layer = layer_create(layer_get_frame(s_canvas_layer));
  layer_set_update_proc(s_corners_layer, corners_layer_update_proc);
  layer_add_child(root, s_corners_layer);

  // The countdown label always sits on top of everything else, so it
  // needs re-adding last after the canvas underneath was just rebuilt.
  if (s_countdown_layer) {
    layer_remove_from_parent(s_countdown_layer);
    layer_add_child(root, s_countdown_layer);
  }

  eclipse_canvas_set_data(s_canvas_layer, &s_data);

  // Newly (re)created layers start at their full, unobstructed frame
  // -- if Quick View already happens to be showing right when a mode
  // switch rebuilds them, apply that immediately rather than waiting
  // for the next .change event, matching Pebble's own recommended
  // pattern for handling Quick View already being active on load.
  unobstructed_change_handler(0, NULL);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  // Added first; apply_layout() re-parents it on top of whatever it
  // builds, both here on first load and again on any later mode
  // switch, so its own creation only has to happen once.
  s_countdown_layer = layer_create(GRect(0, 2, bounds.size.w, 20));
  layer_set_update_proc(s_countdown_layer, countdown_layer_update_proc);
  s_countdown_text_color = GColorBlack;
  layer_add_child(root, s_countdown_layer);

  apply_layout();
  apply_clock_font(); // uses whatever was loaded from persistent storage

  refresh_status_and_maybe_canvas(true);
}

static void window_unload(Window *window) {
  layer_destroy(s_countdown_layer);
  if (s_canvas_layer) eclipse_canvas_destroy(s_canvas_layer);
  if (s_bottom_layer) layer_destroy(s_bottom_layer);
  if (s_hands_layer) layer_destroy(s_hands_layer);
  if (s_corners_layer) layer_destroy(s_corners_layer);
  if (s_marker_bitmap) {
    gbitmap_destroy(s_marker_bitmap);
    s_marker_bitmap = NULL;
    s_marker_bitmap_style = 255;
    s_marker_bitmap_tinted = false;
  }
  if (s_corner_custom_font) {
    fonts_unload_custom_font(s_corner_custom_font);
    s_corner_custom_font = NULL;
  }
  marker_layer_deinit();
}

static void init(void) {
  load_data();

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);
  unobstructed_area_service_subscribe(s_unobstructed_handlers, NULL);
  s_corners_timer = app_timer_register(CORNERS_REFRESH_MS, corners_timer_callback, NULL);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  // Ask the phone for a fresh calculation as soon as we're up; PKJS
  // will also push updates on its own schedule (see index.js).
  request_update();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
  if (s_label_timer) {
    app_timer_cancel(s_label_timer);
    s_label_timer = NULL;
  }
  if (s_corners_timer) {
    app_timer_cancel(s_corners_timer);
    s_corners_timer = NULL;
  }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
