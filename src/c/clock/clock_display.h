#pragma once

#include <pebble.h>
#include "../domain/eclipse_data.h"

void clock_display_init(EclipseData *data);
void clock_display_deinit(void);

Layer *clock_display_panel_layer(void);
Layer *clock_display_countdown_layer(void);

void clock_display_create_panel(Layer *parent, GRect frame);
void clock_display_destroy_panel(void);
void clock_display_create_countdown(Layer *parent, GRect frame);
void clock_display_destroy_countdown(void);
void clock_display_reparent_countdown(Layer *parent);

void clock_display_apply_font(void);
void clock_display_mark_panel_dirty(void);
void clock_display_mark_countdown_dirty(void);

void clock_display_set_countdown(const char *text, GColor color, bool hidden);
