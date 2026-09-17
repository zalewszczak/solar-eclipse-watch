#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"

// The "Grid" layout: a 4x4 grid of single-character TextLayers (HH:MM,
// weekday, day-of-month + ordinal suffix, month name), built in a loop
// rather than 16 hand-declared layers -- see grid_display.c. Uses the
// SAME clock_font field as every other digital layout (a real text font,
// unlike Big Digital's 100+ bitmap-style reuse of that field), so
// grid_display_apply_font() works exactly like clock_display's own.
void grid_display_init(EclipseData *data);
void grid_display_deinit(void);

Layer *grid_display_panel_layer(void);
void grid_display_create_panel(Layer *parent, GRect frame);
void grid_display_destroy_panel(void);

// Recomputes all 16 characters and their color from the current time and
// color scheme -- call once at creation and again on every minute tick
// (there's no seconds row to need finer granularity).
void grid_display_refresh(void);
// (Re)resolves clock_font into a GFont and applies it to all 16 cells --
// call once at creation and again on COMMS_CHANGE_CLOCK_FONT.
void grid_display_apply_font(void);
