#include "./feature_values.h"
#include "./feature_slot.h"
#include "./feature_value_helpers.h"
#include "./feature_value_health.h"
#include "./feature_value_weather.h"
#include "./feature_value_time.h"
#include "./feature_value_composite.h"
#include "./feature_rules.h"
#include "../rendering/background/weather_layer.h"
#include <stdio.h>

// unified position resolution

void feature_values_compute_slot(FeatureSlot *slot, const EclipseData *data,
                                 GColor main_color, GColor accent_color, GColor bg_color,
                                 time_t now, struct tm *t) {
  if (slot->content == 0) {
    slot->segment_count = 0;
    return;
  }

  uint8_t content = slot->content, color_mode = slot->color_mode;
  switch (content) {
    case 100: case 101: case 103: case 104: case 105:
    case 112: case 113: case 118:
      feature_value_composite_compute(slot, content, data, color_mode, main_color, now);
      break;
    case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53:
    case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62:
      feature_value_timezone_compute(slot, content, color_mode, main_color, accent_color, now);
      break;
    case 1: case 2: case 3: case 10: case 17: case 39: case 40: case 41: case 42: case 43: case 78:
      feature_value_health_compute(slot, content, data, color_mode, main_color, accent_color);
      break;
    case 11: case 13: case 16: case 79: case 80: case 81: case 82: case 83: case 84: case 85:
      feature_value_sky_compute(slot, content, data, color_mode, main_color, accent_color, now);
      break;
    case 4: case 5: case 6: case 7: case 8: case 9: case 14: case 15: case 31: case 32: case 34:
    case 35: case 36: case 37: case 38: case 73: case 74: case 75: case 76: case 77: case 87: case 88:
    case 89: case 90: case 91: case 92: case 93: case 94: case 95: case 96: case 97: case 107:
      feature_value_weather_compute(slot, content, data, color_mode, main_color, accent_color, bg_color);
      break;
    default:
      feature_value_date_compute(slot, content, data, color_mode, main_color, accent_color, now, t);
      break;
  }

  // Weather-service errors override the cluster's normal value so the
  if (feature_rules_content_is_weather_derived(content) && weather_layer_should_show_error(data)) {
    char err_buf[10];
    snprintf(err_buf, sizeof(err_buf), "ERR %d", data->weather_error_code);
    slot->segment_count = 1;
    feature_value_set_text_segment(slot, 0, err_buf, GColorRed);
    slot->draw_pill = false;
  }
}
