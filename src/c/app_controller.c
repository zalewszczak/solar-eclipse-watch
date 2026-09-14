#include <pebble.h>
#include "eclipse_data.h"
#include "comms.h"
#include "persistence.h"
#include "battery_saver.h"
#include "battery_saver_controller.h"
#include "input.h"
#include "time_service.h"
#include "background_layer.h"
#include "background_animation.h"
#include "sky_layer.h"
#include "features_layer.h"
#include "clock_display.h"
#include "hands_controller.h"
#include "message_key_index.h" // MK_* is used for the startup key-index consistency check.
#include "eclipse_ui.h"
#include "hourly_vibration.h"
#include "layout_controller.h"
#include "watchface_ui.h"
#include "feature_refresh.h"

static Window *s_window;
static void hands_controller_invalidate(void *context) {
  (void)context;
  Layer *hands_layer = layout_controller_hands_layer();
  if (hands_layer) layer_mark_dirty(hands_layer);
  clock_display_mark_panel_dirty();
}

static EclipseData s_data;

// Battery-saver tick policy and Pebble tick subscription live in
// time_service.c. The application still owns what each tick means visually
// (redraws, vibrations, feature refreshes, etc.); time_service only decides
// SECOND_UNIT versus MINUTE_UNIT and forwards the resulting tick here.

// ---- tick + click ---------------------------------------------------------

static void time_service_tick_handler(struct tm *tick_time, TimeUnits units_changed, void *context) {
  (void)context;

  if (units_changed & MINUTE_UNIT) hourly_vibration_handle_minute(tick_time);

  battery_saver_controller_update();

  // Deep sleep still subscribes at MINUTE_UNIT, because Pebble has no
  // five-minute TickTimerService unit. Skip redraw work on the four minutes
  // between five-minute boundaries.
  bool deep_sleep_skip = (battery_saver_phase() == BATTERY_SAVER_DEEP_SLEEP) && (tick_time->tm_min % 5 != 0);
  if (!deep_sleep_skip) {
    watchface_ui_refresh_status(false);
    if (battery_saver_phase() != BATTERY_SAVER_AWAKE && layout_controller_features_layer()) {
      features_layer_refresh_values(layout_controller_features_layer());
    }
  }

  // Seconds-precision feature slots piggyback on SECOND_UNIT rather than
  // maintaining another wake source.
  if (time_service_is_seconds() && layout_controller_features_layer()) {
    features_layer_refresh_second_slots(layout_controller_features_layer());
  }

  // Re-check on every tick because an eclipse can enter or leave its active
  // contact window without a settings update. The time service changes
  // subscription granularity only when it actually needs to.
  time_service_update();
}


// ---- window lifecycle ----------------------------------------------------

// Creates the right set of layers for the current bottom_style, tearing
// down and rebuilding whatever's there if the style actually changed.
// Needed (rather than just resizing existing layers) because the sky
// canvas's cache bitmap is sized once at creation and Pebble has no
// API to resize a layer's internal state afterward -- a mode switch
// that changes the canvas's height means destroying and recreating
// it, not just adjusting its frame. Idempotent: safe to call after
// every settings update even when the style didn't change, since it
// no-ops in that case.
// Reacts to Timeline Quick View (or any future system overlay using
// this same API) appearing/disappearing at the bottom of the screen.
// Digital mode's bottom panel used to just shrink its own
// height from the bottom (top edge fixed) as the obstruction grew --
// which cropped/hid its content (most visibly the digital clock's own
// time text) rather than keeping it fully visible, since the text's
// own position inside that panel never moved to compensate. Now the
// panel instead shifts UP by exactly however much the obstruction
// ate into it, keeping its own full height (and everything drawn in
// it) intact, with the sky canvas above it shrinking by that same
// amount to make room -- same "make room by moving, not cropping"
// idea analog mode's hands/canvas already used for this.
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

  // Create the countdown first; the layout controller re-parents it on
  // top of the newly composed layers whenever the layout changes.
  // switch, so its own creation only has to happen once.
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
  features_layer_unload_fonts();
}

static void comms_data_applied(CommsChangeFlags changes, void *context) {
  (void)context;

  if (changes & COMMS_CHANGE_CLOCK_FONT) clock_display_apply_font();
  if (changes & COMMS_CHANGE_LAYOUT) layout_controller_apply();
  if (layout_controller_hands_layer() && (changes & COMMS_CHANGE_HANDS)) layer_mark_dirty(layout_controller_hands_layer());
  if (clock_display_panel_layer() && (changes & COMMS_CHANGE_PANEL)) layer_mark_dirty(clock_display_panel_layer());
  if (layout_controller_features_layer() && (changes & (COMMS_CHANGE_FEATURES | COMMS_CHANGE_FEATURE_VALUES))) {
    features_layer_set_data(layout_controller_features_layer(), &s_data);
  }
  if (layout_controller_canvas_layer() && (changes & COMMS_CHANGE_CANVAS)) {
    background_layer_set_data(layout_controller_canvas_layer(), &s_data);
  }

  // Preserve the original invalid-payload fast path: settings that arrive
  // before the first valid eclipse payload are applied and rendered, but
  // battery-saver/tick policy is not re-evaluated until valid data exists.
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
  // The generated MK_* table is intentionally based on package.json's
  // messageKeys ordering. Verify the one arithmetic assumption that the
  // communication parser relies on so a future SDK/key-generation change
  // fails loudly instead of silently corrupting incoming fields.
  if (MESSAGE_KEY_WEATHER_LAST_UPDATE - MESSAGE_KEY_MESSAGE_TYPE != MK_WEATHER_LAST_UPDATE) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "message key base assumption broken -- regenerate message_key_index.h");
  }

  persistence_load(&s_data);
  battery_saver_controller_init(&s_data);

  // Initialize the time service before pushing the window because Pebble may
  // invoke window_load() synchronously during window_stack_push(). The load
  // path computes the countdown's precision, so the scheduling policy must
  // already have a valid data pointer at that point.
  time_service_init(&s_data, time_service_tick_handler, NULL);
  hands_controller_init(&s_data, hands_controller_invalidate, NULL);
  clock_display_init(&s_data);
  hourly_vibration_init(&s_data);

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
  layout_controller_deinit();
  window_destroy(s_window);
}

