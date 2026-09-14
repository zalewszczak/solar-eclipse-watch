#include "feature_weather_icons.h"
#include "feature_icon_assets.h"

void feature_weather_icons_draw_hollow(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_SUN, color);
      return;
    case 1: // partly cloudy
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_PARTLY_CLOUDY, color);
      return;
    case 2: // cloudy / overcast
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_CLOUDY_OVERCAST, color);
      return;
    case 3: // fog
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_FOG, color);
      return;
    case 4: // rain
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_RAIN, color);
      return;
    case 5: // snow
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_SNOW, color);
      return;
    case 6: { // storm
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_STORM, color);
      return;
    }
  }
}



void feature_weather_icons_draw_simple(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_SUN, color);
      return;
    case 1: // partly cloudy
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_PARTLY_CLOUDY, color);
      return;
    case 2: // cloudy / overcast
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_CLOUDY_OVERCAST, color);
      return;
    case 3: // fog
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_FOG, color);
      return;
    case 4: // rain
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_RAIN, color);
      return;
    case 5: // snow
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_SNOW, color);
      return;
    case 6: { // storm
      feature_icon_assets_draw_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_STORM, color);
      return;
    }
  }
}



void feature_weather_icons_draw_filled(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  (void)color;
  switch (category) {
    case 0: // sunny
      feature_icon_assets_draw_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SUN);
      return;
    case 1: // partly cloudy
      feature_icon_assets_draw_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_PARTLY_CLOUDY);
      return;
    case 2: // cloudy / overcast
      feature_icon_assets_draw_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_CLOUDY_OVERCAST);
      return;
    case 3: // fog
      feature_icon_assets_draw_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_FOG);
      return;
    case 4: // rain
      feature_icon_assets_draw_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_RAIN);
      return;
    case 5: // snow
      feature_icon_assets_draw_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SNOW);
      return;
    case 6: { // storm
      feature_icon_assets_draw_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_STORM);
      return;
    }
  }
}



void feature_weather_icons_draw(GContext *ctx, GPoint top_left, uint8_t category, uint8_t style, GColor color) {
  switch (style) {
    case 0: feature_weather_icons_draw_simple(ctx, top_left, category, color); return;
    case 2: feature_weather_icons_draw_filled(ctx, top_left, category, color); return;
    case 1:
    default: feature_weather_icons_draw_hollow(ctx, top_left, category, color); return;
  }
}



