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
#include "feature_slot.h"
#include "feature_values.h"
#include <string.h>
#include <stdlib.h> // atoi(), for parsing strftime's "%V" week-number string back to an int for grading

// See features_layer.h for the module-level design note (metadata cache
// vs. per-redraw layout recompute). This file also owns the shared
// features_draw_text_outlined()/features_contrasting_outline_color() outline primitives and
// the corner/edge custom-font plumbing (features_ensure_corner_custom_font() etc.)
// -- both used outside this module too (the countdown label and the big-
// analog hands/small-analog panel, respectively), which is why they're
// declared in features_layer.h rather than kept private.

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
//     resolved by feature_values_compute_slot(), implemented in feature_values.c,
//     which groups content into per-category functions instead of one giant
//     per-content switch.
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

// FeatureSlot/RenderSegment and slot constants live in feature_slot.h.\n\n// Content-specific formatting and service reads live in feature_values.c.\n\n// per-icon-kind lookup every icon always used, text is measured
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

  feature_values_compute_slot(slot, data, main_color, accent_color, bg_color, now, t);

  if (slot->segment_count > 0) {
    resolve_segment_offsets(slot, font, font_h);
  }
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
  // see feature_values_compute_slot()'s own content==0 early return)
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
