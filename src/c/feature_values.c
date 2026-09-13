#include "feature_values.h"
#include "feature_slot.h"
#include "feature_icons.h"
#include "eclipse_ui.h"
#include "background_layer.h"
#include "sky_layer.h"
#include "marker_layer.h"
#include "celestial_layer.h"
#include "weather_layer.h"
#include "input.h"
#include "font_lookup.h"
#include "feature_timezone.h"
#include "feature_colors.h"
#include "feature_health.h"
#include "feature_rules.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Feature value resolution
// ---------------------------------------------------------------------------
// This module turns a settings-resolved FeatureSlot into its current
// icon/text/color representation. It deliberately owns content-specific
// formatting and service reads; layout measurement and drawing remain in
// features_layer.c.
// ---- content classification -------------------------------------------

// Resolves the shared "one flat color" every content type not doing
// its own per-segment gradient split uses: mono (0) -> main, accent
// (1) -> accent, Pill (2) -> main (drawn over its own solid-bg-color
// plate), color (3) -> whatever gradient/rule that content computed.
// The one and only color_mode switch in this entire file -- every
// cluster function below just calls this once per segment instead of
// re-implementing the same 4-way branch.
static GColor resolve_flat_color(uint8_t color_mode, GColor dynamic_color, GColor main_color, GColor accent_color) {
  switch (color_mode) {
    case 1: return accent_color;
    case 3: return dynamic_color;
    case 0: case 2: default: return main_color;
  }
}

// Write directly into slot->segments[i] instead of building a 32-byte
// RenderSegment on the stack and block-copying it in -- see the size
// analysis's item B. Callers that need icon_extra/icon_flag set them
// on slot->segments[i] themselves right after calling this, same as
// they used to set them on the local RenderSegment before assigning it.
static void set_icon_seg(FeatureSlot *slot, int i, uint8_t icon_kind, GColor color) {
  RenderSegment *g = &slot->segments[i];
  g->is_icon = true;
  g->icon_kind = icon_kind;
  g->icon_extra = 0;
  g->icon_flag = false;
  g->color = color;
  g->color2 = color;
}

static void set_text_seg(FeatureSlot *slot, int i, const char *text, GColor color) {
  RenderSegment *g = &slot->segments[i];
  g->is_icon = false;
  g->icon_kind = 0;
  g->icon_extra = 0;
  g->icon_flag = false;
  g->color = color;
  g->color2 = color;
  snprintf(g->text, sizeof(g->text), "%s", text);
}

// One helper covers the common "icon_kind 0 => text only, else icon+text"
// 1- or 2-segment shape most content-cluster cases use.
static void slot_set(FeatureSlot *slot, uint8_t icon_kind, const char *text, GColor color) {
  if (icon_kind == 0) {
    slot->segment_count = 1;
    set_text_seg(slot, 0, text, color);
  } else {
    slot->segment_count = 2;
    set_icon_seg(slot, 0, icon_kind, color);
    set_text_seg(slot, 1, text, color);
  }
}

// ---- health cluster: heart rate, steps, battery, Bluetooth, sleep,
// Quiet Time, Hourly Vibrations -----------------------------------------

static void __attribute__((noinline)) compute_health_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                  uint8_t color_mode, GColor main_color, GColor accent_color) {
  char buf[24];
  switch (content) {
    case 1: { // heart rate -- pink(low)->red->violet(dangerously high) gradient by actual BPM
      int bpm = feature_health_peek_current_bpm();
      GColor dyn = (bpm > 0) ? feature_colors_heart_rate_gradient(bpm) : GColorLightGray;
      snprintf(buf, sizeof(buf), bpm > 0 ? "%d" : "N/A", bpm);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 1, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      set_text_seg(slot, 1, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
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
      set_icon_seg(slot, 0, 2, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      set_text_seg(slot, 1, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
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
      set_icon_seg(slot, 0, 2, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      set_text_seg(slot, 1, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 10: { // battery %
      BatteryChargeState bs = battery_state_service_peek();
      GColor dyn = bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent);
      snprintf(buf, sizeof(buf), bs.is_charging ? "%d%%+" : "%d%%", bs.charge_percent);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 3, c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      // battery is the one icon that also always shows its own text (percentage) --
      // matches the icon+text shape every other health content uses.
      slot->segment_count = 2;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 17: { // pebble battery logo -- icon draws its own fill bar, no separate text
      BatteryChargeState bs = battery_state_service_peek();
      GColor dyn = bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 12, c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      return;
    }
    case 20: { // Bluetooth connection status -- always its own dynamic color, ignores color_mode
      bool connected = connection_service_peek_pebble_app_connection();
      GColor c = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      slot_set(slot, 13, connected ? "Connected" : "No phone", c);
      return;
    }
    case 78: { // Bluetooth, icon only -- same always-dynamic color as 20
      bool connected = connection_service_peek_pebble_app_connection();
      GColor c = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 13, c);
      return;
    }
    case 105: { // Quiet Time status, icon only -- plain speaker (off) / crossed-out speaker (active)
      bool active = quiet_time_is_active();
      GColor dyn = active ? GColorRed : GColorWhite;
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 29, c);
      slot->segments[0].icon_flag = active; // true = crossed-out
      return;
    }
    case 106: { // Quiet Time status, icon (always plain speaker) + "ON"/"OFF" text
      bool active = quiet_time_is_active();
      GColor dyn = active ? GColorRed : GColorWhite;
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 29, c);
      slot->segments[0].icon_flag = false; // never crossed out here -- the text carries the state instead
      set_text_seg(slot, 1, active ? "ON" : "OFF", c);
      return;
    }
    case 107: { // Hourly Vibrations status, icon only -- watch+buzz (on) / crossed-out (off)
      bool on = feature_rules_hourly_vibe_is_scheduled_now(data, time(NULL));
      GColor dyn = on ? GColorGreen : GColorLightGray;
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 30, c);
      slot->segments[0].icon_flag = !on; // true = crossed-out
      return;
    }
    case 108: { // Hourly Vibrations status, icon (always plain watch+buzz) + "ON"/"OFF" text
      bool on = feature_rules_hourly_vibe_is_scheduled_now(data, time(NULL));
      GColor dyn = on ? GColorGreen : GColorLightGray;
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 30, c);
      slot->segments[0].icon_flag = false;
      set_text_seg(slot, 1, on ? "ON" : "OFF", c);
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
        c = resolve_flat_color(color_mode, feature_colors_seven_stop_gradient_reversed((int32_t)secs, 0, range_secs), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, icon_kind, buf, c);
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
        c = resolve_flat_color(color_mode, feature_colors_red_green_gradient((uint8_t)pct), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, 20, buf, c);
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
        c = resolve_flat_color(color_mode, feature_colors_daynight_gradient(event, data->sun_rise, data->sun_set), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, icon_kind, buf, c);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- weather cluster: temperature, conditions, UV, rain/wind/humidity,
// pressure/AQI/visibility/cloud cover, and the "last weather update"
// readouts. All of these (per feature_rules_content_is_weather_derived() below) can
// be overridden wholesale to a red "ERR ###" by
// features_recompute_slot_value()'s shared tail once this function
// returns, so nothing in here needs to check for a fetch error itself.

static void __attribute__((noinline)) compute_weather_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
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
        set_text_seg(slot, 0, hi_buf, feature_colors_seven_stop_gradient(data->temp_high_c, -10, 40));
        set_text_seg(slot, 1, lo_buf, feature_colors_seven_stop_gradient(data->temp_low_c, -10, 40));
      } else {
        snprintf(buf, sizeof(buf), "H%d L%d", hi, lo);
        slot->segment_count = 1;
        set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, main_color, main_color, accent_color));
      }
      return;
    }
    case 5: { // current conditions
      int16_t temp = feature_rules_convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d %s", temp,
               weather_layer_short_condition_text(data->weather_condition, data->cloud_cover_pct));
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, cond_color, main_color, accent_color));
      return;
    }
    case 6: { // UV index (today's daily max -- see 104 for the current-hour value)
      uint8_t uv = data->uv_index_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(uv, 1, 13), main_color, accent_color));
      return;
    }
    case 104: { // current UV index (this hour, as opposed to 6's daily max)
      uint8_t uv = data->uv_index_current_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(uv, 1, 13), main_color, accent_color));
      return;
    }
    case 7: { // rain chance
      snprintf(buf, sizeof(buf), "%d%%", data->rain_chance_pct);
      GColor c = resolve_flat_color(color_mode, feature_colors_white_to_turquoise_gradient(data->rain_chance_pct, 0, 100), main_color, accent_color);
      slot_set(slot, 5, buf, c);
      return;
    }
    case 8: { // humidity
      snprintf(buf, sizeof(buf), "%d%%", data->humidity_pct);
      GColor c = resolve_flat_color(color_mode, feature_colors_white_to_turquoise_gradient(data->humidity_pct, 0, 100), main_color, accent_color);
      slot_set(slot, 6, buf, c);
      return;
    }
    case 9: { // wind speed
      snprintf(buf, sizeof(buf), "%d", feature_rules_convert_wind(data->wind_speed_kmh, data->wind_speed_unit));
      GColor c = resolve_flat_color(color_mode, feature_colors_white_to_turquoise_gradient(data->wind_speed_kmh, 0, 60), main_color, accent_color);
      slot_set(slot, 7, buf, c);
      return;
    }
    case 14: { // visibility -- grayscale, like cloud cover (vis_score_pct is sent as 100-cloud%)
      snprintf(buf, sizeof(buf), "%d%%", data->vis_score_pct);
      uint8_t equiv_cloud_pct = 100 - data->vis_score_pct;
      GColor dyn = feature_colors_overcast_gray_gradient(equiv_cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : equiv_cloud_pct);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot_set(slot, 9, buf, c);
      return;
    }
    case 15: { // cloud cover
      snprintf(buf, sizeof(buf), "%d%%", data->cloud_cover_pct);
      GColor dyn = feature_colors_overcast_gray_gradient(data->cloud_cover_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : data->cloud_cover_pct);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot_set(slot, 10, buf, c);
      return;
    }
    case 31: { // weather icon only, no text
      GColor c = resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
      return;
    }
    case 32: { // temp + weather icon -- condition-based color, same as 5/31
      int16_t temp = feature_rules_convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d", temp);
      GColor c = resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 34: { // pressure, with rising/falling/flat trend arrow
      snprintf(buf, sizeof(buf), "%d hPa", data->pressure_hpa);
      GColor c = resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(data->pressure_hpa, 970, 1050), main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 15, c);
      slot->segments[0].icon_extra = data->pressure_trend;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 35: { // wind direction, with a rotated compass arrow
      static const char *COMPASS_DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
      int idx = ((data->wind_dir_deg + 22) / 45) % 8;
      if (idx < 0) idx += 8;
      snprintf(buf, sizeof(buf), "%s", COMPASS_DIRS[idx]);
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 16, c);
      slot->segments[0].icon_extra = data->wind_dir_deg;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 36: { // air quality -- shared 7-stop gradient, remapped per scale
      bool use_eu = (data->aqi_unit == 1);
      uint16_t aqi_value = use_eu ? data->aqi_eu : data->aqi_us;
      snprintf(buf, sizeof(buf), "AQI %d", aqi_value);
      GColor c = resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(aqi_value, 0, use_eu ? 100 : 300), main_color, accent_color);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 37: { // dew point -- reuses the humidity feature's droplet icon
      int16_t dew = feature_rules_convert_temp(data->dew_point_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d", dew);
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot_set(slot, 6, buf, c);
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
        c = resolve_flat_color(color_mode, feature_colors_altitude_gradient(data->altitude_m), main_color, accent_color);
      }
      slot_set(slot, 17, buf, c);
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
      GColor c = resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(temp_c, -10, 40), main_color, accent_color);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 76: { // weather icon + current/high/low all in one line -- "mixed" value, condition-based color
      int16_t cur = feature_rules_convert_temp(data->weather_temp_c, data->temp_unit);
      int16_t hi = feature_rules_convert_temp(data->temp_high_c, data->temp_unit);
      int16_t lo = feature_rules_convert_temp(data->temp_low_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d H%d L%d", cur, hi, lo);
      GColor c = resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
      set_text_seg(slot, 1, buf, c);
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
        set_text_seg(slot, 0, buf, c);
        return;
      }
      int16_t shown = feature_rules_convert_temp(data->forecast_temp_c[idx], data->temp_unit);
      snprintf(buf, sizeof(buf), "+%dh %d", hrs_ahead, shown);
      // Same shape as "temp + weather icon" (32): plain 7-stop gradient,
      // not the condition-based color -- per the "Temperature readouts
      // (including temp+weather icon)" rule.
      c = resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(data->forecast_temp_c[idx], -10, 40), main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = feature_icons_weather_category(data->forecast_condition[idx], 50); // no forecast cloud% sent separately -- 50 is a neutral middle guess, only affects which of a few near-identical icon glyphs gets picked
      set_text_seg(slot, 1, buf, c);
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
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      if (content == 94) {
        // Short version only -- the long version's own "Last updated"
        // text already says what this is, so the icon would be
        // redundant there; the short version is just a bare time, so
        // a small two-arrows-chasing "refresh" glyph (icon_kind 28) is
        // what tells you what that time actually means at a glance.
        slot->segment_count = 2;
        set_icon_seg(slot, 0, 28, c);
        set_text_seg(slot, 1, buf, c);
      } else {
        slot->segment_count = 1;
        set_text_seg(slot, 0, buf, c);
      }
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- date/time cluster -------------------------------------------------

static void __attribute__((noinline)) compute_date_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                uint8_t color_mode, GColor main_color, GColor accent_color, time_t now, struct tm *t) {
  char buf[24];
  GColor dyn = main_color;

  switch (content) {
    case 12: { // short date (multi-value), e.g. "Mon 15"
      char day_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      snprintf(buf, sizeof(buf), "%s %d", day_buf, t->tm_mday);
      dyn = feature_colors_date_year_progress_gradient(t);
      break;
    }
    case 18: { // digital time ("Time") -- day/night gradient off the actual sunrise/sunset
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
      dyn = feature_colors_daynight_gradient(now, data->sun_rise, data->sun_set);
      break;
    }
    case 19: { // week number (single-value) -- 7-stop gradient over its own 1-52 range
      char wk_buf[4];
      strftime(wk_buf, sizeof(wk_buf), "%V", t);
      snprintf(buf, sizeof(buf), "WK %s", wk_buf);
      dyn = feature_colors_seven_stop_gradient(atoi(wk_buf), 1, 52);
      break;
    }
    case 21: { // month + day (multi-value), e.g. "SEP 11"
      char mon_buf[4];
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      feature_rules_to_upper_str(mon_buf);
      snprintf(buf, sizeof(buf), "%s %d", mon_buf, t->tm_mday);
      dyn = feature_colors_date_year_progress_gradient(t);
      break;
    }
    case 22: snprintf(buf, sizeof(buf), "%d", t->tm_mday); dyn = feature_colors_seven_stop_gradient(t->tm_mday, 1, 31); break;
    case 23: strftime(buf, sizeof(buf), "%a", t); feature_rules_to_upper_str(buf); dyn = feature_colors_seven_stop_gradient(t->tm_wday, 0, 6); break;
    case 24: strftime(buf, sizeof(buf), "%A", t); dyn = feature_colors_seven_stop_gradient(t->tm_wday, 0, 6); break;
    case 25: strftime(buf, sizeof(buf), "%b", t); feature_rules_to_upper_str(buf); dyn = feature_colors_seven_stop_gradient(t->tm_mon, 0, 11); break;
    case 26: strftime(buf, sizeof(buf), "%B", t); dyn = feature_colors_seven_stop_gradient(t->tm_mon, 0, 11); break;
    case 27: snprintf(buf, sizeof(buf), "%d/%d", t->tm_mday, t->tm_mon + 1); dyn = feature_colors_date_year_progress_gradient(t); break;
    case 28: snprintf(buf, sizeof(buf), "%d/%d", t->tm_mon + 1, t->tm_mday); dyn = feature_colors_date_year_progress_gradient(t); break;
    case 29: snprintf(buf, sizeof(buf), "%d/%d/%d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900); dyn = feature_colors_date_year_progress_gradient(t); break;
    case 30: snprintf(buf, sizeof(buf), "%d/%d/%02d", t->tm_mon + 1, t->tm_mday, (t->tm_year + 1900) % 100); dyn = feature_colors_date_year_progress_gradient(t); break;
    case 63: { // full time with seconds -- day/night gradient, same as digital time
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S %p", t);
      dyn = feature_colors_daynight_gradient(now, data->sun_rise, data->sun_set);
      break;
    }
    case 64: snprintf(buf, sizeof(buf), "%02d", t->tm_hour); dyn = feature_colors_linear_white_black(t->tm_hour, 0, 23); break;
    case 65: snprintf(buf, sizeof(buf), "%d", t->tm_hour); dyn = feature_colors_linear_white_black(t->tm_hour, 0, 23); break;
    case 66: {
      int hour12 = t->tm_hour % 12; if (hour12 == 0) hour12 = 12;
      snprintf(buf, sizeof(buf), "%d", hour12); dyn = feature_colors_linear_white_black(hour12, 1, 12);
      break;
    }
    case 67: snprintf(buf, sizeof(buf), "%d", t->tm_min); dyn = feature_colors_linear_white_black(t->tm_min, 0, 59); break;
    case 68: snprintf(buf, sizeof(buf), "%02d", t->tm_min); dyn = feature_colors_linear_white_black(t->tm_min, 0, 59); break;
    case 69: snprintf(buf, sizeof(buf), "%d", t->tm_sec); dyn = feature_colors_linear_white_black(t->tm_sec, 0, 59); break;
    case 70: snprintf(buf, sizeof(buf), "%02d", t->tm_sec); dyn = feature_colors_linear_white_black(t->tm_sec, 0, 59); break;
    case 71: snprintf(buf, sizeof(buf), "%d", t->tm_sec / 10); dyn = feature_colors_linear_white_black(t->tm_sec / 10, 0, 5); break;
    case 72: snprintf(buf, sizeof(buf), "%d", t->tm_sec % 10); dyn = feature_colors_linear_white_black(t->tm_sec % 10, 0, 9); break;
    case 86: snprintf(buf, sizeof(buf), "%s", t->tm_hour < 12 ? "AM" : "PM"); dyn = (t->tm_hour < 12) ? GColorBlack : GColorWhite; break;
    case 95: { // weekday + day/month (multi-value), e.g. "MON 24/9"
      char day_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t); feature_rules_to_upper_str(day_buf);
      snprintf(buf, sizeof(buf), "%s %d/%d", day_buf, t->tm_mday, t->tm_mon + 1);
      dyn = feature_colors_date_year_progress_gradient(t);
      break;
    }
    case 96: { // weekday + month/day (multi-value), e.g. "MON 9/24"
      char day_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t); feature_rules_to_upper_str(day_buf);
      snprintf(buf, sizeof(buf), "%s %d/%d", day_buf, t->tm_mon + 1, t->tm_mday);
      dyn = feature_colors_date_year_progress_gradient(t);
      break;
    }
    case 103: { // "long date" + week number (multi-value), e.g. "Mon 23 Sep WK34"
      char day_buf[4], mon_buf[4], wk_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      strftime(wk_buf, sizeof(wk_buf), "%V", t);
      snprintf(buf, sizeof(buf), "%s %d %s WK%s", day_buf, t->tm_mday, mon_buf, wk_buf);
      dyn = feature_colors_date_year_progress_gradient(t);
      break;
    }
    default:
      slot->segment_count = 0;
      return;
  }
  slot->segment_count = 1;
  set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
}

// ---- timezone cluster ---------------------------------------------------

static void __attribute__((noinline)) compute_timezone_value(FeatureSlot *slot, uint8_t content, uint8_t color_mode,
                                    GColor main_color, GColor accent_color, time_t now) {
  const TimezoneInfo *tz = feature_timezone_get((uint8_t)(content - 44));
  int16_t offset_min = feature_timezone_current_offset_min(tz, now);
  time_t local_time = now + (int32_t)offset_min * 60;
  int32_t local_secs_of_day = ((local_time % 86400) + 86400) % 86400;
  int local_hour24 = (int)(local_secs_of_day / 3600);
  int local_min = (int)((local_secs_of_day % 3600) / 60);
  char buf[16];
  if (clock_is_24h_style()) {
    snprintf(buf, sizeof(buf), "%s %02d:%02d", tz->abbr, local_hour24, local_min);
  } else {
    int hour12 = local_hour24 % 12; if (hour12 == 0) hour12 = 12;
    snprintf(buf, sizeof(buf), "%s %d:%02d%s", tz->abbr, hour12, local_min, local_hour24 < 12 ? "AM" : "PM");
  }
  slot->segment_count = 1;
  set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, feature_timezone_daylight_color(local_hour24), main_color, accent_color));
}

// ---- sky/astronomy cluster: moon phase, location, sunrise/sunset,
// planets, meteor shower, Saturn rings, ISS, aurora, compass ---------

static void __attribute__((noinline)) compute_sky_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                               uint8_t color_mode, GColor main_color, GColor accent_color, time_t now) {
  char buf[24];

  switch (content) {
    case 11: { // Moon phase -- icon + short name, no natural "value" to grade -- always white
      snprintf(buf, sizeof(buf), "%s", celestial_moon_phase_short_name(data->moon_phase_pct, data->moon_waxing));
      GColor c = resolve_flat_color(color_mode, GColorWhite, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 4, c);
      slot->segments[0].icon_extra = data->moon_phase_pct;
      slot->segments[0].icon_flag = data->moon_waxing;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 13: { // location name
      snprintf(buf, sizeof(buf), "%s", data->location_name[0] != '\0' ? data->location_name : "Unknown");
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot_set(slot, 8, buf, c);
      return;
    }
    case 16: { // sunrise/sunset -- same event/icon as the digital/analog info panel's row
      bool is_sunrise = false;
      time_t sun_event_time = 0;
      if (eclipse_ui_get_next_sun_event(now, data->sun_rise, data->sun_set, data->sun_rise_tomorrow, &sun_event_time, &is_sunrise)) {
        struct tm *event_t = localtime(&sun_event_time);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", event_t);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
      }
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 11, c);
      slot->segments[0].icon_flag = is_sunrise;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 79: { // how many of the 5 tracked planets are above the horizon right now
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else {
        uint8_t count = celestial_count_visible_planets(data, now);
        snprintf(buf, sizeof(buf), "%d planet%s", count, count == 1 ? "" : "s");
        c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      }
      slot_set(slot, 23, buf, c);
      return;
    }
    case 80: { // active meteor shower name, if any -- grayscale by intensity (more meteors = whiter)
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else if (data->meteor_intensity > 0 && data->meteor_shower_name[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->meteor_shower_name);
        c = resolve_flat_color(color_mode, feature_colors_meteor_intensity_gradient(data->meteor_intensity), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 81: { // Saturn's current ring-opening angle
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else {
        snprintf(buf, sizeof(buf), "Rings %d%%", data->saturn_ring_open_pct);
        c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      }
      slot_set(slot, 24, buf, c);
      return;
    }
    case 82: { // which of the 5 tracked planets rises next today, and when
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else {
        static const char *PLANET_ABBR[PLANET_COUNT] = { "MER", "VEN", "MAR", "JUP", "SAT" };
        int best = -1;
        time_t best_t = 0;
        for (int p = 0; p < PLANET_COUNT; p++) {
          time_t r = data->planet_rise[p];
          if (r > now && (best == -1 || r < best_t)) { best = p; best_t = r; }
        }
        if (best >= 0) {
          struct tm *bt = localtime(&best_t);
          char time_buf[8];
          strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", bt);
          snprintf(buf, sizeof(buf), "%s %s", PLANET_ABBR[best], time_buf);
          c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
        } else {
          snprintf(buf, sizeof(buf), "N/A");
          c = GColorLightGray;
        }
      }
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 83: { // start time of the next visible ISS pass
      GColor c;
      if (data->iss_error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->iss_error_code);
        c = GColorRed;
      } else if (data->iss_next_pass > 0) {
        struct tm *it = localtime(&data->iss_next_pass);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", it);
        c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, 25, buf, c);
      return;
    }
    case 84: { // current planetary Kp index
      GColor c;
      if (data->aurora_error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->aurora_error_code);
        c = GColorRed;
      } else {
        snprintf(buf, sizeof(buf), "Kp %d.%d", data->aurora_kp_x10 / 10, data->aurora_kp_x10 % 10);
        c = resolve_flat_color(color_mode, feature_colors_white_to_red_gradient(data->aurora_kp_x10), main_color, accent_color);
      }
      slot_set(slot, 26, buf, c);
      return;
    }
    case 85: { // Compass -- active (real heading) for 15s after a shake, then asleep until the next one.
               // Needs 2 colors at once (north arrow vs the other 3) rather than one flat color --
               // "mono"/"accent"/Pill still mean one shared color for the whole icon; only "color"
               // mode splits into accent (north) + main (other 3).
      bool asleep = input_compass_feature_is_asleep();
      if (asleep) {
        snprintf(buf, sizeof(buf), "---");
      } else {
        static const char *COMPASS_DIRS[16] = {
          "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
          "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
        };
        int32_t heading = input_compass_feature_heading_deg();
        int idx = (int)(((heading * 2 + 22) / 45) % 16);
        if (idx < 0) idx += 16;
        snprintf(buf, sizeof(buf), "%s", COMPASS_DIRS[idx]);
      }
      GColor flat = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 27, flat);
      slot->segments[0].icon_extra = (int16_t)(input_compass_feature_heading_deg() % 360);
      slot->segments[0].icon_flag = asleep;
      if (color_mode == 3) {
        slot->segments[0].color = accent_color;  // north arrow
        slot->segments[0].color2 = main_color;   // other 3 arrows
      }
      set_text_seg(slot, 1, buf, flat);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- combo cluster: multi-icon/multi-value content (97-102, 109-115) --
//
// Per request, these share ONE flat color (always main_color, not
// accent -- there's no single sensible "accent" reading across a
// multi-icon combo) for mono/accent/Pill modes, and only split into
// independently-gradient-colored segments under "color" mode (3).

static void __attribute__((noinline)) compute_combo_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
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
      set_icon_seg(slot, 0, 1, hr_c);
      set_text_seg(slot, 1, buf1, hr_c);
      set_icon_seg(slot, 2, 2, step_c);
      set_text_seg(slot, 3, buf2, step_c);
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
      set_icon_seg(slot, 0, 21, flat);
      set_text_seg(slot, 1, buf1, bed_c);
      set_text_seg(slot, 2, buf2, wake_c);
      return;
    }
    case 99: case 100: { // battery + BT (icons only, 99), battery % + BT (100)
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;

      set_icon_seg(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;

      if (content == 99) {
        slot->segment_count = 2;
        set_icon_seg(slot, 1, 13, bt_c);
      } else {
        snprintf(buf1, sizeof(buf1), "%d%%", bs.charge_percent);
        slot->segment_count = 3;
        set_text_seg(slot, 1, buf1, batt_c);
        set_icon_seg(slot, 2, 13, bt_c);
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
      set_icon_seg(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      set_icon_seg(slot, 1, 13, bt_c);
      set_icon_seg(slot, 2, 29, quiet_c);
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
      set_icon_seg(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      set_text_seg(slot, 1, buf1, batt_c);
      set_icon_seg(slot, 2, 29, quiet_c);
      slot->segments[2].icon_flag = false; // text carries the state here, not the icon shape
      set_text_seg(slot, 3, quiet_active ? "ON" : "OFF", quiet_c);
      set_icon_seg(slot, 4, 13, bt_c);
      set_text_seg(slot, 5, connected ? "ON" : "OFF", bt_c);
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
      set_icon_seg(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      set_icon_seg(slot, 1, 13, bt_c);
      set_icon_seg(slot, 2, 29, quiet_c);
      slot->segments[2].icon_flag = quiet_active;
      set_icon_seg(slot, 3, 30, vibe_c);
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
      set_icon_seg(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      set_text_seg(slot, 1, buf1, batt_c);
      set_icon_seg(slot, 2, 13, bt_c);
      set_icon_seg(slot, 3, 29, quiet_c);
      slot->segments[3].icon_flag = quiet_active;
      set_icon_seg(slot, 4, 30, vibe_c);
      slot->segments[4].icon_flag = !vibe_on;
      return;
    }
    case 112: { // Quiet Time + Hourly Vibrations, icons only
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool vibe_on = feature_rules_hourly_vibe_is_scheduled_now(data, now);
      GColor vibe_c = dynamic ? (vibe_on ? GColorGreen : GColorLightGray) : flat;

      slot->segment_count = 2;
      set_icon_seg(slot, 0, 29, quiet_c);
      slot->segments[0].icon_flag = quiet_active;
      set_icon_seg(slot, 1, 30, vibe_c);
      slot->segments[1].icon_flag = !vibe_on;
      return;
    }
    case 113: { // Quiet Time + Hourly Vibrations, icons + "ON"/"OFF" texts
      bool quiet_active = quiet_time_is_active();
      GColor quiet_c = dynamic ? (quiet_active ? GColorRed : GColorWhite) : flat;
      bool vibe_on = feature_rules_hourly_vibe_is_scheduled_now(data, now);
      GColor vibe_c = dynamic ? (vibe_on ? GColorGreen : GColorLightGray) : flat;

      slot->segment_count = 4;
      set_icon_seg(slot, 0, 29, quiet_c);
      slot->segments[0].icon_flag = false; // text carries the state here, not the icon shape
      set_text_seg(slot, 1, quiet_active ? "ON" : "OFF", quiet_c);
      set_icon_seg(slot, 2, 30, vibe_c);
      slot->segments[2].icon_flag = false;
      set_text_seg(slot, 3, vibe_on ? "ON" : "OFF", vibe_c);
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
      set_icon_seg(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      set_text_seg(slot, 1, buf1, batt_c);
      set_icon_seg(slot, 2, 13, bt_c);
      set_text_seg(slot, 3, connected ? "ON" : "OFF", bt_c);
      set_icon_seg(slot, 4, 29, quiet_c);
      slot->segments[4].icon_flag = false; // text carries the state here, not the icon shape
      set_text_seg(slot, 5, quiet_active ? "ON" : "OFF", quiet_c);
      set_icon_seg(slot, 6, 30, vibe_c);
      slot->segments[6].icon_flag = false;
      set_text_seg(slot, 7, vibe_on ? "ON" : "OFF", vibe_c);
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
        set_icon_seg(slot, 0, 21, flat);
        set_text_seg(slot, 1, total_buf, total_c);
        set_text_seg(slot, 2, restful_paren, restful_c);
        set_text_seg(slot, 3, quality_buf, quality_c);
      } else {
        slot->segment_count = 2;
        set_icon_seg(slot, 0, 21, flat);
        set_text_seg(slot, 1, "N/A", dynamic ? GColorLightGray : flat);
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
      set_text_seg(slot, 0, buf1, date_c);
      set_icon_seg(slot, 1, 11, flat);
      slot->segments[1].icon_flag = is_sunrise;
      set_text_seg(slot, 2, time_buf, flat);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- unified position resolution ---------------------------------------
//
// The one and only place any slot's content gets positioned: measures
// each already-filled-in segment (icon widths are the same fixed
// per-icon-kind lookup every icon always used, text is measured
// against the corner font) and resolves every segment's x_offset/width
// relative to the slot's own box_x, according to the slot's left/
// center/right alignment. Runs once per recompute, never at draw time --
// features_draw_slot() just reads x_offset/width straight off each
// segment.

void feature_values_compute_slot(FeatureSlot *slot, const EclipseData *data,
                                 GColor main_color, GColor accent_color, GColor bg_color,
                                 time_t now, struct tm *t) {
  if (slot->content == 0) {
    slot->segment_count = 0;
    return;
  }

  uint8_t content = slot->content, color_mode = slot->color_mode;
  switch (content) {
    case 97: case 98: case 99: case 100: case 101: case 102:
    case 109: case 110: case 111: case 112: case 113: case 114: case 115:
      compute_combo_value(slot, content, data, color_mode, main_color, now);
      break;
    case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53:
    case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62:
      compute_timezone_value(slot, content, color_mode, main_color, accent_color, now);
      break;
    case 1: case 2: case 3: case 10: case 17: case 20: case 39: case 40: case 41: case 42: case 43: case 78:
    case 105: case 106: case 107: case 108:
      compute_health_value(slot, content, data, color_mode, main_color, accent_color);
      break;
    case 11: case 13: case 16: case 79: case 80: case 81: case 82: case 83: case 84: case 85:
      compute_sky_value(slot, content, data, color_mode, main_color, accent_color, now);
      break;
    case 4: case 5: case 6: case 7: case 8: case 9: case 14: case 15: case 31: case 32: case 34:
    case 35: case 36: case 37: case 38: case 73: case 74: case 75: case 76: case 77: case 87: case 88:
    case 89: case 90: case 91: case 92: case 93: case 94: case 104:
      compute_weather_value(slot, content, data, color_mode, main_color, accent_color, bg_color);
      break;
    default:
      compute_date_value(slot, content, data, color_mode, main_color, accent_color, now, t);
      break;
  }

  // Weather-service errors override the cluster's normal value so the
  // failure is always visible and consistently styled.
  if (feature_rules_content_is_weather_derived(content) && weather_layer_should_show_error(data)) {
    char err_buf[10];
    snprintf(err_buf, sizeof(err_buf), "ERR %d", data->weather_error_code);
    slot->segment_count = 1;
    set_text_seg(slot, 0, err_buf, GColorRed);
    slot->draw_pill = false;
  }
}
