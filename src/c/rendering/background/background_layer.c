#include "./background_layer.h"
#include "../render_math.h"
#include "./background_overlays.h"
#include "../../data/eclipse_status.h"
#include "../../data/eclipse_ui.h"
#include "../../graphics/subpixel.h"
#include "../../features/feature_layout.h"
#include "./marker_layer.h"
#include "./weather_layer.h"
#include "./weather_effects.h"
#include "./celestial_layer.h"
#include "./celestial_bodies.h"
#include "./celestial_ephemeris.h"
#include "./sky_layer.h"
#include "./background_cache.h"
#include "./background_animation.h"

// Marker rendering lives in marker_layer.c. The background canvas owns the

// Height of the black "horizon" strip at the bottom of the canvas --
#define SKY_TOP_MARGIN 20


// 4x4 ordered (Bayer) dither matrix, values 0-15. The same matrix is

typedef struct {
  EclipseData *data;
  bool show_labels;
  int last_eclipse_phase;
                              // the moment this changes, rather than waiting for the
                              // normal once-a-minute cadence to happen to catch up
  time_t last_eclipse_max;
// vibration below tell "still the same eclipse, already
  bool max_vibrated;
  bool last_iss_visible;
  time_t storm_flash_end;    // 0 = no lightning flash in progress; otherwise the time
                               // (see draw_clouds_realistic's storm-flash comment) the
                               // current flash finishes and the sky reverts to normal
  bool storm_flash_was_active; // storm_flash_end > now as of the last tick -- lets the
// once-a-second throttle check (which runs before the
  bool bg_anim_active;       // "animate background on start" -- see background_layer_set_background_animation()
  uint16_t bg_anim_elapsed_ms; // and canvas_update_proc's own use of both these fields
  bool planet_seek_active;      // "Planet seek" (shake_anim_mode 2 or 3) -- see
  uint16_t planet_seek_elapsed_ms; // background_layer_set_planet_seek() and canvas_update_proc's
  int32_t planet_seek_heading_deg; // own use of these three fields
  CelestialLayerState celestial;

  MarkerLayerState markers;
  BackgroundCache cache;
} CanvasState;

// ---- hour/second markers (sub-pixel & rotation fix) --------------------

// Symmetric integer division with rounding to nearest integer for sub-pixel precision
static int32_t bg_anim_ease_out_1000(int32_t t) {
  int32_t inv = 1000 - t;
  int64_t inv3 = ((int64_t)inv * inv * inv) / 1000000;
  int32_t r = 1000 - (int32_t)inv3;
  return (r > 1000) ? 1000 : r;
}




static void canvas_update_proc(Layer *layer, GContext *ctx) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  EclipseData *d = state->data;
  GRect bounds = layer_get_bounds(layer);
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  time_t now = time(NULL);

  if (!d->valid) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    const char *msg = "Waiting for phone...";
    if (d->error_code == 1) msg = "Location unavailable";
    else if (d->error_code == 2) msg = "Calculation error";
    else if (d->error_code == 3) msg = "Couldn't reach watch";
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, msg,
                        fonts_get_system_font(FONT_KEY_GOTHIC_18),
                        GRect(0, center.y - 10, bounds.size.w, 20),
                        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

// "Animate background on start": sweeps the Sun/Moon/planets' own
  time_t sky_now = now;
  if (state->bg_anim_active && d->bg_anim_mode == 1) {
    int32_t progress = ((int32_t)state->bg_anim_elapsed_ms * 1000) / BACKGROUND_ANIMATION_DURATION_MS;
    if (progress > 1000) progress = 1000;
    int32_t eased = bg_anim_ease_out_1000(progress);
    time_t past = now - 2 * 3600; // "a couple hours ago"
    sky_now = past + (time_t)(((int64_t)(now - past) * eased) / 1000);
  }

// Computed early -- cheap (just interpolation, no drawing) -- since
  int16_t alt = celestial_interp_sun_alt_decideg(d, sky_now);
  bool sky_is_dark = alt <= -60;
// Space-view sky mode has no atmosphere to dim the sky in the first
  bool sky_dark_for_bodies = sky_is_dark || d->sky_mode == 2;

// Shared by every Sun/Moon/planet paint call below: skip painting
  bool skip_body_paint = state->planet_seek_active || (state->bg_anim_active && d->bg_anim_mode == 1);

// Same "skip it here, draw it fresh as an overlay after the cache
  bool skip_marker_paint = state->bg_anim_active && d->bg_anim_mode == 2;

  int current_phase = celestial_compute_eclipse_phase(d, now);
  bool current_iss_visible = celestial_compute_iss_visible(d, now, sky_dark_for_bodies);
  bool phase_just_changed = current_phase != state->last_eclipse_phase;
  bool was_first_draw = (state->last_eclipse_phase == -1);

// Fires once per eclipse at the moment of greatest eclipse itself
  if (d->max_t != state->last_eclipse_max) {
    state->last_eclipse_max = d->max_t;
    state->max_vibrated = false;
  }
  bool just_passed_max = d->has_eclipse && d->max_t != 0 && !state->max_vibrated && now >= d->max_t;

// The sky/sun/moon/clouds/planets barely change within a minute,
  bool storm_now = d->sky_mode == 0 && d->weather_condition == 4;
  bool flash_currently_active = storm_now && now < state->storm_flash_end;
  if (storm_now && !flash_currently_active) {
// Deterministic pseudo-random hash of the current second, not a
    uint32_t h = (uint32_t)now * 2654435761u;
    if ((h & 0xFF) < 6) {
      state->storm_flash_end = now + 1; // strikes read as a single ~1s flash
      flash_currently_active = true;
    }
  }
  bool storm_flash_transition = flash_currently_active != state->storm_flash_was_active;
  state->storm_flash_was_active = flash_currently_active;

  bool need_full_draw = phase_just_changed
    || just_passed_max
    || current_iss_visible != state->last_iss_visible
    || storm_flash_transition;
  if (!need_full_draw && background_cache_needs_refresh(&state->cache, now)) {
    need_full_draw = true;
  }

  // The cache is normally useful because the sky/celestial composition only
  // needs to be rebuilt periodically.  During an animation, however, the
  // celestial positions are time-dependent and must be recomputed on every
  // frame.  Blitting the cached frame here used to make the screen redraw
  // correctly while `state->celestial` remained frozen at the first animation
  // frame.  Planet-seek in particular made this obvious with longer shake
  // durations: labels continued to redraw, but the bodies stopped following
  // their current positions.  The startup planet sweep has the same
  // requirement.
  bool animation_needs_fresh_celestial =
      state->planet_seek_active ||
      (state->bg_anim_active && d->bg_anim_mode == 1);

  if (!need_full_draw && !animation_needs_fresh_celestial &&
      background_cache_blit(&state->cache, ctx, bounds)) {
    if (state->planet_seek_active) {
      GColor bg, main_color, accent_color;
      eclipse_ui_get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
      celestial_bodies_draw_planet_seek(ctx, bounds, d, &state->celestial, now,
                                       state->planet_seek_heading_deg,
                                       celestial_bodies_planet_seek_eased_t_1000(state->planet_seek_elapsed_ms, d),
                                       d->label_style, main_color, state->planet_seek_elapsed_ms);
    }
    if (state->bg_anim_active && d->bg_anim_mode == 1) {
      celestial_layer_draw_bg_anim_planets(ctx, bounds, d, &state->celestial);
    }
    if (state->bg_anim_active && d->bg_anim_mode == 2) {
      background_overlays_draw_marker_animation(ctx, &state->markers, d, bounds, now, state->bg_anim_elapsed_ms, BACKGROUND_ANIMATION_DURATION_MS);
    }
    return;
  }

// A brief double-buzz on a real contact-time crossing (C1/C2/C3/C4,
  if (phase_just_changed && !was_first_draw && current_phase >= 2 && d->vibrate_on_phase_change) {
    vibes_double_pulse();
  }
// Same idea, but for the moment of greatest eclipse itself (see
  if (just_passed_max && !was_first_draw && d->vibrate_on_phase_change) {
    vibes_double_pulse();
  }
  if (just_passed_max) state->max_vibrated = true; // mark done either way -- was_first_draw just suppresses the buzz itself, not the bookkeeping

  state->last_eclipse_phase = current_phase;
  state->last_iss_visible = current_iss_visible;

  uint8_t cloud_pct = sky_layer_interp_cloud_pct(d, now);
  bool stormy = d->weather_condition == 4;

// sky_mode: 0=Weather sky (below, unchanged), 1=Clear sky (same
  bool weather_enabled = d->sky_mode == 0;
  if (d->sky_mode == 2) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  } else {
    SkyRgb sky_top_rgb, sky_hz_rgb;
    sky_layer_colors_for_altitude(alt, &sky_top_rgb, &sky_hz_rgb);

// Digital top layout only: this canvas is the BOTTOM
    bool is_digital_top = feature_layout_is_digital_top_layout(d->bottom_style);
    int16_t virtual_top_y = is_digital_top ? DIGITAL_PANEL_H : bounds.origin.y;
    int16_t virtual_total_h = is_digital_top ? (DIGITAL_PANEL_H + bounds.size.h) : bounds.size.h;

// Beneath an overcast deck the sky reads grayer, like the view
    uint8_t gray_amount = 0;
    if (weather_enabled) {
      if (cloud_pct > 35) {
        int32_t g = ((int32_t)(cloud_pct - 35) * 100) / 65;
        gray_amount = (uint8_t)(g > 100 ? 100 : g);
      }
      if (stormy) {
        if (gray_amount < 85) gray_amount = 85;
      } else if (d->weather_condition == 2 || d->weather_condition == 3) {
        if (gray_amount < 55) gray_amount = 55;
      }
    }

    SkyRgb band_rgb = sky_hz_rgb;
    SkyRgb hz_rgb = sky_hz_rgb; // what actually reaches fill_sky_gradient_ex's horizon row
    int16_t band_y_screen = virtual_top_y + virtual_total_h; // off-canvas: no visible band by default
    if (gray_amount > 0) {
// The gradient can fade back UP to the raw (often bright/
      SkyRgb neutral_gray = { 115, 117, 120 };
      SkyRgb dark_gray = { 40, 41, 46 };
      band_rgb.r = render_math_lerp8(sky_hz_rgb.r, neutral_gray.r, gray_amount, 100);
      band_rgb.g = render_math_lerp8(sky_hz_rgb.g, neutral_gray.g, gray_amount, 100);
      band_rgb.b = render_math_lerp8(sky_hz_rgb.b, neutral_gray.b, gray_amount, 100);
      hz_rgb.r = render_math_lerp8(sky_hz_rgb.r, dark_gray.r, gray_amount, 100);
      hz_rgb.g = render_math_lerp8(sky_hz_rgb.g, dark_gray.g, gray_amount, 100);
      hz_rgb.b = render_math_lerp8(sky_hz_rgb.b, dark_gray.b, gray_amount, 100);
      band_y_screen = sky_layer_compute_cloud_band_y_virtual(virtual_top_y, virtual_total_h, d->cloud_altitude_pct);
    }

    sky_layer_fill_gradient(ctx, bounds, virtual_top_y, virtual_total_h, sky_top_rgb, band_rgb, band_y_screen, hz_rgb);
  }

// "Dark enough to see planets/meteors" -- same threshold used
  bool meteors_visible = sky_is_dark && d->meteor_intensity > 0 && d->sky_mode != 2;
  GPoint meteor_label_point = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + 40);
  if (meteors_visible) {
    weather_effects_draw_meteors(ctx, bounds, d->meteor_intensity);
  }

  GColor sun_fill_color;
  GColor sun_outline_color;
  bool fullscreen_sun = (d->bottom_style == 1) && d->has_eclipse && now >= d->c1 && now <= d->c4;
  bool is_space_view = fullscreen_sun || d->sky_mode == 2;
  SkyRgb sun_rgb = is_space_view
    ? sky_layer_space_sun_color()
    : sky_layer_sun_color_for_altitude(alt);
  sun_fill_color = GColorFromRGB(sun_rgb.r, sun_rgb.g, sun_rgb.b);
  sun_outline_color = GColorFromRGB((uint8_t)((uint16_t)sun_rgb.r * 55 / 100),
                                    (uint8_t)((uint16_t)sun_rgb.g * 55 / 100),
                                    (uint8_t)((uint16_t)sun_rgb.b * 55 / 100));
  bool suppress_other_bodies_for_eclipse = d->sky_mode == 2 && eclipse_status_is_active(d, now);
  celestial_layer_update(&state->celestial, ctx, bounds, d, now, sky_now,
                         skip_body_paint, suppress_other_bodies_for_eclipse,
                         sun_fill_color, sun_outline_color);

// Aurora: dark sky, opted in, and the current Kp index plausibly
  bool aurora_visible = d->sky_mode != 2 && d->aurora_enabled && sky_is_dark && d->aurora_visibility_pct > 15;
  GPoint aurora_label_point = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + SKY_TOP_MARGIN + 20);
  if (aurora_visible) {
    weather_effects_draw_aurora(ctx, bounds, d->aurora_visibility_pct, d->aurora_kp_x10);
  }

// Cloud clusters, drawn last so they visibly sit in front of (and
  if (weather_enabled && !state->planet_seek_active) {
    SkyRgb cached_sun_rgb = fullscreen_sun
      ? sky_layer_space_sun_color()
      : sky_layer_sun_color_for_altitude(alt);
    weather_layer_draw_clouds(ctx, bounds, cloud_pct, d->cloud_altitude_pct, d->vis_score_pct, stormy,
                              state->celestial.sun_center, state->celestial.sun_up, flash_currently_active, alt,
                              cached_sun_rgb.r, cached_sun_rgb.g, cached_sun_rgb.b);
    weather_effects_draw_effect(ctx, bounds, d->weather_condition, cloud_pct, d->cloud_altitude_pct);
  }

// Shake-to-reveal: brief name labels next to whichever bodies are
  if (state->show_labels) {
    GColor label_bg, label_main_color, label_accent;
    eclipse_ui_get_active_color_scheme(d, now, &label_bg, &label_main_color, &label_accent);
// Sun/Moon/planets/stars/ISS only get their plain static label
    if (!state->planet_seek_active) {
      celestial_layer_draw_labels(ctx, bounds, d, &state->celestial, d->label_style, label_main_color);
    }
    if (meteors_visible) celestial_layer_draw_label(ctx, bounds, meteor_label_point,
                                     d->meteor_shower_name[0] != '\0' ? d->meteor_shower_name : "Meteors",
                                     d->label_style, label_main_color);
    if (aurora_visible) {
      static char aurora_label_buf[16];
      snprintf(aurora_label_buf, sizeof(aurora_label_buf), "Aurora Kp %d.%d", d->aurora_kp_x10 / 10, d->aurora_kp_x10 % 10);
      celestial_layer_draw_label(ctx, bounds, aurora_label_point, aurora_label_buf, d->label_style, label_main_color);
    }
  }

// Hour/second markers -- analog mode only. Drawn on top of
  if (d->bottom_style == 1 && !skip_marker_paint) {
    GColor bg, main_color, accent_color;
    eclipse_ui_get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
    marker_layer_draw(ctx, &state->markers, center, bounds, d, main_color, accent_color, bg, false, 0, d->draw_debug);
  }

// Cache the base composition before drawing transient overlays. The cache
  GPoint capture_screen_origin = layer_convert_point_to_screen(layer, bounds.origin);
  GRect capture_bounds = GRect(capture_screen_origin.x, capture_screen_origin.y, bounds.size.w, bounds.size.h);
  background_cache_capture(&state->cache, ctx, capture_bounds);
  background_cache_commit(&state->cache, now);

  if (state->planet_seek_active) {
    GColor bg, main_color, accent_color;
    eclipse_ui_get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
    celestial_bodies_draw_planet_seek(ctx, bounds, d, &state->celestial, now,
                                       state->planet_seek_heading_deg,
                                       celestial_bodies_planet_seek_eased_t_1000(state->planet_seek_elapsed_ms, d),
                                       d->label_style, main_color, state->planet_seek_elapsed_ms);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 2) {
    background_overlays_draw_marker_animation(ctx, &state->markers, d, bounds, now, state->bg_anim_elapsed_ms, BACKGROUND_ANIMATION_DURATION_MS);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 1) {
    celestial_layer_draw_bg_anim_planets(ctx, bounds, d, &state->celestial);
  }
}

Layer *background_layer_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(CanvasState));
  if (!layer) return NULL;
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->data = NULL;
  state->show_labels = false;
  state->last_eclipse_phase = -1; // sentinel: guaranteed to differ from celestial_compute_eclipse_phase()'s 0-5
  state->last_eclipse_max = 0;
  state->max_vibrated = false;
  state->last_iss_visible = false;
  celestial_layer_init(&state->celestial);
  background_cache_init(&state->cache, frame.size);
  marker_layer_init(&state->markers);
  layer_set_update_proc(layer, canvas_update_proc);
  return layer;
}

void background_layer_destroy(Layer *layer) {
  if (!layer) return;
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  background_cache_deinit(&state->cache);
  marker_layer_deinit(&state->markers);
  layer_destroy(layer);
}

void background_layer_set_data(Layer *layer, EclipseData *data) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->data = data;
  background_cache_invalidate(&state->cache);
  layer_mark_dirty(layer);
}

void background_layer_set_labels_visible(Layer *layer, bool show) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->show_labels = show;
  background_cache_invalidate(&state->cache);
  layer_mark_dirty(layer);
}

void background_layer_set_background_animation(Layer *layer, bool active, uint16_t elapsed_ms) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  bool was_active = state->bg_anim_active;
  state->bg_anim_active = active;
  state->bg_anim_elapsed_ms = elapsed_ms;
// Marker animation changes only its transient overlay on top of an
  uint8_t mode = state->data ? state->data->bg_anim_mode : 1;
  if (mode == 1 || active != was_active) background_cache_invalidate(&state->cache);
  layer_mark_dirty(layer);
}

// Deliberately does NOT force a full redraw the way background_layer_set_
void background_layer_set_planet_seek(Layer *layer, bool active, uint16_t elapsed_ms, int32_t heading_deg) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  bool was_active = state->planet_seek_active;
  state->planet_seek_active = active;
  state->planet_seek_elapsed_ms = elapsed_ms;
  state->planet_seek_heading_deg = heading_deg;
  if (active != was_active) background_cache_invalidate(&state->cache); // the one moment it DOES need a full draw: entering/leaving the mode, so the cache/backdrop itself gets refreshed at the right moment
  layer_mark_dirty(layer);
}

// Lightweight per-second nudge: marks the layer dirty (so the OS
void background_layer_tick(Layer *layer) {
  layer_mark_dirty(layer);
}
