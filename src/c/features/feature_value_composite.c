#include <pebble.h>
#include "./feature_value_composite.h"
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

void __attribute__((noinline)) feature_value_composite_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                 uint8_t color_mode, GColor main_color, time_t now) {
  bool dynamic = (color_mode == 3);
  GColor flat = main_color;
  char buf1[16], buf2[16];

  switch (content) {
    case 97: { // heart rate + steps
      int bpm = feature_health_peek_current_bpm();
      GColor hr_c = dynamic ? (bpm > 0 ? feature_colors_heart_rate_gradient(bpm) : GColorLightGray) : flat;
      snprintf(buf1, sizeof(buf1), bpm > 0 ? "%d" : "N/A", bpm);

      HealthValue steps = feature_health_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 100) pct = 100;
      GColor step_c = dynamic ? feature_colors_red_green_gradient((uint8_t)pct) : flat;
      snprintf(buf2, sizeof(buf2), "%d", (int)steps);

      slot->segment_count = 4;
      feature_value_set_icon_segment(slot, 0, 1, hr_c);
      feature_value_set_text_segment(slot, 1, buf1, hr_c);
      feature_value_set_icon_segment(slot, 2, 2, step_c);
      feature_value_set_text_segment(slot, 3, buf2, step_c);
      return;
    }
    case 98: { // bed time + wake time, day/night graded independently
      FeatureSleepSpan span = feature_health_get_sleep_span();
      GColor bed_c = flat, wake_c = flat;
      if (span.found) {
        struct tm *bt = localtime(&span.earliest_start);
        strftime(buf1, sizeof(buf1), clock_is_24h_style() ? "%H:%M" : "%I:%M", bt);
        struct tm *wt = localtime(&span.latest_end);
        char wake_time[8];
        strftime(wake_time, sizeof(wake_time), clock_is_24h_style() ? "%H:%M" : "%I:%M", wt);
        snprintf(buf2, sizeof(buf2), "/%s", wake_time);
        if (dynamic) {
          bed_c = feature_colors_daynight_gradient(span.earliest_start, data->sun_rise, data->sun_set);
          wake_c = feature_colors_daynight_gradient(span.latest_end, data->sun_rise, data->sun_set);
        }
      } else {
        snprintf(buf1, sizeof(buf1), "N/A");
        buf2[0] = '\0';
        if (dynamic) bed_c = wake_c = GColorLightGray;
      }
      slot->segment_count = 3;
      feature_value_set_icon_segment(slot, 0, 21, flat);
      feature_value_set_text_segment(slot, 1, buf1, bed_c);
      feature_value_set_text_segment(slot, 2, buf2, wake_c);
      return;
    }
    case 99: case 100: { // battery + BT (icons only, 99), battery % + BT (100)
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;

      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;

      if (content == 99) {
        slot->segment_count = 2;
        feature_value_set_icon_segment(slot, 1, 13, bt_c);
      } else {
        snprintf(buf1, sizeof(buf1), "%d%%", bs.charge_percent);
        slot->segment_count = 3;
        feature_value_set_text_segment(slot, 1, buf1, batt_c);
        feature_value_set_icon_segment(slot, 2, 13, bt_c);
      }
      return;
    }
    case 109: { // battery + BT + Quiet Time, icons only
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;

      slot->segment_count = 3;
      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      feature_value_set_icon_segment(slot, 1, 13, bt_c);
      feature_value_set_icon_segment(slot, 2, 29, quiet_c);
      slot->segments[2].icon_flag = quiet_active;
      return;
    }
    case 110: { // battery % + Quiet Time ON/OFF + BT ON/OFF
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;

      snprintf(buf1, sizeof(buf1), "%d%%", bs.charge_percent);
      slot->segment_count = 6;
      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      feature_value_set_text_segment(slot, 1, buf1, batt_c);
      feature_value_set_icon_segment(slot, 2, 29, quiet_c);
      slot->segments[2].icon_flag = false; // text carries the state here, not the icon shape
      feature_value_set_text_segment(slot, 3, quiet_active ? "ON" : "OFF", quiet_c);
      feature_value_set_icon_segment(slot, 4, 13, bt_c);
      feature_value_set_text_segment(slot, 5, connected ? "ON" : "OFF", bt_c);
      return;
    }
    case 111: { // battery + BT + Quiet Time + Hourly Vibrations, icons only
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool vibe_on = feature_rules_hourly_vibe_is_scheduled_now(data, now);
      GColor vibe_c = dynamic ? (vibe_on ? GColorGreen : GColorLightGray) : flat;

      slot->segment_count = 4;
      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      feature_value_set_icon_segment(slot, 1, 13, bt_c);
      feature_value_set_icon_segment(slot, 2, 29, quiet_c);
      slot->segments[2].icon_flag = quiet_active;
      feature_value_set_icon_segment(slot, 3, 30, vibe_c);
      slot->segments[3].icon_flag = !vibe_on;
      return;
    }
    case 115: { // battery (icon + %) + BT + Quiet Time + Hourly Vibrations (icons only) --
               // same as 111, except battery is the one icon that also shows its own
               // percentage text (matching how battery already behaves everywhere
               // else -- see compute_health_value's own case 10 comment); the other
               // 3 stay icon-only, same as 111.
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool vibe_on = feature_rules_hourly_vibe_is_scheduled_now(data, now);
      GColor vibe_c = dynamic ? (vibe_on ? GColorGreen : GColorLightGray) : flat;

      snprintf(buf1, sizeof(buf1), "%d%%", bs.charge_percent);
      slot->segment_count = 5;
      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      feature_value_set_text_segment(slot, 1, buf1, batt_c);
      feature_value_set_icon_segment(slot, 2, 13, bt_c);
      feature_value_set_icon_segment(slot, 3, 29, quiet_c);
      slot->segments[3].icon_flag = quiet_active;
      feature_value_set_icon_segment(slot, 4, 30, vibe_c);
      slot->segments[4].icon_flag = !vibe_on;
      return;
    }
    case 112: { // Quiet Time + Hourly Vibrations, icons only
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool vibe_on = feature_rules_hourly_vibe_is_scheduled_now(data, now);
      GColor vibe_c = dynamic ? (vibe_on ? GColorGreen : GColorLightGray) : flat;

      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 29, quiet_c);
      slot->segments[0].icon_flag = quiet_active;
      feature_value_set_icon_segment(slot, 1, 30, vibe_c);
      slot->segments[1].icon_flag = !vibe_on;
      return;
    }
    case 113: { // Quiet Time + Hourly Vibrations, icons + "ON"/"OFF" texts
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool vibe_on = feature_rules_hourly_vibe_is_scheduled_now(data, now);
      GColor vibe_c = dynamic ? (vibe_on ? GColorGreen : GColorLightGray) : flat;

      slot->segment_count = 4;
      feature_value_set_icon_segment(slot, 0, 29, quiet_c);
      slot->segments[0].icon_flag = false; // text carries the state here, not the icon shape
      feature_value_set_text_segment(slot, 1, quiet_active ? "ON" : "OFF", quiet_c);
      feature_value_set_icon_segment(slot, 2, 30, vibe_c);
      slot->segments[2].icon_flag = false;
      feature_value_set_text_segment(slot, 3, vibe_on ? "ON" : "OFF", vibe_c);
      return;
    }
    case 114: { // battery % + Bluetooth + Quiet Time + Hourly Vibrations, icon + "ON"/"OFF" (or %) each
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool vibe_on = feature_rules_hourly_vibe_is_scheduled_now(data, now);
      GColor vibe_c = dynamic ? (vibe_on ? GColorGreen : GColorLightGray) : flat;

      snprintf(buf1, sizeof(buf1), "%d%%", bs.charge_percent);
      slot->segment_count = 8;
      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      feature_value_set_text_segment(slot, 1, buf1, batt_c);
      feature_value_set_icon_segment(slot, 2, 13, bt_c);
      feature_value_set_text_segment(slot, 3, connected ? "ON" : "OFF", bt_c);
      feature_value_set_icon_segment(slot, 4, 29, quiet_c);
      slot->segments[4].icon_flag = false; // text carries the state here, not the icon shape
      feature_value_set_text_segment(slot, 5, quiet_active ? "ON" : "OFF", quiet_c);
      feature_value_set_icon_segment(slot, 6, 30, vibe_c);
      slot->segments[6].icon_flag = false;
      feature_value_set_text_segment(slot, 7, vibe_on ? "ON" : "OFF", vibe_c);
      return;
    }
    case 101: { // sleep times: sleep icon, total duration, (restful duration), quality%
      if (feature_health_metric_available(HealthMetricSleepSeconds)) {
        HealthValue total = feature_health_sum_today(HealthMetricSleepSeconds);
        HealthValue restful = feature_health_sum_today(HealthMetricSleepRestfulSeconds);
        char total_buf[12], restful_buf[12], quality_buf[6];
        feature_health_format_duration_hm(total_buf, sizeof(total_buf), (int32_t)total);
        feature_health_format_duration_hm(restful_buf, sizeof(restful_buf), (int32_t)restful);
        int pct = (total > 0) ? (int)((restful * 100) / total) : 0;
        if (pct > 100) pct = 100;
        snprintf(quality_buf, sizeof(quality_buf), "%d%%", pct);
        char restful_paren[14];
        snprintf(restful_paren, sizeof(restful_paren), "(%s)", restful_buf);

        GColor total_c = dynamic ? feature_colors_seven_stop_gradient_reversed((int32_t)total, 0, 9 * 3600) : flat;
        GColor restful_c = dynamic ? feature_colors_seven_stop_gradient_reversed((int32_t)restful, 0, 3 * 3600) : flat;
        GColor quality_c = dynamic ? feature_colors_red_green_gradient((uint8_t)pct) : flat;

        slot->segment_count = 4;
        feature_value_set_icon_segment(slot, 0, 21, flat);
        feature_value_set_text_segment(slot, 1, total_buf, total_c);
        feature_value_set_text_segment(slot, 2, restful_paren, restful_c);
        feature_value_set_text_segment(slot, 3, quality_buf, quality_c);
      } else {
        slot->segment_count = 2;
        feature_value_set_icon_segment(slot, 0, 21, flat);
        feature_value_set_text_segment(slot, 1, "N/A", dynamic ? GColorLightGray : flat);
      }
      return;
    }
    case 102: { // "long date" + sunset/sunrise, e.g. "Mon 23 Sep <icon> 19:45"
      struct tm *t = localtime(&now);
      char day_buf[4], mon_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      snprintf(buf1, sizeof(buf1), "%s %d %s", day_buf, t->tm_mday, mon_buf);
      GColor date_c = dynamic ? feature_colors_date_year_progress_gradient(t) : flat;

      bool is_sunrise = false;
      time_t sun_event_time = 0;
      char time_buf[8];
      if (eclipse_ui_get_next_sun_event(now, data->sun_rise, data->sun_set, data->sun_rise_tomorrow, &sun_event_time, &is_sunrise)) {
        struct tm *et = localtime(&sun_event_time);
        strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", et);
      } else {
        snprintf(time_buf, sizeof(time_buf), "N/A");
      }

      slot->segment_count = 3;
      feature_value_set_text_segment(slot, 0, buf1, date_c);
      feature_value_set_icon_segment(slot, 1, 11, flat);
      slot->segments[1].icon_flag = is_sunrise;
      feature_value_set_text_segment(slot, 2, time_buf, flat);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}
