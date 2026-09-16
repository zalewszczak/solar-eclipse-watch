#include <pebble.h>
#include "./clock_display.h"
#include "../../features/feature_layout.h"
#include "../../features/feature_render.h"
#include "../../hands/hands_controller.h"
#include "../../fonts/font_lookup.h"
#include "../../data/eclipse_ui.h"

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

  // Startup animation: substitute an eased count-up from 120 minutes
  // before the real time. Limiting the span keeps the sweep readable while
  // still covering a useful portion of the current day. The substitution
  // affects DISPLAYED time only; `now` and the color scheme use real time.
  // only -- `now`/the color scheme below still use the real current
  // time. Every read of `t->tm_hour`/`tm_min`/`tm_sec` below shares
  // this one struct tm, so the digital HH:MM(:SS) text gets the
  // count-up for free from this single substitution. Uses the same
  // shared ease-out table every other "settles gracefully into place"
  // animation in this app uses (fast at first, gradually slowing into
  // the real time) -- the shared easing decelerates into the target.
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

  // Shift the clock away from the active side-feature column.
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

  // Reduce spacing if the shifted clock would clip.
  uint8_t side = feature_layout_digital_side_mode(s_data->bottom_style);
  if (side == 2) { // right side only -- shift left
    int16_t initial_allowed_area = clock_rect.size.w;
    clock_rect.size.w += 30;
    GSize calculated_size = graphics_text_layout_get_content_size(time_buf, s_clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment);
    if (calculated_size.w >= initial_allowed_area) {
      alignment = GTextAlignmentLeft;
    } else {
      // Restore spacing after the fit check.
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
      // Restore spacing after the fit check.
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
  // Digital top uses the shared outlined-text renderer; Digital bar remains unoutlined.
  if (is_top) {
    feature_render_draw_text_outlined(ctx, time_buf, s_clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment, text_color, s_data->outline_style);
  } else {
    graphics_context_set_text_color(ctx, text_color);
    graphics_draw_text(ctx, time_buf, s_clock_font, clock_rect, GTextOverflowModeTrailingEllipsis, alignment, NULL);
  }
  // The date/week-or-sunrise content is drawn by the features_layer
  // overlay as its "digital bottom"
  // feature slot (content-selectable in settings, defaulting to
  // "long date + sunrise/sunset" -- the exact information this fixed
  // row always showed) -- drawn by that layer, not this one.
}



// Countdown/status uses a dedicated layer so it can draw outlined text.
static void countdown_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  // Add a contrasting pill only when outline mode is disabled on a transparent panel.
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
