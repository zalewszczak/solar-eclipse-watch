#include "background_layer.h"
#include "subpixel.h"
#include "features_layer.h"
#include "marker_layer.h"
#include "weather_layer.h"
#include "celestial_layer.h"
#include "sky_layer.h"
#include "background_cache.h"

// ---------------------------------------------------------------------------
// Marker rendering lives in marker_layer.c. The background canvas owns the
// cached sky and decides WHEN markers need to be painted; marker_layer owns
// the geometry, text, bitmap resources, and marker animation itself.
// ---------------------------------------------------------------------------

// Height of the black "horizon" strip at the bottom of the canvas --
// also where altitude 0 lines up, so the sun/moon visibly sink behind
// it as they set (it's drawn last, on top, so it naturally clips
// whatever's behind it once a disc's center passes below this line).
// Shared sky-canvas geometry. Celestial body positioning uses the same
// horizon and top-margin values through celestial_layer.h.
#define SKY_TOP_MARGIN 20


// 4x4 ordered (Bayer) dither matrix, values 0-15 -- shared from subpixel.h
// now (see that header's own comment on why keeping a duplicate here
// would actually be a compile error, not just redundant). Used to
// stipple the cloud puffs at a density proportional to cloud cover %,
// rather than drawing them as flat filled shapes -- keeps them looking
// like e-paper "clouds" rather than solid gray blobs, and lets the sky
// and sun show through underneath.

typedef struct {
  EclipseData *data;
  bool show_labels; // shake-to-reveal Sun/Moon/planet name labels
  int last_eclipse_phase;   // see celestial_compute_eclipse_phase(): forces an immediate redraw
                              // the moment this changes, rather than waiting for the
                              // normal once-a-minute cadence to happen to catch up
  time_t last_eclipse_max;   // d->max_t last seen -- lets the "just passed greatest eclipse"
                               // vibration below tell "still the same eclipse, already
                               // handled" apart from "a genuinely new eclipse's max time
                               // just arrived", across the repeated set_data() calls a
                               // normal refresh cycle causes throughout the same eclipse day
  bool max_vibrated;          // fired the "at maximum eclipse" vibration yet for last_eclipse_max?
  bool last_iss_visible;     // same idea, for the ISS appearing/disappearing
  time_t storm_flash_end;    // 0 = no lightning flash in progress; otherwise the time
                               // (see draw_clouds_realistic's storm-flash comment) the
                               // current flash finishes and the sky reverts to normal
  bool storm_flash_was_active; // storm_flash_end > now as of the last tick -- lets the
                                 // once-a-second throttle check (which runs before the
                                 // expensive full redraw below even happens) notice the
                                 // instant a flash starts or ends and force a redraw for
                                 // just that transition, without abandoning the normal
                                 // once-a-minute cadence the rest of the time
  bool bg_anim_active;       // "animate background on start" -- see eclipse_canvas_set_bg_anim()
  uint16_t bg_anim_elapsed_ms; // and canvas_update_proc's own use of both these fields
  bool planet_seek_active;      // "Planet seek" (shake_anim_mode 2 or 3) -- see
  uint16_t planet_seek_elapsed_ms; // eclipse_canvas_set_planet_seek() and canvas_update_proc's
  int32_t planet_seek_heading_deg; // own use of these three fields
  CelestialLayerState celestial;

  MarkerLayerState markers;
  BackgroundCache cache;
} CanvasState;

// ---- hour/second markers (sub-pixel & rotation fix) --------------------

// Symmetric integer division with rounding to nearest integer for sub-pixel precision
static inline int32_t div_round(int32_t num, int32_t den) {
  if (den == 0) return 0;
  if ((num ^ den) >= 0) {
    return (num + den / 2) / den;
  } else {
    return (num - den / 2) / den;
  }
}

// Same idea again, for the full-day cloud-cover samples (0-100 %).
// Decelerates into the target -- "animate background on start"'s own
// easing (graceful, slowing down at the end), separate from
// pebble-eclipse-watch.c's identically-behaved ease_out_cubic_1000
// (used for its own, unrelated clock-hand animation) since these are
// two different translation units and this is a small enough helper
// that duplicating it is simpler than threading a shared declaration
// through a header for one function -- same reasoning subpixel.h's
// own top-of-file comment already gives for this project's small
// self-contained helpers generally.
#define BG_ANIM_MS 1400 // must match pebble-eclipse-watch.c's own BG_ANIM_MS -- same duplication reasoning as above
static int32_t bg_anim_ease_out_1000(int32_t t) {
  int32_t inv = 1000 - t;
  int64_t inv3 = ((int64_t)inv * inv * inv) / 1000000;
  int32_t r = 1000 - (int32_t)inv3;
  return (r > 1000) ? 1000 : r;
}




static int32_t shake_anim_eased_t_1000(uint16_t elapsed, const EclipseData *d) {
  uint32_t duration_ms = (uint32_t)(d->shake_label_seconds > 0 ? d->shake_label_seconds : 3) * 1000;
  if (elapsed < 500) return bg_anim_ease_out_1000(((int32_t)elapsed * 1000) / 500);
  if (duration_ms > 500 && elapsed > duration_ms - 500) {
    int32_t remaining = (int32_t)duration_ms - (int32_t)elapsed;
    if (remaining < 0) remaining = 0;
    return bg_anim_ease_out_1000((remaining * 1000) / 500);
  }
  return 1000;
}

static void draw_bg_anim_markers_overlay(GContext *ctx, CanvasState *state, const EclipseData *d,
                                         GRect bounds, time_t now) {
  if (d->bottom_style != 1) return;
  GPoint center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
  int32_t progress_1000 = ((int32_t)state->bg_anim_elapsed_ms * 1000) / BG_ANIM_MS;
  if (progress_1000 > 1000) progress_1000 = 1000;
  marker_layer_draw(ctx, &state->markers, center, bounds, d, main_color, accent_color, bg, true, progress_1000, d->draw_debug);
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
  // effective observation time from a couple hours ago up to the real
  // `now`, eased to slow down toward the end -- since sky_colors_for_
  // altitude()/the Sun's own color/planet-visibility-threshold below
  // all key off whichever altitude this produces, substituting it in
  // place of `now` for just those position/color lookups (NOT
  // anything else in this function -- eclipse timing, the weather-
  // driven cloud coverage amount, the user's own day/night color
  // scheme, and the countdown label all keep using the real `now`)
  // cascades the sweep across the sky gradient, the Sun's disc color,
  // and every body's screen position all at once from this one
  // substitution point.
  time_t sky_now = now;
  if (state->bg_anim_active && d->bg_anim_mode == 1) {
    int32_t progress = ((int32_t)state->bg_anim_elapsed_ms * 1000) / BG_ANIM_MS;
    if (progress > 1000) progress = 1000;
    int32_t eased = bg_anim_ease_out_1000(progress);
    time_t past = now - 2 * 3600; // "a couple hours ago"
    sky_now = past + (time_t)(((int64_t)(now - past) * eased) / 1000);
  }

  // Computed early -- cheap (just interpolation, no drawing) -- since
  // the throttle decision below needs sky darkness to tell whether
  // the ISS's visibility just changed.
  int16_t alt = celestial_interp_sun_alt_decideg(d, sky_now);
  bool sky_is_dark = alt <= -60;
  // Space-view sky mode has no atmosphere to dim the sky in the first
  // place, so its celestial bodies (planets, ISS) are never gated by
  // brightness -- only by whether they're actually above the horizon,
  // same as the Sun/Moon already are in every mode. Stars get their
  // own separate always-drawn treatment further down, since they
  // don't exist at all outside this mode.
  bool sky_dark_for_bodies = sky_is_dark || d->sky_mode == 2;

  // Shared by every Sun/Moon/planet paint call below: skip painting
  // the body into this frame (it still gets fully positioned/sized as
  // normal, just not drawn) whenever something else is going to draw
  // it separately, on top of whatever gets cached here, instead --
  // Planet seek (see its own celestial.sun_center comment above) and now
  // "Planets" background-on-start mode too, which had the same
  // "duplicate" bug Planet seek was built to avoid: painting the
  // Sun/Moon/planets straight into the frame that becomes the reusable background cache
  // meant the LAST animated frame's positions stayed baked into that
  // cache, and every SUBSEQUENT blit-from-cache redraw (this canvas's
  // normal once-a-minute throttle) kept showing them there even as
  // real time moved the bodies elsewhere -- a second, stale copy
  // wherever they'd been mid-sweep, alongside the real one. See
  // draw_bg_anim_planets_overlay() below for the actual fix.
  bool skip_body_paint = state->planet_seek_active || (state->bg_anim_active && d->bg_anim_mode == 1);

  // Same "skip it here, draw it fresh as an overlay after the cache
  // capture" trick as skip_body_paint above, now for the "Markers"
  // background-on-start mode -- see draw_bg_anim_markers_overlay()'s
  // own comment for why its backdrop can be captured once (unlike
  // mode 1's, which keeps changing throughout the sweep) and only the
  // overlaid element needs to be fresh every frame.
  bool skip_marker_paint = state->bg_anim_active && d->bg_anim_mode == 2;

  int current_phase = celestial_compute_eclipse_phase(d, now);
  bool current_iss_visible = celestial_compute_iss_visible(d, now, sky_dark_for_bodies);
  bool phase_just_changed = current_phase != state->last_eclipse_phase;
  bool was_first_draw = (state->last_eclipse_phase == -1);

  // Fires once per eclipse at the moment of greatest eclipse itself
  // (d->max_t, always populated regardless of type), independent of the
  // phase-boundary vibration below -- that one only ever fires for
  // total/annular eclipses (crossing C1/C2/C3/C4), so a plain partial
  // eclipse never had ANY vibration at its actual visual climax before
  // this. Tracks last_eclipse_max (not just a bool) so repeated
  // set_data() calls for the same eclipse during a normal refresh
  // cycle don't re-arm and re-fire this after it's already happened,
  // while a genuinely new eclipse (different max time) correctly does.
  if (d->max_t != state->last_eclipse_max) {
    state->last_eclipse_max = d->max_t;
    state->max_vibrated = false;
  }
  bool just_passed_max = d->has_eclipse && d->max_t != 0 && !state->max_vibrated && now >= d->max_t;

  // The sky/sun/moon/clouds/planets barely change within a minute,
  // and this is an e-paper display, so the expensive part of this
  // redraw is self-throttled to once a minute -- tracked here rather
  // than only relying on the caller not to mark us dirty too often,
  // so the guarantee holds regardless of what triggers the redraw.
  // New data (set_data), a label toggle (set_show_labels), crossing
  // an eclipse phase boundary (C1/C2/C3/C4), passing the moment of
  // greatest eclipse, or the ISS appearing or disappearing all force
  // through immediately, since those are visible state changes that
  // must show up right away rather than waiting for the next
  // scheduled minute.
  //
  // Critically, the seconds *in between* don't just skip drawing --
  // Pebble doesn't guarantee a layer's previous pixels survive until
  // its next update_proc call, so doing nothing here flickered. The
  // real fix is a cached bitmap: on a full redraw we draw everything
  // as before, then capture the just-drawn framebuffer region into
  // the reusable background cache via the documented graphics_capture_frame_buffer() API;
  // on the throttled seconds we just blit that cached bitmap back,
  // which is cheap and always leaves valid pixels on screen.
  // Realistic-cloud lightning during a storm: a cheap per-second check
  // (not itself a redraw -- just arithmetic) for whether a flash
  // should start or stop right now, so a strike can appear/disappear
  // on the actual second it's due rather than waiting for the next
  // scheduled once-a-minute redraw. Deliberately NOT a general
  // exception to the once-a-minute throttle -- outside an active
  // storm this is always false and costs nothing extra; see
  // draw_clouds_realistic() for what a flash actually looks like.
  bool storm_now = d->sky_mode == 0 && d->weather_condition == 4;
  bool flash_currently_active = storm_now && now < state->storm_flash_end;
  if (storm_now && !flash_currently_active) {
    // Deterministic pseudo-random hash of the current second, not a
    // real RNG (nothing here needs cryptographic quality, and this
    // avoids persisting extra seed state) -- gives each second during
    // a storm roughly a 1-in-43 chance of being a strike, averaging
    // one flash every ~40s.
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

  if (!need_full_draw && background_cache_blit(&state->cache, ctx, bounds)) {
    if (state->planet_seek_active) {
      GColor bg, main_color, accent_color;
      get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
      celestial_layer_draw_planet_seek(ctx, bounds, d, &state->celestial, now,
                                       state->planet_seek_heading_deg,
                                       shake_anim_eased_t_1000(state->planet_seek_elapsed_ms, d),
                                       d->label_style, main_color);
    }
    if (state->bg_anim_active && d->bg_anim_mode == 1) {
      celestial_layer_draw_bg_anim_planets(ctx, bounds, d, &state->celestial);
    }
    if (state->bg_anim_active && d->bg_anim_mode == 2) {
      draw_bg_anim_markers_overlay(ctx, state, d, bounds, now);
    }
    return;
  }

  // A brief double-buzz on a real contact-time crossing (C1/C2/C3/C4,
  // i.e. current_phase 2-5) -- not on app launch happening to land
  // mid-eclipse (was_first_draw), and not on the earlier "there's an
  // eclipse today, waiting" 0->1 transition, which isn't really the
  // start of anything happening yet.
  if (phase_just_changed && !was_first_draw && current_phase >= 2 && d->vibrate_on_phase_change) {
    vibes_double_pulse();
  }
  // Same idea, but for the moment of greatest eclipse itself (see
  // just_passed_max above) -- this is the one that actually fires for
  // a plain partial eclipse, and for total/annular ones it's a second,
  // near-simultaneous buzz alongside the C2 phase-boundary one above
  // (max isn't guaranteed to land exactly at C2), which is a minor,
  // harmless redundancy rather than a bug.
  if (just_passed_max && !was_first_draw && d->vibrate_on_phase_change) {
    vibes_double_pulse();
  }
  if (just_passed_max) state->max_vibrated = true; // mark done either way -- was_first_draw just suppresses the buzz itself, not the bookkeeping

  state->last_eclipse_phase = current_phase;
  state->last_iss_visible = current_iss_visible;

  uint8_t cloud_pct = sky_layer_interp_cloud_pct(d, now);
  bool stormy = d->weather_condition == 4;

  // sky_mode: 0=Weather sky (below, unchanged), 1=Clear sky (same
  // day/night gradient, but weather_enabled below skips the haze and
  // the cloud/weather-effect calls further down), 2=Space view (no
  // gradient at all -- flat near-black, handled entirely in this
  // branch instead of falling through to sky_layer_fill_gradient()).
  bool weather_enabled = d->sky_mode == 0;
  if (d->sky_mode == 2) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  } else {
    SkyRgb sky_top_rgb, sky_hz_rgb;
    sky_layer_colors_for_altitude(alt, &sky_top_rgb, &sky_hz_rgb);

    // Digital top layout only: this canvas is the BOTTOM
    // DIGITAL_PANEL_H..(DIGITAL_PANEL_H+bounds.size.h) slice of a
    // conceptually 228px-tall gradient whose top DIGITAL_PANEL_H rows
    // are painted separately by sky_layer_top_gradient_*() (the
    // transparent panel's own reserved strip -- see that module's own
    // comment) -- computing this canvas's OWN band/horizon math
    // against that same taller virtual span, instead of just its own
    // local bounds, is what makes the two independently-drawn pieces
    // line up into one seamless gradient rather than each stretching
    // the same top/band/hz colors across its own shorter span and
    // visibly disagreeing at the seam. Every other layout keeps the
    // virtual span identical to `bounds` itself -- i.e. no change at
    // all from before this existed.
    bool is_digital_top = bottom_style_is_digital_top(d->bottom_style);
    int16_t virtual_top_y = is_digital_top ? DIGITAL_PANEL_H : bounds.origin.y;
    int16_t virtual_total_h = is_digital_top ? (DIGITAL_PANEL_H + bounds.size.h) : bounds.size.h;

    // Beneath an overcast deck the sky reads grayer, like the view
    // crossing under cloud cover from a plane window -- the effect
    // ramps in past 35% cover, and rain/snow/storm push it further
    // gray on top of whatever the coverage alone would give. Skipped
    // entirely in Clear sky mode -- no weather means no haze either.
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
      // The gradient used to fade back UP to the raw (often bright/
      // warm, especially at sunset/sunrise) horizon color right at
      // the bottom row, undoing the graying effect exactly where a
      // heavy deck should block the most light -- straight down,
      // near the ground. Now the horizon row darkens too, toward a
      // much darker target than the band itself (which sits right at
      // the cloud deck's own height, not blocked by anything above
      // it yet) -- so a heavy storm reads as "darkening further the
      // lower/closer to the ground you look" rather than just a flat
      // gray band that un-grays again beneath it.
      SkyRgb neutral_gray = { 115, 117, 120 };
      SkyRgb dark_gray = { 40, 41, 46 };
      band_rgb.r = sky_layer_lerp8(sky_hz_rgb.r, neutral_gray.r, gray_amount, 100);
      band_rgb.g = sky_layer_lerp8(sky_hz_rgb.g, neutral_gray.g, gray_amount, 100);
      band_rgb.b = sky_layer_lerp8(sky_hz_rgb.b, neutral_gray.b, gray_amount, 100);
      hz_rgb.r = sky_layer_lerp8(sky_hz_rgb.r, dark_gray.r, gray_amount, 100);
      hz_rgb.g = sky_layer_lerp8(sky_hz_rgb.g, dark_gray.g, gray_amount, 100);
      hz_rgb.b = sky_layer_lerp8(sky_hz_rgb.b, dark_gray.b, gray_amount, 100);
      band_y_screen = sky_layer_compute_cloud_band_y_virtual(virtual_top_y, virtual_total_h, d->cloud_altitude_pct);
    }

    sky_layer_fill_gradient(ctx, bounds, virtual_top_y, virtual_total_h, sky_top_rgb, band_rgb, band_y_screen, hz_rgb);
  }

  // "Dark enough to see planets/meteors" -- same threshold used
  // elsewhere for picking light vs dark overlay text, reused here for
  // consistency rather than inventing a second one. Meteors are an
  // atmospheric-entry phenomenon -- with no atmosphere left in space-
  // view mode, there's nothing for one to burn up in, so this stays
  // gated by the real sky_is_dark (not sky_dark_for_bodies) and is
  // additionally suppressed outright in that mode.
  bool meteors_visible = sky_is_dark && d->meteor_intensity > 0 && d->sky_mode != 2;
  GPoint meteor_label_point = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + 40);
  if (meteors_visible) {
    weather_layer_draw_meteors(ctx, bounds, d->meteor_intensity);
  }

  GColor sun_fill_color;
  GColor sun_outline_color;
  bool fullscreen_sun = (d->bottom_style == 1) && d->has_eclipse && now >= d->c1 && now <= d->c4;
  SkyRgb sun_rgb = fullscreen_sun
    ? sky_layer_space_sun_color()
    : sky_layer_sun_color_for_altitude(alt);
  sun_fill_color = GColorFromRGB(sun_rgb.r, sun_rgb.g, sun_rgb.b);
  sun_outline_color = GColorFromRGB((uint8_t)((uint16_t)sun_rgb.r * 55 / 100),
                                    (uint8_t)((uint16_t)sun_rgb.g * 55 / 100),
                                    (uint8_t)((uint16_t)sun_rgb.b * 55 / 100));
  bool suppress_other_bodies_for_eclipse = d->sky_mode == 2 && eclipse_is_active(d, now);
  celestial_layer_update(&state->celestial, ctx, bounds, d, now, sky_now,
                         skip_body_paint, suppress_other_bodies_for_eclipse,
                         sun_fill_color, sun_outline_color);

  // Aurora: dark sky, opted in, and the current Kp index plausibly
  // reaches this latitude (see eclipse_data.h's aurora_visibility_pct
  // comment) -- an atmospheric phenomenon like clouds, so it's absent
  // in Space view (sky_mode == 2, "no atmosphere") same as meteors,
  // but unlike clouds/weather it's NOT gated by weather_enabled --
  // Clear sky mode still shows it (arguably the more realistic
  // combination: clear skies are exactly when aurora is best seen).
  // Threshold of 15 (not >0) avoids drawing a barely-there glow for a
  // visibility estimate this approximate.
  bool aurora_visible = d->sky_mode != 2 && d->aurora_enabled && sky_is_dark && d->aurora_visibility_pct > 15;
  GPoint aurora_label_point = GPoint(bounds.origin.x + bounds.size.w / 2, bounds.origin.y + SKY_TOP_MARGIN + 20);
  if (aurora_visible) {
    weather_layer_draw_aurora(ctx, bounds, d->aurora_visibility_pct, d->aurora_kp_x10);
  }

  // Cloud clusters, drawn last so they visibly sit in front of (and
  // can partially obscure) the sun/moon, same as real clouds. Skipped
  // entirely outside Weather sky mode -- Clear sky and Space view
  // both represent weather-free skies by definition -- and, per
  // request, during Planet seek too (its own separate reason: weather
  // is suppressed for the whole animation, not just this one frame,
  // so it doesn't get baked into the bodies-free cache Planet seek
  // reuses every frame -- see celestial.sun_center's own comment above).
  if (weather_enabled && !state->planet_seek_active) {
    SkyRgb cached_sun_rgb = fullscreen_sun
      ? sky_layer_space_sun_color()
      : sky_layer_sun_color_for_altitude(alt);
    weather_layer_draw_clouds(ctx, bounds, cloud_pct, d->cloud_altitude_pct, d->vis_score_pct, stormy,
                              state->celestial.sun_center, state->celestial.sun_up, flash_currently_active, alt,
                              cached_sun_rgb.r, cached_sun_rgb.g, cached_sun_rgb.b);
    weather_layer_draw_effect(ctx, bounds, d->weather_condition, cloud_pct, d->cloud_altitude_pct);
  }

  // Shake-to-reveal: brief name labels next to whichever bodies are
  // actually on screen right now. Needs its own main_color -- the
  // scheme lookup below is otherwise only computed further down,
  // scoped to the big-analog marker-drawing block, and shake labels
  // apply in every mode.
  if (state->show_labels) {
    GColor label_bg, label_main_color, label_accent;
    get_active_color_scheme(d, now, &label_bg, &label_main_color, &label_accent);
    // Sun/Moon/planets/stars/ISS only get their plain static label
    // OUTSIDE Planet seek -- during it, draw_planet_seek_overlay()
    // (called separately, above) already drew each one its own live,
    // compass-tracked label, so drawing this fixed one too would lay
    // a second, non-moving label right on top of it. Aurora and
    // meteor showers below have no planet-seek equivalent of their
    // own (they're diffuse/wide-area sky elements, not a single
    // point-like body), so they keep revealing at their normal fixed
    // position regardless of mode -- per the request, they shouldn't
    // be panned around by the compass the way a point body is.
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
  // everything above (sky, sun/moon, clouds, labels) so they stay
  // visible over any part of the sky -- matching hands_layer_update_proc's
  // own positioning, which always uses the full unobstructed screen, so
  // markers and hands stay aligned with each other.
  // skip_marker_paint: this frame's markers get drawn afterward
  // instead, by draw_bg_anim_markers_overlay(), so the cache stays
  // marker-free for every subsequent cheap frame to blit and overlay
  // onto -- same "skip it here, draw it fresh as an overlay after"
  // trick skip_body_paint uses above for bg_anim_mode 1's bodies.
  if (d->bottom_style == 1 && !skip_marker_paint) {
    GColor bg, main_color, accent_color;
    get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
    marker_layer_draw(ctx, &state->markers, center, bounds, d, main_color, accent_color, bg, false, 0, d->draw_debug);
  }

  // Cache the base composition before drawing transient overlays. The cache
  // module owns the framebuffer capture/copy details; this layer only decides
  // which composition belongs in the reusable backdrop.
  background_cache_capture(&state->cache, ctx, bounds);
  background_cache_commit(&state->cache, now);

  if (state->planet_seek_active) {
    GColor bg, main_color, accent_color;
    get_active_color_scheme(d, now, &bg, &main_color, &accent_color);
    celestial_layer_draw_planet_seek(ctx, bounds, d, &state->celestial, now,
                                       state->planet_seek_heading_deg,
                                       shake_anim_eased_t_1000(state->planet_seek_elapsed_ms, d),
                                       d->label_style, main_color);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 2) {
    draw_bg_anim_markers_overlay(ctx, state, d, bounds, now);
  }
  if (state->bg_anim_active && d->bg_anim_mode == 1) {
    celestial_layer_draw_bg_anim_planets(ctx, bounds, d, &state->celestial);
  }
}

Layer *eclipse_canvas_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(CanvasState));
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

void eclipse_canvas_destroy(Layer *layer) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  background_cache_deinit(&state->cache);
  marker_layer_deinit(&state->markers);
  layer_destroy(layer);
}

void eclipse_canvas_set_data(Layer *layer, EclipseData *data) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->data = data;
  background_cache_invalidate(&state->cache);
  layer_mark_dirty(layer);
}

void eclipse_canvas_set_show_labels(Layer *layer, bool show) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  state->show_labels = show;
  background_cache_invalidate(&state->cache);
  layer_mark_dirty(layer);
}

void eclipse_canvas_set_bg_anim(Layer *layer, bool active, uint16_t elapsed_ms) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  bool was_active = state->bg_anim_active;
  state->bg_anim_active = active;
  state->bg_anim_elapsed_ms = elapsed_ms;
  // Mode 2 ("Markers") only ever animates its own overlaid element
  // (see draw_bg_anim_markers_overlay()'s own comment) on top of an
  // otherwise-unchanging backdrop -- same "full draw once on entering/
  // leaving the mode, cheap overlay every frame in between" shape
  // eclipse_canvas_set_planet_seek() below already uses -- so only the
  // active/inactive TRANSITION needs a genuine full redraw, not every
  // single frame. Mode 1 ("Planets") is different: its sky_now
  // substitution (see canvas_update_proc's own comment) means the
  // gradient/Sun color itself keeps changing throughout the sweep, not
  // just body position, so it still needs a real full redraw every
  // frame -- forced unconditionally here whenever it's the active
  // mode, same as every mode used to do. state->data may not be set
  // yet the very first time this is ever called (app launch, before
  // the first eclipse_canvas_set_data()); forcing in that case too is
  // the safe default.
  uint8_t mode = state->data ? state->data->bg_anim_mode : 1;
  if (mode == 1 || active != was_active) background_cache_invalidate(&state->cache);
  layer_mark_dirty(layer);
}

// Deliberately does NOT force a full redraw the way eclipse_canvas_set_
// bg_anim() above does -- Planet seek's whole point is to redraw ONLY
// the repositioned bodies each frame on top of a cached backdrop
// (see canvas_update_proc's own "Planet seek" section), so forcing
// the full, expensive gradient+clouds+markers pipeline on every single
// 33ms tick would defeat that entirely. Just updates state and marks
// the layer dirty so canvas_update_proc runs -- it decides for itself
// whether that means a full redraw or the lightweight Planet-seek path.
void eclipse_canvas_set_planet_seek(Layer *layer, bool active, uint16_t elapsed_ms, int32_t heading_deg) {
  CanvasState *state = (CanvasState *)layer_get_data(layer);
  bool was_active = state->planet_seek_active;
  state->planet_seek_active = active;
  state->planet_seek_elapsed_ms = elapsed_ms;
  state->planet_seek_heading_deg = heading_deg;
  if (active != was_active) background_cache_invalidate(&state->cache); // the one moment it DOES need a full draw: entering/leaving the mode, so the cache/backdrop itself gets refreshed at the right moment
  layer_mark_dirty(layer);
}

// Lightweight per-second nudge: marks the layer dirty (so the OS
// invokes canvas_update_proc), but does NOT force a redraw -- the
// canvas's own once-a-minute throttle inside canvas_update_proc
// decides whether anything actually gets recomputed. Safe to call
// every second without it costing a full redraw every time.
void eclipse_canvas_tick(Layer *layer) {
  layer_mark_dirty(layer);
}

// True from first contact up to (not including) last contact -- the
// same c1/c4 window eclipse_get_status_text() itself uses to decide
// PHASE_BEFORE_C1 vs PHASE_DONE, exposed here so other call sites
// (shake/startup animation gating, the tick-rate override, Space
// view's celestial-body suppression) can all agree on exactly the
// same definition of "the eclipse is happening right now" instead of
// each re-deriving their own slightly different c1/c4 comparison.
bool eclipse_is_active(const EclipseData *d, time_t now) {
  return d->valid && d->has_eclipse && now >= d->c1 && now < d->c4;
}

// ---- status text -------------------------------------------------------

static void fmt_countdown(char *buf, size_t buf_len, const char *label, time_t target, time_t now, bool minutes_only) {
  time_t remaining = target - now;
  if (remaining < 0) remaining = 0;
  int h = (int)(remaining / 3600);
  int m = (int)((remaining % 3600) / 60);
  int s = (int)(remaining % 60);
  if (h > 0) {
    snprintf(buf, buf_len, "%s %dh%02dm", label, h, m);
  } else if (minutes_only) {
    snprintf(buf, buf_len, "%s %dm", label, m);
  } else {
    snprintf(buf, buf_len, "%s %d:%02d", label, m, s);
  }
}

// Compact enough to fit the corners overlay's box width alongside an
// icon -- the old full names ("Waxing Gibbous") were fine for a
// full-width countdown line but don't fit there. Same thresholds as
// before, just shorter labels.
const char *moon_phase_short_name(uint8_t pct, bool waxing) {
  if (pct <= 2) return "New";
  if (pct >= 98) return "Full";
  if (waxing) {
    if (pct < 48) return "WxCr";
    if (pct <= 52) return "1stQ";
    return "WxGb";
  }
  if (pct > 52) return "WnGb";
  if (pct >= 48) return "3rdQ";
  return "WnCr";
}

EclipsePhase eclipse_get_status_text(const EclipseData *d, time_t now, char *buf, size_t buf_len, bool live_seconds) {
  if (!d->valid) {
    if (d->error_code == 1) snprintf(buf, buf_len, "No location");
    else if (d->error_code == 2) snprintf(buf, buf_len, "Calc error");
    else if (d->error_code == 3) snprintf(buf, buf_len, "Send failed");
    else snprintf(buf, buf_len, "No data yet");
    return PHASE_NO_ECLIPSE;
  }
  // This field is purely about eclipse phases now -- Moon phase has
  // its own corner content type instead (see corners_layer_update_proc
  // in pebble-eclipse-watch.c). No eclipse today (or today's has
  // already finished) means nothing to report here at all; the caller
  // hides the whole text layer in that case rather than leaving it
  // visible-but-blank.
  if (!d->has_eclipse) {
    buf[0] = '\0';
    return PHASE_NO_ECLIPSE;
  }
  if (d->sunset != 0 && now >= d->sunset && now < d->c4) {
    snprintf(buf, buf_len, "Sun set");
    return PHASE_NIGHT;
  }
  if (now < d->c1) {
    // Before the eclipse actually starts, the screen only ticks every
    // second if something ELSE already needs that (show_seconds, a
    // seconds-precision corner content, ...) -- eclipse_is_active()
    // is false out here, so it doesn't itself force a live-seconds
    // subscription (see update_tick_subscription()'s own comment).
    // A seconds-precision "M:SS" readout would just sit frozen for up
    // to a minute at a time in that case, so this falls back to a
    // plain whole-minutes (+hours past 59m) countdown instead, matching
    // however often this label is actually being redrawn.
    fmt_countdown(buf, buf_len, "Starts in", d->c1, now, !live_seconds);
    return PHASE_BEFORE_C1;
  }
  if (now >= d->c4) {
    buf[0] = '\0';
    return PHASE_DONE;
  }

  // From here on an eclipse is actively in progress (between C1 and
  // C4) -- prefix whichever phase label applies with the live "% of
  // Sun covered" so both are visible on the one line. eclipse_is_active()
  // is true for this whole stretch, which by itself now forces a live-
  // seconds subscription (see update_tick_subscription()), so these can
  // always afford full M:SS precision.
  char phase_buf[24];
  EclipsePhase phase;
  if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now < d->c2) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality in", d->c2, now, false);
    phase = PHASE_PARTIAL_IN;
  } else if (d->type != ECLIPSE_TYPE_PARTIAL && d->c2 != 0 && now >= d->c2 && now < d->c3) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Totality ends", d->c3, now, false);
    phase = PHASE_TOTAL;
  } else if (now < d->max_t && (d->type == ECLIPSE_TYPE_PARTIAL || d->c2 == 0)) {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Peak in", d->max_t, now, false);
    phase = PHASE_PARTIAL_IN;
  } else {
    fmt_countdown(phase_buf, sizeof(phase_buf), "Clears in", d->c4, now, false);
    phase = PHASE_PARTIAL_OUT;
  }

  snprintf(buf, buf_len, "%d%% - %s", celestial_interp_mag_pct(d, now), phase_buf);
  return phase;
}
