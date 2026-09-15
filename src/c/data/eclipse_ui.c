#include <pebble.h>
#include "./eclipse_ui.h"
#include "../rendering/background/sky_layer.h"

static GColor eclipse_ui_color_from_packed(uint8_t packed) {
  GColor color;
  color.argb = packed;
  return color;
}

void eclipse_ui_get_active_color_scheme(const EclipseData *data, time_t now,
                                        GColor *background, GColor *text, GColor *accent) {
  const bool night = data->night_scheme_enabled && !sky_layer_is_bright(data, now);
  if (night) {
    *background = eclipse_ui_color_from_packed(data->night_custom_bg);
    *text = eclipse_ui_color_from_packed(data->night_custom_text);
    *accent = eclipse_ui_color_from_packed(data->night_custom_accent);
  } else {
    *background = eclipse_ui_color_from_packed(data->custom_bg);
    *text = eclipse_ui_color_from_packed(data->custom_text);
    *accent = eclipse_ui_color_from_packed(data->custom_accent);
  }
}

bool eclipse_ui_get_next_sun_event(time_t now, time_t sunrise, time_t sunset,
                                    time_t sunrise_tomorrow, time_t *event_time,
                                    bool *is_sunrise) {
  if (sunrise != 0 && now < sunrise) {
    *event_time = sunrise;
    *is_sunrise = true;
    return true;
  }
  if (sunset != 0 && now < sunset) {
    *event_time = sunset;
    *is_sunrise = false;
    return true;
  }
  if (sunrise_tomorrow != 0) {
    *event_time = sunrise_tomorrow;
    *is_sunrise = true;
    return true;
  }
  return false;
}
