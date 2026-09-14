#pragma once

#include <pebble.h>
#include "eclipse_data.h"
#include "font_lookup.h"

// Renders configured numeric/Roman marker labels. Font resource lifetime
// remains owned by MarkerLayerState; this module only consumes the slot.
void marker_text_draw(GContext *ctx, GPoint center, GRect screen, FontSlot *font_slot,
                      const MarkerTextConfig *text_cfg, const MarkerRingConfig *hour_cfg,
                      const MarkerRingConfig *second_cfg, GColor color,
                      bool anim_active, int32_t anim_overall_progress_1000,
                      bool draw_debug);
