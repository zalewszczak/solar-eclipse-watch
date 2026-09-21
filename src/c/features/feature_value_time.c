#include <pebble.h>
#include "./feature_value_time.h"
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
#include "../rendering/background/celestial_ephemeris.h"
#include "../rendering/background/weather_layer.h"
#include "../input/input.h"
#include "../fonts/font_lookup.h"
#include "./feature_timezone.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void __attribute__((noinline)) feature_value_date_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                uint8_t color_mode, GColor main_color, GColor accent_color, time_t now, struct tm *t) {
  char buf[24];
  GColor dyn = main_color;

  switch (content) {
    case 12: { // short date, atomically decomposed (Phase 8): weekday(mixed), space, day
      char day_buf[4], num_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      snprintf(num_buf, sizeof(num_buf), "%d", t->tm_mday);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 3;
      feature_value_set_text_segment(slot, 0, day_buf, c);
      feature_value_set_text_segment(slot, 1, " ", c);
      feature_value_set_text_segment(slot, 2, num_buf, c);
      return;
    }
    case 18: { // digital time ("Time") -- day/night gradient off the actual sunrise/sunset
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
      dyn = feature_colors_daynight_gradient(now, data->sun_rise, data->sun_set);
      break;
    }
    case 19: { // week number, atomically decomposed (Phase 8): "WK" label, space, number
      char wk_buf[4];
      strftime(wk_buf, sizeof(wk_buf), "%V", t);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_seven_stop_gradient(atoi(wk_buf), 1, 52), main_color, accent_color);
      slot->segment_count = 3;
      feature_value_set_text_segment(slot, 0, "WK", c);
      feature_value_set_text_segment(slot, 1, " ", c);
      feature_value_set_text_segment(slot, 2, wk_buf, c);
      return;
    }
    case 21: { // month + day, atomically decomposed (Phase 8): month(upper), space, day
      char mon_buf[4], num_buf[4];
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      feature_rules_to_upper_str(mon_buf);
      snprintf(num_buf, sizeof(num_buf), "%d", t->tm_mday);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 3;
      feature_value_set_text_segment(slot, 0, mon_buf, c);
      feature_value_set_text_segment(slot, 1, " ", c);
      feature_value_set_text_segment(slot, 2, num_buf, c);
      return;
    }
    case 22: snprintf(buf, sizeof(buf), "%d", t->tm_mday); dyn = feature_colors_seven_stop_gradient(t->tm_mday, 1, 31); break;
    case 23: strftime(buf, sizeof(buf), "%a", t); feature_rules_to_upper_str(buf); dyn = feature_colors_seven_stop_gradient(t->tm_wday, 0, 6); break;
    case 24: strftime(buf, sizeof(buf), "%A", t); dyn = feature_colors_seven_stop_gradient(t->tm_wday, 0, 6); break;
    case 25: strftime(buf, sizeof(buf), "%b", t); feature_rules_to_upper_str(buf); dyn = feature_colors_seven_stop_gradient(t->tm_mon, 0, 11); break;
    case 26: strftime(buf, sizeof(buf), "%B", t); dyn = feature_colors_seven_stop_gradient(t->tm_mon, 0, 11); break;
    case 27: { // day/month, atomically decomposed (Phase 8): day, slash, month
      char day_buf[4], mon_buf[4];
      snprintf(day_buf, sizeof(day_buf), "%d", t->tm_mday);
      snprintf(mon_buf, sizeof(mon_buf), "%d", t->tm_mon + 1);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 3;
      feature_value_set_text_segment(slot, 0, day_buf, c);
      feature_value_set_text_segment(slot, 1, "/", c);
      feature_value_set_text_segment(slot, 2, mon_buf, c);
      return;
    }
    case 28: { // month/day, atomically decomposed (Phase 8): month, slash, day
      char day_buf[4], mon_buf[4];
      snprintf(mon_buf, sizeof(mon_buf), "%d", t->tm_mon + 1);
      snprintf(day_buf, sizeof(day_buf), "%d", t->tm_mday);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 3;
      feature_value_set_text_segment(slot, 0, mon_buf, c);
      feature_value_set_text_segment(slot, 1, "/", c);
      feature_value_set_text_segment(slot, 2, day_buf, c);
      return;
    }
    case 29: { // day/month/year, atomically decomposed (Phase 8)
      char day_buf[4], mon_buf[4], yr_buf[6];
      snprintf(day_buf, sizeof(day_buf), "%d", t->tm_mday);
      snprintf(mon_buf, sizeof(mon_buf), "%d", t->tm_mon + 1);
      snprintf(yr_buf, sizeof(yr_buf), "%d", t->tm_year + 1900);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 5;
      feature_value_set_text_segment(slot, 0, day_buf, c);
      feature_value_set_text_segment(slot, 1, "/", c);
      feature_value_set_text_segment(slot, 2, mon_buf, c);
      feature_value_set_text_segment(slot, 3, "/", c);
      feature_value_set_text_segment(slot, 4, yr_buf, c);
      return;
    }
    case 30: { // month/day/year(short), atomically decomposed (Phase 8)
      char day_buf[4], mon_buf[4], yr_buf[4];
      snprintf(mon_buf, sizeof(mon_buf), "%d", t->tm_mon + 1);
      snprintf(day_buf, sizeof(day_buf), "%d", t->tm_mday);
      snprintf(yr_buf, sizeof(yr_buf), "%02d", (t->tm_year + 1900) % 100);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 5;
      feature_value_set_text_segment(slot, 0, mon_buf, c);
      feature_value_set_text_segment(slot, 1, "/", c);
      feature_value_set_text_segment(slot, 2, day_buf, c);
      feature_value_set_text_segment(slot, 3, "/", c);
      feature_value_set_text_segment(slot, 4, yr_buf, c);
      return;
    }
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
    case 98: { // weekday + day/month, atomically decomposed (Phase 8): weekday(upper), space, day, slash, month
      char day_buf[4], daynum_buf[4], mon_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t); feature_rules_to_upper_str(day_buf);
      snprintf(daynum_buf, sizeof(daynum_buf), "%d", t->tm_mday);
      snprintf(mon_buf, sizeof(mon_buf), "%d", t->tm_mon + 1);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 5;
      feature_value_set_text_segment(slot, 0, day_buf, c);
      feature_value_set_text_segment(slot, 1, " ", c);
      feature_value_set_text_segment(slot, 2, daynum_buf, c);
      feature_value_set_text_segment(slot, 3, "/", c);
      feature_value_set_text_segment(slot, 4, mon_buf, c);
      return;
    }
    case 99: { // weekday + month/day, atomically decomposed (Phase 8): weekday(upper), space, month, slash, day
      char day_buf[4], daynum_buf[4], mon_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t); feature_rules_to_upper_str(day_buf);
      snprintf(mon_buf, sizeof(mon_buf), "%d", t->tm_mon + 1);
      snprintf(daynum_buf, sizeof(daynum_buf), "%d", t->tm_mday);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 5;
      feature_value_set_text_segment(slot, 0, day_buf, c);
      feature_value_set_text_segment(slot, 1, " ", c);
      feature_value_set_text_segment(slot, 2, mon_buf, c);
      feature_value_set_text_segment(slot, 3, "/", c);
      feature_value_set_text_segment(slot, 4, daynum_buf, c);
      return;
    }
    case 106: { // "long date" + week number, atomically decomposed (Phase 8):
                // weekday(mixed), space, day, space, month(mixed), space, "WK" label, week number
      char day_buf[4], daynum_buf[4], mon_buf[4], wk_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      snprintf(daynum_buf, sizeof(daynum_buf), "%d", t->tm_mday);
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      strftime(wk_buf, sizeof(wk_buf), "%V", t);
      GColor c = feature_value_resolve_flat_color(color_mode, feature_colors_date_year_progress_gradient(t), main_color, accent_color);
      slot->segment_count = 8;
      feature_value_set_text_segment(slot, 0, day_buf, c);
      feature_value_set_text_segment(slot, 1, " ", c);
      feature_value_set_text_segment(slot, 2, daynum_buf, c);
      feature_value_set_text_segment(slot, 3, " ", c);
      feature_value_set_text_segment(slot, 4, mon_buf, c);
      feature_value_set_text_segment(slot, 5, " ", c);
      feature_value_set_text_segment(slot, 6, "WK", c);
      feature_value_set_text_segment(slot, 7, wk_buf, c);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
  slot->segment_count = 1;
  feature_value_set_text_segment(slot, 0, buf, feature_value_resolve_flat_color(color_mode, dyn, main_color, accent_color));
}

// timezone cluster

void __attribute__((noinline)) feature_value_timezone_compute(FeatureSlot *slot, uint8_t content, uint8_t color_mode,
                                    GColor main_color, GColor accent_color, time_t now) {
  const TimezoneInfo *tz = feature_timezone_get((uint8_t)(content - 44));
  int16_t offset_min = feature_timezone_current_offset_min(tz, now);
  time_t local_time = now + (int32_t)offset_min * 60;
  int32_t local_secs_of_day = ((local_time % 86400) + 86400) % 86400;
  int local_hour24 = (int)(local_secs_of_day / 3600);
  int local_min = (int)((local_secs_of_day % 3600) / 60);
  char time_buf[8];
  GColor c = feature_value_resolve_flat_color(color_mode, feature_timezone_daylight_color(local_hour24), main_color, accent_color);
  // Atomic decomposition (Phase 8): abbr + space + time, plus AM/PM as its
  // own primitive (reusing PRIM_AMPM, the same one content 86 uses) in 12h
  // style, instead of one composed "PST 07:45" string.
  uint8_t zone_idx = (uint8_t)(content - 44);
  if (clock_is_24h_style()) {
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", local_hour24, local_min);
    slot->segment_count = 3;
    feature_value_set_text_segment(slot, 0, tz->abbr, c);
    feature_value_set_text_segment(slot, 1, " ", c);
    feature_value_set_text_segment(slot, 2, time_buf, c);
    slot->segments[0].icon_extra = zone_idx; // aux, carried through even though these are text segments
    slot->segments[2].icon_extra = zone_idx;
  } else {
    int hour12 = local_hour24 % 12; if (hour12 == 0) hour12 = 12;
    snprintf(time_buf, sizeof(time_buf), "%d:%02d", hour12, local_min);
    slot->segment_count = 4;
    feature_value_set_text_segment(slot, 0, tz->abbr, c);
    feature_value_set_text_segment(slot, 1, " ", c);
    feature_value_set_text_segment(slot, 2, time_buf, c);
    feature_value_set_text_segment(slot, 3, local_hour24 < 12 ? "AM" : "PM", c);
    slot->segments[0].icon_extra = zone_idx;
    slot->segments[2].icon_extra = zone_idx;
  }
}

// sky/astronomy cluster: moon phase, location, sunrise/sunset,


void __attribute__((noinline)) feature_value_sky_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                               uint8_t color_mode, GColor main_color, GColor accent_color, time_t now) {
  char buf[24];

  switch (content) {
    case 11: { // Moon phase -- icon + short name, no natural "value" to grade -- always white
      if (data->moon_phase_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->moon_phase_display);
      } else {
        snprintf(buf, sizeof(buf), "%s", celestial_moon_phase_short_name(data->moon_phase_pct, data->moon_waxing));
      }
      GColor c = feature_value_resolve_flat_color(color_mode, GColorWhite, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 4, c);
      slot->segments[0].icon_extra = data->moon_phase_pct;
      slot->segments[0].icon_flag = data->moon_waxing;
      feature_value_set_text_segment(slot, 1, buf, c);
      return;
    }
    case 13: { // location name
      snprintf(buf, sizeof(buf), "%s", data->location_name[0] != '\0' ? data->location_name : "Unknown");
      GColor c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      feature_value_slot_set(slot, 8, buf, c);
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
      GColor c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 11, c);
      slot->segments[0].icon_flag = is_sunrise;
      feature_value_set_text_segment(slot, 1, buf, c);
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
        c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      }
      feature_value_slot_set(slot, 23, buf, c);
      return;
    }
    case 80: { // active meteor shower name, if any -- grayscale by intensity (more meteors = whiter)
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else if (data->meteor_display[0] != '\0') {
        // Phase 7: PKJS already replicates the active/inactive check below
        // (meteor_intensity > 0 && a real name) into a single "name or
        // N/A" string -- see weather-normalize.js's sky-display.js.
        snprintf(buf, sizeof(buf), "%s", data->meteor_display);
        c = feature_value_color_from_packed(data->meteor_color);
      } else if (data->meteor_intensity > 0 && data->meteor_shower_name[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->meteor_shower_name);
        c = feature_value_resolve_flat_color(color_mode, feature_colors_meteor_intensity_gradient(data->meteor_intensity), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, c);
      return;
    }
    case 81: { // Saturn's current ring-opening angle
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else {
        if (data->saturn_rings_display[0] != '\0') {
          snprintf(buf, sizeof(buf), "%s", data->saturn_rings_display);
        } else {
          snprintf(buf, sizeof(buf), "Rings %d%%", data->saturn_ring_open_pct);
        }
        c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      }
      feature_value_slot_set(slot, 24, buf, c);
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
          c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
        } else {
          snprintf(buf, sizeof(buf), "N/A");
          c = GColorLightGray;
        }
      }
      slot->segment_count = 1;
      feature_value_set_text_segment(slot, 0, buf, c);
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
        c = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      feature_value_slot_set(slot, 25, buf, c);
      return;
    }
    case 84: { // current planetary Kp index
      GColor c;
      if (data->aurora_error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->aurora_error_code);
        c = GColorRed;
      } else if (data->aurora_kp_display[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->aurora_kp_display);
        c = feature_value_resolve_flat_color(color_mode, feature_value_color_from_packed(data->aurora_kp_color), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "Kp %d.%d", data->aurora_kp_x10 / 10, data->aurora_kp_x10 % 10);
        c = feature_value_resolve_flat_color(color_mode, feature_colors_white_to_red_gradient(data->aurora_kp_x10), main_color, accent_color);
      }
      feature_value_slot_set(slot, 26, buf, c);
      return;
    }
    case 85: { // Compass -- active (real heading) for 15s after a shake, then asleep until the next one.
               // Needs 2 colors at once (north arrow vs the other 3) rather than one flat color
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
      GColor flat = feature_value_resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      feature_value_set_icon_segment(slot, 0, 27, flat);
      slot->segments[0].icon_extra = (int16_t)(input_compass_feature_heading_deg() % 360);
      slot->segments[0].icon_flag = asleep;
      if (color_mode == 3) {
        slot->segments[0].color = accent_color;  // north arrow
        slot->segments[0].color2 = main_color;   // other 3 arrows
      }
      feature_value_set_text_segment(slot, 1, buf, flat);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// combo cluster: multi-icon/multi-value content (100-105, 112-118)
