#include "./background_animation.h"
#include "../../data/eclipse_status.h"
#include "./background_layer.h"

static Animation *s_animation;
static Layer *s_canvas_layer;
static bool s_played;
static bool s_active;
static uint16_t s_elapsed_ms;
static int32_t s_eased_progress_1000;

static void animation_update(Animation *animation, AnimationProgress progress) {
  int32_t elapsed = 0;
  animation_get_elapsed(animation, &elapsed);
  if (elapsed < 0) elapsed = 0;
  if (elapsed > BACKGROUND_ANIMATION_DURATION_MS) elapsed = BACKGROUND_ANIMATION_DURATION_MS;
  s_elapsed_ms = (uint16_t)elapsed;
  s_eased_progress_1000 = ((int32_t)progress * 1000) / ANIMATION_NORMALIZED_MAX;
  if (s_eased_progress_1000 < 0) s_eased_progress_1000 = 0;
  if (s_eased_progress_1000 > 1000) s_eased_progress_1000 = 1000;
  if (s_canvas_layer) {
    background_layer_set_background_animation(s_canvas_layer, true, s_elapsed_ms);
  }
}

static void animation_stopped(Animation *animation, bool finished, void *context) {
  (void)context;
  s_active = false;
  s_elapsed_ms = BACKGROUND_ANIMATION_DURATION_MS;
  s_eased_progress_1000 = 1000;
  if (s_canvas_layer) {
    background_layer_set_background_animation(s_canvas_layer, false, s_elapsed_ms);
  }
  if (s_animation == animation) s_animation = NULL;
  animation_destroy(animation);
}

static const AnimationImplementation s_animation_implementation = {
  .setup = NULL,
  .update = animation_update,
  .teardown = NULL
};

void background_animation_start(EclipseData *data, Layer *canvas_layer) {
  if (s_played || !data || data->bg_anim_mode == 0) return;
  if (eclipse_status_is_active(data, time(NULL))) return;

  s_played = true;
  s_canvas_layer = canvas_layer;

// Bitmap-backed marker styles cannot animate their marker artwork. Avoid
  bool bitmap_marker_active = data->big_analog_marker_style >= 3 &&
                              data->big_analog_marker_style <= 7;
  if (data->bg_anim_mode == 2 && bitmap_marker_active) return;

  s_active = true;
  s_elapsed_ms = 0;
  s_eased_progress_1000 = 0;

  s_animation = animation_create();
  if (!s_animation) {
    s_active = false;
    return;
  }
  animation_set_duration(s_animation, BACKGROUND_ANIMATION_DURATION_MS);
  animation_set_curve(s_animation, AnimationCurveEaseOut);
  animation_set_implementation(s_animation, &s_animation_implementation);
  animation_set_handlers(s_animation, (AnimationHandlers) {
    .started = NULL,
    .stopped = animation_stopped
  }, NULL);
  if (!animation_schedule(s_animation)) {
    Animation *failed = s_animation;
    s_animation = NULL;
    s_active = false;
    animation_destroy(failed);
    return;
  }
}

void background_animation_deinit(void) {
  if (s_animation) {
    Animation *animation = s_animation;
    s_animation = NULL;
    animation_unschedule(animation);
  }
  s_active = false;
  s_canvas_layer = NULL;
}

bool background_animation_is_active(void) {
  return s_active;
}

int32_t background_animation_progress_1000(void) {
  if (!s_active) return 1000;
  return s_eased_progress_1000;
}
