#include "./feature_controller.h"
#include <string.h>
#include "../data/eclipse_ui.h"
#include "../fonts/font_lookup.h"
#include "../input/input.h"
#include "./feature_layout.h"
#include "./primitives/primitive_layout.h"
#include "./primitives/primitive_transport.h"

// The controller owns feature-state refresh orchestration and the shared

static FontSlot s_corner_font_slot = FONT_SLOT_EMPTY;

void feature_controller_ensure_corner_custom_font(uint8_t font_id) {
  font_lookup_resolve(&s_corner_font_slot, font_id);
}

void feature_controller_unload_fonts(void) {
  font_lookup_release(&s_corner_font_slot);
}

static bool sequence_has_second_refresh(const PrimitiveFeature *feature) {
  if (!feature) return false;
  for (int i = 0; i < feature->count; i++)
    if (feature->items[i].refresh_class == PRIMITIVE_REFRESH_SECOND) return true;
  return false;
}

static void feature_controller_recompute_slot_value(FeatureSlot *slot, int slot_index, const EclipseData *data,
                                                     GColor main_color, GColor accent_color, GColor bg_color,
                                                     GFont font, int16_t font_h, time_t now, struct tm *t) {
  slot->draw_pill = (slot->color_mode == 2);
  slot->pill_bg = bg_color;
  memset(&slot->primitives, 0, sizeof(slot->primitives));

  const uint8_t *ids = NULL;
  const uint8_t *aux = NULL;
  if (slot_index >= SLOT_CORNER_TL && slot_index <= SLOT_CORNER_BR) {
    int idx = slot_index - SLOT_CORNER_TL;
    ids = data->corner_primitive_ids[idx];
    aux = data->corner_primitive_aux[idx];
  } else if (slot_index >= SLOT_UPPER_L1 && slot_index <= SLOT_RIGHT_L2) {
    ids = data->edge_line_primitive_ids[slot_index];
    aux = data->edge_line_primitive_aux[slot_index];
  }
  if (ids && aux && ids[0] != PRIM_ID_NONE) {
    (void)primitive_transport_try_build(&slot->primitives, ids, aux, data, now, t,
                                        slot->color_mode, main_color, accent_color);
  }
  uint16_t tight_mask = (uint16_t)(data->primitive_tight_mask_lo | ((uint16_t)data->primitive_tight_mask_hi << 8));
  if (slot_index >= SLOT_CORNER_TL && slot_index <= SLOT_CORNER_BR)
    slot->primitives.tight = (tight_mask >> (slot_index - SLOT_CORNER_TL)) & 1u;
  else if (slot_index >= SLOT_UPPER_L1 && slot_index <= SLOT_RIGHT_L2)
    slot->primitives.tight = (tight_mask >> (4 + slot_index)) & 1u;
  slot->needs_second_refresh = sequence_has_second_refresh(&slot->primitives);
  if (slot->primitives.count > 0) primitive_layout_compute(&slot->primitives, slot, font, font_h);
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
    feature_controller_recompute_slot_value(&state->slots[i], i, state->data, main_color, accent_color, bg, font, font_h, now, t);
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
    feature_controller_recompute_slot_value(&state->slots[i], i, state->data, main_color, accent_color, bg, font, font_h, now, t);
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

bool feature_controller_primitive_in_use(const EclipseData *data, PrimitiveId primitive) {
  if (!data || primitive == PRIM_ID_NONE) return false;
  for (int slot = 0; slot < 4; slot++)
    for (int i = 0; i < 10 && data->corner_primitive_ids[slot][i] != PRIM_ID_NONE; i++)
      if (data->corner_primitive_ids[slot][i] == primitive) return true;
  for (int slot = 0; slot < 8; slot++)
    for (int i = 0; i < 10 && data->edge_line_primitive_ids[slot][i] != PRIM_ID_NONE; i++)
      if (data->edge_line_primitive_ids[slot][i] == primitive) return true;
  return false;
}

void feature_controller_refresh_primitive(FeaturesState *state, PrimitiveId primitive) {
  if (!state || !state->data || primitive == PRIM_ID_NONE) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    FeatureSlot *slot = &state->slots[i];
    if (!slot->active) continue;
    bool found = false;
    for (int j = 0; j < slot->primitives.count; j++)
      if (slot->primitives.items[j].id == primitive) { found = true; break; }
    if (found) feature_controller_recompute_slot_value(slot, i, state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

void feature_controller_refresh_second_slots(FeaturesState *state) {
  feature_controller_recompute_second_slots(state);
}

GFont feature_controller_font(uint8_t font_id) {
  feature_controller_ensure_corner_custom_font(font_id);
  return font_lookup_resolve(&s_corner_font_slot, font_id);
}

