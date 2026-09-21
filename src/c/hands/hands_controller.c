#include <pebble.h>
#include "../data/eclipse_status.h"
#include "./hands_controller.h"
#include "./hand_layer.h"
#include "../input/input.h"
#include "../rendering/background/background_animation.h"
#include "../data/eclipse_ui.h"
#include "../features/feature_controller.h"
#include "../timing/hourly_vibration.h"

#include "../rendering/background/background_layer.h"
static EclipseData *s_data = NULL;
static Layer *s_hands_layer = NULL;
static HandsControllerInvalidateHandler s_invalidate_handler = NULL;
static void *s_invalidate_context = NULL;

#define STARTUP_CLOCK_ANIM_MS 1400
#define STARTUP_ANIM_PHASE_A_MS ((STARTUP_CLOCK_ANIM_MS * 3) / 10)
#define STARTUP_ANIM_PHASE_B_MS (STARTUP_CLOCK_ANIM_MS - STARTUP_ANIM_PHASE_A_MS)

static Animation *s_startup_animation = NULL;
static Animation *s_startup_phase_a = NULL;
static Animation *s_startup_phase_b = NULL;
static bool s_startup_clock_anim_active = false;
static bool s_startup_clock_anim_played = false; // Prevent replay after settings/data updates.
static uint16_t s_startup_anim_elapsed_ms = 0;
static int32_t s_startup_phase_eased_progress_1000 = 0;
static int32_t s_startup_overall_eased_progress_1000 = 0;

// The Pebble animation subsystem supplies the eased progress directly to the
// custom implementation. Keeping the conversion here avoids reimplementing
// the SDK's cubic ease-out curve in application code.
static int32_t animation_progress_to_1000(AnimationProgress progress) {
  int32_t value = ((int32_t)progress * 1000) / ANIMATION_NORMALIZED_MAX;
  if (value < 0) return 0;
  if (value > 1000) return 1000;
  return value;
}

static void startup_overall_animation_update(Animation *animation, AnimationProgress progress) {
  (void)animation;
  s_startup_overall_eased_progress_1000 = animation_progress_to_1000(progress);
}

static void startup_phase_animation_update(Animation *animation, AnimationProgress progress) {
  int32_t elapsed = 0;
  animation_get_elapsed(animation, &elapsed);
  if (elapsed < 0) elapsed = 0;

  if (animation == s_startup_phase_a) {
    if (elapsed > STARTUP_ANIM_PHASE_A_MS) elapsed = STARTUP_ANIM_PHASE_A_MS;
    s_startup_anim_elapsed_ms = (uint16_t)elapsed;
  } else {
    if (elapsed > STARTUP_ANIM_PHASE_B_MS) elapsed = STARTUP_ANIM_PHASE_B_MS;
    s_startup_anim_elapsed_ms = (uint16_t)(STARTUP_ANIM_PHASE_A_MS + elapsed);
  }
  s_startup_phase_eased_progress_1000 = animation_progress_to_1000(progress);
}

static const AnimationImplementation s_startup_overall_implementation = {
  .setup = NULL,
  .update = startup_overall_animation_update,
  .teardown = NULL
};

static const AnimationImplementation s_startup_phase_implementation = {
  .setup = NULL,
  .update = startup_phase_animation_update,
  .teardown = NULL
};

static void compute_startup_hand_anim(int32_t target_angle, int32_t phase_eased,
                                       int32_t *out_angle, uint16_t *out_length_scale_1000) {
  if (s_startup_anim_elapsed_ms <= STARTUP_ANIM_PHASE_A_MS) {
    int32_t eased = phase_eased;
    *out_length_scale_1000 = (uint16_t)eased;
    int32_t start_angle = -(TRIG_MAX_ANGLE / 6);
    int32_t angle = start_angle + (int32_t)(((int64_t)(-start_angle) * eased) / 1000);
    if (angle < 0) angle += TRIG_MAX_ANGLE;
    *out_angle = angle;
  } else {
    *out_length_scale_1000 = 1000;
    int32_t eased = phase_eased;
    // Preserve the intentional settling wiggle; only its cubic ease-out base
    // is now supplied by Pebble's AnimationCurveEaseOut.
    int32_t inv = 1000 - eased;
    int32_t decay = (int32_t)(((int64_t)inv * inv) / 1000);
    int32_t wiggle_angle = (int32_t)(((int64_t)eased * TRIG_MAX_ANGLE * 5) / 1000);
    int32_t wiggle = (int32_t)(((int64_t)sin_lookup(wiggle_angle) * decay) / TRIG_MAX_RATIO / 12);
    eased += wiggle;
    int32_t delta = target_angle;
    if (delta > TRIG_MAX_ANGLE / 2) delta -= TRIG_MAX_ANGLE;
    int32_t angle = (int32_t)(((int64_t)delta * eased) / 1000);
    if (angle < 0) angle += TRIG_MAX_ANGLE;
    *out_angle = angle;
  }
}

static void startup_animation_stopped(Animation *animation, bool finished, void *context) {
  (void)animation;
  (void)context;
  if (finished) {
    s_startup_anim_elapsed_ms = STARTUP_CLOCK_ANIM_MS;
    s_startup_phase_eased_progress_1000 = 1000;
    s_startup_overall_eased_progress_1000 = 1000;
  }
  s_startup_clock_anim_active = false;
  Animation *completed = s_startup_animation;
  s_startup_animation = NULL;
  s_startup_phase_a = NULL;
  s_startup_phase_b = NULL;
  if (completed) animation_destroy(completed);
  if (s_invalidate_handler) s_invalidate_handler(s_invalidate_context);
  else if (s_hands_layer) layer_mark_dirty(s_hands_layer);
}

static void hands_controller_update_proc(Layer *layer, GContext *ctx) {
  
  
  
  
  
  GRect bounds = layer_get_unobstructed_bounds(layer);
  GPoint center = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + bounds.size.h / 2);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(s_data, now, &bg, &main_color, &accent_color);

  
  
  
  
  
  feature_controller_ensure_corner_custom_font(s_data->corner_font);

  int32_t hour_angle = (int32_t)(((int64_t)((t->tm_hour % 12) * 3600 + t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600));

  int32_t min_angle = ((t->tm_min * 60 + t->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);

  int32_t sec_angle;
  // Shake modes that request smooth seconds use the sub-second timestamp.
  
  
  
  
  
  
  if (input_shake_animation_active() && s_data->show_seconds && input_shake_animation_wants_smooth_second(s_data->shake_anim_mode)) {
    
    
    
    
    time_t smooth_now;
    uint16_t smooth_ms;
    time_ms(&smooth_now, &smooth_ms);
    struct tm *smooth_t = localtime(&smooth_now);
    sec_angle = (int32_t)((((int64_t)smooth_t->tm_sec * 1000 + smooth_ms) * TRIG_MAX_ANGLE) / 60000);
  } else {
    sec_angle = (t->tm_sec * TRIG_MAX_ANGLE) / 60;
  }

  
  
  
  
  uint16_t hour_length_scale_1000 = 1000, min_length_scale_1000 = 1000, sec_length_scale_1000 = 1000;
  // Startup animation can follow the background planet sweep for hour/minute hands.
  if (s_startup_clock_anim_active) {
    int32_t target_hour_angle = hour_angle, target_min_angle = min_angle, target_sec_angle = sec_angle;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    if (s_data->startup_clock_anim_mode == 2 && background_animation_is_active() && s_data->bg_anim_mode == 1) {
      int32_t eased = s_startup_overall_eased_progress_1000;
      time_t past = now - 120 * 60;
      time_t swept_now = past + (time_t)(((int64_t)(now - past) * eased) / 1000);
      struct tm *st = localtime(&swept_now);
      target_hour_angle = (int32_t)(((int64_t)((st->tm_hour % 12) * 3600 + st->tm_min * 60 + st->tm_sec) * TRIG_MAX_ANGLE) / (12 * 3600));
      target_min_angle = ((st->tm_min * 60 + st->tm_sec) * TRIG_MAX_ANGLE) / (60 * 60);
      // Keep seconds on real time; a two-hour sweep would spin them too fast.
      
      
      
      
      
      
      
      
      
    }
    compute_startup_hand_anim(target_hour_angle, s_startup_phase_eased_progress_1000, &hour_angle, &hour_length_scale_1000);
    compute_startup_hand_anim(target_min_angle, s_startup_phase_eased_progress_1000, &min_angle, &min_length_scale_1000);
    compute_startup_hand_anim(target_sec_angle, s_startup_phase_eased_progress_1000, &sec_angle, &sec_length_scale_1000);
  }

  
  
  
  
  
  
  
  HandConfig hour_cfg = s_data->hand_hour;
  HandConfig min_cfg = s_data->hand_minute;
  HandConfig sec_cfg = s_data->hand_second;

  // Hourly-vibration flash: for the flash's whole duration, one of
  // the hour/minute hands (see hourly_vibration.h's own comment on
  // which) swaps main/background color for one second out of every
  // two -- an inverted-colors blink rather than a font change, since
  // hands don't have one.
  bool flash_inverted = hourly_vibration_flash_is_inverted();
  bool flash_hour = flash_inverted && hourly_vibration_flash_targets_hour_hand();
  bool flash_min = flash_inverted && !hourly_vibration_flash_targets_hour_hand();
  GColor hour_main = flash_hour ? bg : main_color, hour_bg = flash_hour ? main_color : bg;
  GColor min_main = flash_min ? bg : main_color, min_bg = flash_min ? main_color : bg;

  hand_layer_draw(ctx, center, hour_angle, &hour_cfg, hour_main, accent_color, hour_bg, s_data->shadow_translucent, s_data->shadow_angle_deg, hour_length_scale_1000);
  hand_layer_draw(ctx, center, min_angle, &min_cfg, min_main, accent_color, min_bg, s_data->shadow_translucent, s_data->shadow_angle_deg, min_length_scale_1000);
  if (s_data->show_seconds) {
    hand_layer_draw(ctx, center, sec_angle, &sec_cfg, main_color, accent_color, bg, s_data->shadow_translucent, s_data->shadow_angle_deg, sec_length_scale_1000);
  }

  hand_layer_draw_center_circle(ctx, center, s_data->center_circle_radius, s_data->center_circle_color,
                                 main_color, accent_color, bg);
}

void hands_controller_init(EclipseData *data, HandsControllerInvalidateHandler handler, void *context) {
  s_data = data;
  s_invalidate_handler = handler;
  s_invalidate_context = context;
}

void hands_controller_deinit(void) {
  if (s_startup_animation) {
    Animation *animation = s_startup_animation;
    s_startup_animation = NULL;
    animation_unschedule(animation);
  }
  s_startup_phase_a = NULL;
  s_startup_phase_b = NULL;
  s_data = NULL;
  s_invalidate_handler = NULL;
  s_invalidate_context = NULL;
}

Layer *hands_controller_create_layer(GRect frame) {
  s_hands_layer = layer_create(frame);
  if (!s_hands_layer) return NULL;
  layer_set_update_proc(s_hands_layer, hands_controller_update_proc);
  return s_hands_layer;
}

void hands_controller_destroy_layer(Layer *layer) {
  if (layer) layer_destroy(layer);
  if (layer == s_hands_layer) s_hands_layer = NULL;
}

bool hands_controller_animation_active(void) {
  return s_startup_clock_anim_active;
}

int32_t hands_controller_animation_eased_progress_1000(void) {
  return s_startup_clock_anim_active ? s_startup_overall_eased_progress_1000 : 1000;
}

void hands_controller_start_startup_animation(void) {
  if (s_startup_clock_anim_played || !s_data || s_data->startup_clock_anim_mode == 0) return;
  if (eclipse_status_is_active(s_data, time(NULL))) return;
  s_startup_clock_anim_played = true;
  s_startup_clock_anim_active = true;
  s_startup_anim_elapsed_ms = 0;
  s_startup_phase_eased_progress_1000 = 0;
  s_startup_overall_eased_progress_1000 = 0;

  Animation *overall = animation_create();
  Animation *phase_a = animation_create();
  Animation *phase_b = animation_create();
  if (!overall || !phase_a || !phase_b) {
    if (overall) animation_destroy(overall);
    if (phase_a) animation_destroy(phase_a);
    if (phase_b) animation_destroy(phase_b);
    s_startup_clock_anim_active = false;
    return;
  }

  animation_set_duration(overall, STARTUP_CLOCK_ANIM_MS);
  animation_set_curve(overall, AnimationCurveEaseOut);
  animation_set_implementation(overall, &s_startup_overall_implementation);

  animation_set_duration(phase_a, STARTUP_ANIM_PHASE_A_MS);
  animation_set_curve(phase_a, AnimationCurveEaseOut);
  animation_set_implementation(phase_a, &s_startup_phase_implementation);

  animation_set_duration(phase_b, STARTUP_ANIM_PHASE_B_MS);
  animation_set_curve(phase_b, AnimationCurveEaseOut);
  animation_set_implementation(phase_b, &s_startup_phase_implementation);

  s_startup_phase_a = phase_a;
  s_startup_phase_b = phase_b;
  Animation *phase_sequence = animation_sequence_create(phase_a, phase_b, NULL);
  if (!phase_sequence) {
    s_startup_phase_a = NULL;
    s_startup_phase_b = NULL;
    animation_destroy(overall);
    animation_destroy(phase_a);
    animation_destroy(phase_b);
    s_startup_clock_anim_active = false;
    return;
  }

  Animation *spawn = animation_spawn_create(overall, phase_sequence, NULL);
  if (!spawn) {
    s_startup_phase_a = NULL;
    s_startup_phase_b = NULL;
    animation_destroy(overall);
    animation_destroy(phase_sequence);
    s_startup_clock_anim_active = false;
    return;
  }

  s_startup_animation = spawn;
  animation_set_handlers(spawn, (AnimationHandlers) {
    .started = NULL,
    .stopped = startup_animation_stopped
  }, NULL);

  if (!animation_schedule(spawn)) {
    Animation *failed = s_startup_animation;
    s_startup_animation = NULL;
    s_startup_phase_a = NULL;
    s_startup_phase_b = NULL;
    s_startup_clock_anim_active = false;
    animation_destroy(failed);
    return;
  }
}

