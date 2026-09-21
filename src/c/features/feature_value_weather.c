#include <pebble.h>
#include "./feature_value_weather.h"
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

// Phase 7 fallback helper shared by every temperature-display case below:
// prefer the PKJS-formatted string (already unit-converted -- Section 6),
// fall back to the old on-watch conversion only if PKJS hasn't sent one yet
// (see eclipse_data.h's comment on current_temp_display for when that
// happens). Keeping this in one place means each case below reads the same
// as it did before Phase 7, just calling this instead of
// feature_rules_convert_temp()+snprintf() directly.
static void temp_display_or_fallback(char *out, size_t out_sz, const char *pkjs_str,
                                     int16_t raw_c, uint8_t temp_unit) {
  if (pkjs_str[0] != '\0') {
    snprintf(out, out_sz, "%s", pkjs_str);
  } else {
    snprintf(out, out_sz, "%d", feature_rules_convert_temp(raw_c, temp_unit));
  }
}

// As above, but also resolves the value's gradient color -- PKJS sends
// both together (Section 31 addendum: once a value's *text* is PKJS-
// sourced, computing its color from a watch-local raw Celsius field buys
// nothing but keeping feature_colors_seven_stop_gradient()'s color-ramp
// table resident for no reason, so the color moves with the text). Returns
// true when the PKJS-supplied text/color were used, false when the watch
// fell back to computing both itself -- callers that need the raw Celsius
// value only in the fallback case (content 77) check this themselves
// instead of calling this helper.
static bool temp_display_and_color(char *out, size_t out_sz, GColor *out_color,
                                   const char *pkjs_str, uint8_t pkjs_color_packed,
                                   int16_t raw_c, uint8_t temp_unit) {
  if (pkjs_str[0] != '\0') {
    snprintf(out, out_sz, "%s", pkjs_str);
    *out_color = feature_value_color_from_packed(pkjs_color_packed);
    return true;
  }
  snprintf(out, out_sz, "%d", feature_rules_convert_temp(raw_c, temp_unit));
  *out_color = feature_colors_seven_stop_gradient(raw_c, -10, 40);
  return false;
}

// Icon-category selection (Section 4's "static icon" concept, continuing
// Phase 7): prefer PKJS's weather_icon_category, gated on
// current_temp_display's presence since a plain 0-6 byte has no unused
// value to serve as its own "not sent yet" signal (see eclipse_data.h).
// Shared by cases 31/32/76, all of which drew the current-conditions icon.
static uint8_t weather_icon_category_or_fallback(const EclipseData *data) {
  if (data->current_temp_display[0] != '\0') return data->weather_icon_category;
  return feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
}

// Same principle, same gating, for the icon's *color* -- see
// eclipse_data.h's weather_icon_color comment.
static GColor weather_condition_color_or_fallback(const EclipseData *data) {
  if (!data->valid) return GColorClear; // caller only uses this when data->valid anyway (see cond_color below)
  if (data->current_temp_display[0] != '\0') return feature_value_color_from_packed(data->weather_icon_color);
  return feature_colors_weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct);
}

void __attribute__((noinline)) feature_value_weather_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                   uint8_t color_mode, GColor main_color, GColor accent_color, GColor bg_color) {
  char buf[24];
  GColor cond_color = data->valid
    ? weather_condition_color_or_fallback(data)
    : bg_color;

  switch (content) {
    case 4: { // high/low temperature
      char hi_num[8], lo_num[8];
      GColor hi_color, lo_color;
      temp_display_and_color(hi_num, sizeof(hi_num), &hi_color, data->temp_high_display, data->temp_high_color, data->temp_high_c, data->temp_unit);
      temp_display_and_color(lo_num, sizeof(lo_num), &lo_color, data->temp_low_display, data->temp_low_color, data->temp_low_c, data->temp_unit);
      if (color_mode == 3) {
        // "color" mode splits high and low into their own independently
        // colored primitives -- atomic decomposition (Phase 8): 4
        // primitives (H, value, L, value) instead of two composed "H23"/
        // "L18" strings. High and low keep their own independent colors,
        // so "H"/value share hi_color and "L"/value share lo_color rather
        // than everything sharing one flat color the way the plain branch
        // below does.
        slot->segment_count = 4;
        feature_value_set_text_segment(slot, 0, "H", hi_color);
        feature_value_set_text_segment(slot, 1, hi_num, hi_color);
        feature_value_set_text_segment(slot, 2, "L", lo_color);
        feature_value_set_text_segment(slot, 3, lo_num, lo_color);
      } else {
        // Atomic decomposition (Phase 8): 5 primitives (H, value, space, L,
        // value) instead of one composed "H23 L18" string -- this branch
        // never used a dynamic gradient color (color_mode==3 is the other
        // branch above), so every primitive shares the one flat color.
        GColor flat = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
        slot->segment_count = 5;
        feature_value_set_text_segment(slot, 0, "H", flat);
        feature_value_set_text_segment(slot, 1, hi_num, flat);
        feature_value_set_text_segment(slot, 2, " ", flat);
        feature_value_set_text_segment(slot, 3, "L", flat);
        feature_value_set_text_segment(slot, 4, lo_num, flat);
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
    case 6: { // UV index (today's daily max -- see 107 for the current-hour value)
      char num[6];
      GColor dyn;
      if (data->uv_daily_display[0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->uv_daily_display);
        dyn = feature_value_color_from_packed(data->uv_daily_color);
      } else {
        uint8_t uv = data->uv_index_x10 / 10;
        snprintf(num, sizeof(num), "UV%d", uv);
        dyn = feature_colors_seven_stop_gradient(uv, 1, 13);
      }
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, num, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 107: { // current UV index (this hour, as opposed to 6's daily max)
      char num[6];
      GColor dyn;
      if (data->uv_current_display[0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->uv_current_display);
        dyn = feature_value_color_from_packed(data->uv_current_color);
      } else {
        uint8_t uv = data->uv_index_current_x10 / 10;
        snprintf(num, sizeof(num), "UV%d", uv);
        dyn = feature_colors_seven_stop_gradient(uv, 1, 13);
      }
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, num, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 7: { // rain chance
      GColor dyn;
      if (data->rain_chance_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->rain_chance_display);
        dyn = feature_value_color_from_packed(data->rain_chance_color);
      } else {
        snprintf(buf, sizeof(buf), "%d%%", data->rain_chance_pct);
        dyn = feature_colors_white_to_turquoise_gradient(data->rain_chance_pct, 0, 100);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      feature_value_slot_set(slot, 5, buf, c);
      return;
    }
    case 8: { // humidity
      GColor dyn;
      if (data->humidity_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->humidity_display);
        dyn = feature_value_color_from_packed(data->humidity_color);
      } else {
        snprintf(buf, sizeof(buf), "%d%%", data->humidity_pct);
        dyn = feature_colors_white_to_turquoise_gradient(data->humidity_pct, 0, 100);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      feature_value_slot_set(slot, 6, buf, c);
      return;
    }
    case 9: { // wind speed
      GColor dyn;
      if (data->wind_speed_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->wind_speed_display);
        dyn = feature_value_color_from_packed(data->wind_speed_color);
      } else {
        snprintf(buf, sizeof(buf), "%d", feature_rules_convert_wind(data->wind_speed_kmh, data->wind_speed_unit));
        dyn = feature_colors_white_to_turquoise_gradient(data->wind_speed_kmh, 0, 60);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      feature_value_slot_set(slot, 7, buf, c);
      return;
    }
    case 14: { // visibility -- grayscale, like cloud cover (vis_score_pct is sent as 100-cloud%)
      GColor dyn;
      if (data->vis_score_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->vis_score_display);
        dyn = feature_value_color_from_packed(data->vis_score_color);
      } else {
        snprintf(buf, sizeof(buf), "%d%%", data->vis_score_pct);
        uint8_t equiv_cloud_pct = 100 - data->vis_score_pct;
        dyn = feature_colors_overcast_gray_gradient(equiv_cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : equiv_cloud_pct);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      feature_value_slot_set(slot, 9, buf, c);
      return;
    }
    case 15: { // cloud cover
      GColor dyn;
      if (data->cloud_cover_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->cloud_cover_display);
        dyn = feature_value_color_from_packed(data->cloud_cover_color);
      } else {
        snprintf(buf, sizeof(buf), "%d%%", data->cloud_cover_pct);
        dyn = feature_colors_overcast_gray_gradient(data->cloud_cover_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : data->cloud_cover_pct);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      feature_value_slot_set(slot, 10, buf, c);
      return;
    }
    case 31: { // weather icon only, no text
      GColor c = feature_value_resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_icon_segment(slot, 0, 14, c);
      slot->segments[0].icon_extra = weather_icon_category_or_fallback(data);
      slot->segments[0].icon_flag = !sky_layer_is_bright(data, time(NULL)); // day/night art for sun/partly-cloudy
      return;
    }
    case 32: { // temp + weather icon -- condition-based color, same as 5/31
      // Phase 7 proof-of-concept (FEATURE_PRIMITIVE_REFACTOR_REPORT_REVISED.md,
      // Section 6): current_temp_display is PKJS-formatted and already unit-
      // converted (weather-normalize.js's formatTempDisplay()), so this no
      // longer calls feature_rules_convert_temp() on the watch. Falls back to
      // the old on-watch conversion only if PKJS hasn't sent a display value
      // yet (empty string) -- see eclipse_data.h's comment on the field.
      if (data->current_temp_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->current_temp_display);
      } else {
        int16_t temp = feature_rules_convert_temp(data->weather_temp_c, data->temp_unit);
        snprintf(buf, sizeof(buf), "%d", temp);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 14, c);
      slot->segments[0].icon_extra = weather_icon_category_or_fallback(data);
      slot->segments[0].icon_flag = !sky_layer_is_bright(data, time(NULL)); // day/night art for sun/partly-cloudy
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 34: { // pressure, with rising/falling/flat trend arrow
      GColor dyn;
      if (data->pressure_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->pressure_display);
        dyn = feature_value_color_from_packed(data->pressure_color);
      } else {
        snprintf(buf, sizeof(buf), "%d hPa", data->pressure_hpa);
        dyn = feature_colors_seven_stop_gradient(data->pressure_hpa, 970, 1050);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 15, c);
      slot->segments[0].icon_extra = data->pressure_trend; // already PKJS-classified -- nothing to migrate here
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
      GColor dyn;
      if (data->aqi_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->aqi_display);
        dyn = feature_value_color_from_packed(data->aqi_color);
      } else {
        bool use_eu = (data->aqi_unit == 1);
        uint16_t aqi_value = use_eu ? data->aqi_eu : data->aqi_us;
        snprintf(buf, sizeof(buf), "AQI %d", aqi_value);
        dyn = feature_colors_seven_stop_gradient(aqi_value, 0, use_eu ? 100 : 300);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, c);
      return;
    }
    case 37: { // dew point -- reuses the humidity feature's droplet icon; no
               // dynamic color, ever -- always the flat color_mode color.
      if (data->dew_point_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->dew_point_display);
      } else {
        int16_t dew = feature_rules_convert_temp(data->dew_point_c, data->temp_unit);
        snprintf(buf, sizeof(buf), "%d", dew);
      }
      GColor c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      feature_value_slot_set(slot, 6, buf, c);
      return;
    }
    case 38: { // altitude -- white(sea level)->turquoise(high) gradient
      GColor c;
      if (data->altitude_m <= -32000) { // sentinel: not available -- stays watch-side regardless
                                        // of any stale altitude_display (see eclipse_data.h)
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      } else if (data->altitude_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->altitude_display);
        c = feature_value_resolve_flat_color(color_mode, feature_value_color_from_packed(data->altitude_color), main_color, accent_color);
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
      const char *prefix = "";
      char num[8];
      GColor dyn;
      if (content == 73) {
        temp_display_and_color(num, sizeof(num), &dyn, data->current_temp_display, data->current_temp_color, data->weather_temp_c, data->temp_unit);
      } else if (content == 74) {
        temp_display_and_color(num, sizeof(num), &dyn, data->temp_high_display, data->temp_high_color, data->temp_high_c, data->temp_unit);
        prefix = "H ";
      } else if (content == 75) {
        temp_display_and_color(num, sizeof(num), &dyn, data->temp_low_display, data->temp_low_color, data->temp_low_c, data->temp_unit);
        prefix = "L ";
      } else {
        // Unlike 73/74/75, feels-like's raw Celsius isn't a field the watch
        // already has -- getting it requires feature_rules_apparent_temp_c()
        // itself, so this can't go through the shared helper's raw_c
        // parameter. In the common case (PKJS has sent feels_like_display),
        // the watch computes nothing here at all -- no apparent-temp call,
        // no gradient lookup, just the string and color PKJS already
        // resolved. feature_rules_apparent_temp_c() only runs at all when
        // PKJS hasn't sent a value yet (see eclipse_data.h).
        if (data->feels_like_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->feels_like_display);
          dyn = feature_value_color_from_packed(data->feels_like_color);
        } else {
          int16_t temp_c = feature_rules_apparent_temp_c(data->weather_temp_c, data->wind_speed_kmh, data->humidity_pct);
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(temp_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(temp_c, -10, 40);
        }
        prefix = "FL ";
      }
      snprintf(buf, sizeof(buf), "%s%s", prefix, num);
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, c);
      return;
    }
    case 76: { // weather icon + current/high/low all in one line -- "mixed" value, condition-based color
      char cur_n[8], hi_n[8], lo_n[8];
      temp_display_or_fallback(cur_n, sizeof(cur_n), data->current_temp_display, data->weather_temp_c, data->temp_unit);
      temp_display_or_fallback(hi_n, sizeof(hi_n), data->temp_high_display, data->temp_high_c, data->temp_unit);
      temp_display_or_fallback(lo_n, sizeof(lo_n), data->temp_low_display, data->temp_low_c, data->temp_unit);
      GColor c = feature_value_resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      // Atomic decomposition (Phase 8): the text half is now 7 primitives
      // (current, space, "H", high, space, "L", low) instead of one
      // composed "23 H18 L15" string, plus the icon = 8 segments total
      // (MAX_RENDER_SEGMENTS).
      slot->segment_count = 8;
      feature_value_set_icon_segment(slot, 0, 14, c);
      slot->segments[0].icon_extra = weather_icon_category_or_fallback(data);
      slot->segments[0].icon_flag = !sky_layer_is_bright(data, time(NULL)); // day/night art for sun/partly-cloudy
      feature_value_set_text_segment(slot, 1, cur_n, c);
      feature_value_set_text_segment(slot, 2, " ", c);
      feature_value_set_text_segment(slot, 3, "H", c);
      feature_value_set_text_segment(slot, 4, hi_n, c);
      feature_value_set_text_segment(slot, 5, " ", c);
      feature_value_set_text_segment(slot, 6, "L", c);
      feature_value_set_text_segment(slot, 7, lo_n, c);
      return;
    }
    case 87: case 88: case 89: case 90: case 91: case 92:
    case 93: case 94: case 95: { // weather in 1-6 hours (87-92) or 1-3 days (93-95): half-icon "+N" + weather icon + temp
      int idx = content - 87; // 0-8 -- forecast_temp_c/forecast_condition[6..8] hold the 3 day-ahead values
      bool is_hour = content <= 92;
      GColor c;
      if (data->forecast_temp_c[idx] <= -128) { // N/A sentinel checked first, same as altitude's case 38 --
                                                // never trusts a possibly-stale forecast_temp_display[idx]
        c = GColorLightGray;
        slot->segment_count = 2;
        feature_value_set_icon_segment(slot, 0, 30, c);
        slot->segments[0].icon_extra = idx;
        feature_value_set_text_segment(slot, 1, "N/A", c);
        return;
      }
      char num[6];
      if (data->forecast_temp_display[idx][0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->forecast_temp_display[idx]);
        c = feature_value_resolve_flat_color(color_mode, feature_value_color_from_packed(data->forecast_temp_color[idx]), main_color, accent_color);
      } else {
        int16_t shown = feature_rules_convert_temp(data->forecast_temp_c[idx], data->temp_unit);
        snprintf(num, sizeof(num), "%d", shown);
        c = feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(data->forecast_temp_c[idx], -10, 40), main_color, accent_color);
      }
      snprintf(buf, sizeof(buf), "%s", num);
      slot->segment_count = 3;
      feature_value_set_icon_segment(slot, 0, 30, c); // "+Xh"/"+X day" half-icon
      slot->segments[0].icon_extra = idx;
      feature_value_set_icon_segment(slot, 1, 14, c);
      slot->segments[1].icon_extra = data->forecast_temp_display[idx][0] != '\0'
        ? data->forecast_icon_category[idx] // gated on the same per-index signal as the temp text/color above
        : feature_icons_weather_category(data->forecast_condition[idx], 50); // no forecast cloud% sent separately -- 50 is a neutral middle guess, only affects which of a few near-identical i...
      // Day/night art for the forecast hour itself (not "now") -- sky_layer_is_bright()
      // takes any time_t and interpolates against the same sun-altitude samples used
      // for the live sky, so this is the actual predicted day/night state then. A
      // day-ahead forecast has no single "then" moment to test that against, so it
      // always draws with daytime art instead.
      slot->segments[1].icon_flag = is_hour ? !sky_layer_is_bright(data, time(NULL) + (time_t)(idx + 1) * 3600) : false;
      feature_value_set_text_segment(slot, 2, buf, c);
      return;
    }
    case 96: case 97: { // last weather update time, long (96, "Last updated 12:34") / short (97, "12:34")
      GColor dyn;
      time_t now = time(NULL);
      if (data->weather_last_update > 0) {
        struct tm *ut = localtime(&data->weather_last_update);
        char time_buf[8];
        strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", ut);
        if (content == 96) snprintf(buf, sizeof(buf), "Last updated %s", time_buf);
        else snprintf(buf, sizeof(buf), "%s", time_buf);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
      }
      dyn = feature_colors_weather_staleness_gradient(now, data->weather_last_update);
      GColor c = feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color);
      if (content == 97) {
        // Short version only -- the long version's own "Last updated"
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
