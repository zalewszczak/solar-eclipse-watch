#include "./big_digital_display.h"
#include "./big_digital_lookup.h"
#include "../../features/feature_icon_assets.h"
#include "../../data/eclipse_ui.h"

#define DIGIT_W 38
#define DIGIT_H 160
#define COLON_GAP_W 20

static EclipseData *s_data;
static Layer *s_panel_layer;

static void draw_digit(GContext *ctx, int16_t x, int16_t y, uint8_t style, uint8_t digit, GColor color) {
  if (style >= BIG_DIGITAL_STYLE_COUNT || digit > BIG_DIGITAL_COLON_INDEX) return;
  GBitmap *bmp = gbitmap_create_with_resource(BIG_DIGITAL_DIGIT_RESOURCES[style][digit]);
  if (!bmp) return;
  feature_icon_assets_draw_bitmap_tinted_sized(ctx, bmp, GPoint(x, y), color, DIGIT_W, DIGIT_H);
  gbitmap_destroy(bmp);
}

static void draw_big_digital_panel(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // No fill here -- this panel sits on top of a full-screen sky canvas
  // (same as Analog's hands layer does), not its own background.
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(s_data, now, &bg, &main_color, &accent_color);
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

  int16_t total_w = DIGIT_W * 4 + COLON_GAP_W;
  int16_t x = bounds.origin.x + (bounds.size.w - total_w) / 2;
  int16_t y = bounds.origin.y + (bounds.size.h - DIGIT_H) / 2;

  int hour = t->tm_hour, minute = t->tm_min;
  draw_digit(ctx, x, y, style, (uint8_t)(hour / 10), main_color);
  draw_digit(ctx, x + DIGIT_W, y, style, (uint8_t)(hour % 10), main_color);

  // Colon: the 11th resource per style (index BIG_DIGITAL_COLON_INDEX,
  // "10.png"), not procedural dots -- a fixed dot size never fit every
  // style's actual digit proportions. Drawn at the same DIGIT_W x
  // DIGIT_H canvas as every other digit (its own art is just a thin
  // mark in the middle of an otherwise transparent image), centered on
  // the same midpoint the dots used to be centered on -- H1/H2/M1/M2's
  // own positions below are unchanged.
  int16_t colon_cx = x + DIGIT_W * 2 + COLON_GAP_W / 2;
  draw_digit(ctx, colon_cx - DIGIT_W / 2, y, style, BIG_DIGITAL_COLON_INDEX, main_color);

  draw_digit(ctx, x + DIGIT_W * 2 + COLON_GAP_W, y, style, (uint8_t)(minute / 10), main_color);
  draw_digit(ctx, x + DIGIT_W * 3 + COLON_GAP_W, y, style, (uint8_t)(minute % 10), main_color);
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
