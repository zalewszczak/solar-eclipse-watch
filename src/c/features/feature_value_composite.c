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

typedef struct {
  BatteryChargeState battery;
  bool connected;
  GColor battery_color;
  GColor bluetooth_color;
} CompositeBatteryStatus;

static void composite_battery_status_get(CompositeBatteryStatus *status,
                                         bool dynamic, GColor flat) {
  status->battery = battery_state_service_peek();
  status->connected = connection_service_peek_pebble_app_connection();
  status->battery_color = dynamic
    ? (status->battery.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)status->battery.charge_percent))
    : flat;
  status->bluetooth_color = dynamic
    ? (status->connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0))
    : flat;
}

static void composite_quiet_get(bool dynamic, GColor flat,
                                bool *quiet_active, GColor *quiet_color) {
  *quiet_active = quiet_time_is_active();
  *quiet_color = dynamic ? (*quiet_active ? GColorRed : GColorWhite) : flat;
}

void __attribute__((noinline)) feature_value_composite_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                 uint8_t color_mode, GColor main_color, time_t now) {
  bool dynamic = (color_mode == 3);
  GColor flat = main_color;
  char buf1[16], buf2[16];

  switch (content) {
    case 100: { // heart rate + steps
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
    case 101: { // bed time + wake time, day/night graded independently
      FeatureSleepSpan span = feature_health_get_sleep_span();
      GColor bed_c = flat, wake_c = flat;
      if (span.found) {
        struct tm *bt = localtime(&span.earliest_start);
        strftime(buf1, sizeof(buf1), clock_is_24h_style() ? "%H:%M" : "%I:%M", bt);
        struct tm *wt = localtime(&span.latest_end);
        strftime(buf2, sizeof(buf2), clock_is_24h_style() ? "%H:%M" : "%I:%M", wt);
        if (dynamic) {
          bed_c = feature_colors_daynight_gradient(span.earliest_start, data->sun_rise, data->sun_set);
          wake_c = feature_colors_daynight_gradient(span.latest_end, data->sun_rise, data->sun_set);
        }
      } else {
        snprintf(buf1, sizeof(buf1), "N/A");
        buf2[0] = '\0';
        if (dynamic) bed_c = wake_c = GColorLightGray;
      }
      // Atomic decomposition (Phase 8): the wake-time half is now SLASH +
      // WAKE_TIME_TEXT (reusing content 43's own wake-time primitive)
      // instead of one composed "/22:10" primitive. When there's no sleep
      // data, buf2 is empty and the slash+wake segments are simply omitted
      // -- same "N/A" bed-time-only display as before.
      if (buf2[0] != '\0') {
        slot->segment_count = 4;
        feature_value_set_icon_segment(slot, 0, 21, flat);
        feature_value_set_text_segment(slot, 1, buf1, bed_c);
        feature_value_set_text_segment(slot, 2, "/", wake_c);
        feature_value_set_text_segment(slot, 3, buf2, wake_c);
      } else {
        slot->segment_count = 2;
        feature_value_set_icon_segment(slot, 0, 21, flat);
        feature_value_set_text_segment(slot, 1, buf1, bed_c);
      }
      return;
    }
    case 102: case 103: { // battery + BT (icons only, 102), battery % + BT (103)
      CompositeBatteryStatus status;
      composite_battery_status_get(&status, dynamic, flat);
      BatteryChargeState bs = status.battery;
      GColor batt_c = status.battery_color;
      GColor bt_c = status.bluetooth_color;

      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;

      if (content == 102) {
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
    case 112: { // battery + BT + Quiet Time, icons only
      CompositeBatteryStatus status;
      composite_battery_status_get(&status, dynamic, flat);
      BatteryChargeState bs = status.battery;
      GColor batt_c = status.battery_color;
      GColor bt_c = status.bluetooth_color;
      bool quiet_active;
      GColor quiet_c;
      composite_quiet_get(dynamic, flat, &quiet_active, &quiet_c);

      slot->segment_count = 3;
      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      feature_value_set_icon_segment(slot, 1, 13, bt_c);
      feature_value_set_icon_segment(slot, 2, 29, quiet_c);
      slot->segments[2].icon_flag = quiet_active;
      return;
    }
    case 113: { // battery % + Quiet Time ON/OFF + BT ON/OFF
      CompositeBatteryStatus status;
      composite_battery_status_get(&status, dynamic, flat);
      BatteryChargeState bs = status.battery;
      bool quiet_active;
      GColor quiet_c;
      composite_quiet_get(dynamic, flat, &quiet_active, &quiet_c);
      bool connected = status.connected;
      GColor batt_c = status.battery_color;
      GColor bt_c = status.bluetooth_color;

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
    case 118: { // battery (icon + %) + BT + Quiet Time, icons only for BT/Quiet --
               // battery is the one icon that also shows its own text (percentage)
      CompositeBatteryStatus status;
      composite_battery_status_get(&status, dynamic, flat);
      BatteryChargeState bs = status.battery;
      GColor batt_c = status.battery_color;
      GColor bt_c = status.bluetooth_color;
      bool quiet_active;
      GColor quiet_c;
      composite_quiet_get(dynamic, flat, &quiet_active, &quiet_c);

      snprintf(buf1, sizeof(buf1), "%d%%", bs.charge_percent);
      slot->segment_count = 4;
      feature_value_set_icon_segment(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      feature_value_set_text_segment(slot, 1, buf1, batt_c);
      feature_value_set_icon_segment(slot, 2, 13, bt_c);
      feature_value_set_icon_segment(slot, 3, 29, quiet_c);
      slot->segments[3].icon_flag = quiet_active;
      return;
    }
    case 104: { // sleep times: sleep icon, total duration, (restful duration), quality%
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
    case 105: { // "long date" + sunset/sunrise, e.g. "Mon 23 Sep <icon> 19:45"
      struct tm *t = localtime(&now);
      char day_buf[4], daynum_buf[4], mon_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      snprintf(daynum_buf, sizeof(daynum_buf), "%d", t->tm_mday);
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
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

      // Atomic decomposition (Phase 8): the date half is now 5 primitives
      // (weekday, space, day, space, month) instead of one composed
      // "Mon 23 Sep" string; 5 + icon + time = 7 segments total.
      slot->segment_count = 7;
      feature_value_set_text_segment(slot, 0, day_buf, date_c);
      feature_value_set_text_segment(slot, 1, " ", date_c);
      feature_value_set_text_segment(slot, 2, daynum_buf, date_c);
      feature_value_set_text_segment(slot, 3, " ", date_c);
      feature_value_set_text_segment(slot, 4, mon_buf, date_c);
      feature_value_set_icon_segment(slot, 5, 11, flat);
      slot->segments[5].icon_flag = is_sunrise;
      feature_value_set_text_segment(slot, 6, time_buf, flat);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}
