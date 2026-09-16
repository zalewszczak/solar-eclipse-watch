#include "./feature_forecast_offset_icons.h"
#include "./feature_icon_assets.h"

// Each icon here is a full 16x16 canvas like every other icon in this app
// (so this reuses feature_icon_assets_draw_styled_with_outline() completely
// unchanged -- no separate narrow-icon code path), but only the LEFT 8px
// actually has content ("1".."6" or "1d".."3d"); the right half is blank/
// transparent. That's what lets feature_icons.c's own width table give
// this icon kind a layout width of roughly half a normal icon (see
// s_icon_plus_gap_width there) so the weather icon drawn right after it
// sits snug against the populated half instead of leaving a gap sized for
// a full icon -- the whole point of drawing it "oversized" like this
// rather than adding new fixed-width-icon drawing code.
//
// Index 0 is unused; 1-6 = "+1h" through "+6h" (features_layer.c content
// ids 87-92), 7-9 = "+1 day" through "+3 days" (content ids 93-95) -- both
// computed as content-86 by feature_value_weather.c, so one table and one
// draw function covers both.
static const IconResourceSet FORECAST_OFFSET_ICON_SETS[10] = {
  [1] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_HOUR_1, RESOURCE_ID_ICON_HOLLOW_FORECAST_HOUR_1, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_HOUR_1 },
  [2] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_HOUR_2, RESOURCE_ID_ICON_HOLLOW_FORECAST_HOUR_2, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_HOUR_2 },
  [3] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_HOUR_3, RESOURCE_ID_ICON_HOLLOW_FORECAST_HOUR_3, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_HOUR_3 },
  [4] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_HOUR_4, RESOURCE_ID_ICON_HOLLOW_FORECAST_HOUR_4, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_HOUR_4 },
  [5] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_HOUR_5, RESOURCE_ID_ICON_HOLLOW_FORECAST_HOUR_5, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_HOUR_5 },
  [6] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_HOUR_6, RESOURCE_ID_ICON_HOLLOW_FORECAST_HOUR_6, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_HOUR_6 },
  [7] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_DAY_1, RESOURCE_ID_ICON_HOLLOW_FORECAST_DAY_1, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_DAY_1 },
  [8] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_DAY_2, RESOURCE_ID_ICON_HOLLOW_FORECAST_DAY_2, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_DAY_2 },
  [9] = { RESOURCE_ID_ICON_SIMPLE_FORECAST_DAY_3, RESOURCE_ID_ICON_HOLLOW_FORECAST_DAY_3, RESOURCE_ID_ICON_FULLCOLOR_FORECAST_DAY_3 },
};

#define ICON_W 16
#define ICON_H 16

void feature_forecast_offset_icons_draw_with_outline(GContext *ctx, GPoint top_left, uint8_t offset,
                                                      uint8_t style, uint8_t outline_style,
                                                      GColor outline_color, GColor color) {
  if (offset < 1 || offset > 9) return;
  feature_icon_assets_draw_styled_with_outline(ctx, top_left, &FORECAST_OFFSET_ICON_SETS[offset], style,
                                               outline_style, outline_color, color, ICON_W, ICON_H);
}
