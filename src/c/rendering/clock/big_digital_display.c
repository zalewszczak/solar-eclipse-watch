#include "./big_digital_display.h"
#include "./big_digital_lookup.h"
#include "../../features/feature_icon_assets.h"
#include "../../data/eclipse_ui.h"

#define DIGIT_W 38
#define DIGIT_H 160
#define COLON_GAP_W 20
#define ONE_W_OFFSET 14

static EclipseData *s_data;
static Layer *s_panel_layer;

static int16_t draw_digit(GContext *ctx, int16_t x, int16_t y, uint8_t style, uint8_t digit, GColor color,
                       bool transparent, bool draw) {
  int16_t width = DIGIT_W;
  GPoint p = GPoint(x, y);
  if (digit == 1) {
    width -= ONE_W_OFFSET;
    p.x -= ONE_W_OFFSET / 2;
  } else if (digit == BIG_DIGITAL_COLON_INDEX) {
    width = COLON_GAP_W;
    p.x -= (DIGIT_W - COLON_GAP_W) / 2;
  }
  
  if (draw) {
    if (style >= BIG_DIGITAL_STYLE_COUNT || digit > BIG_DIGITAL_COLON_INDEX) return x;
    GBitmap *bmp = gbitmap_create_with_resource(BIG_DIGITAL_DIGIT_RESOURCES[style][digit]);
    if (!bmp) return x;
    feature_icon_assets_draw_bitmap_tinted_sized(ctx, bmp, p, color, DIGIT_W, DIGIT_H, transparent);
    gbitmap_destroy(bmp);
  }
  
  return x + width;
}

static int16_t run_big_digits_sequence(GContext *ctx, int16_t x, int16_t y, uint8_t style, GColor main_color, bool transparent, bool draw) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int8_t hour = t->tm_hour;
  if (!clock_is_24h_style()) {
    hour = hour % 12;
  }
  
  int8_t hour_tens = (int8_t)(hour / 10),
         hour_singles = (uint8_t)(hour % 10),
         minute_tens = (int8_t)(t->tm_min / 10),
         minute_singles = (uint8_t)(t->tm_min % 10);
  
  if (hour_tens > 0) x = draw_digit(ctx, x, y, style, hour_tens, main_color, transparent, draw);
  x = draw_digit(ctx, x, y, style, hour_singles, main_color, transparent, draw);
  x = draw_digit(ctx, x, y, style, BIG_DIGITAL_COLON_INDEX, main_color, transparent, draw);
  x = draw_digit(ctx, x, y, style, minute_tens, main_color, transparent, draw);
  x = draw_digit(ctx, x, y, style, minute_singles, main_color, transparent, draw);
  
  return x;
}

static void draw_big_digital_panel(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // No fill here -- this panel sits on top of a full-screen sky canvas
  // (same as Analog's hands layer does), not its own background.
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(s_data, time(NULL), &bg, &main_color, &accent_color);
  (void)bg; // only main_color (digit/colon tint) is used now that the sky underneath draws the background

  // clock_font's normal meaning (a text font id, see font_lookup.h) doesn't
  // apply to this layout -- a value of 100+ here means "Big Digital style
  // (value - 100)" instead, same field reused for a different purpose the
  // way corner/edge content fields already mean different things in
  // analog vs. digital layouts (see feature_layout.c). Below 100 shouldn't
  // happen once this is the active layout (the font picker only offers
  // 100+ choices then), but falls back to style 0 rather than drawing
  // nothing if it ever does.
  uint8_t style = (s_data->clock_font >= 100) ? (uint8_t)(s_data->clock_font - 100) : 0;

  // Same CONFIG_BITMAP_MARKER_TRANSPARENT setting bitmap analog markers
  // use (see marker_layer_draw()) -- Big Digital and bitmap markers are
  // never on screen at once (mutually exclusive bottom_style values), so
  // one shared field/settings-page checkbox can drive whichever bitmap
  // art is actually showing without a second key or a second persisted
  // field.
  bool transparent = s_data->bitmap_marker_transparent;
  
  int16_t total_w = 0;
  total_w = run_big_digits_sequence(ctx, total_w, 0, style, main_color, transparent, false);
  int16_t x = bounds.origin.x + (bounds.size.w - total_w) / 2;
  int16_t y = bounds.origin.y + (bounds.size.h - DIGIT_H) / 2;
  
  if (s_data->upper_middle_line1_content == 0 && s_data->bottom_middle_line1_content != 0) {
    y -= 10;
  } else if (s_data->upper_middle_line1_content != 0 && s_data->bottom_middle_line1_content == 0) {
    y += 10;
  }
  
  run_big_digits_sequence(ctx, x, y, style, main_color, transparent, true);
}

void big_digital_display_init(EclipseData *data) {
  s_data = data;
}

void big_digital_display_deinit(void) {
  s_data = NULL;
}

Layer *big_digital_display_panel_layer(void) {
  return s_panel_layer;
}

void big_digital_display_create_panel(Layer *parent, GRect frame) {
  s_panel_layer = layer_create(frame);
  layer_set_update_proc(s_panel_layer, draw_big_digital_panel);
  layer_add_child(parent, s_panel_layer);
}

void big_digital_display_destroy_panel(void) {
  if (s_panel_layer) {
    layer_destroy(s_panel_layer);
    s_panel_layer = NULL;
  }
}

void big_digital_display_mark_panel_dirty(void) {
  if (s_panel_layer) layer_mark_dirty(s_panel_layer);
}
