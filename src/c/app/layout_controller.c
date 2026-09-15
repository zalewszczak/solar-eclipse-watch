#include <pebble.h>
#include "./layout_controller.h"
#include "../rendering/background/background_layer.h"
#include "../rendering/background/sky_layer.h"
#include "../features/features_layer.h"
#include "../features/feature_layout.h"
#include "../rendering/clock/clock_display.h"
#include "../hands/hands_controller.h"

static EclipseData *s_data;
static Window *s_window;
static Layer *s_canvas_layer;
static Layer *s_top_gradient_layer;
static Layer *s_hands_layer;
static Layer *s_features_layer;
static uint8_t s_current_layout_style = 255;
static bool s_current_draw_features_beneath_hands;

Layer *layout_controller_canvas_layer(void) { return s_canvas_layer; }
static Layer *layout_controller_top_gradient_layer(void) { return s_top_gradient_layer; }
Layer *layout_controller_hands_layer(void) { return s_hands_layer; }
Layer *layout_controller_features_layer(void) { return s_features_layer; }

void layout_controller_init(EclipseData *data, Window *window) {
  s_data = data;
  s_window = window;
  s_current_layout_style = 255;
  s_current_draw_features_beneath_hands = false;
}

void layout_controller_unload(void) {
  if (s_canvas_layer) {
    background_layer_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
  }
  clock_display_destroy_panel();
  if (s_top_gradient_layer) {
    sky_layer_top_gradient_destroy(s_top_gradient_layer);
    s_top_gradient_layer = NULL;
  }
  if (s_hands_layer) {
    hands_controller_destroy_layer(s_hands_layer);
    s_hands_layer = NULL;
  }
  if (s_features_layer) {
    features_layer_destroy(s_features_layer);
    s_features_layer = NULL;
  }
}

void layout_controller_deinit(void) {
  layout_controller_unload();
  s_data = NULL;
  s_window = NULL;
}

void layout_controller_handle_unobstructed(AnimationProgress progress) {
  (void)progress;
  if (!s_window || !s_data) return;

  Layer *root = window_get_root_layer(s_window);
  GRect full_bounds = layer_get_bounds(root);
  GRect unobstructed = layer_get_unobstructed_bounds(root);
  int16_t obstruction_h = full_bounds.size.h - unobstructed.size.h;
  if (obstruction_h < 0) obstruction_h = 0;

  // Digital bar keeps its clock panel at the bottom, moving it upward by
  // the obstruction height. Its sky canvas shrinks to the same boundary.
  if (s_data->bottom_style != 1 &&
      !feature_layout_is_digital_top_layout(s_data->bottom_style) &&
      clock_display_panel_layer()) {
    int16_t full_top = 152;
    int16_t new_top = full_top - obstruction_h;
    if (new_top < 0) new_top = 0;

    GRect frame = layer_get_frame(clock_display_panel_layer());
    if (frame.origin.y != new_top) {
      frame.origin.y = new_top;
      layer_set_frame(clock_display_panel_layer(), frame);
      layer_mark_dirty(clock_display_panel_layer());
    }

    if (s_canvas_layer) {
      GRect canvas_frame = layer_get_frame(s_canvas_layer);
      if (canvas_frame.size.h != new_top) {
        canvas_frame.size.h = new_top;
        layer_set_frame(s_canvas_layer, canvas_frame);
        background_layer_set_data(s_canvas_layer, s_data);
      }
    }
  }

  // Analog and Digital-top layouts shrink their sky canvas from the bottom.
  bool canvas_tracks_unobstructed_bottom =
      s_data->bottom_style == 1 ||
      feature_layout_is_digital_top_layout(s_data->bottom_style);
  if (canvas_tracks_unobstructed_bottom && s_canvas_layer) {
    int16_t canvas_top = feature_layout_is_digital_top_layout(s_data->bottom_style)
        ? DIGITAL_PANEL_H : 0;
    int16_t new_h = unobstructed.size.h - canvas_top;
    if (new_h < 0) new_h = 0;

    GRect frame = layer_get_frame(s_canvas_layer);
    if (frame.size.h != new_h) {
      frame.size.h = new_h;
      layer_set_frame(s_canvas_layer, frame);
      background_layer_set_data(s_canvas_layer, s_data);
    }
  }

  if (s_hands_layer) layer_mark_dirty(s_hands_layer);
  if (s_features_layer) layer_mark_dirty(s_features_layer);
}

void layout_controller_apply(void) {
  if (!s_window || !s_data) return;

  Layer *root = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(root);
  uint8_t style = s_data->bottom_style;
  bool beneath_hands = s_data->draw_features_beneath_hands;

  if (style == s_current_layout_style &&
      beneath_hands == s_current_draw_features_beneath_hands &&
      s_canvas_layer != NULL) {
    return;
  }

  s_current_layout_style = style;
  s_current_draw_features_beneath_hands = beneath_hands;
  layout_controller_unload();

  if (style == 1) {
    s_canvas_layer = background_layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
    layer_add_child(root, s_canvas_layer);

    s_hands_layer = hands_controller_create_layer(GRect(0, 0, bounds.size.w, bounds.size.h));
    if (beneath_hands) {
      s_features_layer = features_layer_create(layer_get_frame(s_canvas_layer));
      layer_add_child(root, s_features_layer);
      layer_add_child(root, s_hands_layer);
    } else {
      layer_add_child(root, s_hands_layer);
    }
  } else if (feature_layout_is_digital_top_layout(style)) {
    s_canvas_layer = background_layer_create(
        GRect(0, DIGITAL_PANEL_H, bounds.size.w, bounds.size.h - DIGITAL_PANEL_H));
    layer_add_child(root, s_canvas_layer);

    s_top_gradient_layer = sky_layer_top_gradient_create(
        GRect(0, 0, bounds.size.w, DIGITAL_PANEL_H));
    layer_add_child(root, s_top_gradient_layer);

    clock_display_create_panel(root, GRect(0, 0, bounds.size.w, DIGITAL_PANEL_H));
    clock_display_apply_font();
  } else {
    s_canvas_layer = background_layer_create(GRect(0, 0, bounds.size.w, 152));
    layer_add_child(root, s_canvas_layer);

    clock_display_create_panel(root, GRect(0, 152, bounds.size.w, bounds.size.h - 152));
    clock_display_apply_font();
  }

  if (!s_features_layer) {
    s_features_layer = features_layer_create(GRect(0, 0, bounds.size.w, bounds.size.h));
    layer_add_child(root, s_features_layer);
  }

  clock_display_reparent_countdown(root);
  background_layer_set_data(s_canvas_layer, s_data);
  if (s_top_gradient_layer) sky_layer_top_gradient_set_data(s_top_gradient_layer, s_data);
  features_layer_set_data(s_features_layer, s_data);

  layout_controller_handle_unobstructed(0);
}
