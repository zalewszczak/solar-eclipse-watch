#include <pebble.h>
#include "clock_display.h"
#include "../features/feature_layout.h"
#include "../features/feature_render.h"
#include "../hands/hands_controller.h"
#include "../services/font_lookup.h"
#include "../domain/eclipse_ui.h"

static EclipseData *s_data;
static Layer *s_panel_layer;
static Layer *s_countdown_layer;
static GColor s_countdown_text_color;
static char s_countdown_buf[40];
static GFont s_clock_font;
static FontSlot s_clock_font_slot = FONT_SLOT_EMPTY;

static void draw_digital_clock_panel(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  bool is_top = feature_layout_is_digital_top_layout(s_data->bottom_style);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Startup animation: substitutes an eased count-up from 120 minutes
  // before the real time (not all the way from midnight -- a many-
  // hour jump used to make the digits blur through most of the day
  // in under 1.5s, which read as noisy digit-flicker rather than a
  // deliberate sweep) up to the real time, for the DISPLAYED time
  // only -- `now`/the color scheme below still use the real current
  // time. Every read of `t->tm_hour`/`tm_min`/`tm_sec` below shares
  // this one struct tm, so the digital HH:MM(:SS) text gets the
  // count-up for free from this single substitution. Uses the same
  // shared ease-out table every other "settles gracefully into place"
  // animation in this app uses (fast at first, gradually slowing into
  // the real time) -- this used to accelerate INTO the stop instead
  // (ease-in), which read as an abrupt halt right at the end.
  struct tm anim_tm;
  if (hands_controller_animation_active()) {
    int32_t eased = hands_controller_animation_eased_progress_1000();
    time_t anim_start = now - 120 * 60; // 2 hours before the real time
    int32_t total_seconds = (int32_t)(now - anim_start);
    int32_t fake_seconds = (int32_t)(((int64_t)total_seconds * eased) / 1000);
    time_t fake_time = anim_start + fake_seconds;
    anim_tm = *localtime(&fake_time);
    t = &anim_tm;
  }

  GColor bg, text_color, accent_color;
  eclipse_ui_get_active_color_scheme(s_data, now, &bg, &text_color, &accent_color);

  if (!is_top) {
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  }

  // show_seconds is only ever true here for a font PKJS has already
  // determined can fit a full "HH:MM:SS" readout at its normal size --
  // wide fonts have seconds forced off and the checkbox hidden
  // entirely on the settings page, so there's no on-watch "does this
  // font fit seconds" decision left to make, and no small-side-digit
  // fallback rendering needed for the fonts that don't.
  char time_buf[10];
  if (s_data->show_seconds) {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S", t);
  } else {
    strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  }

  // Shifts away from whichever single side-feature column is active
  // (feature_layout_digital_side_mode() 2 or 3, regardless of which layout), or stays
  // centered/full-width otherwise (0, or 4 with both columns on -- see
  // feature_layout_digital_clock_area()'s own comment for why "both" doesn't shrink
  // the clock further). The features_layer overlay draws the side
  // columns themselves and the single bottom feature (which used to be
  // the fixed date/sun-time row directly below, now a user-selectable
  // content slot instead) -- this layer only ever draws the clock
  // digits.
  int16_t clock_x, clock_w;
  feature_layout_digital_clock_area(s_data->bottom_style, bounds.size.w, &clock_x, &clock_w);
  int16_t font_h = font_lookup_height(s_data->clock_font) + font_lookup_y_offset(s_data->clock_font);
  // Centered 24px from whichever edge of the panel sits next to the
  // clock's own "inner" boundary with the sky -- the panel's own top
  // for Digital bar (the sky is right above it), its own bottom for
  // Digital top (mirrored -- the sky is right below it instead), so
  // the two layouts read as a literal vertical flip of one another
  // rather than each being independently tuned.
  int16_t clock_y =  (is_top ? 44 : 24) - font_h / 2;
  GRect clock_rect = GRect(bounds.origin.x + clock_x, bounds.origin.y + clock_y, clock_w, font_h);
  GTextAlignment alignment = GTextAlignmentCenter;

  // Trick to avoid clipping of shifted clocks and not having text trimmed (...) or having clock outside of the screen in extreme cases
  uint8_t side = feature_layout_digital_side_mode(s_data->bottom_style);
  if (side == 2) { // right side only -- shift left
    int16_t initial_allowed_area = clock_rect.size.w;
    clock_rect.size.w += 30;
    GSize calculated_size = graphics_text_layout_get_content_size(time_buf, s_clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment);
    if (calculated_size.w >= initial_allowed_area) {
      alignment = GTextAlignmentLeft;
    } else {
      // revert spacing coz we fit
      clock_rect.size.w -= 30;
    }
  } else if (side == 3) { // left side only -- shift right
    int16_t initial_allowed_area = clock_rect.size.w;
    clock_rect.size.w += 30;
    clock_rect.origin.x -= 30;
    GSize calculated_size = graphics_text_layout_get_content_size(time_buf, s_clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment);
    if (calculated_size.w >= initial_allowed_area) {
      alignment = GTextAlignmentRight;
    } else {
      // revert spacing coz we fit
      clock_rect.size.w -= 30;
      clock_rect.origin.x += 30;
    }
  }

  if (s_data->draw_debug){
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorCyan);
    graphics_draw_rect(ctx, clock_rect);
  }

  // ---- big time ----
  // Digital top only: routed through the shared outline primitive
  // (same one corner/edge text and the countdown label already use)
  // instead of a plain graphics_draw_text() -- this is the one clock
  // panel with sky visible directly behind it, so it's the one place
  // outline_style's existing "stay readable over any part of the sky"
  // job actually applies to the clock digits themselves. Digital bar's
  // own opaque backing has never needed this, so passing outline_style
  // 0 there (via features_draw_text_outlined's own no-op-at-0 handling) keeps
  // its look pixel-identical to before.
  if (is_top) {
    feature_render_draw_text_outlined(ctx, time_buf, s_clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment, text_color, s_data->outline_style);
  } else {
    graphics_context_set_text_color(ctx, text_color);
    graphics_draw_text(ctx, time_buf, s_clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment, NULL);
  }
  // The date/week-or-sunrise row that used to sit directly below the
  // clock is now the features_layer overlay's own "digital bottom"
  // feature slot (content-selectable in settings, defaulting to
  // "long date + sunrise/sunset" -- the exact information this fixed
  // row always showed) -- drawn by that layer, not this one.
}



// The countdown/status label used to be a plain TextLayer, but that
// has no way to draw a custom outline, so it's a plain Layer with its
// own update_proc instead -- reads whatever refresh_status_and_maybe_
// canvas() last stored in s_countdown_buf/s_countdown_text_color
// rather than taking them as parameters, since layer update_procs
// have a fixed signature.
static void countdown_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // This label floats directly over the busy sky view in Analog mode
  // and (since its own panel is transparent) Digital top too -- both
  // have real sky right behind it at this label's fixed position (near
  // the screen's top edge). Normally feature_render_draw_text_outlined()'s 4-shifted-
  // copy outline keeps it legible against any background there, but
  // with that setting off there's nothing else backing the text, so it
  // can disappear into a similarly-colored patch of sky. Give it a
  // solid pill background in that specific case instead
  // (feature_render_contrasting_outline_color() picks black or white, whichever
  // contrasts with the text color) -- outline mode already handles
  // legibility fine on its own, and Digital bar's own panel is already
  // a solid color the text sits on, so neither of those needs this
  // extra background.
  if (s_data->outline_style == 0 && (s_data->bottom_style == 1 || feature_layout_is_digital_top_layout(s_data->bottom_style)) && s_countdown_buf[0] != '\0') {
    GSize text_size = graphics_text_layout_get_content_size(s_countdown_buf, font, bounds,
                                                              GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
    int16_t pad_x = 6;
    GRect bg_rect = GRect(bounds.origin.x + (bounds.size.w - text_size.w) / 2 - pad_x,
                           bounds.origin.y, text_size.w + pad_x * 2, bounds.size.h);
    graphics_context_set_fill_color(ctx, feature_render_contrasting_outline_color(s_countdown_text_color));
    graphics_fill_rect(ctx, bg_rect, 4, GCornersAll);
  }

  feature_render_draw_text_outlined(ctx, s_countdown_buf, font, bounds,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                      s_countdown_text_color, s_data->outline_style);
}


void clock_display_init(EclipseData *data) {
  s_data = data;
}

void clock_display_deinit(void) {
  clock_display_destroy_countdown();
  clock_display_destroy_panel();
  font_lookup_release(&s_clock_font_slot);
  s_clock_font = NULL;
  s_data = NULL;
}

Layer *clock_display_panel_layer(void) { return s_panel_layer; }
Layer *clock_display_countdown_layer(void) { return s_countdown_layer; }

void clock_display_create_panel(Layer *parent, GRect frame) {
  clock_display_destroy_panel();
  s_panel_layer = layer_create(frame);
  if (!s_panel_layer) return;
  layer_set_update_proc(s_panel_layer, draw_digital_clock_panel);
  layer_add_child(parent, s_panel_layer);
}

void clock_display_destroy_panel(void) {
  if (s_panel_layer) {
    layer_destroy(s_panel_layer);
    s_panel_layer = NULL;
  }
}

void clock_display_create_countdown(Layer *parent, GRect frame) {
  clock_display_destroy_countdown();
  s_countdown_layer = layer_create(frame);
  if (!s_countdown_layer) return;
  layer_set_update_proc(s_countdown_layer, countdown_layer_update_proc);
  s_countdown_text_color = GColorBlack;
  s_countdown_buf[0] = '\0';
  layer_add_child(parent, s_countdown_layer);
}

void clock_display_destroy_countdown(void) {
  if (s_countdown_layer) {
    layer_destroy(s_countdown_layer);
    s_countdown_layer = NULL;
  }
}

void clock_display_reparent_countdown(Layer *parent) {
  if (!s_countdown_layer) return;
  layer_remove_from_parent(s_countdown_layer);
  layer_add_child(parent, s_countdown_layer);
}

void clock_display_apply_font(void) {
  s_clock_font = font_lookup_resolve(&s_clock_font_slot, s_data->clock_font);
  clock_display_mark_panel_dirty();
}

void clock_display_mark_panel_dirty(void) {
  if (s_panel_layer) layer_mark_dirty(s_panel_layer);
}

void clock_display_mark_countdown_dirty(void) {
  if (s_countdown_layer) layer_mark_dirty(s_countdown_layer);
}

void clock_display_set_countdown(const char *text, GColor color, bool hidden) {
  if (text) {
    snprintf(s_countdown_buf, sizeof(s_countdown_buf), "%s", text);
  } else {
    s_countdown_buf[0] = '\0';
  }
  s_countdown_text_color = color;
  if (s_countdown_layer) {
    layer_set_hidden(s_countdown_layer, hidden);
    layer_mark_dirty(s_countdown_layer);
  }
}
