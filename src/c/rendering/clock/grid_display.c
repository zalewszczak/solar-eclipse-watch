#include "./grid_display.h"
#include "../../fonts/font_lookup.h"
#include "../../data/eclipse_ui.h"
#include "../../features/feature_rules.h"

#define GRID_SIZE 4
#define GRID_CELLS (GRID_SIZE * GRID_SIZE)
#define GRID_PAD_SIDES 10
#define GRID_PAD_TOPBOTTOM 24

static EclipseData *s_data;
static Layer *s_panel_layer;
static TextLayer *s_cells[GRID_CELLS];
static char s_cell_text[GRID_CELLS][2]; // 1 char + NUL; TextLayer keeps a pointer, not a copy
static FontSlot s_font_slot = FONT_SLOT_EMPTY;

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

static void set_cell(int idx, char c) {
  s_cell_text[idx][0] = c;
  s_cell_text[idx][1] = '\0';
  text_layer_set_text(s_cells[idx], s_cell_text[idx]);
}

void grid_display_refresh(void) {
  if (!s_data || !s_cells[0]) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(s_data, now, &bg, &main_color, &accent_color);
  (void)bg;

  char buf[8];

  // Row 0: HH MM, one digit per cell, always zero-padded.
  int hour = t->tm_hour, minute = t->tm_min;
  set_cell(0, (char)('0' + hour / 10));
  set_cell(1, (char)('0' + hour % 10));
  set_cell(2, (char)('0' + minute / 10));
  set_cell(3, (char)('0' + minute % 10));

  // Row 1: first 4 letters of the weekday name, uppercased.
  strftime(buf, sizeof(buf), "%A", t);
  feature_rules_to_upper_str(buf);
  for (int i = 0; i < 4; i++) set_cell(4 + i, buf[i] ? buf[i] : ' ');

  // Row 2: day of month (zero-padded) + its ordinal suffix, e.g. "14TH".
  int day = t->tm_mday;
  const char *suffix = ordinal_suffix(day);
  set_cell(8, (char)('0' + day / 10));
  set_cell(9, (char)('0' + day % 10));
  set_cell(10, suffix[0]);
  set_cell(11, suffix[1]);

  // Row 3: first 4 letters of the month name, uppercased -- blank cells
  // for a shorter name (e.g. "MAY ").
  strftime(buf, sizeof(buf), "%B", t);
  feature_rules_to_upper_str(buf);
  for (int i = 0; i < 4; i++) set_cell(12 + i, buf[i] ? buf[i] : ' ');

  for (int i = 0; i < GRID_CELLS; i++) {
    text_layer_set_text_color(s_cells[i], main_color);
  }
}

void grid_display_apply_font(void) {
  if (!s_data) return;
  GFont font = font_lookup_resolve(&s_font_slot, s_data->clock_font);
  for (int i = 0; i < GRID_CELLS; i++) {
    if (s_cells[i]) text_layer_set_font(s_cells[i], font);
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
  int16_t cell_w = grid_w / GRID_SIZE;
  int16_t cell_h = grid_h / GRID_SIZE;

  for (int r = 0; r < GRID_SIZE; r++) {
    for (int c = 0; c < GRID_SIZE; c++) {
      int idx = r * GRID_SIZE + c;
      GRect cell_frame = GRect(GRID_PAD_SIDES + c * cell_w, GRID_PAD_TOPBOTTOM + r * cell_h, cell_w, cell_h);
      TextLayer *cell = text_layer_create(cell_frame);
      text_layer_set_background_color(cell, GColorClear);
      text_layer_set_text_alignment(cell, GTextAlignmentCenter);
      s_cell_text[idx][0] = ' ';
      s_cell_text[idx][1] = '\0';
      text_layer_set_text(cell, s_cell_text[idx]);
      layer_add_child(s_panel_layer, text_layer_get_layer(cell));
      s_cells[idx] = cell;
    }
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
  layer_destroy(s_panel_layer);
  s_panel_layer = NULL;
}
