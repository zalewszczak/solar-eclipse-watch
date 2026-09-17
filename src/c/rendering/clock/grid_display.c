#include "./grid_display.h"
#include "../../fonts/font_lookup.h"
#include "../../data/eclipse_ui.h"
#include "../../features/feature_rules.h"
#include "../../features/feature_layout.h"
#include "../../features/feature_health.h"
#include "../../features/feature_icon_assets.h"
#include "../../features/feature_render.h"

#define GRID_SIZE 4
#define GRID_CELLS (GRID_SIZE * GRID_SIZE)
#define GRID_PAD_SIDES 10
#define GRID_PAD_TOPBOTTOM 24
// Row 2 (index 8-11), column 0 -- the only cell any Grid variant ever
// swaps for an icon instead of a character (Health/Altitude's "heart
// rate" row). A plain int rather than a #define pair since it's always
// this one specific cell, never computed from a row/col.
#define HEART_ICON_CELL 8

static EclipseData *s_data;
static Layer *s_panel_layer;
static TextLayer *s_cells[GRID_CELLS]; // NULL at HEART_ICON_CELL when s_heart_layer is in use
static char s_cell_text[GRID_CELLS][2]; // 1 char + NUL; TextLayer keeps a pointer, not a copy
static FontSlot s_font_slot = FONT_SLOT_EMPTY;
static int16_t s_grid_x0, s_grid_y0, s_cell_w, s_cell_h;
static Layer *s_heart_layer; // non-NULL only for the Health/Altitude variants

static bool wants_heart_icon(void) {
  return s_data && (s_data->bottom_style == BOTTOM_STYLE_GRID_HEALTH || s_data->bottom_style == BOTTOM_STYLE_GRID_ALTITUDE);
}

static const char *ordinal_suffix(int day) {
  int mod100 = day % 100;
  if (mod100 >= 11 && mod100 <= 13) return "TH";
  switch (day % 10) {
    case 1: return "ST";
    case 2: return "ND";
    case 3: return "RD";
    default: return "TH";
  }
}

// Same condition/cloud_pct thresholds as weather_layer_short_condition_text()
// (see weather_layer.c), just with 4-character-or-less abbreviations
// instead of full words -- that function's own strings don't all fit a
// single grid row, so this is a parallel small table rather than
// truncating its output (which would cut mid-word for several of them).
static const char *grid_weather_abbrev(uint8_t condition, uint8_t cloud_pct) {
  switch (condition) {
    case 1: return "FOG ";
    case 2: return "RAIN";
    case 3: return "SNOW";
    case 4: return "STRM";
    default:
      if (cloud_pct < 20) return "SUN ";
      if (cloud_pct < 60) return "PCLD";
      if (cloud_pct < 90) return "CLDY";
      return "OVER";
  }
}

static void set_cell(int idx, char c) {
  s_cell_text[idx][0] = c;
  s_cell_text[idx][1] = '\0';
  text_layer_set_text(s_cells[idx], s_cell_text[idx]);
}

// ---- row content: one function per distinct row shown across the 6
// variants, shared between whichever of them use it (see the dispatch
// switch in grid_display_refresh() below for which row uses which). ----

static void fill_weekday(struct tm *t, int base) {
  char buf[10]; // must fit the longest full %A name + NUL (e.g. "Wednesday")
  strftime(buf, sizeof(buf), "%A", t);
  feature_rules_to_upper_str(buf);
  for (int i = 0; i < 4; i++) set_cell(base + i, buf[i] ? buf[i] : ' ');
}

static void fill_month_day(struct tm *t, int base) {
  char buf[10]; // must fit the longest full %B name + NUL (e.g. "September")
  strftime(buf, sizeof(buf), "%B", t);
  feature_rules_to_upper_str(buf);
  set_cell(base + 0, buf[0] ? buf[0] : ' ');
  set_cell(base + 1, buf[1] ? buf[1] : ' ');
  set_cell(base + 2, (char)('0' + t->tm_mday / 10));
  set_cell(base + 3, (char)('0' + t->tm_mday % 10));
}

static void fill_ordinal_date(struct tm *t, int base) {
  int day = t->tm_mday;
  const char *suffix = ordinal_suffix(day);
  set_cell(base + 0, (char)('0' + day / 10));
  set_cell(base + 1, (char)('0' + day % 10));
  set_cell(base + 2, suffix[0]);
  set_cell(base + 3, suffix[1]);
}

static void fill_month(struct tm *t, int base) {
  char buf[10];
  strftime(buf, sizeof(buf), "%B", t);
  feature_rules_to_upper_str(buf);
  for (int i = 0; i < 4; i++) set_cell(base + i, buf[i] ? buf[i] : ' ');
}

static void fill_week_number(struct tm *t, int base) {
  char wk[4]; // "%V" is always 2 digits, zero-padded
  strftime(wk, sizeof(wk), "%V", t);
  set_cell(base + 0, 'W');
  set_cell(base + 1, 'K');
  set_cell(base + 2, wk[0]);
  set_cell(base + 3, wk[1]);
}

static void fill_temperature(int base) {
  int16_t shown = feature_rules_convert_temp(s_data->weather_temp_c, s_data->temp_unit);
  bool neg = shown < 0;
  int mag = neg ? -shown : shown;
  set_cell(base + 0, neg ? '-' : '+');
  if (s_data->temp_unit == 0) { // Celsius: 2 digits + trailing unit letter
    if (mag > 99) mag = 99;
    set_cell(base + 1, (char)('0' + mag / 10));
    set_cell(base + 2, (char)('0' + mag % 10));
    set_cell(base + 3, 'C');
  } else { // Fahrenheit/Kelvin: 3 digits, no room left for a unit letter
    if (mag > 999) mag = 999;
    set_cell(base + 1, (char)('0' + mag / 100));
    set_cell(base + 2, (char)('0' + (mag / 10) % 10));
    set_cell(base + 3, (char)('0' + mag % 10));
  }
}

static void fill_weather_abbrev(int base) {
  const char *w = grid_weather_abbrev(s_data->weather_condition, s_data->cloud_cover_pct);
  for (int i = 0; i < 4; i++) set_cell(base + i, w[i]);
}

// 4-digit zero-padded below 10000; above that, 2 digits + 'K' + one
// truncated tenths-of-thousand digit (13643 -> "13K6").
static void fill_step_digits(int base, int32_t steps) {
  if (steps < 0) steps = 0;
  if (steps > 99999) steps = 99999;
  if (steps < 10000) {
    set_cell(base + 0, (char)('0' + (steps / 1000) % 10));
    set_cell(base + 1, (char)('0' + (steps / 100) % 10));
    set_cell(base + 2, (char)('0' + (steps / 10) % 10));
    set_cell(base + 3, (char)('0' + steps % 10));
  } else {
    set_cell(base + 0, (char)('0' + (steps / 10000) % 10));
    set_cell(base + 1, (char)('0' + (steps / 1000) % 10));
    set_cell(base + 2, 'K');
    set_cell(base + 3, (char)('0' + (steps % 1000) / 100));
  }
}

static void fill_steps(int base) {
  fill_step_digits(base, (int32_t)feature_health_sum_today(HealthMetricStepCount));
}

static void fill_steps_percent(int base) {
  HealthValue steps = feature_health_sum_today(HealthMetricStepCount);
  uint16_t goal = s_data->daily_step_goal > 0 ? s_data->daily_step_goal : 10000;
  int32_t pct = (steps * 100) / goal;
  if (pct > 999) pct = 999;
  if (pct < 0) pct = 0;
  set_cell(base + 0, (char)('0' + (pct / 100) % 10));
  set_cell(base + 1, (char)('0' + (pct / 10) % 10));
  set_cell(base + 2, (char)('0' + pct % 10));
  set_cell(base + 3, '%');
}

// base is HEART_ICON_CELL -- only base+1..base+3 are real TextLayers here,
// base+0 is s_heart_layer (see draw_heart_icon()) and is never touched.
static void fill_bpm_digits(int base) {
  int bpm = feature_health_peek_current_bpm();
  if (bpm < 0) bpm = 0;
  if (bpm > 999) bpm = 999;
  set_cell(base + 1, (char)('0' + (bpm / 100) % 10));
  set_cell(base + 2, (char)('0' + (bpm / 10) % 10));
  set_cell(base + 3, (char)('0' + bpm % 10));
}

// Positive: 4-digit zero-padded, or (>=10000) 2 digits + 'K' + 1 truncated
// tenths-of-thousand digit. Negative: same idea but one fewer digit slot
// since '-' takes the first cell (3-digit pad below 1000, else 1 digit +
// 'K' + 1 tenths digit). Truncated, not rounded, throughout -- see the
// worked examples in this row's own request for why (they don't agree
// with each other under a single rounding rule; truncation is simplest
// and self-consistent).
static void fill_altitude(int base) {
  int32_t alt = s_data->altitude_m;
  if (alt <= -32000) alt = 0; // sentinel: not available
  if (s_data->altitude_unit == 1) alt = (alt * 328) / 100; // meters -> feet, same approx as content 38

  bool neg = alt < 0;
  int32_t mag = neg ? -alt : alt;
  int cell = base;
  if (neg) set_cell(cell++, '-');

  int digit_cells = neg ? 3 : 4;
  int32_t k_threshold = (digit_cells == 4) ? 10000 : 1000;
  if (mag < k_threshold) {
    int32_t max_mag = (digit_cells == 4) ? 9999 : 999;
    if (mag > max_mag) mag = max_mag;
    int32_t div = (digit_cells == 4) ? 1000 : 100;
    for (int i = 0; i < digit_cells; i++) {
      set_cell(cell + i, (char)('0' + (mag / div) % 10));
      div /= 10;
    }
  } else {
    int32_t max_mag = (digit_cells == 4) ? 99999 : 9999;
    if (mag > max_mag) mag = max_mag;
    int32_t thousands = mag / 1000;
    int32_t tenths = (mag % 1000) / 100;
    int thousands_digits = digit_cells - 2;
    if (thousands_digits == 2) {
      set_cell(cell + 0, (char)('0' + (thousands / 10) % 10));
      set_cell(cell + 1, (char)('0' + thousands % 10));
    } else {
      set_cell(cell + 0, (char)('0' + thousands % 10));
    }
    set_cell(cell + thousands_digits, 'K');
    set_cell(cell + thousands_digits + 1, (char)('0' + tenths));
  }
}

static void draw_heart_icon(Layer *layer, GContext *ctx) {
  if (!s_data) return;
  GRect b = layer_get_bounds(layer);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(s_data, time(NULL), &bg, &main_color, &accent_color);
  (void)bg;
  // Same resources feature_icons.c's own STYLED_ICONS table uses for icon
  // kind 1 (heart rate) elsewhere -- reused directly rather than adding a
  // second heart image just for this "enlarged" size, since
  // feature_icon_assets_draw_styled_with_outline() already draws into
  // whatever w/h it's given.
  static const IconResourceSet HEART_ICON_SET = {
    RESOURCE_ID_ICON_SIMPLE_HEART, RESOURCE_ID_ICON_HOLLOW_HEART, RESOURCE_ID_ICON_FULLCOLOR_HEART
  };
  GColor outline_color = feature_render_contrasting_outline_color(main_color);
  feature_icon_assets_draw_styled_with_outline(ctx, b.origin, &HEART_ICON_SET, s_data->icon_style,
                                               s_data->outline_style, outline_color, main_color,
                                               b.size.w, b.size.h);
}

void grid_display_refresh(void) {
  if (!s_data || !s_cells[0]) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(s_data, now, &bg, &main_color, &accent_color);
  (void)bg;

  // Row 0: HH MM, one digit per cell, always zero-padded -- identical
  // across every Grid variant.
  int hour = t->tm_hour, minute = t->tm_min;
  set_cell(0, (char)('0' + hour / 10));
  set_cell(1, (char)('0' + hour % 10));
  set_cell(2, (char)('0' + minute / 10));
  set_cell(3, (char)('0' + minute % 10));

  switch (s_data->bottom_style) {
    case BOTTOM_STYLE_GRID_WEATHER:
      fill_month_day(t, 4);
      fill_temperature(8);
      fill_weather_abbrev(12);
      break;
    case BOTTOM_STYLE_GRID_HEALTH:
      fill_month_day(t, 4);
      fill_bpm_digits(HEART_ICON_CELL);
      fill_steps(12);
      break;
    case BOTTOM_STYLE_GRID_STEPS:
      fill_month_day(t, 4);
      fill_steps(8);
      fill_steps_percent(12);
      break;
    case BOTTOM_STYLE_GRID_ALTITUDE:
      fill_month_day(t, 4);
      fill_bpm_digits(HEART_ICON_CELL);
      fill_altitude(12);
      break;
    case BOTTOM_STYLE_GRID_WEEK:
      fill_weekday(t, 4);
      fill_month_day(t, 8);
      fill_week_number(t, 12);
      break;
    default: // BOTTOM_STYLE_GRID (Default)
      fill_weekday(t, 4);
      fill_ordinal_date(t, 8);
      fill_month(t, 12);
      break;
  }

  for (int i = 0; i < GRID_CELLS; i++) {
    if (s_cells[i]) text_layer_set_text_color(s_cells[i], main_color);
  }
  if (s_heart_layer) layer_mark_dirty(s_heart_layer);
}

void grid_display_apply_font(void) {
  if (!s_data) return;
  GFont font = font_lookup_resolve(&s_font_slot, s_data->clock_font);
  // Same technique as clock_display.c's own digital clock: a fixed
  // per-cell box tall enough for the tallest font would leave every
  // shorter font sitting high in its cell (Pebble draws text top-aligned
  // within its box), so instead each cell's own box is resized to the
  // selected font's real height + fine-tune offset and re-centered on
  // that cell's fixed center point -- text_layer_set_font() alone
  // doesn't touch layout, so this has to happen here too, not just at
  // creation.
  int16_t font_h = font_lookup_height(s_data->clock_font) + font_lookup_y_offset(s_data->clock_font);
  for (int i = 0; i < GRID_CELLS; i++) {
    if (!s_cells[i]) continue;
    text_layer_set_font(s_cells[i], font);
    int16_t row = i / GRID_SIZE, col = i % GRID_SIZE;
    int16_t cell_cy = s_grid_y0 + row * s_cell_h + s_cell_h / 2;
    GRect cell_frame = GRect(s_grid_x0 + col * s_cell_w, cell_cy - font_h / 2, s_cell_w, font_h);
    layer_set_frame(text_layer_get_layer(s_cells[i]), cell_frame);
  }
  if (s_heart_layer) {
    // "Enlarged" relative to a normal 16px feature icon: sized to most of
    // the cell itself rather than the small fixed icon size used
    // elsewhere, since feature_icon_assets_draw_styled_with_outline()
    // draws into whatever rect it's given.
    int16_t row = HEART_ICON_CELL / GRID_SIZE, col = HEART_ICON_CELL % GRID_SIZE;
    int16_t cell_cx = s_grid_x0 + col * s_cell_w + s_cell_w / 2;
    int16_t cell_cy = s_grid_y0 + row * s_cell_h + s_cell_h / 2;
    int16_t icon_wh = ((s_cell_w < s_cell_h ? s_cell_w : s_cell_h) * 8) / 10;
    layer_set_frame(s_heart_layer, GRect(cell_cx - icon_wh / 2, cell_cy - icon_wh / 2, icon_wh, icon_wh));
  }
}

void grid_display_init(EclipseData *data) {
  s_data = data;
}

void grid_display_deinit(void) {
  font_lookup_release(&s_font_slot);
  s_data = NULL;
}

Layer *grid_display_panel_layer(void) {
  return s_panel_layer;
}

void grid_display_create_panel(Layer *parent, GRect frame) {
  s_panel_layer = layer_create(frame);
  layer_add_child(parent, s_panel_layer);

  int16_t grid_w = frame.size.w - 2 * GRID_PAD_SIDES;
  int16_t grid_h = frame.size.h - 2 * GRID_PAD_TOPBOTTOM;
  s_grid_x0 = GRID_PAD_SIDES;
  s_grid_y0 = GRID_PAD_TOPBOTTOM;
  s_cell_w = grid_w / GRID_SIZE;
  s_cell_h = grid_h / GRID_SIZE;

  bool heart_icon = wants_heart_icon();

  for (int i = 0; i < GRID_CELLS; i++) {
    if (heart_icon && i == HEART_ICON_CELL) continue; // built as s_heart_layer below instead
    // Real per-cell frame (font-height-aware) is set by
    // grid_display_apply_font() below, once the font is known -- this
    // placeholder just needs to exist so text_layer_create() has
    // somewhere to put it first; its exact position/size doesn't matter.
    TextLayer *cell = text_layer_create(GRectZero);
    text_layer_set_background_color(cell, GColorClear);
    text_layer_set_text_alignment(cell, GTextAlignmentCenter);
    s_cell_text[i][0] = ' ';
    s_cell_text[i][1] = '\0';
    text_layer_set_text(cell, s_cell_text[i]);
    layer_add_child(s_panel_layer, text_layer_get_layer(cell));
    s_cells[i] = cell;
  }

  if (heart_icon) {
    s_heart_layer = layer_create(GRectZero); // real frame set by grid_display_apply_font() below
    layer_set_update_proc(s_heart_layer, draw_heart_icon);
    layer_add_child(s_panel_layer, s_heart_layer);
  }

  grid_display_apply_font();
  grid_display_refresh();
}

void grid_display_destroy_panel(void) {
  if (!s_panel_layer) return;
  for (int i = 0; i < GRID_CELLS; i++) {
    if (s_cells[i]) {
      text_layer_destroy(s_cells[i]);
      s_cells[i] = NULL;
    }
  }
  if (s_heart_layer) {
    layer_destroy(s_heart_layer);
    s_heart_layer = NULL;
  }
  layer_destroy(s_panel_layer);
  s_panel_layer = NULL;
}
