#include <pebble.h>
#include "../data/eclipse_status.h"
#include "./watchface_ui.h"
#include "../rendering/background/sky_layer.h"
#include "../rendering/background/background_layer.h"
#include "../rendering/clock/clock_display.h"
#include "./layout_controller.h"
#include "../features/features_layer.h"
#include "../input/input.h"
#include "../timing/time_service.h"
#include "../power/battery_saver.h"

static EclipseData *s_data;

void watchface_ui_init(EclipseData *data) {
  s_data = data;
}

void watchface_ui_deinit(void) {
  s_data = NULL;
}

void watchface_ui_refresh_status(bool force_canvas) {
  if (!s_data) return;

  time_t now = time(NULL);
  char text[40];
  EclipsePhase phase = eclipse_status_get_text(s_data, now, text, sizeof(text),
                                               time_service_live_seconds_now(now));
  GColor text_color = sky_layer_is_bright(s_data, now) ? GColorBlack : GColorWhite;
  bool hide_label = s_data->valid && !s_data->has_eclipse;

  if (phase == PHASE_TOTAL && (now % 2) == 0) {
    text[0] = '\0';
    hide_label = true;
  }

  if (battery_saver_phase() == BATTERY_SAVER_DEEP_SLEEP) {
    snprintf(text, sizeof(text), "Zzzzzzz");
    hide_label = false;
  } else if (battery_saver_phase() == BATTERY_SAVER_SLEEP) {
    snprintf(text, sizeof(text), "Zzz");
    hide_label = false;
  }

  clock_display_set_countdown(text, text_color, hide_label);
  clock_display_mark_panel_dirty();
  if (layout_controller_hands_layer()) layer_mark_dirty(layout_controller_hands_layer());

  if (force_canvas) {
    if (layout_controller_canvas_layer()) background_layer_set_data(layout_controller_canvas_layer(), s_data);
  } else if (layout_controller_canvas_layer()) {
    background_layer_tick(layout_controller_canvas_layer());
  }
}

void watchface_ui_update_planet_seek_accuracy_label(bool active) {
  if (!s_data || !clock_display_countdown_layer()) return;

  time_t now = time(NULL);
  bool bright = sky_layer_is_bright(s_data, now);
  if (active && input_planet_seek_compass_low_accuracy()) {
    char text[40];
    if ((now % 2) != 0) {
      snprintf(text, sizeof(text), "Low compass accuracy");
      clock_display_set_countdown(text, bright ? GColorBlack : GColorWhite, false);
    } else {
      clock_display_set_countdown("", bright ? GColorBlack : GColorWhite, true);
    }
  } else {
    char text[40];
    eclipse_status_get_text(s_data, now, text, sizeof(text), time_service_live_seconds_now(now));
    clock_display_set_countdown(text, bright ? GColorBlack : GColorWhite, s_data->valid && !s_data->has_eclipse);
  }
}

static void input_wake_handler(void *context) {
  (void)context;
  watchface_ui_refresh_status(true);
}

static void input_labels_changed(bool visible, void *context) {
  (void)context;
  if (layout_controller_canvas_layer()) background_layer_set_labels_visible(layout_controller_canvas_layer(), visible);
}

static void input_animation_frame_handler(bool active, uint32_t elapsed_ms, bool planet_seek, void *context) {
  (void)context;
  if (planet_seek) watchface_ui_update_planet_seek_accuracy_label(active);
  if (layout_controller_hands_layer()) layer_mark_dirty(layout_controller_hands_layer());
  if (layout_controller_features_layer()) layer_mark_dirty(layout_controller_features_layer());
  clock_display_mark_countdown_dirty();
  if (layout_controller_canvas_layer() && planet_seek) {
    background_layer_set_planet_seek(layout_controller_canvas_layer(), active, elapsed_ms, input_planet_seek_heading_deg());
  }
}

static void input_compass_feature_refresh_handler(void *context) {
  (void)context;
  if (layout_controller_features_layer()) features_layer_refresh_content(layout_controller_features_layer(), 85);
}

const InputCallbacks *watchface_ui_input_callbacks(void) {
  static const InputCallbacks callbacks = {
    .wake = input_wake_handler,
    .labels_changed = input_labels_changed,
    .animation_frame = input_animation_frame_handler,
    .compass_feature_refresh = input_compass_feature_refresh_handler,
    .context = NULL,
  };
  return &callbacks;
}
