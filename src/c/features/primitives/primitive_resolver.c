#include "./primitive_resolver.h"
#include "../feature_colors.h"
#include "../feature_rules.h"
#include "../feature_value_helpers.h"
#include "../feature_health.h"
#include "../feature_icons.h"
#include "../feature_timezone.h"
#include "../../rendering/background/sky_layer.h"
#include <string.h>
#include <stdio.h>

static void resolve_text(PrimitiveResolved *out, const char *text, GColor color) {
  out->kind = PRIMITIVE_KIND_TEXT;
  snprintf(out->text, sizeof(out->text), "%s", text);
  out->dynamic_color = color;
}

static void resolve_icon(PrimitiveResolved *out, uint8_t icon_kind, int16_t aux, bool aux_flag, GColor color) {
  out->kind = PRIMITIVE_KIND_ICON;
  out->icon_kind = icon_kind;
  out->aux = aux;
  out->aux_flag = aux_flag;
  out->dynamic_color = color;
}

bool primitive_resolver_resolve(PrimitiveId id, uint8_t aux_in, const EclipseData *data, time_t now, struct tm *t, PrimitiveResolved *out) {
  // `now` is used by PRIM_WEATHER_CONDITION_ICON's day/night check below;
  // every other case only needs `t` (already-broken-down local time).
  memset(out, 0, sizeof(*out));
  // Every currently-covered date/time primitive shares this one gradient
  // (the same one feature_value_date_compute() uses for every content id
  // enabled for transport so far -- see primitive_transport.h) -- computed
  // once here rather than per-case, since date/time cases below all want
  // the same value.
  GColor date_c = feature_colors_date_year_progress_gradient(t);
  char buf[8];

  switch (id) {
    case PRIM_SPACE:
      out->kind = PRIMITIVE_KIND_SPACE;
      out->dynamic_color = date_c;
      return true;
    case PRIM_SLASH:
      out->kind = PRIMITIVE_KIND_SLASH;
      out->dynamic_color = date_c;
      return true;

    case PRIM_WEEKDAY_SHORT_MIXED:
      strftime(buf, sizeof(buf), "%a", t);
      resolve_text(out, buf, date_c);
      return true;
    case PRIM_WEEKDAY_SHORT_UPPER:
      strftime(buf, sizeof(buf), "%a", t);
      feature_rules_to_upper_str(buf);
      resolve_text(out, buf, date_c);
      return true;
    case PRIM_MONTH_SHORT_UPPER:
      strftime(buf, sizeof(buf), "%b", t);
      feature_rules_to_upper_str(buf);
      resolve_text(out, buf, date_c);
      return true;
    case PRIM_DAY_OF_MONTH:
      snprintf(buf, sizeof(buf), "%d", t->tm_mday);
      resolve_text(out, buf, date_c);
      return true;
    case PRIM_MONTH_NUMBER:
      snprintf(buf, sizeof(buf), "%d", t->tm_mon + 1);
      resolve_text(out, buf, date_c);
      return true;
    case PRIM_YEAR_FULL: {
      char ybuf[6];
      snprintf(ybuf, sizeof(ybuf), "%d", t->tm_year + 1900);
      resolve_text(out, ybuf, date_c);
      return true;
    }
    case PRIM_YEAR_SHORT:
      snprintf(buf, sizeof(buf), "%02d", (t->tm_year + 1900) % 100);
      resolve_text(out, buf, date_c);
      return true;

    // Date-component family, continued -- unified under the same date_c
    // gradient as everything above (color-ownership principle: the watch
    // computes every one of these, so the watch decides their color, and
    // "the watch's decision" means ONE consistent rule per family, not a
    // different bespoke gradient depending on which content happened to
    // compose it -- see IMPLEMENTATION_NOTES.md's Phase 3 update). This
    // deliberately does NOT match every one of these primitives' old
    // standalone-content gradient (e.g. content 19's week number used its
    // own 7-stop 1-52 gradient, content 23's weekday used a 7-stop 0-6
    // gradient) -- that per-content variation was the compromise being
    // replaced, not a behavior being preserved.
    case PRIM_WEEK_NUMBER:
      strftime(buf, sizeof(buf), "%V", t);
      resolve_text(out, buf, date_c);
      return true;
    case PRIM_MONTH_SHORT_MIXED:
      strftime(buf, sizeof(buf), "%b", t);
      resolve_text(out, buf, date_c);
      return true;
    case PRIM_WEEKDAY_LONG: {
      char lbuf[12];
      strftime(lbuf, sizeof(lbuf), "%A", t);
      resolve_text(out, lbuf, date_c);
      return true;
    }
    case PRIM_MONTH_LONG: {
      char lbuf[12];
      strftime(lbuf, sizeof(lbuf), "%B", t);
      resolve_text(out, lbuf, date_c);
      return true;
    }
    // "WK" is a constant glyph, same reasoning as "H"/"L" above -- matches
    // PRIM_WEEK_NUMBER's own color rather than duplicating its logic.
    case PRIM_LABEL_WK_PREFIX: {
      PrimitiveResolved wv;
      primitive_resolver_resolve(PRIM_WEEK_NUMBER, 0, data, now, t, &wv);
      resolve_text(out, "WK", wv.dynamic_color);
      return true;
    }

    case PRIM_TEMP_HIGH_TEXT: {
      char num[8];
      GColor c;
      if (data->temp_high_display[0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->temp_high_display);
        c = feature_value_color_from_packed(data->temp_high_color);
      } else {
        snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->temp_high_c, data->temp_unit));
        c = feature_colors_seven_stop_gradient(data->temp_high_c, -10, 40);
      }
      resolve_text(out, num, c);
      return true;
    }
    case PRIM_TEMP_LOW_TEXT: {
      char num[8];
      GColor c;
      if (data->temp_low_display[0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->temp_low_display);
        c = feature_value_color_from_packed(data->temp_low_color);
      } else {
        snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->temp_low_c, data->temp_unit));
        c = feature_colors_seven_stop_gradient(data->temp_low_c, -10, 40);
      }
      resolve_text(out, num, c);
      return true;
    }
    // "H"/"L" are constant glyphs (Section 31: nothing to resolve about
    // their own value), but their COLOR should visually match the value
    // they label -- resolved by delegating to that value's own case above
    // rather than duplicating its fallback logic here.
    case PRIM_LABEL_HIGH_PREFIX: {
      PrimitiveResolved hv;
      primitive_resolver_resolve(PRIM_TEMP_HIGH_TEXT, 0, data, now, t, &hv);
      resolve_text(out, "H", hv.dynamic_color);
      return true;
    }
    case PRIM_LABEL_LOW_PREFIX: {
      PrimitiveResolved lv;
      primitive_resolver_resolve(PRIM_TEMP_LOW_TEXT, 0, data, now, t, &lv);
      resolve_text(out, "L", lv.dynamic_color);
      return true;
    }
    // PRIM_CURRENT_TEMP_TEXT/PRIM_WEATHER_CONDITION_ICON: content 32/76's
    // current-temp text and weather icon. Legacy code forced these (and
    // content 76's high/low halves) to all share one weather-*condition*
    // color rather than each value's own natural gradient -- exactly the
    // kind of compromise this whole resolver design rejects (each
    // primitive resolves its own best color, per its own source, "down to
    // primitives" -- see IMPLEMENTATION_NOTES.md's Phase 3 update). Here,
    // current temp gets its own current_temp_color (same source as
    // PRIM_CURRENT_TEMP_ONLY_TEXT), and the icon gets its own
    // weather_icon_color (Section 31: PKJS decided the icon's category, so
    // PKJS decides its color too) -- not cond_color. This means content 76
    // rendered through transport shows each value in its own color instead
    // of one shared condition color, a deliberate, direction-endorsed
    // departure from the legacy behavior, not an oversight.
    case PRIM_CURRENT_TEMP_TEXT: {
      char num[8];
      GColor c;
      if (data->current_temp_display[0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->current_temp_display);
        c = feature_value_color_from_packed(data->current_temp_color);
      } else {
        snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->weather_temp_c, data->temp_unit));
        c = feature_colors_seven_stop_gradient(data->weather_temp_c, -10, 40);
      }
      resolve_text(out, num, c);
      return true;
    }
    case PRIM_WEATHER_CONDITION_ICON: {
      uint8_t category;
      GColor c;
      if (data->current_temp_display[0] != '\0') {
        category = data->weather_icon_category;
        c = feature_value_color_from_packed(data->weather_icon_color);
      } else {
        category = feature_icons_weather_category(data->weather_condition, data->cloud_cover_pct);
        c = data->valid
          ? feature_colors_weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct)
          : GColorLightGray;
      }
      bool night = !sky_layer_is_bright(data, now); // day/night art for sun/partly-cloudy
      resolve_icon(out, 14, category, night, c);
      return true;
    }

    // ---- health/status cluster (feature_value_health_compute) ----------
    // All watch-runtime (Section 2.1): read straight from Pebble's own
    // services, no PKJS round-trip. Each icon/text pair below is used by
    // exactly one canonical color rule across every content id currently
    // enabled for transport (content 1/2/3/10/17/20/78/108/109/39/40/41/
    // 42/43) -- see IMPLEMENTATION_NOTES.md's Phase 3 update for the
    // per-content color audit this was checked against before enabling.
    case PRIM_HEART_RATE_ICON:
    case PRIM_HEART_RATE_VALUE: {
      int bpm = feature_health_peek_current_bpm();
      GColor c = (bpm > 0) ? feature_colors_heart_rate_gradient(bpm) : GColorLightGray;
      if (id == PRIM_HEART_RATE_ICON) { resolve_icon(out, 1, 0, false, c); return true; }
      char vbuf[6];
      snprintf(vbuf, sizeof(vbuf), bpm > 0 ? "%d" : "N/A", bpm);
      resolve_text(out, vbuf, c);
      return true;
    }
    case PRIM_STEPS_ICON:
    case PRIM_STEPS_VALUE:
    case PRIM_STEP_GOAL_PCT: {
      HealthValue steps = feature_health_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      GColor icon_c = feature_colors_red_green_gradient((uint8_t)(pct > 100 ? 100 : pct));
      if (id == PRIM_STEPS_ICON) { resolve_icon(out, 2, 0, false, icon_c); return true; }
      if (id == PRIM_STEPS_VALUE) {
        char vbuf[8];
        snprintf(vbuf, sizeof(vbuf), "%d", (int)steps);
        resolve_text(out, vbuf, icon_c);
        return true;
      }
      // PRIM_STEP_GOAL_PCT: content 3's own pct can exceed 100% (shown
      // uncapped up to 999), unlike the icon color above which clamps at
      // 100 -- same as feature_value_health_compute()'s case 3.
      if (pct > 999) pct = 999;
      char pbuf[6];
      snprintf(pbuf, sizeof(pbuf), "%d%%", (int)pct);
      resolve_text(out, pbuf, feature_colors_red_green_gradient((uint8_t)(pct > 100 ? 100 : pct)));
      return true;
    }
    case PRIM_BATTERY_ICON:
    case PRIM_BATTERY_PCT_TEXT:
    case PRIM_BATTERY_LOGO_ICON: {
      BatteryChargeState bs = battery_state_service_peek();
      GColor c = bs.is_charging ? GColorGreen : feature_colors_red_green_gradient((uint8_t)bs.charge_percent);
      if (id == PRIM_BATTERY_LOGO_ICON) { resolve_icon(out, 12, bs.charge_percent, bs.is_charging, c); return true; }
      if (id == PRIM_BATTERY_ICON) { resolve_icon(out, 3, bs.charge_percent, bs.is_charging, c); return true; }
      char pbuf[8];
      snprintf(pbuf, sizeof(pbuf), bs.is_charging ? "%d%%+" : "%d%%", bs.charge_percent);
      resolve_text(out, pbuf, c);
      return true;
    }
    case PRIM_BLUETOOTH_ICON:
    case PRIM_BLUETOOTH_STATUS_TEXT: {
      // Always its own dynamic color, ignoring color_mode entirely -- see
      // feature_value_health_compute()'s case 20/78 comment; hence
      // force_color rather than leaving this to the usual dispatch.
      bool connected = connection_service_peek_pebble_app_connection();
      GColor c = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      if (id == PRIM_BLUETOOTH_ICON) { resolve_icon(out, 13, 0, false, c); }
      else { resolve_text(out, connected ? "Connected" : "No phone", c); }
      out->force_color = true;
      return true;
    }
    case PRIM_QUIET_TIME_ICON:
    case PRIM_QUIET_TIME_STATUS_TEXT: {
      bool active = quiet_time_is_active();
      GColor c = active ? GColorRed : GColorWhite;
      if (id == PRIM_QUIET_TIME_ICON) { resolve_icon(out, 29, 0, false, c); return true; }
      resolve_text(out, active ? "ON" : "OFF", c);
      return true;
    }
    case PRIM_SLEEP_TOTAL_ICON:
    case PRIM_SLEEP_TOTAL_TEXT:
    case PRIM_SLEEP_RESTFUL_ICON:
    case PRIM_SLEEP_RESTFUL_TEXT: {
      bool restful = (id == PRIM_SLEEP_RESTFUL_ICON || id == PRIM_SLEEP_RESTFUL_TEXT);
      HealthMetric metric = restful ? HealthMetricSleepRestfulSeconds : HealthMetricSleepSeconds;
      int32_t range_secs = restful ? 3 * 3600 : 9 * 3600;
      uint8_t icon_kind = restful ? 22 : 21;
      bool is_icon = (id == PRIM_SLEEP_TOTAL_ICON || id == PRIM_SLEEP_RESTFUL_ICON);
      if (feature_health_metric_available(metric)) {
        HealthValue secs = feature_health_sum_today(metric);
        GColor c = feature_colors_seven_stop_gradient_reversed((int32_t)secs, 0, range_secs);
        if (is_icon) { resolve_icon(out, icon_kind, 0, false, c); return true; }
        char dbuf[8];
        feature_health_format_duration_hm(dbuf, sizeof(dbuf), (int32_t)secs);
        resolve_text(out, dbuf, c);
        return true;
      }
      // N/A: always gray, bypassing color_mode entirely -- see
      // feature_value_health_compute()'s case 39/40 comment.
      if (is_icon) { resolve_icon(out, icon_kind, 0, false, GColorLightGray); }
      else { resolve_text(out, "N/A", GColorLightGray); }
      out->force_color = true;
      return true;
    }
    case PRIM_SLEEP_QUALITY_ICON:
    case PRIM_SLEEP_QUALITY_PCT: {
      if (feature_health_metric_available(HealthMetricSleepSeconds)) {
        HealthValue total = feature_health_sum_today(HealthMetricSleepSeconds);
        HealthValue restful = feature_health_sum_today(HealthMetricSleepRestfulSeconds);
        int pct = (total > 0) ? (int)((restful * 100) / total) : 0;
        if (pct > 100) pct = 100;
        GColor c = feature_colors_red_green_gradient((uint8_t)pct);
        if (id == PRIM_SLEEP_QUALITY_ICON) { resolve_icon(out, 20, 0, false, c); return true; }
        char pbuf[6];
        snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
        resolve_text(out, pbuf, c);
        return true;
      }
      if (id == PRIM_SLEEP_QUALITY_ICON) { resolve_icon(out, 20, 0, false, GColorLightGray); }
      else { resolve_text(out, "N/A", GColorLightGray); }
      out->force_color = true;
      return true;
    }
    case PRIM_BED_TIME_ICON:
    case PRIM_BED_TIME_TEXT:
    case PRIM_WAKE_TIME_ICON:
    case PRIM_WAKE_TIME_TEXT: {
      bool wake = (id == PRIM_WAKE_TIME_ICON || id == PRIM_WAKE_TIME_TEXT);
      bool is_icon = (id == PRIM_BED_TIME_ICON || id == PRIM_WAKE_TIME_ICON);
      FeatureSleepSpan span = feature_health_get_sleep_span();
      uint8_t icon_kind = wake ? 19 : 18;
      if (span.found) {
        time_t event = wake ? span.latest_end : span.earliest_start;
        GColor c = feature_colors_daynight_gradient(event, data->sun_rise, data->sun_set);
        if (is_icon) { resolve_icon(out, icon_kind, 0, false, c); return true; }
        struct tm *et = localtime(&event);
        char tbuf[8];
        strftime(tbuf, sizeof(tbuf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", et);
        resolve_text(out, tbuf, c);
        return true;
      }
      if (is_icon) { resolve_icon(out, icon_kind, 0, false, GColorLightGray); }
      else { resolve_text(out, "N/A", GColorLightGray); }
      out->force_color = true;
      return true;
    }

    // ---- weather singles (feature_value_weather.c cases 73/74/75/77) ---
    // Each is a distinct primitive ID from content 4/32/76's *_TEXT
    // primitives (no color-ownership conflict -- see
    // IMPLEMENTATION_NOTES.md), so these are safe to resolve independently.
    case PRIM_CURRENT_TEMP_ONLY_TEXT:
    case PRIM_TEMP_HIGH_ONLY_TEXT:
    case PRIM_TEMP_LOW_ONLY_TEXT:
    case PRIM_FEELS_LIKE_TEXT: {
      char num[8];
      GColor dyn;
      const char *prefix = "";
      if (id == PRIM_CURRENT_TEMP_ONLY_TEXT) {
        if (data->current_temp_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->current_temp_display);
          dyn = feature_value_color_from_packed(data->current_temp_color);
        } else {
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->weather_temp_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(data->weather_temp_c, -10, 40);
        }
      } else if (id == PRIM_TEMP_HIGH_ONLY_TEXT) {
        prefix = "H ";
        if (data->temp_high_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->temp_high_display);
          dyn = feature_value_color_from_packed(data->temp_high_color);
        } else {
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->temp_high_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(data->temp_high_c, -10, 40);
        }
      } else if (id == PRIM_TEMP_LOW_ONLY_TEXT) {
        prefix = "L ";
        if (data->temp_low_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->temp_low_display);
          dyn = feature_value_color_from_packed(data->temp_low_color);
        } else {
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->temp_low_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(data->temp_low_c, -10, 40);
        }
      } else { // PRIM_FEELS_LIKE_TEXT
        prefix = "FL ";
        if (data->feels_like_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->feels_like_display);
          dyn = feature_value_color_from_packed(data->feels_like_color);
        } else {
          int16_t temp_c = feature_rules_apparent_temp_c(data->weather_temp_c, data->wind_speed_kmh, data->humidity_pct);
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(temp_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(temp_c, -10, 40);
        }
      }
      char full[16];
      snprintf(full, sizeof(full), "%s%s", prefix, num);
      resolve_text(out, full, dyn);
      return true;
    }

    // ---- timezone cluster (feature_value_timezone_compute) --------------
    // aux_in is the zone index (feature_timezone_get()'s own index space,
    // 0-18) rather than a raw UTC offset -- see primitive_resolver.h's own
    // comment on why: reusing feature_timezone_get()/
    // feature_timezone_current_offset_min() gets DST handling for free,
    // which a bare minutes-offset parameter sent once at content-selection
    // time could never track correctly across a DST transition. The watch
    // computes the zone's local time from that index and its own clock, so
    // the watch decides the color too (feature_timezone_daylight_color()),
    // based on the *computed* local hour -- same color-ownership principle
    // as the rest of the date/time cluster, just parametrized.
    case PRIM_TIMEZONE_ABBR:
    case PRIM_TIMEZONE_TIME:
    case PRIM_TIMEZONE_AMPM: {
      const TimezoneInfo *tz = feature_timezone_get(aux_in);
      if (!tz) return false; // out-of-range zone index -- never trust a bad wire value
      int16_t offset_min = feature_timezone_current_offset_min(tz, now);
      time_t local_time = now + (int32_t)offset_min * 60;
      int32_t local_secs_of_day = ((local_time % 86400) + 86400) % 86400;
      int local_hour24 = (int)(local_secs_of_day / 3600);
      int local_min = (int)((local_secs_of_day % 3600) / 60);
      GColor c = feature_timezone_daylight_color(local_hour24);
      if (id == PRIM_TIMEZONE_ABBR) { resolve_text(out, tz->abbr, c); return true; }
      if (id == PRIM_TIMEZONE_AMPM) {
        // The transmitted sequence is fixed at content-selection time and
        // can't vary with clock_is_24h_style() (a runtime, watch-only
        // setting -- Section 2.1, and the same "sequence can't react to a
        // setting that changes later" constraint content 4's color_mode
        // already has). So this always resolves, but resolves to an empty,
        // zero-width primitive in 24h style rather than showing "AM"/"PM"
        // when nothing should be there -- letting one fixed sequence
        // ([ABBR, SPACE, TIME, SPACE, AMPM]) work correctly either way.
        resolve_text(out, clock_is_24h_style() ? "" : (local_hour24 < 12 ? "AM" : "PM"), c);
        return true;
      }
      char tbuf[8];
      if (clock_is_24h_style()) {
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", local_hour24, local_min);
      } else {
        int hour12 = local_hour24 % 12; if (hour12 == 0) hour12 = 12;
        snprintf(tbuf, sizeof(tbuf), "%d:%02d", hour12, local_min);
      }
      resolve_text(out, tbuf, c);
      return true;
    }

    default:
      return false; // not covered yet -- caller falls back to the legacy content path
  }
}
