#pragma once

#include <pebble.h>
#include "../../data/eclipse_data.h"

// The "Big Digital" layout: 4 tall digit bitmaps (HH:MM, always
// zero-padded) plus a procedurally-drawn colon, in place of clock_display's
// text-rendered digital clock. Digit art comes from big_digital_lookup.h,
// selected via the SAME clock_font field digital top/bar use for their own
// font choice -- see big_digital_display.c for how a value means something
// different here. No font loading of any kind happens for this layout, so
// unlike clock_display there's no "apply font" step to call.
void big_digital_display_init(EclipseData *data);
void big_digital_display_deinit(void);

Layer *big_digital_display_panel_layer(void);
void big_digital_display_create_panel(Layer *parent, GRect frame);
void big_digital_display_destroy_panel(void);
void big_digital_display_mark_panel_dirty(void);
