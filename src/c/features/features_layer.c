#include "./features_layer.h"
#include "./feature_controller.h"
#include "./feature_slot.h"
#include "./feature_render.h"
#include "../fonts/font_lookup.h"

// The feature layer is intentionally thin. Feature layout, value resolution,

static void features_layer_update_proc(Layer *layer, GContext *ctx) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  if (!state->data) return;

  GRect bounds = layer_get_unobstructed_bounds(layer);
  GFont font = feature_controller_font(state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);
  int16_t font_offset = font_lookup_y_offset(state->data->corner_font);
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    feature_render_draw_slot(ctx, bounds, &state->slots[i], font, font_h, font_offset,
                             state->data->outline_style, state->data->weather_icon_style,
                             state->data->draw_debug);
  }
}

Layer *features_layer_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(FeaturesState));
  if (!layer) return NULL;
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  state->data = NULL;
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) state->slots[i].active = false;
  layer_set_update_proc(layer, features_layer_update_proc);
  return layer;
}

void features_layer_destroy(Layer *layer) {
  if (layer) layer_destroy(layer);
}

void features_layer_set_data(Layer *layer, EclipseData *data) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  feature_controller_set_data(state, data);
  layer_mark_dirty(layer);
}

void features_layer_refresh_values(Layer *layer) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  feature_controller_refresh_values(state);
  layer_mark_dirty(layer);
}


void features_layer_refresh_second_slots(Layer *layer) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  feature_controller_refresh_second_slots(state);
  layer_mark_dirty(layer);
}

void features_layer_refresh_content(Layer *layer, uint8_t content) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  feature_controller_refresh_content(state, content);
  layer_mark_dirty(layer);
}

