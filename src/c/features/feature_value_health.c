#include <pebble.h>
#include "./feature_value_health.h"
#include "./feature_value_helpers.h"
#include "./feature_slot.h"
#include "../data/eclipse_data.h"
#include "./feature_colors.h"
#include "./feature_rules.h"
#include "./feature_health.h"
#include "./feature_icons.h"
#include "../data/eclipse_ui.h"
#include "../rendering/background/background_layer.h"
#include "../rendering/background/sky_layer.h"
#include "../rendering/background/marker_layer.h"
#include "../rendering/background/celestial_layer.h"
#include "../rendering/background/weather_layer.h"
#include "../input/input.h"
#include "../fonts/font_lookup.h"
#include "./feature_timezone.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void __attribute__((noinline)) feature_value_health_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                  uint8_t color_mode, GColor main_color, GColor accent_color) {
  char buf[24];
  switch (content) {
    case 1: { // heart rate -- pink(low)->red->violet(dangerously high) gradient by actual BPM
      int bpm = feature_health_peek_current_bpm();
      GColor dyn = (bpm > 0) ? feature_colors_heart_rate_gradient(bpm) : GColorLightGray;
      snprintf(buf, sizeof(buf), bpm > 0 ? "%d" : "N/A", bpm);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 1, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      feature_value_set_text_segment(slot, 1, buf, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 2: { // steps today
      HealthValue steps = feature_health_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 100) pct = 100;
      GColor dyn = feature_colors_red_green_gradient((uint8_t)pct);
      snprintf(buf, sizeof(buf), "%d", (int)steps);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 2, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      feature_value_set_text_segment(slot, 1, buf, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 3: { // step goal %
      HealthValue steps = feature_health_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 999) pct = 999;
      GColor dyn = feature_colors_red_green_gradient((uint8_t)(pct > 100 ? 100 : pct));
      snprintf(buf, sizeof(buf), "%d%%", (int)pct);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 2, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      feature_value_set_text_segment(slot, 1, buf, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 10: { // battery %
      BatteryChargeState bs = battery_state_service_peek();
      GColor dyn = bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent);
      snprintf(buf, sizeof(buf), bs.is_charging ? "%d%%+" : "%d%%", bs.charge_percent);
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_icon_segment(slot, 0, 3, c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      // battery is the one icon that also always shows its own text (percentage)
      slot->segment_count = 2;
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 17: { // pebble battery logo -- icon draws its own fill bar, no separate text
      BatteryChargeState bs = battery_state_service_peek();
      GColor dyn = bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent);
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_icon_segment(slot, 0, 12, c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      return;
    }
    case 78: { // Bluetooth, icon only -- always its own dynamic color, ignores color_mode
      bool connected = connection_service_peek_pebble_app_connection();
      GColor c = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      slot->segment_count = 1;
      feature_value_set_icon_segment(slot, 0, 13, c);
      return;
    }
    case 39: case 40: { // sleep duration (total, 39) / restful (deep) sleep duration (40)
      HealthMetric metric = (content == 39) ? HealthMetricSleepSeconds : HealthMetricSleepRestfulSeconds;
      int32_t range_secs = (content == 39) ? 9 * 3600 : 3 * 3600;
      uint8_t icon_kind = (content == 39) ? 21 : 22;
      GColor c;
      if (feature_health_metric_available(metric)) {
        HealthValue secs = feature_health_sum_today(metric);
        feature_health_format_duration_hm(buf, sizeof(buf), (int32_t)secs);
        c = feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient_reversed((int32_t)secs, 0, range_secs), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      feature_value_slot_set(slot, icon_kind, buf, c);
      return;
    }
    case 41: { // sleep quality -- restful / total, as a percentage
      GColor c;
      if (feature_health_metric_available(HealthMetricSleepSeconds)) {
        HealthValue total = feature_health_sum_today(HealthMetricSleepSeconds);
        HealthValue restful = feature_health_sum_today(HealthMetricSleepRestfulSeconds);
        int pct = (total > 0) ? (int)((restful * 100) / total) : 0;
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        c = feature_value_resolve_flat_color(color_mode, feature_colors_red_green_gradient((uint8_t)pct), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      feature_value_slot_set(slot, 20, buf, c);
      return;
    }
    case 42: case 43: { // bed time (42) / wake time (43) -- day/night graded
      FeatureSleepSpan span = feature_health_get_sleep_span();
      time_t event = (content == 42) ? span.earliest_start : span.latest_end;
      uint8_t icon_kind = (content == 42) ? 18 : 19;
      GColor c;
      if (span.found) {
        struct tm *et = localtime(&event);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", et);
        c = feature_value_resolve_flat_color(color_mode, feature_colors_daynight_gradient(event, data->sun_rise, data->sun_set), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      feature_value_slot_set(slot, icon_kind, buf, c);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// weather cluster: temperature, conditions, UV, rain/wind/humidity,
