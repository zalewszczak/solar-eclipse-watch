#include "./feature_weather_icons.h"
#include "./feature_icon_assets.h"

// One row per weather category (see feature_icons_weather_category()).
// This table -- not a switch/case per style -- is what
// feature_weather_icons_draw_with_outline() below loads from, so adding a
// category or a style never grows the code, only the data.
static const IconResourceSet WEATHER_ICON_SETS_DAY[7] = {
  [0] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_SUN, RESOURCE_ID_ICON_WEATHER_HOLLOW_SUN, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SUN },
  [1] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_PARTLY_CLOUDY, RESOURCE_ID_ICON_WEATHER_HOLLOW_PARTLY_CLOUDY, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_PARTLY_CLOUDY },
  [2] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_CLOUDY_OVERCAST, RESOURCE_ID_ICON_WEATHER_HOLLOW_CLOUDY_OVERCAST, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_CLOUDY_OVERCAST },
  [3] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_FOG, RESOURCE_ID_ICON_WEATHER_HOLLOW_FOG, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_FOG },
  [4] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_RAIN, RESOURCE_ID_ICON_WEATHER_HOLLOW_RAIN, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_RAIN },
  [5] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_SNOW, RESOURCE_ID_ICON_WEATHER_HOLLOW_SNOW, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SNOW },
  [6] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_STORM, RESOURCE_ID_ICON_WEATHER_HOLLOW_STORM, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_STORM },
};

// Night variants for the 2 categories that actually look different in the
// dark (clear sky -> moon, partly cloudy -> cloud + moon). Every other
// category reuses its own day set at night too (see the `category <= 1`
// check below) rather than duplicating rows here.
static const IconResourceSet WEATHER_ICON_SETS_NIGHT[2] = {
  [0] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_MOON, RESOURCE_ID_ICON_WEATHER_HOLLOW_MOON, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_MOON },
  [1] = { RESOURCE_ID_ICON_WEATHER_SIMPLE_PARTLY_CLOUDY_NIGHT, RESOURCE_ID_ICON_WEATHER_HOLLOW_PARTLY_CLOUDY_NIGHT, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_PARTLY_CLOUDY_NIGHT },
};

#define ICON_W 16
#define ICON_H 16

void feature_weather_icons_draw_with_outline(GContext *ctx, GPoint top_left, uint8_t category, bool is_night,
                                             uint8_t style, uint8_t outline_style, GColor outline_color, GColor color) {
  if (category >= 7) return;
  const IconResourceSet *set = (is_night && category <= 1) ? &WEATHER_ICON_SETS_NIGHT[category] : &WEATHER_ICON_SETS_DAY[category];
  feature_icon_assets_draw_styled_with_outline(ctx, top_left, set, style, outline_style, outline_color, color, ICON_W, ICON_H);
}
