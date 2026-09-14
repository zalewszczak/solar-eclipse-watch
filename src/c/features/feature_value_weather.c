#include <pebble.h>
#include "feature_value_weather.h"
#include "feature_value_helpers.h"
#include "feature_slot.h"
#include "../domain/eclipse_data.h"
#include "feature_colors.h"
#include "feature_rules.h"
#include "feature_health.h"
#include "feature_icons.h"
#include "../domain/eclipse_ui.h"
#include "../background/background_layer.h"
#include "../background/sky_layer.h"
#include "../background/marker_layer.h"
#include "../background/celestial_layer.h"
#include "../background/weather_layer.h"
#include "../application/input.h"
#include "../services/font_lookup.h"
#include "feature_timezone.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void __attribute__((noinline)) feature_value_weather_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                   uint8_t color_mode, GColor main_color, GColor accent_color, GColor bg_color) {
  char buf[24];
  GColor cond_color = data->valid
    ? feature_colors_weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct)
    : bg_color;

  switch (content) {
    case 4: { // high/low temperature
      int16_t hi = feature_rules_convert_temp(data->temp_high_c, data->temp_unit);
      int16_t lo = feature_rules_convert_temp(data->temp_low_c, data->temp_unit);
      if (color_mode == 3) {
        // "color" mode splits high and low into their own independently
        // gradient-colored segments instead of sharing one flat color.
        char hi_buf[8], lo_buf[8];
        snprintf(hi_buf, sizeof(hi_buf), "H%d", hi);
        snprintf(lo_buf, sizeof(lo_buf), "L%d", lo);
        slot->segment_count = 2;
        feature_value_set_text_segment(slot, 0, hi_buf, feature_colors_seven_stop_gradient(data->temp_high_c, -10, 40));
        feature_value_set_text_segment(slot, 1, lo_buf, feature_colors_seven_stop_gradient(data->temp_low_c, -10, 40));
      } else {
        snprintf(buf, sizeof(buf), "H%d L%d", hi, lo);
        slot->segment_count = 1;
        feature_value_set_text_segment(slot, 0, buf, feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color));
      }
      return;
    }
    case 5: { // current conditions
      int16_t temp = feature_rules_convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d %s", temp,
               weather_layer_short_condition_text(data->weather_condition, data->cloud_cover_pct));
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, feature_value_resolve_flat_color(color_mode, cond_color, main_color, accent_color));
      return;
    }
    case 6: { // UV index (today's daily max -- see 104 for the current-hour value)
      uint8_t uv = data->uv_index_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(uv, 1, 13), main_color, accent_color));
      return;
    }
    case 104: { // current UV index (this hour, as opposed to 6's daily max)
      uint8_t uv = data->uv_index_current_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(uv, 1, 13), main_color, accent_color));
      return;
    }
    case 7: { // rain chance
      snprintf(buf, sizeof(buf), "%d%%", data->rain_chance_pct);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_white_to_turquoise_gradient(data->rain_chance_pct, 0, 100), main_color, accent_color);
      feature_value_slot_set(slot, 5, buf, c);
      return;
    }
    case 8: { // humidity
      snprintf(buf, sizeof(buf), "%d%%", data->humidity_pct);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_white_to_turquoise_gradient(data->humidity_pct, 0, 100), main_color, accent_color);
      feature_value_slot_set(slot, 6, buf, c);
      return;
    }
    case 9: { // wind speed
      snprintf(buf, sizeof(buf), "%d", feature_rules_convert_wind(data->wind_speed_kmh, data->wind_speed_unit));
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_white_to_turquoise_gradient(data->wind_speed_kmh, 0, 60), main_color, accent_color);
      feature_value_slot_set(slot, 7, buf, c);
      return;
    }
    case 14: { // visibility -- grayscale, like cloud cover (vis_score_pct is sent as 100-cloud%)
      snprintf(buf, sizeof(buf), "%d%%", data->vis_score_pct);
      uint8_t equiv_cloud_pct = 100 - data->vis_score_pct;
      GColor dyn = feature_colors_overcast_gray_gradient(equiv_cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : equiv_cloud_pct);
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      feature_value_slot_set(slot, 9, buf, c);
      return;
    }
    case 15: { // cloud cover
      snprintf(buf, sizeof(buf), "%d%%", data->cloud_cover_pct);
      GColor dyn = feature_colors_overcast_gray_gradient(data->cloud_cover_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : data->cloud_cover_pct);
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      feature_value_slot_set(slot, 10, buf, c);
      return;
    }
    case 31: { // weather icon only, no text
      GColor c = feature_value_resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_icon_segment(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
      return;
    }
    case 32: { // temp + weather icon -- condition-based color, same as 5/31
      int16_t temp = feature_rules_convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d", temp);
      GColor c = feature_value_resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 34: { // pressure, with rising/falling/flat trend arrow
      snprintf(buf, sizeof(buf), "%d hPa", data->pressure_hpa);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(data->pressure_hpa, 970, 1050), main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 15, c);
      slot->segments[0].icon_extra = data->pressure_trend;
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 35: { // wind direction, with a rotated compass arrow
      static const char *COMPASS_DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
      int idx = ((data->wind_dir_deg + 22) / 45) % 8;
      if (idx < 0) idx += 8;
      snprintf(buf, sizeof(buf), "%s", COMPASS_DIRS[idx]);
      GColor c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 16, c);
      slot->segments[0].icon_extra = data->wind_dir_deg;
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 36: { // air quality -- shared 7-stop gradient, remapped per scale
      bool use_eu = (data->aqi_unit == 1);
      uint16_t aqi_value = use_eu ? data->aqi_eu : data->aqi_us;
      snprintf(buf, sizeof(buf), "AQI %d", aqi_value);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(aqi_value, 0, use_eu ? 100 : 300), main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, c);
      return;
    }
    case 37: { // dew point -- reuses the humidity feature's droplet icon
      int16_t dew = feature_rules_convert_temp(data->dew_point_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d", dew);
      GColor c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      feature_value_slot_set(slot, 6, buf, c);
      return;
    }
    case 38: { // altitude -- white(sea level)->turquoise(high) gradient
      GColor c;
      if (data->altitude_m <= -32000) { // sentinel: not available
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      } else {
        if (data->altitude_unit == 1) { // feet
          int32_t feet = ((int32_t)data->altitude_m * 328) / 100; // *3.28084, integer approximation
          snprintf(buf, sizeof(buf), "%ldft", (long)feet);
        } else {
          snprintf(buf, sizeof(buf), "%dm", data->altitude_m);
        }
        c = feature_value_resolve_flat_color(color_mode, feature_colors_altitude_gradient(data->altitude_m), main_color, accent_color);
      }
      feature_value_slot_set(slot, 17, buf, c);
      return;
    }
    case 73: case 74: case 75: case 77: { // current/high/low/feels-like temp only -- all 7-stop, -10..40C
      int16_t temp_c, shown;
      const char *prefix = "";
      if (content == 73) { temp_c = data->weather_temp_c; shown = feature_rules_convert_temp(temp_c, data->temp_unit); }
      else if (content == 74) { temp_c = data->temp_high_c; shown = feature_rules_convert_temp(temp_c, data->temp_unit); prefix = "H "; }
      else if (content == 75) { temp_c = data->temp_low_c; shown = feature_rules_convert_temp(temp_c, data->temp_unit); prefix = "L "; }
      else { temp_c = feature_rules_apparent_temp_c(data->weather_temp_c, data->wind_speed_kmh, data->humidity_pct); shown = feature_rules_convert_temp(temp_c, data->temp_unit); prefix = "FL "; }
      snprintf(buf, sizeof(buf), "%s%d", prefix, shown);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(temp_c, -10, 40), main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, c);
      return;
    }
    case 76: { // weather icon + current/high/low all in one line -- "mixed" value, condition-based color
      int16_t cur = feature_rules_convert_temp(data->weather_temp_c, data->temp_unit);
      int16_t hi = feature_rules_convert_temp(data->temp_high_c, data->temp_unit);
      int16_t lo = feature_rules_convert_temp(data->temp_low_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d H%d L%d", cur, hi, lo);
      GColor c = feature_value_resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 87: case 88: case 89: case 90: case 91: case 92: { // weather in 1-6 hours, e.g. "+3h <icon> 28C"
      int hrs_ahead = content - 86; // 1-6
      int idx = hrs_ahead - 1;
      GColor c;
      if (data->forecast_temp_c[idx] <= -128) {
        snprintf(buf, sizeof(buf), "+%dh N/A", hrs_ahead);
        c = GColorLightGray;
        slot->segment_count = 1;
        feature_value_set_text_segment(slot, 0, buf, c);
        return;
      }
      int16_t shown = feature_rules_convert_temp(data->forecast_temp_c[idx], data->temp_unit);
      snprintf(buf, sizeof(buf), "+%dh %d", hrs_ahead, shown);
      // Same shape as "temp + weather icon" (32): plain 7-stop gradient,
      // not the condition-based color -- per the "Temperature readouts
      // (including temp+weather icon)" rule.
      c = feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(data->forecast_temp_c[idx], -10, 40), main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->forecast_condition[idx], 50); // no forecast cloud% sent separately -- 50 is a neutral middle guess, only affects which of a few near-identical icon glyphs gets picked
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 93: case 94: { // last weather update time, long (93, "Last updated 12:34") / short (94, "12:34")
      GColor dyn;
      time_t now = time(NULL);
      if (data->weather_last_update > 0) {
        struct tm *ut = localtime(&data->weather_last_update);
        char time_buf[8];
        strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", ut);
        if (content == 93) snprintf(buf, sizeof(buf), "Last updated %s", time_buf);
        else snprintf(buf, sizeof(buf), "%s", time_buf);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
      }
      dyn = feature_colors_weather_staleness_gradient(now, data->weather_last_update);
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      if (content == 94) {
        // Short version only -- the long version's own "Last updated"
        // text already says what this is, so the icon would be
        // redundant there; the short version is just a bare time, so
        // a small two-arrows-chasing "refresh" glyph (icon_kind 28) is
        // what tells you what that time actually means at a glance.
        slot->segment_count = 2;
        feature_value_set_icon_segment(slot, 0, 28, c);
        feature_value_set_text_segment(slot, 1, buf, c);
      } else {
        slot->segment_count = 1;
        feature_value_set_text_segment(slot, 0, buf, c);
      }
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}
