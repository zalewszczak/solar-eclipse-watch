#include <pebble.h>
#include "feature_value_time.h"
#include "feature_value_helpers.h"
#include "feature_slot.h"
#include "eclipse_data.h"
#include "feature_colors.h"
#include "feature_rules.h"
#include "feature_health.h"
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void __attribute__((noinline)) feature_value_sky_compute(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                               uint8_t color_mode, GColor main_color, GColor accent_color, time_t now) {
  char buf[24];

  switch (content) {
    case 11: { // Moon phase -- icon + short name, no natural "value" to grade -- always white
      snprintf(buf, sizeof(buf), "%s", celestial_moon_phase_short_name(data->moon_phase_pct, data->moon_waxing));
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
        snprintf(buf, sizeof(buf), "Rings %d%%", data->saturn_ring_open_pct);
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
      } else {
        snprintf(buf, sizeof(buf), "Kp %d.%d", data->aurora_kp_x10 / 10, data->aurora_kp_x10 % 10);
        c = feature_value_resolve_flat_color(color_mode, feature_colors_white_to_red_gradient(data->aurora_kp_x10), main_color, accent_color);
      }
      feature_value_slot_set(slot, 26, buf, c);
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

// ---- combo cluster: multi-icon/multi-value content (97-102, 109-115) --
//
// Per request, these share ONE flat color (always main_color, not
// accent -- there's no single sensible "accent" reading across a
// multi-icon combo) for mono/accent/Pill modes, and only split into
// independently-gradient-colored segments under "color" mode (3).
