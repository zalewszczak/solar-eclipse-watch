#pragma once
#include <pebble.h>
#include "eclipse_data.h"

typedef struct {
  void (*wake)(void *context);
  void (*labels_changed)(bool visible, void *context);
  void (*animation_frame)(bool active, uint32_t elapsed_ms, bool planet_seek, void *context);
  void (*compass_feature_refresh)(void *context);
  void *context;
} InputCallbacks;

void input_init(EclipseData *data, InputCallbacks callbacks, void *context);
void input_deinit(void);
bool input_shake_animation_active(void);
bool input_shake_animation_wants_smooth_second(uint8_t mode);
int32_t input_planet_seek_heading_deg(void);
bool input_planet_seek_compass_low_accuracy(void);
int32_t input_compass_feature_heading_deg(void);
bool input_compass_feature_is_asleep(void);
