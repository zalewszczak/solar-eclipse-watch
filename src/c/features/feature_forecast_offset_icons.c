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
// Unlike every other icon table in this app, there's only ONE piece of art
// per index here, not a simple/hollow/fullcolor triple -- these are plain
// digit glyphs with nothing for "hollow" or "full color" to add, so the
// same resource is reused for all three IconResourceSet fields rather than
// tripling the number of image files for no visual benefit. The outline
// pass and simple/hollow tinting in feature_icon_assets_draw_styled_with_outline()
// still apply on top of that one piece of art, same as any other icon.
//
// Index 0-5 = "+1h" through "+6h" (features_layer.c content ids 87-92),
// 6-8 = "+1 day" through "+3 days" (content ids 93-95) -- both computed as
// content-87 by feature_value_weather.c, the same index forecast_temp_c/
// forecast_condition in eclipse_data.h already use for this same data.
#define FORECAST_ICON(n) { RESOURCE_ID_ICON_FORECAST_##n, RESOURCE_ID_ICON_FORECAST_##n, RESOURCE_ID_ICON_FORECAST_##n }
static const IconResourceSet FORECAST_OFFSET_ICON_SETS[9] = {
  FORECAST_ICON(0), FORECAST_ICON(1), FORECAST_ICON(2),
  FORECAST_ICON(3), FORECAST_ICON(4), FORECAST_ICON(5),
  FORECAST_ICON(6), FORECAST_ICON(7), FORECAST_ICON(8),
};
#undef FORECAST_ICON

#define ICON_W 16
#define ICON_H 16

void feature_forecast_offset_icons_draw_with_outline(GContext *ctx, GPoint top_left, uint8_t idx,
                                                      uint8_t style, uint8_t outline_style,
                                                      GColor outline_color, GColor color) {
  if (idx > 8) return;
  feature_icon_assets_draw_styled_with_outline(ctx, top_left, &FORECAST_OFFSET_ICON_SETS[idx], style,
                                               outline_style, outline_color, color, ICON_W, ICON_H);
}
