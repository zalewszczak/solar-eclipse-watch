#include "background_animation.h"
#include "eclipse_status.h"
#include "background_layer.h"

#define BACKGROUND_ANIMATION_FRAME_MS 40

static AppTimer *s_timer;
static Layer *s_canvas_layer;
static bool s_played;
static bool s_active;
static uint16_t s_elapsed_ms;

static void animation_timer_callback(void *context) {
  (void)context;

  s_elapsed_ms += BACKGROUND_ANIMATION_FRAME_MS;
  if (s_elapsed_ms >= BACKGROUND_ANIMATION_DURATION_MS) {
    s_elapsed_ms = BACKGROUND_ANIMATION_DURATION_MS;
    s_active = false;
    s_timer = NULL;
  } else {
    s_timer = app_timer_register(BACKGROUND_ANIMATION_FRAME_MS,
                                  animation_timer_callback, NULL);
  }

  if (s_canvas_layer) {
    background_layer_set_background_animation(s_canvas_layer, s_active, s_elapsed_ms);
  }
}

void background_animation_start(EclipseData *data, Layer *canvas_layer) {
  if (s_played || !data || data->bg_anim_mode == 0) return;
  if (eclipse_status_is_active(data, time(NULL))) return;

  s_played = true;
  s_canvas_layer = canvas_layer;

  // Bitmap-backed marker styles cannot animate their marker artwork. Avoid
  // spending the startup redraw burst at the exact moment those PNGs are
  // first decoded; visually this is equivalent to the old non-animated path.
  bool bitmap_marker_active = data->big_analog_marker_style >= 3 &&
                              data->big_analog_marker_style <= 7;
  if (data->bg_anim_mode == 2 && bitmap_marker_active) return;

  s_active = true;
  s_elapsed_ms = 0;
  if (s_canvas_layer) {
    background_layer_set_background_animation(s_canvas_layer, true, 0);
  }
  s_timer = app_timer_register(BACKGROUND_ANIMATION_FRAME_MS,
                               animation_timer_callback, NULL);
}

void background_animation_deinit(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  s_active = false;
  s_canvas_layer = NULL;
}

bool background_animation_is_active(void) {
  return s_active;
}

int32_t background_animation_progress_1000(void) {
  if (!s_active) return 1000;
  int32_t progress = ((int32_t)s_elapsed_ms * 1000) /
                     BACKGROUND_ANIMATION_DURATION_MS;
  if (progress > 1000) progress = 1000;
  if (progress < 0) progress = 0;
  return progress;
}
