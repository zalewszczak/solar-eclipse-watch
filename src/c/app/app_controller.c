#include <pebble.h>
#include "../data/eclipse_data.h"
#include "../comms/comms.h"
#include "../persistence/persistence.h"
#include "../power/battery_saver.h"
#include "../power/battery_saver_controller.h"
#include "../input/input.h"
#include "../timing/time_service.h"
#include "../rendering/background/background_layer.h"
#include "../rendering/background/background_animation.h"
#include "../rendering/background/sky_layer.h"
#include "../features/feature_controller.h"
#include "../features/features_layer.h"
#include "../rendering/clock/clock_display.h"
#include "../rendering/clock/big_digital_display.h"
#include "../rendering/clock/grid_display.h"
#include "../hands/hands_controller.h"
#include "../data/eclipse_ui.h"
#include "../timing/hourly_vibration.h"
#include "./layout_controller.h"
#include "./watchface_ui.h"
#include "../timing/feature_refresh.h"

static Window *s_window;
static void hands_controller_invalidate(void *context) {
  (void)context;
  Layer *hands_layer = layout_controller_hands_layer();
  if (hands_layer) layer_mark_dirty(hands_layer);
  clock_display_mark_panel_dirty();
}

static EclipseData s_data;

// Tick subscription policy lives in time_service.c; this callback owns visual work.
// ---- tick + click ---------------------------------------------------------

static void time_service_tick_handler(struct tm *tick_time, TimeUnits units_changed, void *context) {
  (void)context;

  if (units_changed & MINUTE_UNIT) hourly_vibration_handle_minute(tick_time);

  battery_saver_controller_update();

  // Deep sleep uses MINUTE_UNIT; process only five-minute boundaries.
  bool deep_sleep_skip = (battery_saver_phase() == BATTERY_SAVER_DEEP_SLEEP) && (tick_time->tm_min % 5 != 0);
  if (!deep_sleep_skip) {
    watchface_ui_refresh_status(false);
    if (battery_saver_phase() != BATTERY_SAVER_AWAKE && layout_controller_features_layer()) {
      features_layer_refresh_values(layout_controller_features_layer());
    }
  }

  // Seconds-precision feature slots use SECOND_UNIT.
  if (time_service_is_seconds() && layout_controller_features_layer()) {
    features_layer_refresh_second_slots(layout_controller_features_layer());
  }

  // Re-check eclipse activity on each tick.
  time_service_update();
}


// ---- window lifecycle ----------------------------------------------------

// Rebuild layers when bottom_style changes; the sky cache is sized at creation.
// Keep the digital panel above system overlays and shrink the sky to match.
static void unobstructed_change_handler(AnimationProgress progress, void *context) {
  (void)context;
  layout_controller_handle_unobstructed(progress);
}

static UnobstructedAreaHandlers s_unobstructed_handlers = {
  .change = unobstructed_change_handler
};

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  // Create the countdown once; layout changes re-parent it as needed.
  clock_display_create_countdown(root, GRect(0, 20, bounds.size.w, 20));

  layout_controller_apply();
  clock_display_apply_font(); // uses whatever was loaded from persistent storage

  watchface_ui_refresh_status(true);
  hands_controller_start_startup_animation();
  background_animation_start(&s_data, layout_controller_canvas_layer());
}

static void window_unload(Window *window) {
  clock_display_destroy_countdown();
  layout_controller_unload();
  feature_controller_unload_fonts();
}

static void comms_data_applied(CommsChangeFlags changes, void *context) {
  (void)context;

  if (changes & COMMS_CHANGE_CLOCK_FONT) {
    clock_display_apply_font();
    big_digital_display_mark_panel_dirty(); // no font to load here, just redraw with the new style
    grid_display_apply_font();
  }
  if (changes & COMMS_CHANGE_LAYOUT) layout_controller_apply();
  if (layout_controller_hands_layer() && (changes & COMMS_CHANGE_HANDS)) layer_mark_dirty(layout_controller_hands_layer());
  if (clock_display_panel_layer() && (changes & COMMS_CHANGE_PANEL)) layer_mark_dirty(clock_display_panel_layer());
  if (layout_controller_features_layer() && (changes & COMMS_CHANGE_FEATURES)) {
    features_layer_set_data(layout_controller_features_layer(), &s_data);
  }
  if (layout_controller_canvas_layer() && (changes & COMMS_CHANGE_CANVAS)) {
    background_layer_set_data(layout_controller_canvas_layer(), &s_data);
  }

  // Apply early settings, but defer tick-policy changes until valid eclipse data exists.
  if (!s_data.valid) {
    persistence_save(&s_data);
    watchface_ui_refresh_status(true);
    return;
  }

  battery_saver_controller_apply_enabled(s_data.battery_saver_enabled);
  time_service_update();
  persistence_save(&s_data);
  watchface_ui_refresh_status(true);
}

void app_controller_init(void) {
  // The MESSAGE_KEY_MESSAGE_TYPE + MK_* assumption the decoder relies on
  // is now verified where it can be checked for free: at generation
  // time, in scripts/generate-message-keys.js. The equivalent runtime
  // check that used to live here cost more binary (an error string, two
  // more MESSAGE_KEY_* words in .data and their relocations) than it
  // could ever save.
  persistence_load(&s_data);
  battery_saver_controller_init(&s_data);

  // Initialize time service before pushing the window; window_load() may run synchronously.
  time_service_init(&s_data, time_service_tick_handler, NULL);
  hands_controller_init(&s_data, hands_controller_invalidate, NULL);
  clock_display_init(&s_data);
  big_digital_display_init(&s_data);
  grid_display_init(&s_data);
  hourly_vibration_init(&s_data, hands_controller_invalidate, NULL);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  layout_controller_init(&s_data, s_window);
  watchface_ui_init(&s_data);
  window_stack_push(s_window, true);

  input_init(&s_data, *watchface_ui_input_callbacks(), NULL);
  unobstructed_area_service_subscribe(s_unobstructed_handlers, NULL);
  feature_refresh_init();
  feature_refresh_start();

  comms_init(&s_data, comms_data_applied, NULL);
  battery_saver_controller_update();
}

void app_controller_deinit(void) {
  battery_saver_controller_deinit();
  time_service_deinit();
  input_deinit();
  watchface_ui_deinit();
  unobstructed_area_service_unsubscribe();
  feature_refresh_deinit();
  comms_deinit();
  background_animation_deinit();
  hands_controller_deinit();
  hourly_vibration_deinit();
  clock_display_deinit();
  big_digital_display_deinit();
  grid_display_deinit();
  layout_controller_deinit();
  window_destroy(s_window);
}

