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
