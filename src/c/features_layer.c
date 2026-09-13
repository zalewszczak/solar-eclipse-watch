#include "features_layer.h"
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
#include <stdlib.h> // atoi(), for parsing strftime's "%V" week-number string back to an int for grading

// See features_layer.h for the module-level design note (metadata cache
// vs. per-redraw layout recompute). This file also owns the shared
// features_draw_text_outlined()/features_contrasting_outline_color() outline primitives and
// the corner/edge custom-font plumbing (features_ensure_corner_custom_font() etc.)
// -- both used outside this module too (the countdown label and the big-
// analog hands/small-analog panel, respectively), which is why they're
// declared in features_layer.h rather than kept private.

#define CORNER_BOX_W 68

// How far a corner slot's box sits from the screen edge.
#define CORNER_INSET_PX 2

// DIGITAL_PANEL_H (the digital clock panel's own fixed height -- screen
// height 228 minus the sky canvas's fixed top-of-panel value 152) now
// lives in background_layer.h (background_layer.c needs it too, to size
// Digital top's own reserved gradient-only strip) -- see that header's
// own comment for the full explanation this used to carry here. Same
// name/value, still used below purely to keep the two BOTTOM (Digital
// bar) or TOP (Digital top) corner slots anchored to the sky's own
// edge rather than the full screen's.

// Pulls bottom_style's side-code (0/2/3/4, same meaning regardless of
// which layout) and top-vs-bottom bit back apart -- see that field's
// own comment in eclipse_data.h. Declared in features_layer.h; used
// throughout this file and by pebble-eclipse-watch.c's own clock-text
// drawing.
uint8_t features_digital_side_mode(uint8_t bottom_style) {
  return features_is_digital_top_layout(bottom_style) ? bottom_style - 5 : bottom_style;
}
bool features_is_digital_top_layout(uint8_t bottom_style) {
  return bottom_style >= 5;
}

// point_in_convex_polygon()/fill_polygon_dithered() used to live here
// for the old "semi" color mode's dithered highlight plate -- removed
// now that Pill mode draws a plain solid-fill capsule instead (see
// draw_pill below), which needs neither. Smaller binary, one less
// per-pixel loop.

// Shared by every outline implementation in this file (text, icons):
// draw once per offset with a contrasting color, then once more
// normally on top. Cheap and guarantees contrast against any
// background without needing per-pixel edge detection.
// Thin is the original/default look (4 cardinal 1px shifts). Thick
// adds 4 cardinal 2px shifts and 4 diagonal 1px shifts on top of
// thin's own 4 -- 12 points total -- for a visibly bolder line, per
// the request's exact offset list.
static const GPoint OUTLINE_OFFSETS_THIN[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
static const GPoint OUTLINE_OFFSETS_THICK[12] = {
  {-1, 0}, {1, 0}, {0, -1}, {0, 1},
  {-2, 0}, {2, 0}, {0, -2}, {0, 2},
  {1, 1}, {-1, 1}, {-1, -1}, {1, -1},
};
// Resolves outline_style (0=none, 1=thin, 2=thick) to the actual
// offset table + count every outline-drawing call site below loops
// over -- style 0 is never passed in here (every caller already
// guards on it separately, same as outline_enabled used to be a plain
// bool guard), so this only ever needs to pick between thin/thick.
static void get_outline_offsets(uint8_t outline_style, const GPoint **out_offsets, int *out_count) {
  if (outline_style >= 2) {
    *out_offsets = OUTLINE_OFFSETS_THICK;
    *out_count = 12;
  } else {
    *out_offsets = OUTLINE_OFFSETS_THIN;
    *out_count = 4;
  }
}

// White for dark colors, black for bright ones -- the outline has to
// contrast with the text/icon's OWN color to do its job (a dark
// outline on dark text is invisible regardless of what's behind it),
// not with the scheme's background color, which is what this used to
// (incorrectly) use.
//
// Threshold is 9, not the "true" midpoint of 15, specifically so a
// fully-saturated primary like pure red (r=3,g=0,b=0, luma 9) lands on
// the black-outline side: the classic 0.3/0.6/0.1 luma weights treat
// red as fairly dark, but on the watch's own display a solid red
// doesn't read as dark the way that formula implies -- a white
// outline on it looked wrong in practice. Weaker/darker reds (and
// blue, which is genuinely dark at any saturation) still correctly
// fall below this and get a white outline.
GColor features_contrasting_outline_color(GColor c) {
  uint8_t r = (c.argb >> 4) & 0x03;
  uint8_t g = (c.argb >> 2) & 0x03;
  uint8_t b = c.argb & 0x03;
  int luma = r * 3 + g * 6 + b; // approximates 0.3/0.6/0.1 luma weights, out of 30
  return (luma >= 9) ? GColorBlack : GColorWhite;
}

void features_draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                                GTextOverflowMode overflow, GTextAlignment alignment,
                                GColor color, uint8_t outline_style) {
  if (outline_style != 0) {
    const GPoint *offsets; int offset_count;
    get_outline_offsets(outline_style, &offsets, &offset_count);
    graphics_context_set_text_color(ctx, features_contrasting_outline_color(color));
    for (int i = 0; i < offset_count; i++) {
      GRect shifted = GRect(box.origin.x + offsets[i].x, box.origin.y + offsets[i].y,
                             box.size.w, box.size.h);
      graphics_draw_text(ctx, text, font, shifted, overflow, alignment, NULL);
    }
  }
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, overflow, alignment, NULL);
}



// Corner/edge feature text and the big-analog date's own font slot --
// resolved via font_lookup_resolve() (see font_lookup.h for the
// shared table every font-selecting system in this app draws from).
static FontSlot s_corner_font_slot = FONT_SLOT_EMPTY;

void features_ensure_corner_custom_font(uint8_t font_id) {
  font_lookup_resolve(&s_corner_font_slot, font_id);
}

void features_layer_unload_fonts(void) {
  font_lookup_release(&s_corner_font_slot);
}

// 7-stop gradient: turquoise (cold/low end) -> light blue -> green ->
// yellow -> orange -> red -> violet (hot/high end). Used for both
// temperature (-10..40C) and UV index (1..13) by passing different
// min/max, per the brief's request for "more colors" than a simple
// 2-stop blend -- see feature_colors_seven_stop_gradient()'s call sites
// below (temp high/low, UV index, pressure, AQI, week number, etc.).
// Clear/cloudy condition coloring (sunny-vs-overcast by cloud_pct alone)
// is handled directly by feature_colors_weather_condition_color()/
// feature_colors_overcast_gray_gradient() at their own call sites further
// down -- no separate wrapper needed here.

// HealthService access is isolated in feature_health.c.

// Renders one corner's chosen content type in one of the four color
// modes. The icon+text group is measured and positioned as a unit:
// left-anchored slots (TL/BL/middle-left) keep it flush left, right-
// anchored slots (TR/BR/middle-right) flush it against the box's own
// right edge (which is itself already anchored near the screen edge)
// so short content doesn't leave an empty gap before the edge, and
// center-anchored slots (upper/bottom-middle) center it within the
// box. The icon (when present) always precedes the text in reading
// order regardless of alignment -- only the whole group's position
// changes, not the icon/text order within it.
// allow_outline gates data->outline_style on top of the user
// setting rather than replacing it -- pass true from every caller
// that draws over the busy sky/hands canvas (corners, edge-middle
// slots), where the outline is what keeps text legible against an
// unpredictable background. The small-analog info panel's rows sit
// on their own solid-color background instead, so they pass false
// and never get one regardless of the outline_style setting --
// there's nothing there for it to contrast against.

// =========================================================================
// TABLE-DRIVEN FEATURE SLOTS
// =========================================================================
//
// Every one of the 12 feature slots (4 corners + 8 middle-edge lines) is
// represented by one FeatureSlot below. A slot is split into two halves
// that change at very different rates, and are recomputed independently:
//
//   - LAYOUT (is_top/is_left/is_middle/top_offset/bottom_shift/
//     middle_inset/center_horizontal/center_vertical/content/color_mode)
//     only ever changes when a setting or the shake-label state changes
//     -- resolved once by features_recompute_layout(), exactly like the
//     old FeatureSlot always did.
//
//   - VALUE (the actual text/icon/color to draw, plus the exact pixel
//     offsets where each piece goes) changes far more often -- a health
//     reading, the weather, the clock, a compass heading -- and is
//     resolved by features_recompute_slot_value(), grouped into a
//     handful of per-category functions (compute_health_value(),
//     compute_weather_value(), compute_date_value(),
//     compute_timezone_value(), compute_sky_value(),
//     compute_combo_value()) instead of one giant per-content switch.
//     Called from three places: a full settings/shake change (every
//     slot), the periodic ~1-minute refresh (every active slot), and
//     the once-a-second tick (ONLY the slot(s) whose content actually
//     needs second-by-second updates, tracked per-slot via
//     needs_second_refresh -- never all 12 just because one shows
//     seconds).
//
// Either way, by the time features_layer_update_proc() actually runs,
// every pixel position, color, and string a slot needs is already
// sitting in its FeatureSlot -- the update proc itself does no
// formatting, no gradient math, and no alignment/width measurement at
// all, just a straight loop blitting whatever's already resolved.

// One icon or one run of text within a slot, at a resolved offset from
// the slot's own box_x -- used both by the single icon+text path (as a
// 2-element case: an optional icon segment, then the text segment) and
// by the multi-icon combo content (heart rate + steps, battery + BT,
// ...), so both paths share the exact same draw-time code. Raised from
// 4 to 6 for content 110 (battery % + Quiet Time icon+"ON"/"OFF" +
// Bluetooth icon+"ON"/"OFF" -- 3 icon+text pairs, 6 segments), then 6
// to 8 for content 114 (all 4 -- battery, Bluetooth, Quiet Time,
// Hourly Vibrations -- icon+text each) -- the widest combo needs, so
// every slot pays the same fixed sizeof(RenderSegment)*2 extra bytes
// regardless of which content it's actually showing, same as raising
// it for any future wider combo would.
#define MAX_RENDER_SEGMENTS 8
typedef struct {
  bool is_icon;
  uint8_t icon_kind;    // 0 = none; same icon_kind numbering the old single-icon path always used
  int16_t icon_extra;    // weather category / battery charge% / compass heading-degrees / pressure trend / wind degrees / moon phase %, depending on icon_kind
  bool icon_flag;         // is_charging / is_sunrise / moon_waxing / compass_asleep, depending on icon_kind
  char text[20];           // when !is_icon
  GColor color;
  GColor color2;            // compass "other 3 arrows" color only; equals color everywhere else
  int16_t x_offset;          // resolved once, relative to the slot's box_x
  int16_t width;               // resolved once (icon: fixed box width; text: measured width)
} RenderSegment;

typedef struct {
  bool active;             // false = this slot draws nothing this cycle
  uint8_t content;
  uint8_t color_mode;

  // ---- layout: settings/shake-driven, resolved by features_recompute_layout() ----
  bool is_top;
  bool is_left;
  bool is_middle;
  bool is_edge; // flag for features that sit on the edge of the screen
  int16_t top_offset;
  int16_t bottom_shift;
  int16_t middle_inset;
  bool center_horizontal;
  bool center_vertical;
  bool allow_outline;
  bool needs_second_refresh; // true if this content must be recomputed every second (time-with-seconds displays)
  // Digital-mode's single bottom feature is wider than a normal 68px
  // slot and needs to shift/resize with the clock itself (see
  // features_digital_clock_area()) rather than sit flush against a screen edge
  // or centered across the full screen width -- when true, box_x/box_w
  // below are used verbatim (relative to bounds.origin) instead of the
  // normal is_left/center_horizontal+CORNER_BOX_W math, and the slot's
  // content is always centered within that box.
  bool custom_box;
  int16_t box_x;
  int16_t box_w;

  // ---- value: resolved by features_recompute_slot_value() ----
  bool draw_pill;
  GColor pill_bg;
  int16_t segment_count;      // > 0 means "use segments[] below"; 0 means this slot draws nothing
  RenderSegment segments[MAX_RENDER_SEGMENTS];
} FeatureSlot;

typedef struct {
  EclipseData *data;
  FeatureSlot slots[FEATURES_MAX_SLOTS];
} FeaturesState;

enum {
  SLOT_UPPER_L1 = 0, SLOT_UPPER_L2,
  SLOT_BOTTOM_L1, SLOT_BOTTOM_L2,
  SLOT_LEFT_L1, SLOT_LEFT_L2,
  SLOT_RIGHT_L1, SLOT_RIGHT_L2,
  SLOT_CORNER_TL, SLOT_CORNER_TR, SLOT_CORNER_BL, SLOT_CORNER_BR,
};

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
static void resolve_segment_offsets(FeatureSlot *slot, GFont font, int16_t font_h) {
  int16_t box_w = slot->custom_box ? slot->box_w : CORNER_BOX_W;
  int16_t advance[MAX_RENDER_SEGMENTS]; // width INCLUDING this segment's own trailing gap
  int16_t total_w = 0;
  for (int i = 0; i < slot->segment_count; i++) {
    RenderSegment *seg = &slot->segments[i];
    if (seg->is_icon) {
      seg->width = FEATURE_ICON_WIDTH;
      advance[i] = feature_icons_plus_gap_width(seg->icon_kind);
    } else {
      GSize sz = graphics_text_layout_get_content_size(seg->text, font, GRect(0, 0, 200, font_h + 10),
                                                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
      seg->width = sz.w + 2;
      advance[i] = seg->width + 5; // 5px gap before whatever segment follows (icon or more text) -- matches icon_plus_gap_width()'s own gap
    }
    total_w += advance[i];
  }
  if (total_w > box_w) {
    if (slot->is_middle) {
      if (slot->is_left) { // this is middle aligned feature, move it to the right
        total_w = box_w;
      } else {
//        total_w -= box_w;
      }
    } else if (!slot->is_left && !slot->is_edge) { // this is right aligned feature - move it further to the left
      total_w -= total_w - box_w;
    }
  }

  // A custom-box slot (currently just the digital-mode bottom feature)
  // is always centered within its own box_w -- it has no left/right
  // edge of its own to hug, unlike every normal corner/edge slot.
  int16_t start_x = slot->custom_box ? (box_w - total_w) / 2
                    : (slot->center_horizontal ? (box_w - total_w) / 2
                       : (!slot->is_left ? box_w - total_w : 0));
  int16_t x = start_x;
  for (int i = 0; i < slot->segment_count; i++) {
    slot->segments[i].x_offset = x;
    x += advance[i];
  }
}

// ---- value recompute dispatcher ----------------------------------------
//
// Routes a slot's already-settings-resolved content/color_mode to the
// right cluster function above, applies the one shared override every
// weather-derived content needs (a service error blanks everything to
// a flat red "ERR ###", overriding whatever the cluster function just
// computed), then resolves positions. This is the only place that
// looks at `content` as a giant range of numbers -- every cluster
// function above only ever sees the handful of ids it actually owns.
static void features_recompute_slot_value(FeatureSlot *slot, const EclipseData *data,
                                           GColor main_color, GColor accent_color, GColor bg_color,
                                           GFont font, int16_t font_h, time_t now, struct tm *t) {
  slot->draw_pill = (slot->color_mode == 2);
  slot->pill_bg = bg_color;

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
    default: // every date/time format variant (12, 18-19, 21-30, 63-72, 86, 95-96, 103)
      compute_date_value(slot, content, data, color_mode, main_color, accent_color, now, t);
      break;
  }

  // A weather fetch error blanks the whole slot to a flat red "ERR ###" --
  // uniformly, regardless of which weather cluster case built it, and
  // regardless of color_mode (an error needs to stay legible, not blend
  // in as a normal reading would).
  if (feature_rules_content_is_weather_derived(content) && weather_layer_should_show_error(data)) {
    char err_buf[10];
    snprintf(err_buf, sizeof(err_buf), "ERR %d", data->weather_error_code);
    slot->segment_count = 1;
    set_text_seg(slot, 0, err_buf, GColorRed);
    slot->draw_pill = false;
  }

  if (slot->segment_count > 0) resolve_segment_offsets(slot, font, font_h);
}


// ---- drawing: the trivial per-slot blit --------------------------------
//
// Everything this needs -- position, color, text, which icon -- was
// already resolved by features_recompute_slot_value() above. This
// function does no formatting, no gradient math, and no alignment or
// width measurement: box_x/box_y is the one bit of arithmetic left
// (added back at draw time, not cached, purely so a system
// notification's screen obstruction can shrink `bounds` without
// needing its own recompute pass -- see the module-level comment up
// top), and every segment inside the slot just gets blitted at its
// already-resolved x_offset.
static void features_draw_slot(GContext *ctx, GRect bounds, const FeatureSlot *slot,
                                GFont font, int16_t font_h, int16_t font_offset,
                                uint8_t outline_style, uint8_t weather_icon_style, bool draw_debug) {
  if (!slot->active || slot->segment_count == 0) return;

  int16_t box_w = slot->custom_box ? slot->box_w : CORNER_BOX_W;
  int16_t box_x = slot->custom_box ? bounds.origin.x + slot->box_x
    : (slot->center_horizontal
       ? bounds.origin.x + (bounds.size.w - CORNER_BOX_W) / 2
       : (slot->is_left ? bounds.origin.x + CORNER_INSET_PX : bounds.origin.x + bounds.size.w - CORNER_INSET_PX - CORNER_BOX_W));
  int16_t box_y = slot->center_vertical
    ? bounds.origin.y + (bounds.size.h - CORNER_ROW_H) / 2 + slot->top_offset
    : (slot->is_top ? bounds.origin.y + slot->top_offset - 2
                     : bounds.origin.y + bounds.size.h - CORNER_ROW_H - CORNER_INSET_PX - slot->bottom_shift + 2);
  if (slot->is_middle) {
    if (slot->is_left) box_x += slot->middle_inset; else box_x -= slot->middle_inset;
  }

  if (slot->draw_pill) {
    graphics_context_set_fill_color(ctx, slot->pill_bg);
    graphics_fill_rect(ctx, GRect(box_x, box_y, box_w, CORNER_ROW_H), CORNER_ROW_H / 2, GCornersAll);
  }

  uint8_t effective_outline_style = slot->allow_outline ? outline_style : 0;
  for (int i = 0; i < slot->segment_count; i++) {
    const RenderSegment *seg = &slot->segments[i];
    int16_t seg_x = box_x + seg->x_offset;
    if (seg->is_icon) {
      feature_icons_draw_render_icon(ctx, seg->icon_kind, seg->icon_extra, seg->icon_flag,
                                      seg->color, seg->color2, seg_x, box_y, CORNER_ROW_H,
                                      effective_outline_style, weather_icon_style, draw_debug);
    } else {
      int16_t box_h = font_h + font_offset + 2;
      GRect bounding_box = GRect(seg_x, box_y + (CORNER_ROW_H - box_h) / 2 - 1, seg->width + 2, box_h);
      features_draw_text_outlined(ctx, seg->text, font, bounding_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, seg->color, effective_outline_style);
      
      if (draw_debug){
        graphics_context_set_stroke_width(ctx, 1);
        graphics_context_set_stroke_color(ctx, GColorGreen);
        graphics_draw_rect(ctx, bounding_box);
      }
    }
  }
}

// ---- layout resolution (settings-driven) -------------------------
//
// Which of the 12 slots are even active, and where each one's box
// sits, depends only on settings (bottom_style, big_analog_marker_style,
// the corner/edge content+color-mode fields) -- never on the current
// time, any live reading, or the shake-to-reveal state. Resolved here,
// then features_recompute_slot_value() is called once per newly-active
// slot to fill in its actual content -- see features_layer_set_data()
// below for when this whole function runs (a settings change) versus
// features_layer_refresh_values()/refresh_second_slots() (which only
// re-run the VALUE half, for slots whose layout hasn't changed at all).
// Which horizontal band of the digital clock panel's full width the
// clock text (and the single "bottom" feature under it, which always
// shifts in lockstep with it) actually occupies, given which side
// feature columns are active. One side active -> the clock shifts
// away from it, into the freed-up space; both or neither active ->
// centered across the full width, exactly as if no sides existed --
// per the request, a font enabled for both-sides use is expected to
// already be narrow enough to coexist with a centered clock without
// needing to shrink further. Shared between pebble-eclipse-watch.c
// (positions the clock text itself) and this file's own
// features_recompute_layout() (positions the bottom feature to match)
// so the two can never drift out of sync with each other.
void features_digital_clock_area(uint8_t bottom_style, int16_t screen_w, int16_t *out_x, int16_t *out_w) {
  uint8_t side = features_digital_side_mode(bottom_style);
  if (side == 2) { // right side only -- shift left
    *out_x = 0;
    *out_w = screen_w - CORNER_BOX_W;
  } else if (side == 3) { // left side only -- shift right
    *out_x = CORNER_BOX_W;
    *out_w = screen_w - CORNER_BOX_W;
  } else { // 0 (no sides) or 4 (both sides) -- centered, full width
    *out_x = 0;
    *out_w = screen_w;
  }
}

static void features_recompute_layout(FeaturesState *state) {
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) state->slots[i].active = false;

  EclipseData *d = state->data;
  if (!d) return;

  bool is_analog = d->bottom_style == 1;
  bool is_digital_top = features_is_digital_top_layout(d->bottom_style);
  uint8_t marker_style = d->big_analog_marker_style;
  bool is_bitmap_style = is_analog && marker_style >= 3 && marker_style != 8 && marker_style != 9;

  // Which of the 12 slots actually apply for the current marker/
  // bottom_style is decided entirely on the phone (see
  // computeSlotAvailability()/CORNER_CATEGORIES in config-page.js and
  // the availability check in index.js's dict-building step) -- by the
  // time a content field reaches here, it's already 0 ("None") for any
  // slot that shouldn't show for the current style, so this file just
  // builds every slot from whatever content it was given, unconditionally,
  // and trusts a content of 0 to mean "draws nothing" (already true --
  // see features_recompute_slot_value()'s own content==0 early return)
  // rather than keeping its own separate copy of "which styles support
  // which slots" to decide that upfront.

  // Inner-empty-area margins: procedural presets (0/1/2) and "none" (9)
  // are calculated from that style's own marker-ring geometry via
  // marker_layer_inner_reach() (same marker_layer_point_on_ring() technique
  // custom (8) uses below, just fed a fixed preset instead of a live
  // user config); bitmap styles (3-7) have no ring geometry at all, so
  // they use a fixed per-style table instead, each side independent.
  typedef struct { int16_t top, bottom, left, right; } EdgeMargins;
  static const EdgeMargins BITMAP_STYLE_MARGINS[5] = {
    { 34, 30, 40, 35 }, // 3: Modern
    { 37, 33, 38, 33 }, // 4: Shadow
    { 44, 40, 25, 15 }, // 5: Tally
    { 46, 42, 45, 45 }, // 6: Bell
    { 44, 40, 30, 20 }, // 7: Fancy
  };
  int16_t dyn_upper_offset = 24, dyn_bottom_shift = 20, dyn_left_inset = 0, dyn_right_inset = 0;
  if (is_bitmap_style && marker_style >= 3 && marker_style <= 7) {
    const EdgeMargins *m = &BITMAP_STYLE_MARGINS[marker_style - 3];
    dyn_upper_offset = m->top; dyn_bottom_shift = m->bottom; dyn_left_inset = m->left; dyn_right_inset = m->right;
  } else if (marker_style == 8) {
    GRect screen = GRect(0, 0, 200, 228);
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    uint8_t pct = d->custom_hour_marker.thickness != 0 ? d->custom_hour_marker.inner_border_pct : 100;
    uint8_t ecc = d->custom_hour_marker.thickness != 0 ? d->custom_hour_marker.inner_eccentricity : 100;
    uint8_t h_offset = 0;
    if (d->marker_text.target != 0) {
      if (d->marker_text.target == 1) { // hours
        int16_t marker_pct = d->custom_hour_marker.thickness == 0 ? 100 + d->marker_text.offset_px : d->custom_hour_marker.inner_border_pct + d->marker_text.offset_px;
        if(marker_pct>100) {
          marker_pct = 100;
        } else if (marker_pct < 0) {
          marker_pct = 0;
        }
        pct = marker_pct;
        ecc = d->custom_hour_marker.thickness == 0 ? 100 : d->custom_hour_marker.inner_eccentricity;
      } else { // seconds
        int16_t marker_pct = d->custom_second_marker.thickness == 0 ? 100 + d->marker_text.offset_px : d->custom_second_marker.inner_border_pct + d->marker_text.offset_px;
        if(marker_pct>100) {
          marker_pct = 100;
        } else if (marker_pct < 0) {
          marker_pct = 0;
        }
        pct = marker_pct;
        ecc = d->custom_second_marker.thickness == 0 ? 100 : d->custom_second_marker.inner_eccentricity;
      }
      h_offset = (font_lookup_height(d->marker_text.font_choice) + font_lookup_y_offset(d->marker_text.font_choice)) / 2;
    }
    GPoint top_pt = marker_layer_point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = marker_layer_point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    top_pt.y += h_offset;
    right_pt.x -= h_offset; // close enough
    bottom_pt.y -= h_offset;
    left_pt.x += h_offset;
    int16_t margin = 2;
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_reach = left_pt.x + margin, right_reach = screen.size.w - right_pt.x + margin;
    if (left_reach > dyn_left_inset) dyn_left_inset = left_reach;
    if (right_reach > dyn_right_inset) dyn_right_inset = right_reach;
  } else if (marker_style <= 2 || marker_style == 9) {
    uint8_t pct, ecc;
    marker_layer_inner_reach(marker_style, &pct, &ecc);
    GRect screen = GRect(0, 0, 200, 228);
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    GPoint top_pt = marker_layer_point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = marker_layer_point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = marker_layer_point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    int16_t margin = 4;
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_reach = left_pt.x + margin, right_reach = screen.size.w - right_pt.x + margin;
    if (left_reach > dyn_left_inset) dyn_left_inset = left_reach;
    if (right_reach > dyn_right_inset) dyn_right_inset = right_reach;
  }

  if (is_analog) {
    bool has_line2 = d->upper_middle_line2_content != 0;
    int16_t line1_offset = has_line2 ? dyn_upper_offset : dyn_upper_offset + CORNER_ROW_H / 2;
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_UPPER_L2] = (FeatureSlot){
        .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
        .is_top = true, .is_left = true, .is_middle = false, .is_edge = false,
        .top_offset = dyn_upper_offset + CORNER_ROW_H, .bottom_shift = 0, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line2_content),
      };
    }

    has_line2 = d->bottom_middle_line2_content != 0;
    int16_t line1_shift = has_line2 ? dyn_bottom_shift + CORNER_ROW_H : dyn_bottom_shift + CORNER_ROW_H / 2;
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = 0, .bottom_shift = line1_shift, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_BOTTOM_L2] = (FeatureSlot){
        .active = true, .content = d->bottom_middle_line2_content, .color_mode = d->bottom_middle_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = false, .is_edge = false,
        .top_offset = 0, .bottom_shift = dyn_bottom_shift, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line2_content),
      };
    }

    has_line2 = d->middle_left_line2_content != 0;
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = true, .is_edge = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_left_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_LEFT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = true, .is_edge = false,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_left_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line2_content),
      };
    }

    has_line2 = d->middle_right_line2_content != 0;
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = false, .is_left = false, .is_middle = true, .is_edge = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_right_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
        .is_top = false, .is_left = false, .is_middle = true, .is_edge = false,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_right_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line2_content),
      };
    }
  }

  // Digital-mode-only equivalent of the 4 blocks above -- the 8 edge
  // slot indices are unused by any analog "show_*" block when
  // !is_analog (none of those ran), so it's safe to repurpose 7 of
  // them here: SLOT_LEFT_L1/L2 + SLOT_UPPER_L1 as the 3-line left
  // column, SLOT_RIGHT_L1/L2 + SLOT_UPPER_L2 as the 3-line right
  // column, SLOT_BOTTOM_L1 as the single bottom feature (SLOT_BOTTOM_L2
  // stays unused). Deliberately reuses the SAME 8 EclipseData fields
  // analog mode's upper/bottom/left/right-middle content uses (see
  // their own dual-purpose comment in eclipse_data.h) rather than a
  // separate set of digital-only fields -- the two modes never run at
  // once, so there's nothing to actually preserve by keeping them
  // apart, and sharing saves both the extra bytes on the watch and the
  // extra AppMessage keys/traffic a second set would cost. Which side
  // columns actually apply for the current bottom_style (0/2/3/4) is
  // decided phone-side same as everything else here -- the content
  // fields already arrive zeroed for whichever side isn't turned on,
  // so both columns are just built unconditionally below.
  if (!is_analog) {
    int16_t clock_x, clock_w;
    features_digital_clock_area(d->bottom_style, 200, &clock_x, &clock_w);

    // 1 = nearest the clock, 3 = nearest the screen's own outer edge --
    // anchored off whichever edge is adjacent to the clock for the
    // CURRENT layout (the panel's own top for Digital bar, since the
    // clock sits near there with the sky above it; the panel's own
    // bottom for Digital top, mirrored, since the clock sits near
    // THERE with the sky below it instead) via is_top/top_offset vs
    // bottom_shift -- same row_1/2/3_off magnitudes either way, so
    // Digital top's column reads as a literal vertical flip of Digital
    // bar's rather than a separately-tuned layout. Bottom-anchored (not
    // top-anchored off a fixed panel offset) in the Digital bar case so
    // a shrinking screen during a system notification shifts the whole
    // stack up together, same as the corners already do, rather than
    // the top row drifting away from the panel it's meant to sit
    // inside -- Digital top has no such obstruction to react to (system
    // notifications only ever eat into the screen's bottom), so being
    // top-anchored there costs nothing.
    int16_t row1_off = CORNER_ROW_H * 2, row2_off = CORNER_ROW_H, row3_off = 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line1_content),
    };
    state->slots[SLOT_LEFT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_left_line2_content),
    };
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){ // reused: digital left column, row 3 -- reads upper_middle_line1
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line1_content),
    };

    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row1_off : 0, .bottom_shift = is_digital_top ? 0 : row1_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line1_content),
    };
    state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = is_digital_top ? row2_off : 0, .bottom_shift = is_digital_top ? 0 : row2_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->middle_right_line2_content),
    };
    state->slots[SLOT_UPPER_L2] = (FeatureSlot){ // reused: digital right column, row 3 -- reads upper_middle_line2
      .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
      .is_top = is_digital_top, .is_left = false, .is_middle = false, .is_edge = true,
      .top_offset = row3_off, .bottom_shift = row3_off, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->upper_middle_line2_content),
    };

    // Single bottom feature -- reuses bottom_middle_line1 (analog's
    // upper of its own 2-line pair; bottom_middle_line2 has no
    // digital-mode role, 7 slots needed against 8 available fields).
    // Shares clock_x/clock_w with the clock text itself
    // (draw_digital_clock_panel() in pebble-eclipse-watch.c uses the
    // exact same features_digital_clock_area() call), always centered within
    // that band, anchored to the screen's own outer edge -- the true
    // bottom for Digital bar, the true top for Digital top (row 3's own
    // edge in both cases, per the comment above).
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = is_digital_top, .is_left = true, .is_middle = false, .is_edge = false,
      .top_offset = 0, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .custom_box = true, .box_x = clock_x, .box_w = clock_w,
      .needs_second_refresh = feature_rules_content_needs_second_refresh(d->bottom_middle_line1_content),
    };
  }

  // Corners always just draw whatever d->corner_content[] says, for
  // every marker style including bitmap ones -- defaulting that to
  // "off" for bitmap styles (and offering an "enable corner features"
  // override) is the settings page's job, not this file's.
  //
  // Top corners (TL/TR): anchored to the screen's own top edge for
  // Analog and Digital bar (both have open sky right there), but
  // pulled DOWN by DIGITAL_PANEL_H for Digital top -- that layout's
  // panel sits at the top instead, so its own sky begins
  // DIGITAL_PANEL_H down, and these two need to land at THAT boundary
  // instead of the screen's real top edge (features_layer's frame
  // spans the full screen in every layout -- see apply_layout() -- so
  // without this they'd land inside the transparent panel itself,
  // overlapping the clock). Exact mirror of bottom_corner_shift below,
  // just for the opposite pair of corners.
  int16_t top_corner_shift = is_digital_top ? DIGITAL_PANEL_H : 0;
  state->slots[SLOT_CORNER_TL] = (FeatureSlot){
    .active = true, .content = d->corner_content[0], .color_mode = d->corner_color_mode[0],
    .is_top = true, .is_left = true, .is_middle = false, .is_edge = true,
    .top_offset = CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[0]),
  };
  state->slots[SLOT_CORNER_TR] = (FeatureSlot){
    .active = true, .content = d->corner_content[1], .color_mode = d->corner_color_mode[1],
    .is_top = true, .is_left = false, .is_middle = false, .is_edge = true,
    .top_offset = CORNER_INSET_PX + top_corner_shift, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[1]),
  };
  // Bottom corners (BL/BR): stay anchored to the SKY's own bottom
  // edge, not the full screen's -- meaningfully different only in
  // Digital bar, where the sky canvas only occupies the screen's top
  // portion and the digital clock's own bottom panel fills the rest.
  // features_layer's own frame spans the FULL screen in every layout
  // (see apply_layout()), so a plain bottom_shift of 0 here would put
  // these two corners down inside the digital panel instead of at the
  // sky's own bottom-left/-right -- adding DIGITAL_PANEL_H's worth of
  // shift pulls them back up to the sky boundary. Analog mode has no
  // separate panel (sky already fills the screen) and Digital top's
  // own sky already reaches all the way to the real screen bottom (its
  // panel is up at the TOP instead -- see top_corner_shift above), so
  // bottom_shift stays 0 for both of those.
  int16_t bottom_corner_shift = (is_analog || is_digital_top) ? 0 : DIGITAL_PANEL_H;
  state->slots[SLOT_CORNER_BL] = (FeatureSlot){
    .active = true, .content = d->corner_content[2], .color_mode = d->corner_color_mode[2],
    .is_top = false, .is_left = true, .is_middle = false, .is_edge = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[2]),
  };
  state->slots[SLOT_CORNER_BR] = (FeatureSlot){
    .active = true, .content = d->corner_content[3], .color_mode = d->corner_color_mode[3],
    .is_top = false, .is_left = false, .is_middle = false, .is_edge = true,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = feature_rules_content_needs_second_refresh(d->corner_content[3]),
  };
}

// ---- value refresh (recomputes VALUE only, layout stays as-is) --------

// Recomputes every active slot's value -- called after a full layout
// change (so newly-active slots get real content right away) and from
// the periodic ~1-minute refresh (so slower-changing readings --
// weather, health, battery, dates -- actually update).
static void features_recompute_all_values(FeaturesState *state) {
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  features_ensure_corner_custom_font(state->data->corner_font);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active) continue;
    features_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

// Recomputes ONLY the slot(s) whose content needs second-by-second
// updating (a seconds-showing time display, or nothing at all most of
// the time) -- if only one feature slot shows seconds, only that one
// slot gets touched, not all 12.
static void features_recompute_second_slots(FeaturesState *state) {
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active || !state->slots[i].needs_second_refresh) continue;
    features_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

// ---- layer lifecycle --------------------------------------------------

// The always-on-top feature overlay. Deliberately a separate layer with
// its own independent refresh timer (see main.c's corners_timer_callback)
// rather than being drawn as part of the sky canvas or tied to its redraw
// cycle. Never fills its own background, so whatever's underneath (sky
// canvas, and in big-analog mode possibly the hands too) shows through
// everywhere except where content is actually drawn.
//
// Does no computation of its own at all -- every slot's position,
// color, icon, and text was already resolved by whichever of
// features_layer_set_data()/refresh_values()/refresh_second_slots()
// last ran. This is purely a blit loop.
static void features_layer_update_proc(Layer *layer, GContext *ctx) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  if (!state->data) return;

  GRect bounds = layer_get_unobstructed_bounds(layer);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);
  int16_t font_offset = font_lookup_y_offset(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    features_draw_slot(ctx, bounds, &state->slots[i], font, font_h, font_offset,
                        state->data->outline_style, state->data->weather_icon_style, state->data->draw_debug);
  }
}

Layer *features_layer_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(FeaturesState));
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  state->data = NULL;
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) state->slots[i].active = false;
  layer_set_update_proc(layer, features_layer_update_proc);
  return layer;
}

void features_layer_destroy(Layer *layer) {
  layer_destroy(layer);
}

void features_layer_set_data(Layer *layer, EclipseData *data) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  state->data = data;
  features_recompute_layout(state);
  features_recompute_all_values(state);
  layer_mark_dirty(layer);
}

void features_layer_refresh_values(Layer *layer) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  features_recompute_all_values(state);
  layer_mark_dirty(layer);
}

bool features_layer_content_in_use(const EclipseData *data, uint8_t content) {
  if (!data) return false;
  for (int i = 0; i < 4; i++) {
    if (data->corner_content[i] == content) return true;
  }
  return data->upper_middle_line1_content == content || data->upper_middle_line2_content == content
      || data->bottom_middle_line1_content == content || data->bottom_middle_line2_content == content
      || data->middle_left_line1_content == content || data->middle_left_line2_content == content
      || data->middle_right_line1_content == content || data->middle_right_line2_content == content;
}

void features_layer_refresh_second_slots(Layer *layer) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  features_recompute_second_slots(state);
  layer_mark_dirty(layer);
}

void features_layer_refresh_content(Layer *layer, uint8_t content) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  eclipse_ui_get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active || state->slots[i].content != content) continue;
    features_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
  layer_mark_dirty(layer);
}
