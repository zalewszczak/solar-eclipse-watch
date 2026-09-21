#pragma once

// Phase 6: the universal renderer. draw_feature() performs a complete
// outline pass across the feature's primitives before the content pass
// (Section 13), and otherwise "should not parse feature semantics, convert
// units, calculate weather values, determine icon meanings... construct
// strings" (Section 12) -- every value here is already resolved.
//
// Known scope limit (documented rather than silently dropped -- see
// IMPLEMENTATION_NOTES.md): icon primitives still draw their outline and
// fill in one combined call during the content pass, because the underlying
// per-icon-kind drawing (feature_icons_draw_render_icon and the styled/
// weather/forecast-offset icon helpers it calls into) do not yet expose an
// outline-only entry point the way text now does via
// feature_render_draw_text_outline_only/_content_only. Splitting every icon
// kind's drawing routine the same way text's was split is real remaining
// work, not done here since it touches several other rendering files
// (feature_icon_assets.c, feature_weather_icons.c,
// feature_forecast_offset_icons.c) beyond the corner/middle-slot scope of
// this pass. Because primitives within one feature never overlap on screen,
// this ordering choice does not change the drawn pixels versus a fully
// split implementation -- only the code's pass structure differs.

#include <pebble.h>
#include "./primitive_storage.h"

typedef struct FeatureSlot FeatureSlot; // feature_slot.h

void primitive_renderer_draw_feature(GContext *ctx, int16_t box_x, int16_t box_y, int16_t row_height,
                                     const PrimitiveFeature *feature, const FeatureSlot *slot,
                                     GFont font, int16_t font_h, int16_t font_offset,
                                     uint8_t outline_style, uint8_t icon_style, bool draw_debug);
