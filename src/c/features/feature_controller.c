#include "./feature_controller.h"
#include "../data/eclipse_ui.h"
#include "../fonts/font_lookup.h"
#include "./feature_values.h"
#include "./feature_render.h"
#include "./feature_layout.h"

// The controller owns feature-state refresh orchestration and the shared
// custom corner/edge font cache. It deliberately does not know how slots
// are positioned or rendered; those concerns belong to feature_layout and
// feature_render respectively.

static FontSlot s_corner_font_slot = FONT_SLOT_EMPTY;

void feature_controller_ensure_corner_custom_font(uint8_t font_id) {
  font_lookup_resolve(&s_corner_font_slot, font_id);
}

void feature_controller_unload_fonts(void) {
  font_lookup_release(&s_corner_font_slot);
}

static void feature_controller_recompute_slot_value(FeatureSlot *slot, const EclipseData *data,
                                                     GColor main_color, GColor accent_color, GColor bg_color,
                                                     GFont font, int16_t font_h, time_t now, struct tm *t) {
  slot->draw_pill = (slot->color_mode == 2);
  slot->pill_bg = bg_color;

  feature_values_compute_slot(slot, data, main_color, accent_color, bg_color, now, t);

  if (slot->segment_count > 0) {
    feature_render_resolve_segment_offsets(slot, font, font_h);
  }
}

static void feature_controller_recompute_all_values(FeaturesState *state) {
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  feature_controller_ensure_corner_custom_font(state->data->corner_font);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active) continue;
    feature_controller_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

static void feature_controller_recompute_second_slots(FeaturesState *state) {
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active || !state->slots[i].needs_second_refresh) continue;
    feature_controller_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

void feature_controller_set_data(FeaturesState *state, EclipseData *data) {
  state->data = data;
  feature_layout_recompute(state);
  feature_controller_recompute_all_values(state);
}

void feature_controller_refresh_values(FeaturesState *state) {
  feature_controller_recompute_all_values(state);
}

bool feature_controller_content_in_use(const EclipseData *data, uint8_t content) {
  if (!data) return false;
  for (int i = 0; i < 4; i++) {
    if (data->corner_content[i] == content) return true;
  }
  return data->upper_middle_line1_content == content || data->upper_middle_line2_content == content
      || data->bottom_middle_line1_content == content || data->bottom_middle_line2_content == content
      || data->middle_left_line1_content == content || data->middle_left_line2_content == content
      || data->middle_right_line1_content == content || data->middle_right_line2_content == content;
}

void feature_controller_refresh_second_slots(FeaturesState *state) {
  feature_controller_recompute_second_slots(state);
}

void feature_controller_refresh_content(FeaturesState *state, uint8_t content) {
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active || state->slots[i].content != content) continue;
    feature_controller_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

GFont feature_controller_font(uint8_t font_id) {
  feature_controller_ensure_corner_custom_font(font_id);
  return font_lookup_resolve(&s_corner_font_slot, font_id);
}

int16_t feature_controller_font_height(uint8_t font_id) {
  return font_lookup_height(font_id);
}

int16_t feature_controller_font_y_offset(uint8_t font_id) {
  return font_lookup_y_offset(font_id);
}
