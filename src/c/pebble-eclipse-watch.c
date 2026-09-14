#include <pebble.h>
#include "eclipse_data.h"
#include "comms.h"
#include "persistence.h"
#include "battery_saver.h"
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

static Window *s_window;
static void hands_controller_invalidate(void *context) {
  (void)context;
  Layer *hands_layer = layout_controller_hands_layer();
  if (hands_layer) layer_mark_dirty(hands_layer);
  clock_display_mark_panel_dirty();
}

static EclipseData s_data;

static void refresh_status_and_maybe_canvas(bool force_canvas);
static void battery_saver_sync_phase_to_phone(void);

// Battery-saver tick policy and Pebble tick subscription live in
// time_service.c. The application still owns what each tick means visually
// (redraws, vibrations, feature refreshes, etc.); time_service only decides
// SECOND_UNIT versus MINUTE_UNIT and forwards the resulting tick here.

// ---- tick + click ---------------------------------------------------------

static void time_service_tick_handler(struct tm *tick_time, TimeUnits units_changed, void *context) {
  (void)context;

  if (units_changed & MINUTE_UNIT) hourly_vibration_handle_minute(tick_time);

  battery_saver_update(time(NULL));
  battery_saver_sync_phase_to_phone();

  // Deep sleep still subscribes at MINUTE_UNIT, because Pebble has no
  // five-minute TickTimerService unit. Skip redraw work on the four minutes
  // between five-minute boundaries.
  bool deep_sleep_skip = (battery_saver_phase() == BATTERY_SAVER_DEEP_SLEEP) && (tick_time->tm_min % 5 != 0);
  if (!deep_sleep_skip) {
    refresh_status_and_maybe_canvas(false);
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

static void update_planet_seek_accuracy_label(bool active) {
  if (!clock_display_countdown_layer()) return;
  if (active && input_planet_seek_compass_low_accuracy()) {
    time_t now = time(NULL);
    bool flash_visible = (now % 2) != 0; // on for odd seconds, off for even seconds
    if (flash_visible) {
      char text[40];
      snprintf(text, sizeof(text), "Low compass accuracy");
      clock_display_set_countdown(text, sky_layer_is_bright(&s_data, now) ? GColorBlack : GColorWhite, false);
    } else {
      clock_display_set_countdown("", sky_layer_is_bright(&s_data, now) ? GColorBlack : GColorWhite, true);
    }
  } else {
    // Same condition countdown_layer_update_proc/refresh_status_and_maybe_canvas
    // already use elsewhere: hidden whenever there's confirmed to be
    // no eclipse today. Planet seek only ever runs on a non-eclipse
    // day, so this always resolves to "hidden" in practice here, but
    // spelling it out the same way keeps this in sync if that ever
    // changes.
    char text[40];
    eclipse_get_status_text(&s_data, time(NULL), text, sizeof(text), time_service_live_seconds_now(time(NULL)));
    clock_display_set_countdown(text, sky_layer_is_bright(&s_data, time(NULL)) ? GColorBlack : GColorWhite, s_data.valid && !s_data.has_eclipse);
  }
}


static void refresh_status_and_maybe_canvas(bool force_canvas) {
  time_t now = time(NULL);
  char text[40];
  EclipsePhase phase = eclipse_get_status_text(&s_data, now, text, sizeof(text),
                                               time_service_live_seconds_now(now));
  GColor text_color = sky_layer_is_bright(&s_data, now) ? GColorBlack : GColorWhite;
  bool hide_label = s_data.valid && !s_data.has_eclipse;

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
    if (layout_controller_canvas_layer()) background_layer_set_data(layout_controller_canvas_layer(), &s_data);
  } else if (layout_controller_canvas_layer()) {
    background_layer_tick(layout_controller_canvas_layer());
  }
}

// ---- input integration ---------------------------------------------------

static void input_wake_handler(void *context) {
  (void)context;
  refresh_status_and_maybe_canvas(true);
}

static void input_labels_changed(bool visible, void *context) {
  (void)context;
  if (layout_controller_canvas_layer()) background_layer_set_labels_visible(layout_controller_canvas_layer(), visible);
}

static void input_animation_frame_handler(bool active, uint32_t elapsed_ms, bool planet_seek, void *context) {
  (void)context;
  if (planet_seek) update_planet_seek_accuracy_label(active);
  if (layout_controller_hands_layer()) layer_mark_dirty(layout_controller_hands_layer());
  if (layout_controller_features_layer()) layer_mark_dirty(layout_controller_features_layer());
  clock_display_mark_countdown_dirty();
  if (layout_controller_canvas_layer() && planet_seek) background_layer_set_planet_seek(layout_controller_canvas_layer(), active, elapsed_ms, input_planet_seek_heading_deg());
}

static void input_compass_feature_refresh_handler(void *context) {
  (void)context;
  if (layout_controller_features_layer()) features_layer_refresh_content(layout_controller_features_layer(), 85);
}

static void input_init_services(void) {
  input_init(&s_data, (InputCallbacks){
    .wake = input_wake_handler,
    .labels_changed = input_labels_changed,
    .animation_frame = input_animation_frame_handler,
    .compass_feature_refresh = input_compass_feature_refresh_handler,
  }, NULL);
}

// ---- corners overlay's own independent refresh cycle ----------------------

// Once a minute, matching the time service's normal MINUTE_UNIT baseline
// for anything that doesn't need to be genuinely live -- used to be a
// much shorter 5s cadence specifically for health data (heart rate
// especially), but Pebble's own HealthService doesn't actually refresh
// a heart-rate reading that often either, so 5s bought little real
// freshness for a real, constant battery cost. Seconds-precision
// content (Time: second, etc.) no longer depends on this timer at all
// -- see time_service_tick_handler()'s piggyback on SECOND_UNIT ticks.
// The time service requests seconds whenever that content is
// active, which gives it genuinely live per-second updates instead of
// whatever staleness this cadence would otherwise leave it with.
// CORNERS_REFRESH_MS retired -- see FEATURES_REFRESH_MS in features_layer.h
static AppTimer *s_corners_timer = NULL;

static void corners_timer_callback(void *data) {
  if (layout_controller_features_layer()) features_layer_refresh_values(layout_controller_features_layer());
  s_corners_timer = app_timer_register(FEATURES_REFRESH_MS, corners_timer_callback, NULL);
}

// Battery-saver policy/state is implemented in battery_saver.c.
// This file only owns the consequences of a phase change: tick
// subscription, the corners refresh timer, redraws, and phone sync.

static void battery_saver_sync_phase_to_phone(void) {
  static uint8_t s_last_sent_phase = 0xFF;
  uint8_t phase = (uint8_t)battery_saver_phase();
  if (phase == s_last_sent_phase) return;
  if (comms_send_battery_saver_phase(phase)) s_last_sent_phase = phase;
}

static void battery_saver_phase_changed(BatterySaverPhase previous_phase, BatterySaverPhase new_phase, void *context) {
  (void)context;
  bool was_resting = previous_phase != BATTERY_SAVER_AWAKE;
  bool now_resting = new_phase != BATTERY_SAVER_AWAKE;

  // battery_saver_update() has already committed the new phase.
  time_service_update();

  if (now_resting && !was_resting) {
    if (s_corners_timer) {
      app_timer_cancel(s_corners_timer);
      s_corners_timer = NULL;
    }
  } else if (!now_resting && was_resting) {
    if (!s_corners_timer) {
      s_corners_timer = app_timer_register(FEATURES_REFRESH_MS, corners_timer_callback, NULL);
    }
  }

  battery_saver_sync_phase_to_phone();
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

  refresh_status_and_maybe_canvas(true);
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
    refresh_status_and_maybe_canvas(true);
    return;
  }

  battery_saver_set_enabled(s_data.battery_saver_enabled);
  battery_saver_update(time(NULL));
  time_service_update();
  persistence_save(&s_data);
  refresh_status_and_maybe_canvas(true);
}

static void init(void) {
  // The generated MK_* table is intentionally based on package.json's
  // messageKeys ordering. Verify the one arithmetic assumption that the
  // communication parser relies on so a future SDK/key-generation change
  // fails loudly instead of silently corrupting incoming fields.
  if (MESSAGE_KEY_WEATHER_LAST_UPDATE - MESSAGE_KEY_MESSAGE_TYPE != MK_WEATHER_LAST_UPDATE) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "message key base assumption broken -- regenerate message_key_index.h");
  }

  persistence_load(&s_data);
  battery_saver_init(s_data.battery_saver_enabled, time(NULL), battery_saver_phase_changed, NULL);

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
  window_stack_push(s_window, true);

  input_init_services();
  unobstructed_area_service_subscribe(s_unobstructed_handlers, NULL);
  s_corners_timer = app_timer_register(FEATURES_REFRESH_MS, corners_timer_callback, NULL);

  comms_init(&s_data, comms_data_applied, NULL);
  battery_saver_sync_phase_to_phone();
}

static void deinit(void) {
  time_service_deinit();
  input_deinit();
  unobstructed_area_service_unsubscribe();
  if (s_corners_timer) {
    app_timer_cancel(s_corners_timer);
    s_corners_timer = NULL;
  }
  comms_deinit();
  background_animation_deinit();
  hands_controller_deinit();
  hourly_vibration_deinit();
  clock_display_deinit();
  layout_controller_deinit();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
