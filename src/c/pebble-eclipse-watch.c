#include <pebble.h>
#include "eclipse_data.h"
#include "background_layer.h"

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
// A GColor is just a packed byte (2 bits each of alpha/r/g/b) under
// the hood -- reconstructing one from a raw byte the settings page
// sent is exactly how the "pick any of the 64 real display colors"
// picker round-trips: the phone sends back whichever of the 64 the
// user tapped, packed the same way, and this just re-wraps it.
GColor gcolor_from_packed(uint8_t packed) {
  GColor c;
  c.argb = packed;
  return c;
}

// Picks the day or night set of colors based on the Sun's altitude
// (reusing eclipse_sky_is_bright()'s existing civil-twilight threshold
// rather than a second definition of "night") -- falls back to the day
// colors entirely if the user hasn't turned on separate night ones.
// Takes `d` explicitly (rather than reading the global s_data) so
// background_layer.c can call this too, for marker colors, using its
// own `d` (the same EclipseData, via the pointer eclipse_canvas_set_data()
// stored). The watch has no notion of a "preset" here -- every color
// arriving from the phone is already a concrete packed value; picking
// a named preset in the settings page just fills in these same three
// fields before sending, same as manually choosing each color would.
void get_active_color_scheme(const EclipseData *d, time_t now, GColor *bg, GColor *text, GColor *accent) {
  bool night = d->night_scheme_enabled && !eclipse_sky_is_bright(d, now);
  if (night) {
    *bg = gcolor_from_packed(d->night_custom_bg);
    *text = gcolor_from_packed(d->night_custom_text);
    *accent = gcolor_from_packed(d->night_custom_accent);
  } else {
    *bg = gcolor_from_packed(d->custom_bg);
    *text = gcolor_from_packed(d->custom_text);
    *accent = gcolor_from_packed(d->custom_accent);
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
// Whichever of today's sunrise/sunset is still ahead of `now`, falling
// back to tomorrow's sunrise once both of today's have passed (rather
// than reporting "no event" -- which used to show as "--:--" once
// today's sunset had passed, since only *today's* two contacts used to
// be sent at all).
static bool get_next_sun_event(time_t now, time_t sun_rise, time_t sun_set, time_t sun_rise_tomorrow,
                                time_t *event_time, bool *is_sunrise) {
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
  if (sun_rise_tomorrow != 0) {
    *event_time = sun_rise_tomorrow;
    *is_sunrise = true;
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

// ---- big-analogue marker styles (procedural + bitmap) --------------------
// Moved into background_layer.c -- markers now draw as part of the sky
// canvas's own cached redraw (see the design note at the top of that
// file), not from here every tick.

// ---- big-analogue hand-style presets (procedural, styles 0-3) ------------
// Recreates the 4 non-custom big_analog_hand_style options through
// hand_layer.c's shared drawing code, rather than a separate procedural
// drawing function -- same simplification trade-off the recreated
// procedural marker presets in background_layer.c make: these are static
// approximations (~92px reference radius, a 200x228 screen) rather than
// proportional to the actual screen radius the way the original
// formula-driven draw_big_hand() was. outline_enabled/outline_color/
// translucent are left at their zero-value defaults here and filled in
// at the call site instead, from the (still-global, still applicable to
// these 4 styles) outline_enabled and big_analog_hands_transparent
// settings -- see hands_layer_update_proc.
static const HandConfig HAND_STYLE_HOUR_PRESETS[4] = {
  { .style = 1, .width = 12, .length = 51, .back_offset = 6, .color = 0 },                     // 0: pointy
  { .style = 2, .width = 8,  .length = 51, .back_offset = 6, .color = 0 },                     // 1: square
  { .style = 2, .width = 6,  .length = 51, .back_offset = 8, .color = 0, .hollow = true },      // 2: modern
  { .style = 0, .width = 6,  .length = 51, .back_offset = 0, .color = 1 },                     // 3: rounded/classic (accent hour hand)
};
static const HandConfig HAND_STYLE_MIN_PRESETS[4] = {
  { .style = 1, .width = 12, .length = 78, .back_offset = 6, .color = 0 },
  { .style = 2, .width = 8, .length = 78, .back_offset = 6, .color = 0 },
  { .style = 2, .width = 6,  .length = 78, .back_offset = 8, .color = 0, .hollow = true },
  { .style = 0, .width = 6, .length = 78, .back_offset = 0, .color = 0 },
};
// The 4 procedural styles never customized the second hand -- always a
// plain accent-colored thin line -- so one shared preset covers all of them.
static const HandConfig HAND_STYLE_SEC_PRESET = {
  .style = 1, .width = 2, .length = 85, .back_offset = 6, .color = 1,
};

// Custom font for corner/edge feature text and the big-analog date --
// a single slot since only one custom font can be selected at a time,
// swapped (or unloaded entirely, for "default") whenever the setting
// changes. Same lazy load/unload lifecycle as apply_clock_font() uses
// for the main clock typeface.
static GFont s_corner_custom_font = NULL;
static uint8_t s_corner_custom_font_loaded_choice = 255; // 255 = nothing loaded yet

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

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  GColor bg, main_color, accent_color;
  get_active_color_scheme(&s_data, now, &bg, &main_color, &accent_color);

  // Markers (procedural presets, custom, and bitmap styles alike) are no
  // longer drawn here -- they're part of the sky canvas's own cached
  // redraw now (background_layer.c), composited once per its own
  // once-a-minute/force-redraw cadence rather than every tick this
  // always-on-top hands layer runs.
  ensure_corner_custom_font(s_data.corner_custom_font);

  int32_t hour_angle = (((t->tm_hour % 12) * 3600 + t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600);

  int32_t min_angle = ((t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);

  // All 5 hand styles now go through hand_layer_draw() -- custom (4) uses
  // the user's own per-hand settings; 0-3 use one of the hardcoded
  // HAND_STYLE_*_PRESETS above, with the two still-global hand settings
  // (outline_enabled, big_analog_hands_transparent) applied uniformly to
  // all 3 hands, matching what draw_big_hand_outlined() used to do.
  HandConfig hour_cfg, min_cfg, sec_cfg;
  if (s_data.big_analog_hand_style == 4) {
    hour_cfg = s_data.hand_hour;
    min_cfg = s_data.hand_minute;
    sec_cfg = s_data.hand_second;
  } else {
    uint8_t idx = (s_data.big_analog_hand_style <= 3) ? s_data.big_analog_hand_style : 0;
    hour_cfg = HAND_STYLE_HOUR_PRESETS[idx];
    min_cfg = HAND_STYLE_MIN_PRESETS[idx];
    sec_cfg = HAND_STYLE_SEC_PRESET;
    hour_cfg.translucent = min_cfg.translucent = sec_cfg.translucent = s_data.big_analog_hands_transparent;
    hour_cfg.outline_enabled = min_cfg.outline_enabled = sec_cfg.outline_enabled = s_data.outline_enabled;
    // contrasting_outline_color() picked black/white dynamically by luma;
    // the closest fixed equivalent in HandConfig's 3-option scheme-color
    // enum is the scheme's own background, which is high-contrast against
    // its text/accent colors in every built-in scheme.
    hour_cfg.outline_color = min_cfg.outline_color = sec_cfg.outline_color = 2;
  }

  hand_layer_draw(ctx, center, hour_angle, &hour_cfg, main_color, accent_color, bg);
  hand_layer_draw(ctx, center, min_angle, &min_cfg, main_color, accent_color, bg);
  if (s_data.show_seconds) {
    int32_t sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;
    hand_layer_draw(ctx, center, sec_angle, &sec_cfg, main_color, accent_color, bg);
  }

  if (s_data.big_analog_hand_style == 4) {
    hand_layer_draw_center_circle(ctx, center, s_data.center_circle_radius, s_data.center_circle_color,
                                   main_color, accent_color, bg);
  } else {
    graphics_context_set_fill_color(ctx, main_color);
    graphics_fill_circle(ctx, center, 3);
  }
}

// ---- corners overlay -------------------------------------------------

// Procedural icon bitmaps (1 bit per pixel, MSB = left column) --
// Most of them are 16 wide by 12 height, exceptions would use
//    hardcoded values directly in function call for now
// TODO: Make this system more elegant, use one unified struct for all data related icon and thus simplify draw_tiny_icon function call
static const uint8_t ICON_WIDTH = 16;
static const uint8_t ICON_ROWS = 12;

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
static const uint8_t CLOUD_ICON[24]   = { 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x22, 0xE0, 0x41, 0x10, 0x82, 0x0C,
  0x82, 0x16, 0x80, 0x01, 0x40, 0x01, 0x20, 0x02, 0x1F, 0xFC, 0x00, 0x00 };
static const uint8_t BLUETOOTH_ICON[24] = { 0x03, 0x00, 0x03, 0x80, 0x12, 0xC0, 0x1A, 0x60, 0x0E, 0xC0, 0x07, 0x80,
  0x07, 0x80, 0x0E, 0xC0, 0x1A, 0x60, 0x12, 0xC0, 0x03, 0x80, 0x03, 0x00 };
//static const uint8_t ERROR_ICON[24]   = { 0x01, 0x80, 0x02, 0x40, 0x04, 0x20, 0x05, 0xA0, 0x09, 0x90, 0x09, 0x90,
//  0x11, 0x88, 0x10, 0x08, 0x21, 0x84, 0x41, 0x82, 0x40, 0x02, 0x3F, 0xFC };

// simple weather icons set:
static const uint8_t SIMPLE_SUN_ICON[24]   = { 0x01, 0x00, 0x11, 0x08, 0x09, 0x10, 0x05, 0xA0, 0x03, 0xC0, 0x07, 0xFC,
  0x3F, 0xE0, 0x03, 0xC0, 0x05, 0xA0, 0x08, 0x90, 0x10, 0x88, 0x00, 0x80 };
static const uint8_t SIMPLE_PARTLY_CLOUDY_ICON[24]   = { 0x01, 0x10, 0x00, 0x92, 0x1F, 0x74, 0x3F, 0xB8, 0x7F, 0xC3, 0xFF, 0xFC,
  0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x3F, 0xFE, 0x1F, 0xFC };
static const uint8_t SIMPLE_CLOUDY_OVERCAST_ICON[24]   = { 0x00, 0x00, 0x1F, 0x00, 0x3F, 0x80, 0x7F, 0xC0, 0xFF, 0xFC, 0xFF, 0xFE,
  0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x3F, 0xFE, 0x1F, 0xFC, 0x00, 0x00 };
static const uint8_t SIMPLE_FOG_ICON[24]   = { 0x00, 0x00, 0x00, 0x00, 0x0C, 0xC0, 0x33, 0x30, 0x00, 0x00, 0x03, 0x30,
  0x0C, 0xCC, 0x00, 0x00, 0x0C, 0xC0, 0x33, 0x30, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t SIMPLE_RAIN_ICON[24]   = { 0x1E, 0x00, 0x3F, 0x00, 0x7F, 0x80, 0xFF, 0xCC, 0xFF, 0xFE, 0xFF, 0xFF,
  0x7F, 0xFF, 0x3F, 0xFE, 0x1F, 0xFC, 0x00, 0x00, 0x0C, 0xCC, 0x06, 0x66 };
static const uint8_t SIMPLE_SNOW_ICON[24]   = { 0x1E, 0x00, 0x3F, 0x00, 0x7F, 0x80, 0xFF, 0xCC, 0xFF, 0xFE, 0xFF, 0xFF,
  0x7F, 0xFF, 0x3F, 0xFE, 0x00, 0x00, 0x14, 0xA5, 0x08, 0x42, 0x14, 0xA5 };
static const uint8_t SIMPLE_STORM_ICON[24]   = { 0x1E, 0x00, 0x3F, 0x00, 0x7F, 0x80, 0xFF, 0xCC, 0xFF, 0x3E, 0xFE, 0xBF,
  0x7D, 0xBF, 0x3B, 0x7E, 0x07, 0x00, 0x61, 0xCC, 0x31, 0x86, 0x03, 0x00 };

// hollow weather icons set:
static const uint8_t HOLLOW_SUN_ICON[24]   = { 0x11, 0x88, 0x39, 0x9C, 0x1B, 0xD8, 0x07, 0xE0, 0x0E, 0x70, 0x3C, 0x3C,
  0x3C, 0x3C, 0x0E, 0x70, 0x07, 0xE0, 0x1B, 0xD8, 0x39, 0x9C, 0x11, 0x88 };
static const uint8_t HOLLOW_PARTLY_CLOUDY_ICON[24]   = { 0x04, 0x62, 0x0E, 0x67, 0x02, 0xF6, 0x1D, 0xF8, 0x3E, 0x9C, 0x63, 0x0F,
  0xC1, 0xB7, 0xC1, 0xF8, 0xC0, 0xCC, 0x60, 0x0C, 0x3F, 0xFB, 0x1F, 0xF2 };
static const uint8_t HOLLOW_CLOUDY_OVERCAST_ICON[24]   = { 0x00, 0x00, 0x0E, 0x00, 0x1F, 0x00, 0x31, 0x80, 0x60, 0xD8, 0x60, 0xFC,
  0x60, 0x66, 0x30, 0x06, 0x1F, 0xFC, 0x0F, 0xF8, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t HOLLOW_FOG_ICON[24]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF8, 0x1F, 0xF8, 0x00, 0x00,
  0x1F, 0xF8, 0x1F, 0xF8, 0x00, 0x00, 0x1F, 0xF8, 0x1F, 0xF8, 0x00, 0x00 };
static const uint8_t HOLLOW_RAIN_ICON[24]   = { 0x0E, 0x00, 0x1F, 0x70, 0x3B, 0xF8, 0x31, 0xDC, 0x38, 0x0C, 0x1F, 0xFC,
  0x0F, 0xF8, 0x00, 0x00, 0x19, 0x98, 0x33, 0x30, 0x66, 0x60, 0x00, 0x00 };
static const uint8_t HOLLOW_SNOW_ICON[24]   = { 0x0E, 0x00, 0x1F, 0x70, 0x3B, 0xF8, 0x31, 0xDC, 0x38, 0x0C, 0x1F, 0xFC,
  0x0F, 0xF8, 0x00, 0x00, 0x03, 0x0C, 0x03, 0x0C, 0x18, 0x60, 0x18, 0x60 };
static const uint8_t HOLLOW_STORM_ICON[24]   = { 0x0E, 0x00, 0x1F, 0x70, 0x3B, 0xF8, 0x31, 0xDC, 0x38, 0x0C, 0x1E, 0xFC,
  0x0C, 0x78, 0x01, 0x00, 0x33, 0x06, 0x67, 0xCC, 0xC1, 0x98, 0x01, 0x00 };

// filled weather icons set:
static const uint8_t FILL_SUN_ICON[24]   = { 0x01, 0x00, 0x11, 0x08, 0x09, 0x10, 0x05, 0xA0, 0x03, 0xC0, 0x07, 0xFC,
  0x3F, 0xE0, 0x03, 0xC0, 0x05, 0xA0, 0x08, 0x90, 0x10, 0x88, 0x00, 0x80 };
static const uint8_t FILL_PARTLY_CLOUDY_ICON[24]   = { 0x01, 0x10, 0x00, 0x92, 0x1F, 0x74, 0x3F, 0xB8, 0x7F, 0xC3, 0xFF, 0xFC,
  0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x3F, 0xFE, 0x1F, 0xFC };
static const uint8_t FILL_CLOUDY_OVERCAST_ICON[24]   = { 0x00, 0x00, 0x1F, 0x00, 0x3F, 0x80, 0x7F, 0xC0, 0xFF, 0xFC, 0xFF, 0xFE,
  0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x3F, 0xFE, 0x1F, 0xFC, 0x00, 0x00 };
static const uint8_t FILL_FOG_ICON[24]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF8, 0x1F, 0xF8, 0x00, 0x00,
  0x1F, 0xF8, 0x1F, 0xF8, 0x00, 0x00, 0x1F, 0xF8, 0x1F, 0xF8, 0x00, 0x00 };
static const uint8_t FILL_RAIN_ICON[24]   = { 0x1E, 0x00, 0x3F, 0x00, 0x7F, 0x80, 0xFF, 0xCC, 0xFF, 0xFE, 0xFF, 0xFF,
  0x7F, 0xFF, 0x3F, 0xFE, 0x1F, 0xFC, 0x00, 0x00, 0x0C, 0xCC, 0x06, 0x66 };
static const uint8_t FILL_SNOW_ICON[24]   = { 0x1E, 0x00, 0x3F, 0x00, 0x7F, 0x80, 0xFF, 0xCC, 0xFF, 0xFE, 0xFF, 0xFF,
  0x7F, 0xFF, 0x3F, 0xFE, 0x00, 0x00, 0x14, 0xA5, 0x08, 0x42, 0x14, 0xA5 };
static const uint8_t FILL_STORM_ICON[24]   = { 0x1E, 0x00, 0x3F, 0x00, 0x7F, 0x80, 0xFF, 0xCC, 0xFF, 0x3E, 0xFE, 0xBF,
  0x7D, 0xBF, 0x3B, 0x7E, 0x07, 0x00, 0x61, 0xCC, 0x31, 0x86, 0x03, 0x00 };

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

// ---- weather condition icon ("Weather icon" / "Temp + weather icon" -----
// corner content, style-selectable via weather_icon_style) -----------------

// Which of the 7 weather icon categories to show, from the same
// weather_condition + cloud_cover_pct combination short_condition_text()
// already uses -- kept in sync with those exact thresholds so the icon
// and the "Sunny"/"P.Cloudy"/etc. text (when both are visible somewhere)
// never disagree. 0=sunny, 1=partly cloudy, 2=cloudy/overcast, 3=fog,
// 4=rain, 5=snow, 6=storm.
static uint8_t weather_icon_category(uint8_t weather_condition, uint8_t cloud_pct) {
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

// These three are similar to other icon systems wrapped in separate call for clarity
static void draw_weather_icon_hollow(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      draw_tiny_icon(ctx, top_left, HOLLOW_SUN_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 1: // partly cloudy
      draw_tiny_icon(ctx, top_left, HOLLOW_PARTLY_CLOUDY_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 2: // cloudy / overcast
      draw_tiny_icon(ctx, top_left, HOLLOW_CLOUDY_OVERCAST_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 3: // fog
      draw_tiny_icon(ctx, top_left, HOLLOW_FOG_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 4: // rain
      draw_tiny_icon(ctx, top_left, HOLLOW_RAIN_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 5: // snow
      draw_tiny_icon(ctx, top_left, HOLLOW_SNOW_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 6: { // storm
      draw_tiny_icon(ctx, top_left, HOLLOW_STORM_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    }
  }
}

static void draw_weather_icon_simple(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      draw_tiny_icon(ctx, top_left, SIMPLE_SUN_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 1: // partly cloudy
      draw_tiny_icon(ctx, top_left, SIMPLE_PARTLY_CLOUDY_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 2: // cloudy / overcast
      draw_tiny_icon(ctx, top_left, SIMPLE_CLOUDY_OVERCAST_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 3: // fog
      draw_tiny_icon(ctx, top_left, SIMPLE_FOG_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 4: // rain
      draw_tiny_icon(ctx, top_left, SIMPLE_RAIN_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 5: // snow
      draw_tiny_icon(ctx, top_left, SIMPLE_SNOW_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 6: { // storm
      draw_tiny_icon(ctx, top_left, SIMPLE_STORM_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    }
  }
}

static void draw_weather_icon_filled(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      draw_tiny_icon(ctx, top_left, FILL_SUN_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 1: // partly cloudy
      draw_tiny_icon(ctx, top_left, FILL_PARTLY_CLOUDY_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 2: // cloudy / overcast
      draw_tiny_icon(ctx, top_left, FILL_CLOUDY_OVERCAST_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 3: // fog
      draw_tiny_icon(ctx, top_left, FILL_FOG_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 4: // rain
      draw_tiny_icon(ctx, top_left, FILL_RAIN_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 5: // snow
      draw_tiny_icon(ctx, top_left, FILL_SNOW_ICON, ICON_ROWS, ICON_WIDTH, color);
      return;
    case 6: { // storm
      draw_tiny_icon(ctx, top_left, FILL_STORM_ICON, ICON_ROWS, ICON_WIDTH, color);
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

// ---- timezone feature -----------------------------------------------------
// A curated list of major cities (not the full IANA database) with a
// fixed, always-shown 3-letter city code (not "GMT"/"BST"-style, per
// the brief -- "LON"/"TOK" stay the same year-round even though the
// underlying UTC offset shifts with DST) plus enough to compute the
// CURRENT actual offset: a standard-time UTC offset in minutes, and
// which DST rule (if any) applies. DST is modeled for the two rules
// covering most of what's likely to be picked -- current-era US
// (2nd Sunday March - 1st Sunday November) and EU (last Sunday March -
// last Sunday October) -- both computed exactly from the actual date,
// not a lookup table, so they stay correct in future years. Southern-
// hemisphere DST (Sydney, Auckland) is NOT modeled -- those two just
// use their fixed standard-time offset year-round, a known simplification.
typedef struct {
  const char *abbr;         // fixed on-watch label, e.g. "LON"
  int16_t base_offset_min;  // standard-time UTC offset, in minutes (can be negative)
  uint8_t dst_rule;         // 0=none, 1=US, 2=EU
} TimezoneInfo;

static const TimezoneInfo TIMEZONES[] = {
  { "LON",    0, 2 }, // London
  { "PAR",   60, 2 }, // Paris/Berlin/Madrid (Central European Time)
  { "CAI",  120, 0 }, // Cairo
  { "MOW",  180, 0 }, // Moscow
  { "DXB",  240, 0 }, // Dubai
  { "DEL",  330, 0 }, // Delhi/Mumbai (UTC+5:30)
  { "DAC",  360, 0 }, // Dhaka
  { "BKK",  420, 0 }, // Bangkok/Jakarta
  { "BJS",  480, 0 }, // Beijing/Shanghai/Singapore
  { "TOK",  540, 0 }, // Tokyo
  { "SYD",  600, 0 }, // Sydney (DST not modeled -- see note above)
  { "AKL",  720, 0 }, // Auckland (DST not modeled -- see note above)
  { "NYC", -300, 1 }, // New York
  { "CHI", -360, 1 }, // Chicago
  { "DEN", -420, 1 }, // Denver
  { "LAX", -480, 1 }, // Los Angeles
  { "ANC", -540, 1 }, // Anchorage
  { "HNL", -600, 0 }, // Honolulu
  { "SAO", -180, 0 }, // Sao Paulo
};
#define TIMEZONE_COUNT (int)(sizeof(TIMEZONES) / sizeof(TIMEZONES[0]))

// civil_from_days()/days_from_civil() -- the well-known constant-time
// Gregorian-calendar<->epoch-days conversion (Howard Hinnant's
// "civil_from_days"/"days_from_civil"), used instead of gmtime() so this
// doesn't depend on anything beyond plain integer arithmetic. Verified
// numerically against Python's datetime for round-trips across leap
// years and the epoch boundary before use.
static void civil_from_days(int32_t z, int *y, int *m, int *d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t year = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  uint32_t month = mp + (mp < 10 ? 3 : (uint32_t)-9);
  *y = year + (month <= 2 ? 1 : 0);
  *m = (int)month;
  *d = (int)day;
}

static int32_t days_from_civil(int y, int m, int d) {
  y -= (m <= 2) ? 1 : 0;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// 0=Sunday..6=Saturday -- day 0 (1970-01-01) was a Thursday.
static int day_of_week_from_days(int32_t days) {
  int32_t d = (days + 4) % 7;
  return (int)(d < 0 ? d + 7 : d);
}

// The Nth Sunday of a month as epoch days (nth=1 => first Sunday,
// nth=-1 => last Sunday).
static int32_t nth_sunday_epoch_days(int year, int month, int nth) {
  if (nth > 0) {
    int32_t d1 = days_from_civil(year, month, 1);
    int dow1 = day_of_week_from_days(d1);
    int first_sunday_day = (dow1 == 0) ? 1 : (8 - dow1);
    return days_from_civil(year, month, first_sunday_day + (nth - 1) * 7);
  }
  static const int DAYS_IN_MONTH[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int last_day = DAYS_IN_MONTH[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) last_day = 29;
  int32_t d_last = days_from_civil(year, month, last_day);
  return d_last - day_of_week_from_days(d_last);
}

// Whether US-rule DST is active at this exact UTC instant. Transition
// hours are approximated with a single fixed UTC hour common to
// continental US zones (2am local standard time is ~7am UTC for the
// March start, ~6am UTC for the November end) -- exact for the correct
// calendar day either way, could be off by up to a couple hours right
// at the transition instant itself for the westernmost zones.
static bool is_us_dst(int32_t epoch_days, int32_t secs_of_day, int year) {
  int32_t start = nth_sunday_epoch_days(year, 3, 2) * 86400 + 7 * 3600;
  int32_t end = nth_sunday_epoch_days(year, 11, 1) * 86400 + 6 * 3600;
  int32_t now = epoch_days * 86400 + secs_of_day;
  return now >= start && now < end;
}

// EU-rule DST -- exact, since the EU rule is itself defined in UTC
// terms (01:00 UTC on the last Sunday of March/October).
static bool is_eu_dst(int32_t epoch_days, int32_t secs_of_day, int year) {
  int32_t start = nth_sunday_epoch_days(year, 3, -1) * 86400 + 3600;
  int32_t end = nth_sunday_epoch_days(year, 10, -1) * 86400 + 3600;
  int32_t now = epoch_days * 86400 + secs_of_day;
  return now >= start && now < end;
}

// Resolves a TimezoneInfo's actual current UTC offset in minutes,
// including DST if applicable right now.
static int16_t timezone_current_offset_min(const TimezoneInfo *tz, time_t utc_now) {
  int32_t epoch_days = (int32_t)(utc_now / 86400);
  int32_t secs_of_day = (int32_t)(utc_now % 86400);
  int y, m, d;
  civil_from_days(epoch_days, &y, &m, &d);
  bool dst = false;
  if (tz->dst_rule == 1) dst = is_us_dst(epoch_days, secs_of_day, y);
  else if (tz->dst_rule == 2) dst = is_eu_dst(epoch_days, secs_of_day, y);
  return tz->base_offset_min + (dst ? 60 : 0);
}

// Black at local noon, white at local midnight, linear in between --
// a simple hour-of-day heuristic rather than real sun-altitude
// astronomy (which isn't available for an arbitrary remote timezone
// the way it is for the user's own location via eclipse_sky_is_bright()).
static GColor timezone_daylight_color(int local_hour24) {
  int dist_from_noon = (local_hour24 <= 12) ? (12 - local_hour24) : (local_hour24 - 12); // 0..12
  uint8_t v = (uint8_t)((255 * dist_from_noon) / 12);
  return GColorFromRGB(v, v, v);
}

// ---- pressure trend / wind direction icons -------------------------------

// A small up/down chevron (rising/falling) or a flat horizontal line
// (flat), drawn with plain line primitives -- no bitmap needed.
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

// A small compass arrow, rotated via sin/cos (same technique
// hand_layer.c uses for the analog hands). `from_deg` is the direction
// the wind blows FROM (standard meteorological convention, e.g. Open-
// Meteo's winddirection field) -- the arrow itself points the other
// way, toward where the wind is actually blowing, since that reads as
// more immediately useful at a glance than the source bearing would.
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

// A simple two-peak mountain silhouette, drawn as two filled triangles
// -- used by the "Altitude" corner content.
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

// ---- sleep data (Pebble HealthService, entirely on-watch -- no phone
// involvement, unlike the weather/location features above) ---------------

// "Xh Ym" -- shared by the sleep-duration and restful-sleep-duration
// corner content types.
static void format_duration_hm(char *buf, size_t buf_size, int32_t total_seconds) {
  if (total_seconds < 0) total_seconds = 0;
  int hours = (int)(total_seconds / 3600);
  int minutes = (int)((total_seconds % 3600) / 60);
  snprintf(buf, buf_size, "%dh %dm", hours, minutes);
}

typedef struct {
  time_t earliest_start;
  time_t latest_end;
  bool found;
} SleepSpan;

static bool sleep_span_iterator_cb(HealthActivity activity, time_t time_start, time_t time_end, void *context) {
  SleepSpan *span = (SleepSpan *)context;
  if (!span->found || time_start < span->earliest_start) span->earliest_start = time_start;
  if (!span->found || time_end > span->latest_end) span->latest_end = time_end;
  span->found = true;
  return true; // keep going -- want the full extent, not just the first segment
}

// Earliest sleep-activity start and latest end within the last 24
// hours, used for the "Bed time"/"Wake time" corner content types.
// Segments (there can be more than one per night, e.g. brief wake-ups)
// are merged into one overall span rather than tracked individually.
static SleepSpan get_sleep_span(void) {
  SleepSpan span = { 0, 0, false };
  time_t now = time(NULL);
  time_t day_ago = now - 24 * 3600;
  // HealthActivitySleep is already a single-bit mask value (see the
  // HealthActivityMaskAll macro in the SDK docs, and the SDK's own
  // "if (activities & HealthActivitySleep)" example) -- no extra
  // shifting needed, unlike some other Pebble bitmask enums.
  health_service_activities_iterate(HealthActivitySleep, day_ago, now, HealthIterationDirectionPast,
                                     sleep_span_iterator_cb, &span);
  return span;
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
    case 1: case 2: case 5: case 6: case 7: case 8: case 9: case 10: case 13:
      return 11; // bitmap icons (7-wide at 140% scale) + gap
    case 3: return 10; // battery + gap
    case 4: return 21; // moon (radius 9, so 2*9+2 diameter box) + gap
    case 11: return 22; // sun-time glyph (fixed 20px, drawn via direct primitives) + gap
    case 14: return 20; // weather icon (16-wide box, worst case a bit wider for the sun's rays) + gap
    case 15: return 12; // pressure trend chevron + gap
    case 16: return 14; // wind direction arrow + gap
    case 17: return 20; // mountain icon (16-wide box) + gap
    default: return 0; // no icon
  }
}

// Renders one corner's chosen content type in one of the four color
// In-place uppercase -- used by the short weekday/month date formats
// below, since strftime's %a/%b give "Mon"/"Sep" (title case) and these
// are deliberately styled ALL CAPS instead (matching the long forms,
// which stay in strftime's natural title case: "Monday"/"September").
static void to_upper_str(char *s) {
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z') *s -= 32;
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

  // 40, not 24 -- the actual longest real content (e.g. "September",
  // "Restful sleep") stays well under 24, but GCC's -Wformat-truncation
  // sizes snprintf's *worst case* off each %d's full possible range (up
  // to 11 characters, for a very negative 32-bit int), not the small
  // calendar-sized values (day/month/year) actually passed in -- the
  // 3-%d date format below is the tightest case, needing up to 36 by
  // that conservative accounting even though real dates need under 12.
  char buf[40];
  int icon_kind = 0; // 0=none, 1=heart, 2=foot, 3=battery, 4=moon phase, 5=umbrella, 6=droplet,
                       // 7=wind, 8=GPS pin, 9=eye, 10=clouds, 11=sunrise/sunset
  bool icon_is_sunrise = false; // only meaningful when icon_kind == 11
  uint8_t icon_weather_category = 0; // only meaningful when icon_kind == 14 -- see weather_icon_category()
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
      if (get_next_sun_event(now, s_data.sun_rise, s_data.sun_set, s_data.sun_rise_tomorrow, &sun_event_time, &icon_is_sunrise)) {
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
    case 20: { // Bluetooth connection status
      icon_kind = 13;
      bool connected = connection_service_peek_pebble_app_connection();
      snprintf(buf, sizeof(buf), "%s", connected ? "Connected" : "No phone");
      // Bright, saturated colors deliberately outside the muted palettes
      // used elsewhere (this is a binary connected/not state, not
      // something to grade smoothly along a gradient).
      dynamic_color = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      break;
    }
    // ---- date format variants (21-30) -- none of these have a natural
    // "value" to grade a color on, so they all just take main_color,
    // same as the original short-date (case 12).
    case 21: { // month + day, e.g. "SEP 11"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      char mon_buf[4];
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      to_upper_str(mon_buf);
      snprintf(buf, sizeof(buf), "%s %d", mon_buf, t->tm_mday);
      dynamic_color = main_color;
      break;
    }
    case 22: { // day of month only, e.g. "11"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d", t->tm_mday);
      dynamic_color = main_color;
      break;
    }
    case 23: { // weekday, short + all caps, e.g. "MON"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%a", t);
      to_upper_str(buf);
      dynamic_color = main_color;
      break;
    }
    case 24: { // weekday, long, e.g. "Monday"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%A", t);
      dynamic_color = main_color;
      break;
    }
    case 25: { // month, short + all caps, e.g. "SEP"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%b", t);
      to_upper_str(buf);
      dynamic_color = main_color;
      break;
    }
    case 26: { // month, long, e.g. "September"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%B", t);
      dynamic_color = main_color;
      break;
    }
    case 27: { // day/month, e.g. "11/9"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d", t->tm_mday, t->tm_mon + 1);
      dynamic_color = main_color;
      break;
    }
    case 28: { // month/day, e.g. "9/11"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d", t->tm_mon + 1, t->tm_mday);
      dynamic_color = main_color;
      break;
    }
    case 29: { // full, day/month/year, e.g. "24/9/2026"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d/%d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
      dynamic_color = main_color;
      break;
    }
    case 30: { // full imperial, month/day/2-digit year, e.g. "9/24/26"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d/%02d", t->tm_mon + 1, t->tm_mday, (t->tm_year + 1900) % 100);
      dynamic_color = main_color;
      break;
    }
    case 31: { // weather icon only, no text
      icon_kind = 14;
      icon_weather_category = weather_icon_category(s_data.weather_condition, s_data.cloud_cover_pct);
      buf[0] = '\0';
      dynamic_color = s_data.valid
        ? weather_condition_color(s_data.weather_condition, s_data.cloud_cover_pct, s_data.rain_chance_pct)
        : bg_color;
      break;
    }
    case 32: { // temp + weather icon
      icon_kind = 14;
      icon_weather_category = weather_icon_category(s_data.weather_condition, s_data.cloud_cover_pct);
      int16_t temp = convert_temp(s_data.weather_temp_c, s_data.temp_unit);
      snprintf(buf, sizeof(buf), "%d%s", temp, temp_unit_suffix(s_data.temp_unit));
      dynamic_color = s_data.valid
        ? weather_condition_color(s_data.weather_condition, s_data.cloud_cover_pct, s_data.rain_chance_pct)
        : bg_color;
      break;
    }
    case 33: { // timezone -- "ABBR H:MM" (or "ABBR HH:MM" in 24h style)
      const TimezoneInfo *tz = &TIMEZONES[s_data.timezone_id < TIMEZONE_COUNT ? s_data.timezone_id : 0];
      time_t now = time(NULL);
      int16_t offset_min = timezone_current_offset_min(tz, now);
      time_t local_time = now + (int32_t)offset_min * 60;
      int32_t local_secs_of_day = ((local_time % 86400) + 86400) % 86400;
      int local_hour24 = (int)(local_secs_of_day / 3600);
      int local_min = (int)((local_secs_of_day % 3600) / 60);
      if (clock_is_24h_style()) {
        snprintf(buf, sizeof(buf), "%s %02d:%02d", tz->abbr, local_hour24, local_min);
      } else {
        int hour12 = local_hour24 % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(buf, sizeof(buf), "%s %d:%02d%s", tz->abbr, hour12, local_min, local_hour24 < 12 ? "AM" : "PM");
      }
      dynamic_color = timezone_daylight_color(local_hour24);
      break;
    }
    case 34: { // pressure, with rising/falling/flat trend arrow
      icon_kind = 15;
      snprintf(buf, sizeof(buf), "%d hPa", s_data.pressure_hpa);
      // Green when in the ordinary ~1000-1025 hPa band, ambering out
      // toward either extreme -- reuses the same 7-stop gradient as
      // temperature/UV, just remapped to a pressure-appropriate range.
      dynamic_color = seven_stop_gradient(s_data.pressure_hpa, 970, 1050);
      break;
    }
    case 35: { // wind direction, with a rotated compass arrow
      icon_kind = 16;
      static const char *COMPASS_DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
      int compass_idx = ((s_data.wind_dir_deg + 22) / 45) % 8;
      if (compass_idx < 0) compass_idx += 8;
      snprintf(buf, sizeof(buf), "%s", COMPASS_DIRS[compass_idx]);
      dynamic_color = main_color; // no natural "value" to grade a color on
      break;
    }
    case 36: { // air quality -- unit picked in the Weather settings section
      bool use_eu = (s_data.aqi_unit == 1);
      uint16_t aqi_value = use_eu ? s_data.aqi_eu : s_data.aqi_us;
      snprintf(buf, sizeof(buf), "AQI %d", aqi_value);
      // US AQI: good <=50, moderate <=100, unhealthy >150 (0-500 scale).
      // European AQI: good <=20, moderate <=40, poor >60 (0-100+ scale).
      // Different thresholds per scale, same green->yellow->red shape.
      uint16_t good_max = use_eu ? 20 : 50;
      uint16_t bad_min = use_eu ? 60 : 150;
      GColor good = GColorFromRGB(0, 200, 0), mid = GColorFromRGB(230, 200, 0), bad = GColorFromRGB(220, 0, 0);
      if (aqi_value <= good_max) dynamic_color = good;
      else if (aqi_value >= bad_min) dynamic_color = bad;
      else dynamic_color = mid;
      break;
    }
    case 37: { // dew point -- reuses the humidity feature's droplet icon
      icon_kind = 6;
      int16_t dew = convert_temp(s_data.dew_point_c, s_data.temp_unit);
      snprintf(buf, sizeof(buf), "%d%s", dew, temp_unit_suffix(s_data.temp_unit));
      dynamic_color = main_color;
      break;
    }
    case 38: { // altitude
      icon_kind = 17;
      if (s_data.altitude_m <= -32000) { // sentinel: not available
        snprintf(buf, sizeof(buf), "N/A");
      } else if (s_data.altitude_unit == 1) { // feet
        int32_t feet = ((int32_t)s_data.altitude_m * 328) / 100; // *3.28084, integer approximation
        snprintf(buf, sizeof(buf), "%ldft", (long)feet);
      } else {
        snprintf(buf, sizeof(buf), "%dm", s_data.altitude_m);
      }
      dynamic_color = main_color;
      break;
    }
    case 39: { // sleep duration (total)
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricSleepSeconds, time(NULL) - 86400, time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue secs = health_service_sum_today(HealthMetricSleepSeconds);
        format_duration_hm(buf, sizeof(buf), (int32_t)secs);
        dynamic_color = main_color;
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 40: { // restful (deep) sleep duration
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricSleepRestfulSeconds, time(NULL) - 86400, time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue secs = health_service_sum_today(HealthMetricSleepRestfulSeconds);
        format_duration_hm(buf, sizeof(buf), (int32_t)secs);
        dynamic_color = main_color;
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 41: { // sleep quality -- restful / total, as a percentage
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricSleepSeconds, time(NULL) - 86400, time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue total = health_service_sum_today(HealthMetricSleepSeconds);
        HealthValue restful = health_service_sum_today(HealthMetricSleepRestfulSeconds);
        int pct = (total > 0) ? (int)((restful * 100) / total) : 0;
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        dynamic_color = red_green_gradient((uint8_t)pct);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 42: { // bed time -- earliest sleep-activity start in the last 24h
      SleepSpan span = get_sleep_span();
      if (span.found) {
        struct tm *t = localtime(&span.earliest_start);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
        dynamic_color = main_color;
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 43: { // wake time -- latest sleep-activity end in the last 24h
      SleepSpan span = get_sleep_span();
      if (span.found) {
        struct tm *t = localtime(&span.latest_end);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
        dynamic_color = main_color;
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
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
    ? bounds.origin.y + (bounds.size.h - CORNER_ROW_H) / 2 + top_offset // top_offset doubles as a
                                                                          // vertical nudge from center here
                                                                          // (middle-left/right's 2-line pairs)
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
    case 13: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          BLUETOOTH_ICON, ICON_ROWS, ICON_WIDTH, icon_outline_color);
        }
      }
      draw_tiny_icon(ctx, pos, BLUETOOTH_ICON, ICON_ROWS, ICON_WIDTH, color);
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
    case 14: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - ICON_ROWS) / 2 - 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_weather_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                             icon_weather_category, s_data.weather_icon_style, icon_outline_color);
        }
      }
      draw_weather_icon(ctx, pos, icon_weather_category, s_data.weather_icon_style, color);
      break;
    }
    case 15: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_pressure_trend_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                                    s_data.pressure_trend, icon_outline_color);
        }
      }
      draw_pressure_trend_icon(ctx, pos, s_data.pressure_trend, color);
      break;
    }
    case 16: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_wind_direction_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                                    s_data.wind_dir_deg, icon_outline_color);
        }
      }
      draw_wind_direction_icon(ctx, pos, s_data.wind_dir_deg, color);
      break;
    }
    case 17: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_mountain_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), icon_outline_color);
        }
      }
      draw_mountain_icon(ctx, pos, color);
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
  get_active_color_scheme(&s_data, now, &bg, &main_color, &accent_color);
  ensure_corner_custom_font(s_data.corner_custom_font);

  bool is_big_analog = s_data.bottom_style == 2;
  uint8_t marker_style = s_data.big_analog_marker_style;
  bool is_bitmap_style = is_big_analog && marker_style >= 3 && marker_style != 8 && marker_style != 9;

  // Which of the 4 edge-middle slots (upper/bottom/left/right-middle)
  // does the current mode/style actually support? Digital/analog
  // modes use none of them. Big-analogue procedural styles (<3), custom
  // (8), and none (9, no marker ring drawn at all) have no artwork to
  // work around, so all 4 are available alongside the corners.
  // Big-analogue bitmap styles (3-7) are limited to whichever slots
  // that specific mask graphic's design has room for, and always
  // suppress the 4 corners (the mask already fills most of the screen
  // either way).
  bool show_upper = false, show_bottom = false, show_left = false, show_right = false;
  if (is_big_analog) {
    if (marker_style < 3 || marker_style == 8 || marker_style == 9) {
      // Procedural styles, custom (8), and none (9): no fixed artwork to
      // work around, so all 4 corners stay available same as styles 0-2.
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
    bool has_line2 = s_data.middle_left_line2_content != 0;
    int16_t line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    draw_corner_item(ctx, bounds, s_data.middle_left_line1_content, s_data.middle_left_line1_color_mode,
                      main_color, accent_color, bg, false, true, true, line1_offset, 0, false, true);
    if (has_line2) {
      draw_corner_item(ctx, bounds, s_data.middle_left_line2_content, s_data.middle_left_line2_color_mode,
                        main_color, accent_color, bg, false, true, true, CORNER_ROW_H / 2, 0, false, true);
    }
  }
  if (show_right) {
    bool has_line2 = s_data.middle_right_line2_content != 0;
    int16_t line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    draw_corner_item(ctx, bounds, s_data.middle_right_line1_content, s_data.middle_right_line1_color_mode,
                      main_color, accent_color, bg, false, false, true, line1_offset, 0, false, true);
    if (has_line2) {
      draw_corner_item(ctx, bounds, s_data.middle_right_line2_content, s_data.middle_right_line2_color_mode,
                        main_color, accent_color, bg, false, false, true, CORNER_ROW_H / 2, 0, false, true);
    }
  }

  if (is_bitmap_style) return; // corners fully replaced by the slots above

  // Bottom corners shift up out of the way of the "Clouds/visibility/
  // location" bar (background_layer.c's canvas_update_proc) whenever
  // that bar is actually going to be drawn -- which depends on
  // bottom_info_bar_mode, not just whether a shake is currently
  // active: Off (0) never draws it (never shift), Permanent (2) always
  // draws it (always shift), and On shake (1) draws it only while
  // s_labels_visible is true (shift only then). Checking s_labels_visible
  // alone, without bottom_info_bar_mode, used to shift the corners on
  // every shake regardless of this setting -- including when the bar
  // was set to Off and would never actually be drawn at all -- and
  // conversely never shifted for it in Permanent mode outside of an
  // active shake, even though the bar is always there in that mode.
  // Also itself suppressed in analog mode (bottom_style == 1), where
  // the bar is redundant with the persistent info panel and never drawn.
  bool bar_will_draw = (s_data.bottom_info_bar_mode == 2) ||
                        (s_data.bottom_info_bar_mode == 1 && s_labels_visible);
  int16_t bottom_shift = (bar_will_draw && s_data.bottom_style != 1) ? 18 : 0;

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
  get_active_color_scheme(&s_data, now, &bg, &text_color, &accent_color);

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
      get_next_sun_event(now, s_data.sun_rise, s_data.sun_set, s_data.sun_rise_tomorrow, &sun_event_time, &sun_event_is_rise);
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
      get_next_sun_event(now, s_data.sun_rise, s_data.sun_set, s_data.sun_rise_tomorrow, &sun_event_time, &sun_event_is_rise);

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
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // In big-analog mode this label floats directly over the busy sky
  // view. Normally draw_text_outlined()'s 4-shifted-copy outline keeps
  // it legible against any background there, but with that setting
  // off there's nothing else backing the text, so it can disappear
  // into a similarly-colored patch of sky. Give it a solid pill
  // background in that specific case instead (contrasting_outline_color()
  // picks black or white, whichever contrasts with the text color) --
  // outline mode already handles legibility fine on its own, and
  // outside big-analog mode the bottom bar/panel is already a solid
  // color the text sits on, so neither of those needs this extra
  // background.
  if (!s_data.outline_enabled && s_data.bottom_style == 2 && s_countdown_buf[0] != '\0') {
    GSize text_size = graphics_text_layout_get_content_size(s_countdown_buf, font, bounds,
                                                              GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
    int16_t pad_x = 6;
    GRect bg_rect = GRect(bounds.origin.x + (bounds.size.w - text_size.w) / 2 - pad_x,
                           bounds.origin.y, text_size.w + pad_x * 2, bounds.size.h);
    graphics_context_set_fill_color(ctx, contrasting_outline_color(s_countdown_text_color));
    graphics_fill_rect(ctx, bg_rect, 4, GCornersAll);
  }

  draw_text_outlined(ctx, s_countdown_buf, font, bounds,
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
  // Custom marker system (big_analog_marker_style == 8) -- see eclipse_data.h
  // (MarkerRingConfig/MarkerTextConfig) and background_layer.c (where these
  // now actually get drawn, as part of its own cached redraw). No per-key
  // dirty-marking needed here beyond copying the values in -- every inbox
  // message unconditionally forces a full canvas redraw at the end of this
  // handler (see refresh_status_and_maybe_canvas(true) below).
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
  if ((t = dict_find(iter, MESSAGE_KEY_MARKER_TEXT_ROMAN))) s_data.marker_text.roman_numerals = t->value->uint8 != 0;
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
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE1_CONTENT))) {
    s_data.middle_left_line1_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE1_COLOR_MODE))) {
    s_data.middle_left_line1_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE2_CONTENT))) {
    s_data.middle_left_line2_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_LEFT_LINE2_COLOR_MODE))) {
    s_data.middle_left_line2_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE1_CONTENT))) {
    s_data.middle_right_line1_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE1_COLOR_MODE))) {
    s_data.middle_right_line1_color_mode = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE2_CONTENT))) {
    s_data.middle_right_line2_content = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_MIDDLE_RIGHT_LINE2_COLOR_MODE))) {
    s_data.middle_right_line2_color_mode = t->value->uint8;
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
  if ((t = dict_find(iter, MESSAGE_KEY_WEATHER_ICON_STYLE))) {
    s_data.weather_icon_style = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TIMEZONE_ID))) {
    s_data.timezone_id = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_DIR_DEG))) s_data.wind_dir_deg = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_DEW_POINT_C))) s_data.dew_point_c = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_PRESSURE_HPA))) s_data.pressure_hpa = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_PRESSURE_TREND))) s_data.pressure_trend = t->value->uint8;
  if ((t = dict_find(iter, MESSAGE_KEY_AQI_US))) s_data.aqi_us = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_AQI_EU))) s_data.aqi_eu = t->value->uint16;
  if ((t = dict_find(iter, MESSAGE_KEY_AQI_UNIT))) {
    s_data.aqi_unit = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ALTITUDE_M))) s_data.altitude_m = t->value->int16;
  if ((t = dict_find(iter, MESSAGE_KEY_ALTITUDE_UNIT))) {
    s_data.altitude_unit = t->value->uint8;
    if (s_corners_layer) layer_mark_dirty(s_corners_layer);
  }
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
  if ((t = dict_find(iter, MESSAGE_KEY_SUN_RISE_TOMORROW))) s_data.sun_rise_tomorrow = (time_t)t->value->int32;
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
  if (s_canvas_layer) eclipse_canvas_destroy(s_canvas_layer); // also frees marker_bitmap/marker_text_font now
  if (s_bottom_layer) layer_destroy(s_bottom_layer);
  if (s_hands_layer) layer_destroy(s_hands_layer);
  if (s_corners_layer) layer_destroy(s_corners_layer);
  if (s_corner_custom_font) {
    fonts_unload_custom_font(s_corner_custom_font);
    s_corner_custom_font = NULL;
  }
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
