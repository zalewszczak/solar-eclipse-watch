#include "./primitive_resolver.h"
#include "../feature_colors.h"
#include "../feature_rules.h"
#include "../feature_color_helpers.h"
#include "../feature_health.h"
#include "../feature_icons.h"
#include "../feature_timezone.h"
#include "../feature_support.h"
#include "../../data/eclipse_ui.h"
#include "../../rendering/background/sky_layer.h"
#include "../../input/input.h"
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

static void resolve_punct(PrimitiveResolved *out, const char *text, GColor color) {
  out->kind = PRIMITIVE_KIND_PUNCT;
  snprintf(out->text, sizeof(out->text), "%s", text);
  out->dynamic_color = color;
}

// Removes `prefix` from the start and `suffix` from the end of the resolved
// text when present. Used to turn a legacy "legend + value + unit" primitive
// into the bare value ("UV6" -> "6", "1013 hPa" -> "1013"); fallbacks that do
// not carry the legend ("N/A", "ERR 2") are left alone.
static void strip_text(PrimitiveResolved *out, const char *prefix, const char *suffix) {
  size_t len = strlen(out->text);
  size_t pl = prefix ? strlen(prefix) : 0;
  if (pl && len >= pl && strncmp(out->text, prefix, pl) == 0) {
    memmove(out->text, out->text + pl, len - pl + 1);
    len -= pl;
  }
  size_t sl = suffix ? strlen(suffix) : 0;
  if (sl && len >= sl && strcmp(out->text + len - sl, suffix) == 0) out->text[len - sl] = '\0';
}

// Custom-feature builder additions: punctuation, bare values, unit symbols.
// Returns true when `id` is one of them (and *ok says whether it resolved).
static bool resolve_builder_extra(PrimitiveId id, uint8_t aux_in, const EclipseData *data, time_t now,
                                  struct tm *t, PrimitiveResolved *out, bool *ok) {
  *ok = true;
  switch (id) {
    case PRIM_PAREN_OPEN: resolve_punct(out, "(", feature_colors_date_year_progress_gradient(t)); return true;
    case PRIM_PAREN_CLOSE: resolve_punct(out, ")", feature_colors_date_year_progress_gradient(t)); return true;
    case PRIM_DOT: resolve_punct(out, ".", feature_colors_date_year_progress_gradient(t)); return true;
    case PRIM_COMMA: resolve_punct(out, ",", feature_colors_date_year_progress_gradient(t)); return true;

    case PRIM_FEELS_LIKE_VALUE:
      *ok = primitive_resolver_resolve(PRIM_FEELS_LIKE_TEXT, 0, data, now, t, out);
      if (*ok) strip_text(out, "FL ", NULL);
      return true;
    case PRIM_UV_DAILY_VALUE:
      *ok = primitive_resolver_resolve(PRIM_UV_DAILY_TEXT, 0, data, now, t, out);
      if (*ok) strip_text(out, "UV", NULL);
      return true;
    case PRIM_UV_CURRENT_VALUE:
      *ok = primitive_resolver_resolve(PRIM_UV_CURRENT_TEXT, 0, data, now, t, out);
      if (*ok) strip_text(out, "UV", NULL);
      return true;
    case PRIM_PRESSURE_VALUE:
      *ok = primitive_resolver_resolve(PRIM_PRESSURE_TEXT, 0, data, now, t, out);
      if (*ok) strip_text(out, NULL, " hPa");
      return true;
    case PRIM_AQI_VALUE:
      *ok = primitive_resolver_resolve(PRIM_AQI_TEXT, 0, data, now, t, out);
      if (*ok) strip_text(out, "AQI ", NULL);
      return true;
    case PRIM_ALTITUDE_VALUE:
      *ok = primitive_resolver_resolve(PRIM_ALTITUDE_TEXT, 0, data, now, t, out);
      if (*ok) { strip_text(out, NULL, "ft"); strip_text(out, NULL, "m"); }
      return true;
    case PRIM_PLANETS_VISIBLE_VALUE:
      *ok = primitive_resolver_resolve(PRIM_PLANETS_VISIBLE_COUNT, 0, data, now, t, out);
      if (*ok) { strip_text(out, NULL, " planets"); strip_text(out, NULL, " planet"); }
      return true;
    case PRIM_SATURN_RINGS_VALUE:
      *ok = primitive_resolver_resolve(PRIM_SATURN_RINGS_PCT, 0, data, now, t, out);
      if (*ok) strip_text(out, "Rings ", NULL);
      return true;
    case PRIM_AURORA_KP_VALUE:
      *ok = primitive_resolver_resolve(PRIM_AURORA_KP, 0, data, now, t, out);
      if (*ok) strip_text(out, "Kp ", NULL);
      return true;
    case PRIM_NEXT_PLANET_NAME:
    case PRIM_NEXT_PLANET_TIME: {
      *ok = primitive_resolver_resolve(PRIM_NEXT_PLANET_RISE, 0, data, now, t, out);
      if (!*ok) return true;
      char *space = strchr(out->text, ' '); // "VEN 18:32" -> name / time; "N/A", "ERR n" keep as they are
      if (space && strncmp(out->text, "ERR", 3) != 0) {
        if (id == PRIM_NEXT_PLANET_NAME) *space = '\0';
        else memmove(out->text, space + 1, strlen(space + 1) + 1);
      }
      return true;
    }

    case PRIM_TEMP_UNIT_TEXT: {
      static const char *const UNITS[3] = { "C", "F", "K" };
      resolve_punct(out, UNITS[data->temp_unit < 3 ? data->temp_unit : 0], GColorWhite);
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_WIND_UNIT_TEXT: {
      static const char *const UNITS[4] = { "km/h", "mph", "m/s", "kn" };
      resolve_punct(out, UNITS[data->wind_speed_unit < 4 ? data->wind_speed_unit : 0], GColorWhite);
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_PRESSURE_UNIT_TEXT:
      resolve_punct(out, "hPa", GColorWhite);
      out->dynamic_is_main = true;
      return true;
    case PRIM_ALTITUDE_UNIT_TEXT:
      resolve_punct(out, data->altitude_unit == 1 ? "ft" : "m", GColorWhite);
      out->dynamic_is_main = true;
      return true;
    case PRIM_PLUGIN_TEXT: {
      if (aux_in >= PLUGIN_SLOT_COUNT) return false;
      const char *txt = data->plugin_text[aux_in];
      resolve_text(out, txt[0] ? txt : "--", GColorWhite);
      out->dynamic_is_main = true;
      return true;
    }
    default:
      (void)aux_in;
      return false;
  }
}

bool primitive_resolver_resolve(PrimitiveId id, uint8_t aux_in, const EclipseData *data, time_t now, struct tm *t, PrimitiveResolved *out) {
  // `now` is used by PRIM_WEATHER_CONDITION_ICON's day/night check below;
  // every other case only needs `t` (already-broken-down local time).
  memset(out, 0, sizeof(*out));
  {
    bool extra_ok = false;
    if (resolve_builder_extra(id, aux_in, data, now, t, out, &extra_ok)) return extra_ok;
  }
  // Every currently-covered date/time primitive shares this one gradient
  // (the same one legacy date computation() uses for every content id
  // enabled for transport so far -- see primitive_transport.h) -- computed
  // once here rather than per-case, since date/time cases below all want
  // the same value.
  GColor date_c = feature_colors_date_year_progress_gradient(t);
  // Sized for the longest strftime/snprintf result written into it below --
  // "11:59:59 PM" (PRIM_TIME_HMS, 12h style) is the longest at 11 chars + NUL.
  char buf[16];

  switch (id) {
    case PRIM_SPACE:
      out->kind = PRIMITIVE_KIND_SPACE;
      out->dynamic_color = date_c;
      return true;
    case PRIM_SLASH:
      out->kind = PRIMITIVE_KIND_SLASH;
      out->dynamic_color = date_c;
      return true;
    case PRIM_DASH:
      // Universal "-" spacer (report Section 4/19). Renderer/layout already
      // handle PRIMITIVE_KIND_DASH; only the resolver case was missing.
      out->kind = PRIMITIVE_KIND_DASH;
      out->dynamic_color = date_c;
      return true;

    // ---- clock cluster (legacy date computation's plain time/second
    // cases, content 18/63-72/86) -- all watch-runtime, read straight off
    // `t`/`now`. Color rules copied 1:1 from the legacy switch.
    case PRIM_TIME_HM:
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
      resolve_text(out, buf, feature_colors_daynight_gradient(now, data->sun_rise, data->sun_set));
      return true;
    case PRIM_TIME_HMS:
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S %p", t);
      resolve_text(out, buf, feature_colors_daynight_gradient(now, data->sun_rise, data->sun_set));
      return true;
    case PRIM_HOUR_24_PAD:
      snprintf(buf, sizeof(buf), "%02d", t->tm_hour);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_hour, 0, 23));
      return true;
    case PRIM_HOUR_24:
      snprintf(buf, sizeof(buf), "%d", t->tm_hour);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_hour, 0, 23));
      return true;
    case PRIM_HOUR_12: {
      int hour12 = t->tm_hour % 12; if (hour12 == 0) hour12 = 12;
      snprintf(buf, sizeof(buf), "%d", hour12);
      resolve_text(out, buf, feature_colors_linear_white_black(hour12, 1, 12));
      return true;
    }
    case PRIM_MINUTE:
      snprintf(buf, sizeof(buf), "%d", t->tm_min);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_min, 0, 59));
      return true;
    case PRIM_MINUTE_PAD:
      snprintf(buf, sizeof(buf), "%02d", t->tm_min);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_min, 0, 59));
      return true;
    case PRIM_SECOND:
      snprintf(buf, sizeof(buf), "%d", t->tm_sec);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_sec, 0, 59));
      return true;
    case PRIM_SECOND_PAD:
      snprintf(buf, sizeof(buf), "%02d", t->tm_sec);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_sec, 0, 59));
      return true;
    case PRIM_SECOND_TENS:
      snprintf(buf, sizeof(buf), "%d", t->tm_sec / 10);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_sec / 10, 0, 5));
      return true;
    case PRIM_SECOND_ONES:
      snprintf(buf, sizeof(buf), "%d", t->tm_sec % 10);
      resolve_text(out, buf, feature_colors_linear_white_black(t->tm_sec % 10, 0, 9));
      return true;
    case PRIM_AMPM:
      resolve_text(out, t->tm_hour < 12 ? "AM" : "PM", (t->tm_hour < 12) ? GColorBlack : GColorWhite);
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
      char ybuf[12];
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
        c = feature_color_from_packed(data->temp_high_color);
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
        c = feature_color_from_packed(data->temp_low_color);
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
        c = feature_color_from_packed(data->current_temp_color);
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
        c = feature_color_from_packed(data->weather_icon_color);
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

    // ---- weather cluster (primitive weather-value resolution, content
    // 5/6/7/8/9/14/15/34/35/36/37/38/87-97/107) -- PKJS-supplied display
    // string when present, watch-side fallback otherwise. Color rules
    // copied 1:1 from the legacy switch; every case is force_color-free
    // (each already resolves its own natural gradient, no flat/COLOR-mode
    // dispatch needed beyond the shared transport step).
    case PRIM_CURRENT_CONDITIONS_TEXT: {
      GColor c = data->valid
        ? feature_colors_weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct)
        : GColorLightGray;
      resolve_text(out, feature_weather_short_condition_text(data->weather_condition, data->cloud_cover_pct), c);
      return true;
    }
    case PRIM_UV_DAILY_TEXT: {
      char num[6];
      GColor c;
      if (data->uv_daily_display[0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->uv_daily_display);
        c = feature_color_from_packed(data->uv_daily_color);
      } else {
        uint8_t uv = data->uv_index_x10 / 10;
        snprintf(num, sizeof(num), "UV%d", uv);
        c = feature_colors_seven_stop_gradient(uv, 1, 13);
      }
      resolve_text(out, num, c);
      return true;
    }
    case PRIM_UV_CURRENT_TEXT: {
      char num[6];
      GColor c;
      if (data->uv_current_display[0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->uv_current_display);
        c = feature_color_from_packed(data->uv_current_color);
      } else {
        uint8_t uv = data->uv_index_current_x10 / 10;
        snprintf(num, sizeof(num), "UV%d", uv);
        c = feature_colors_seven_stop_gradient(uv, 1, 13);
      }
      resolve_text(out, num, c);
      return true;
    }
    case PRIM_RAIN_CHANCE_ICON:
    case PRIM_RAIN_CHANCE_TEXT: {
      char buf2[8];
      GColor c;
      if (data->rain_chance_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->rain_chance_display);
        c = feature_color_from_packed(data->rain_chance_color);
      } else {
        snprintf(buf2, sizeof(buf2), "%d%%", data->rain_chance_pct);
        c = feature_colors_white_to_turquoise_gradient(data->rain_chance_pct, 0, 100);
      }
      if (id == PRIM_RAIN_CHANCE_ICON) { resolve_icon(out, 5, 0, false, c); return true; }
      resolve_text(out, buf2, c);
      return true;
    }
    case PRIM_HUMIDITY_ICON:
    case PRIM_HUMIDITY_TEXT: {
      char buf2[8];
      GColor c;
      if (data->humidity_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->humidity_display);
        c = feature_color_from_packed(data->humidity_color);
      } else {
        snprintf(buf2, sizeof(buf2), "%d%%", data->humidity_pct);
        c = feature_colors_white_to_turquoise_gradient(data->humidity_pct, 0, 100);
      }
      if (id == PRIM_HUMIDITY_ICON) { resolve_icon(out, 6, 0, false, c); return true; }
      resolve_text(out, buf2, c);
      return true;
    }
    case PRIM_WIND_SPEED_ICON:
    case PRIM_WIND_SPEED_TEXT: {
      char buf2[8];
      GColor c;
      if (data->wind_speed_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->wind_speed_display);
        c = feature_color_from_packed(data->wind_speed_color);
      } else {
        snprintf(buf2, sizeof(buf2), "%d", feature_rules_convert_wind(data->wind_speed_kmh, data->wind_speed_unit));
        c = feature_colors_white_to_turquoise_gradient(data->wind_speed_kmh, 0, 60);
      }
      if (id == PRIM_WIND_SPEED_ICON) { resolve_icon(out, 7, 0, false, c); return true; }
      resolve_text(out, buf2, c);
      return true;
    }
    case PRIM_VISIBILITY_ICON:
    case PRIM_VISIBILITY_TEXT: {
      char buf2[8];
      GColor c;
      if (data->vis_score_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->vis_score_display);
        c = feature_color_from_packed(data->vis_score_color);
      } else {
        snprintf(buf2, sizeof(buf2), "%d%%", data->vis_score_pct);
        uint8_t equiv_cloud_pct = 100 - data->vis_score_pct;
        c = feature_colors_overcast_gray_gradient(equiv_cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : equiv_cloud_pct);
      }
      if (id == PRIM_VISIBILITY_ICON) { resolve_icon(out, 9, 0, false, c); return true; }
      resolve_text(out, buf2, c);
      return true;
    }
    case PRIM_CLOUD_COVER_ICON:
    case PRIM_CLOUD_COVER_TEXT: {
      char buf2[8];
      GColor c;
      if (data->cloud_cover_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->cloud_cover_display);
        c = feature_color_from_packed(data->cloud_cover_color);
      } else {
        snprintf(buf2, sizeof(buf2), "%d%%", data->cloud_cover_pct);
        c = feature_colors_overcast_gray_gradient(data->cloud_cover_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : data->cloud_cover_pct);
      }
      if (id == PRIM_CLOUD_COVER_ICON) { resolve_icon(out, 10, 0, false, c); return true; }
      resolve_text(out, buf2, c);
      return true;
    }
    case PRIM_PRESSURE_TEXT: {
      char buf2[16];
      GColor c;
      if (data->pressure_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->pressure_display);
        c = feature_color_from_packed(data->pressure_color);
      } else {
        snprintf(buf2, sizeof(buf2), "%d hPa", data->pressure_hpa);
        c = feature_colors_seven_stop_gradient(data->pressure_hpa, 970, 1050);
      }
      resolve_text(out, buf2, c);
      return true;
    }
    case PRIM_WIND_DIRECTION_TEXT: {
      static const char *COMPASS8[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
      int idx = ((data->wind_dir_deg + 22) / 45) % 8;
      if (idx < 0) idx += 8;
      // Legacy case 35 always uses main_color, not a graded value.
      resolve_text(out, COMPASS8[idx], GColorWhite);
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_AQI_TEXT: {
      char buf2[10];
      GColor c;
      if (data->aqi_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->aqi_display);
        c = feature_color_from_packed(data->aqi_color);
      } else {
        bool use_eu = (data->aqi_unit == 1);
        uint16_t aqi_value = use_eu ? data->aqi_eu : data->aqi_us;
        snprintf(buf2, sizeof(buf2), "AQI %d", aqi_value);
        c = feature_colors_seven_stop_gradient(aqi_value, 0, use_eu ? 100 : 300);
      }
      resolve_text(out, buf2, c);
      return true;
    }
    case PRIM_DEW_POINT_TEXT: {
      // Legacy case 37 never has a dynamic color -- always the slot's flat
      // color_mode color (main_color here; primitive_transport.c resolves
      // the same way any dynamic_is_main primitive does).
      char num[8];
      if (data->dew_point_display[0] != '\0') { snprintf(num, sizeof(num), "%s", data->dew_point_display); }
      else { snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->dew_point_c, data->temp_unit)); }
      resolve_text(out, num, GColorWhite);
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_ALTITUDE_ICON:
    case PRIM_ALTITUDE_TEXT: {
      char buf2[10];
      GColor c;
      if (data->altitude_m <= -32000) {
        snprintf(buf2, sizeof(buf2), "N/A");
        c = GColorLightGray;
        if (id == PRIM_ALTITUDE_ICON) { resolve_icon(out, 17, 0, false, c); } else { resolve_text(out, buf2, c); }
        out->force_color = true;
        return true;
      }
      if (data->altitude_display[0] != '\0') {
        snprintf(buf2, sizeof(buf2), "%s", data->altitude_display);
        c = feature_color_from_packed(data->altitude_color);
      } else {
        if (data->altitude_unit == 1) {
          int32_t feet = ((int32_t)data->altitude_m * 328) / 100;
          snprintf(buf2, sizeof(buf2), "%ldft", (long)feet);
        } else {
          snprintf(buf2, sizeof(buf2), "%dm", data->altitude_m);
        }
        c = feature_colors_altitude_gradient(data->altitude_m);
      }
      if (id == PRIM_ALTITUDE_ICON) { resolve_icon(out, 17, 0, false, c); } else { resolve_text(out, buf2, c); }
      return true;
    }
    case PRIM_FORECAST_OFFSET_ICON:
    case PRIM_FORECAST_TEMP_TEXT: {
      // aux_in carries the forecast index (0-8: 1-6h ahead, then 1-3 days
      // ahead) -- the one astronomy/weather primitive whose aux is a plain
      // index rather than a timezone slot. A custom sequence can therefore
      // pick any of the 9 forecast slots via the composer's zone-style aux
      // picker (labelled "Forecast slot" there, not "Zone").
      if (aux_in >= FORECAST_SLOT_COUNT) return false;
      if (id == PRIM_FORECAST_OFFSET_ICON && aux_in > 8) return false; // +7h..+12h have no icon art
      bool is_hour = aux_in <= 5 || aux_in >= 9;
      if (data->forecast_temp_c[aux_in] <= -128) {
        if (id == PRIM_FORECAST_OFFSET_ICON) { resolve_icon(out, 30, aux_in, false, GColorLightGray); }
        else { resolve_text(out, "N/A", GColorLightGray); }
        out->force_color = true;
        return true;
      }
      char num[12];
      GColor c;
      if (data->forecast_temp_display[aux_in][0] != '\0') {
        snprintf(num, sizeof(num), "%s", data->forecast_temp_display[aux_in]);
        c = feature_color_from_packed(data->forecast_temp_color[aux_in]);
      } else {
        snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->forecast_temp_c[aux_in], data->temp_unit));
        c = feature_colors_seven_stop_gradient(data->forecast_temp_c[aux_in], -10, 40);
      }
      if (id == PRIM_FORECAST_OFFSET_ICON) {
        resolve_icon(out, 30, aux_in, false, c);
      } else {
        resolve_text(out, num, c);
      }
      (void)is_hour; // the half-icon's glyph itself is aux-driven; no separate flag needed here
      return true;
    }
    case PRIM_WEATHER_REFRESH_ICON:
    case PRIM_WEATHER_LAST_UPDATE_LONG_TEXT:
    case PRIM_WEATHER_LAST_UPDATE_SHORT_TEXT: {
      char buf2[24];
      if (data->weather_last_update > 0) {
        struct tm *ut = localtime(&data->weather_last_update);
        char tb[8];
        strftime(tb, sizeof(tb), clock_is_24h_style() ? "%H:%M" : "%I:%M", ut);
        if (id == PRIM_WEATHER_LAST_UPDATE_LONG_TEXT) { snprintf(buf2, sizeof(buf2), "Last updated %s", tb); }
        else { snprintf(buf2, sizeof(buf2), "%s", tb); }
      } else {
        snprintf(buf2, sizeof(buf2), "N/A");
      }
      GColor c = feature_colors_weather_staleness_gradient(now, data->weather_last_update);
      if (id == PRIM_WEATHER_REFRESH_ICON) { resolve_icon(out, 28, 0, false, c); return true; }
      resolve_text(out, buf2, c);
      return true;
    }

    // ---- health/status cluster (primitive health-value resolution) ----------
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
      // 100 -- same as primitive health-value resolution()'s case 3.
      if (pct > 999) pct = 999;
      char pbuf[16];
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
      // primitive health-value resolution()'s case 20/78 comment; hence
      // force_color rather than leaving this to the usual dispatch.
      bool connected = connection_service_peek_pebble_app_connection();
      GColor c = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      // aux_flag = disconnected: the Full color art cannot be tinted red, so the
      // icon renderer crosses it out instead (feature_icons.c, case 13).
      if (id == PRIM_BLUETOOTH_ICON) { resolve_icon(out, 13, 0, !connected, c); }
      else { resolve_text(out, connected ? "Connected" : "No phone", c); }
      out->force_color = true;
      return true;
    }
    case PRIM_QUIET_TIME_ICON:
    case PRIM_QUIET_TIME_PLAIN_ICON:
    case PRIM_QUIET_TIME_STATUS_TEXT: {
      bool active = quiet_time_is_active();
      GColor c = active ? GColorRed : GColorWhite;
      // PRIM_QUIET_TIME_ICON shows state in its shape (crossed-out while
      // active) -- content 108/112/118. The old code always passed false
      // here, so a transported content 108 never showed the crossed-out
      // speaker its legacy path does. PRIM_QUIET_TIME_PLAIN_ICON is the
      // never-crossed-out variant (109/113 carry the state in their ON/OFF
      // text instead).
      if (id == PRIM_QUIET_TIME_ICON) { resolve_icon(out, 29, 0, active, c); return true; }
      if (id == PRIM_QUIET_TIME_PLAIN_ICON) { resolve_icon(out, 29, 0, false, c); return true; }
      resolve_text(out, active ? "ON" : "OFF", c);
      return true;
    }
    case PRIM_BLUETOOTH_ONOFF_TEXT: {
      // Same always-its-own-color rule as the other Bluetooth primitives.
      bool connected = connection_service_peek_pebble_app_connection();
      resolve_text(out, connected ? "ON" : "OFF", connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0));
      out->force_color = true;
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
      // primitive health-value resolution()'s case 39/40 comment.
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
        char pbuf[16];
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

    // ---- weather singles (legacy weather feature code cases 73/74/75/77) ---
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
          dyn = feature_color_from_packed(data->current_temp_color);
        } else {
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->weather_temp_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(data->weather_temp_c, -10, 40);
        }
      } else if (id == PRIM_TEMP_HIGH_ONLY_TEXT) {
        prefix = "H ";
        if (data->temp_high_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->temp_high_display);
          dyn = feature_color_from_packed(data->temp_high_color);
        } else {
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->temp_high_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(data->temp_high_c, -10, 40);
        }
      } else if (id == PRIM_TEMP_LOW_ONLY_TEXT) {
        prefix = "L ";
        if (data->temp_low_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->temp_low_display);
          dyn = feature_color_from_packed(data->temp_low_color);
        } else {
          snprintf(num, sizeof(num), "%d", feature_rules_convert_temp(data->temp_low_c, data->temp_unit));
          dyn = feature_colors_seven_stop_gradient(data->temp_low_c, -10, 40);
        }
      } else { // PRIM_FEELS_LIKE_TEXT
        prefix = "FL ";
        if (data->feels_like_display[0] != '\0') {
          snprintf(num, sizeof(num), "%s", data->feels_like_display);
          dyn = feature_color_from_packed(data->feels_like_color);
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

    // ---- timezone cluster (legacy timezone computation) --------------
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

    // ---- astronomy cluster (the previous astronomy feature path, content 11/13/16/79-85) ----
    // Values are either PKJS-supplied display strings (moon, meteor, Saturn,
    // aurora -- Update 6) or watch-runtime selections over PKJS data
    // (sun event, planets visible, next planet rise -- time-dependent and
    // 12h/24h-formatted, so they can't be precomputed). Color rules copied
    // 1:1 from the legacy cases: an error is always red and an unavailable
    // value always gray, both bypassing color_mode (force_color); otherwise
    // most use the slot's main color as their COLOR-mode candidate
    // (dynamic_is_main).
    case PRIM_MOON_PHASE_ICON:
    case PRIM_MOON_PHASE_NAME: {
      // Moon phase has no natural value to grade -- always white in COLOR mode.
      if (id == PRIM_MOON_PHASE_ICON) {
        resolve_icon(out, 4, data->moon_phase_pct, data->moon_waxing, GColorWhite);
        return true;
      }
      resolve_text(out, data->moon_phase_display[0] != '\0'
                          ? data->moon_phase_display
                          : feature_moon_phase_short_name(data->moon_phase_pct, data->moon_waxing),
                   GColorWhite);
      return true;
    }
    case PRIM_LOCATION_ICON:
    case PRIM_LOCATION_NAME:
      if (id == PRIM_LOCATION_ICON) { resolve_icon(out, 8, 0, false, GColorWhite); }
      else { resolve_text(out, data->location_name[0] != '\0' ? data->location_name : "Unknown", GColorWhite); }
      out->dynamic_is_main = true;
      return true;
    case PRIM_SUN_EVENT_ICON:
    case PRIM_SUN_EVENT_TIME: {
      bool is_sunrise = false;
      time_t ev = 0;
      bool have = eclipse_ui_get_next_sun_event(now, data->sun_rise, data->sun_set, data->sun_rise_tomorrow, &ev, &is_sunrise);
      if (id == PRIM_SUN_EVENT_ICON) { resolve_icon(out, 11, 0, is_sunrise, GColorWhite); }
      else {
        char tbuf[8];
        if (have) { struct tm *et = localtime(&ev); strftime(tbuf, sizeof(tbuf), clock_is_24h_style() ? "%H:%M" : "%I:%M", et); }
        else { snprintf(tbuf, sizeof(tbuf), "N/A"); }
        resolve_text(out, tbuf, GColorWhite);
      }
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_PLANETS_VISIBLE_ICON:
    case PRIM_PLANETS_VISIBLE_COUNT: {
      char pbuf[16];
      if (data->error_code != 0) {
        snprintf(pbuf, sizeof(pbuf), "ERR %d", data->error_code);
        if (id == PRIM_PLANETS_VISIBLE_ICON) { resolve_icon(out, 23, 0, false, GColorRed); }
        else { resolve_text(out, pbuf, GColorRed); }
        out->force_color = true;
        return true;
      }
      uint8_t count = feature_count_visible_planets(data, now);
      snprintf(pbuf, sizeof(pbuf), "%d planet%s", count, count == 1 ? "" : "s");
      if (id == PRIM_PLANETS_VISIBLE_ICON) { resolve_icon(out, 23, 0, false, GColorWhite); }
      else { resolve_text(out, pbuf, GColorWhite); }
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_METEOR_SHOWER_NAME: {
      char mbuf[20];
      if (data->error_code != 0) {
        snprintf(mbuf, sizeof(mbuf), "ERR %d", data->error_code);
        resolve_text(out, mbuf, GColorRed);
        out->force_color = true;
      } else if (data->meteor_display[0] != '\0') {
        // Legacy assigns the PKJS-graded color directly (no color_mode dispatch).
        resolve_text(out, data->meteor_display, feature_color_from_packed(data->meteor_color));
        out->force_color = true;
      } else if (data->meteor_intensity > 0 && data->meteor_shower_name[0] != '\0') {
        resolve_text(out, data->meteor_shower_name, feature_colors_meteor_intensity_gradient(data->meteor_intensity));
      } else {
        resolve_text(out, "N/A", GColorLightGray);
        out->force_color = true;
      }
      return true;
    }
    case PRIM_SATURN_RINGS_ICON:
    case PRIM_SATURN_RINGS_PCT: {
      char sbuf[20];
      if (data->error_code != 0) {
        snprintf(sbuf, sizeof(sbuf), "ERR %d", data->error_code);
        if (id == PRIM_SATURN_RINGS_ICON) { resolve_icon(out, 24, 0, false, GColorRed); }
        else { resolve_text(out, sbuf, GColorRed); }
        out->force_color = true;
        return true;
      }
      if (data->saturn_rings_display[0] != '\0') { snprintf(sbuf, sizeof(sbuf), "%s", data->saturn_rings_display); }
      else { snprintf(sbuf, sizeof(sbuf), "Rings %d%%", data->saturn_ring_open_pct); }
      if (id == PRIM_SATURN_RINGS_ICON) { resolve_icon(out, 24, 0, false, GColorWhite); }
      else { resolve_text(out, sbuf, GColorWhite); }
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_NEXT_PLANET_RISE: {
      char rbuf[20];
      if (data->error_code != 0) {
        snprintf(rbuf, sizeof(rbuf), "ERR %d", data->error_code);
        resolve_text(out, rbuf, GColorRed);
        out->force_color = true;
        return true;
      }
      static const char *PLANET_ABBR[PLANET_COUNT] = { "MER", "VEN", "MAR", "JUP", "SAT" };
      int best = -1;
      time_t best_t = 0;
      for (int p = 0; p < PLANET_COUNT; p++) {
        time_t r = data->planet_rise[p];
        if (r > now && (best == -1 || r < best_t)) { best = p; best_t = r; }
      }
      if (best < 0) { resolve_text(out, "N/A", GColorLightGray); out->force_color = true; return true; }
      char tb[8];
      struct tm *bt = localtime(&best_t);
      strftime(tb, sizeof(tb), clock_is_24h_style() ? "%H:%M" : "%I:%M", bt);
      snprintf(rbuf, sizeof(rbuf), "%s %s", PLANET_ABBR[best], tb);
      resolve_text(out, rbuf, GColorWhite);
      out->dynamic_is_main = true;
      return true;
    }
    case PRIM_ISS_ICON:
    case PRIM_ISS_NEXT_PASS: {
      char ibuf[20];
      GColor c;
      bool forced = true;
      if (data->iss_error_code != 0) {
        snprintf(ibuf, sizeof(ibuf), "ERR %d", data->iss_error_code);
        c = GColorRed;
      } else if (data->iss_next_pass > 0) {
        struct tm *it = localtime(&data->iss_next_pass);
        strftime(ibuf, sizeof(ibuf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", it);
        c = GColorWhite;
        forced = false;
      } else {
        snprintf(ibuf, sizeof(ibuf), "N/A");
        c = GColorLightGray;
      }
      if (id == PRIM_ISS_ICON) { resolve_icon(out, 25, 0, false, c); } else { resolve_text(out, ibuf, c); }
      out->force_color = forced;
      out->dynamic_is_main = !forced;
      return true;
    }
    case PRIM_AURORA_ICON:
    case PRIM_AURORA_KP: {
      char abuf[20];
      GColor c;
      if (data->aurora_error_code != 0) {
        snprintf(abuf, sizeof(abuf), "ERR %d", data->aurora_error_code);
        c = GColorRed;
        out->force_color = true;
      } else if (data->aurora_kp_display[0] != '\0') {
        snprintf(abuf, sizeof(abuf), "%s", data->aurora_kp_display);
        c = feature_color_from_packed(data->aurora_kp_color);
      } else {
        snprintf(abuf, sizeof(abuf), "Kp %d.%d", data->aurora_kp_x10 / 10, data->aurora_kp_x10 % 10);
        c = feature_colors_white_to_red_gradient(data->aurora_kp_x10);
      }
      bool forced = out->force_color;
      if (id == PRIM_AURORA_ICON) { resolve_icon(out, 26, 0, false, c); } else { resolve_text(out, abuf, c); }
      out->force_color = forced;
      return true;
    }
    case PRIM_COMPASS_ICON:
    case PRIM_COMPASS_DIR_TEXT: {
      // Active (real heading) for 15s after a shake, then asleep (input.c).
      bool asleep = input_compass_feature_is_asleep();
      if (id == PRIM_COMPASS_ICON) {
        resolve_icon(out, 27, (int16_t)(input_compass_feature_heading_deg() % 360), asleep, GColorWhite);
        out->dynamic_is_main = true;
        out->two_tone_accent_main = true;
        return true;
      }
      static const char *COMPASS_DIRS[16] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
      };
      if (asleep) { resolve_text(out, "---", GColorWhite); }
      else {
        int idx = (int)(((input_compass_feature_heading_deg() * 2 + 22) / 45) % 16);
        if (idx < 0) idx += 16;
        resolve_text(out, COMPASS_DIRS[idx], GColorWhite);
      }
      out->dynamic_is_main = true;
      return true;
    }

    default:
      return false; // invalid/unsupported primitive; there is no legacy fallback
  }
}
