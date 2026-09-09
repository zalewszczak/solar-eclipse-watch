#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// ---------------------------------------------------------------------------
// The always-on-top text/icon overlay for the corner and edge-middle
// "feature" slots (health, weather, timezones, astronomy, ...) -- split out
// of pebble-eclipse-watch.c's corners_layer_update_proc into its own module,
// mirroring background_layer.c/hand_layer.c.
//
// Table-driven: every one of the 12 slots is a FeatureSlot holding
// everything features_layer_update_proc() needs to draw it -- position,
// color, icon, text -- fully resolved ahead of time. A slot has two
// halves that change at very different rates:
//
//   - LAYOUT (which of the 12 slots are active, and where each one's box
//     sits) depends only on settings (bottom_style, big_analog_marker_style,
//     the corner/edge content+color-mode fields) -- resolved by
//     features_recompute_layout() (features_layer.c, private), only ever
//     called from features_layer_set_data() below.
//
//   - VALUE (the actual text/icon/color for a slot's current content --
//     a health reading, the weather, the clock, a compass heading) changes
//     far more often, and is resolved separately by
//     features_recompute_slot_value() (features_layer.c, private), grouped
//     into a handful of per-category functions rather than one giant
//     per-content switch. features_layer_refresh_values() re-runs this for
//     every active slot (the periodic ~1-minute tick); refresh_second_slots()
//     re-runs it ONLY for the slot(s) whose content actually needs
//     second-by-second updates (never all 12 just because one shows
//     seconds).
//
// features_layer_update_proc() itself (features_layer.c) does none of the
// above -- by the time it runs, every slot already has its final pixel
// positions/colors/strings sitting in the table, so drawing is a plain
// blit loop with no formatting, gradient math, or alignment/width
// measurement of its own.
// ---------------------------------------------------------------------------

#define FEATURES_MAX_SLOTS 12

// How long, in ms, the periodic refresh (features_layer_refresh_values(),
// called from corners_timer_callback() in pebble-eclipse-watch.c)
// re-resolves every active slot's value, independent of any settings
// change. Named here as a single shared knob rather than a magic number
// duplicated in the main .c file.
#define FEATURES_REFRESH_MS 60000

#define CORNER_ROW_H 24

Layer *features_layer_create(GRect frame);
void features_layer_destroy(Layer *layer);

// Recomputes every slot's layout AND value from the current settings in
// `data`, then marks the layer dirty. Call once right after creating the
// layer, and again whenever an inbox message may have changed a setting
// that affects feature layout or content (style, marker style,
// bottom-info-bar mode, any of the corner/edge content fields, colors,
// units, ...).
void features_layer_set_data(Layer *layer, EclipseData *data);

// The shake-to-reveal ground bar shifts the two bottom corners up out of
// its way while it's showing -- affects slot position like a settings
// change would, so (like features_layer_set_data()) this recomputes
// layout and value for every slot rather than just marking the layer
// dirty.
void features_layer_set_labels_visible(Layer *layer, bool visible);

// Re-resolves every active slot's VALUE (not layout -- that's unchanged)
// from the current data/time/live sensor state, then marks the layer
// dirty. Call from the periodic refresh timer -- see FEATURES_REFRESH_MS
// above.
void features_layer_refresh_values(Layer *layer);

// Re-resolves ONLY the slot(s) whose content needs second-by-second
// updating (a seconds-showing time display), then marks the layer dirty.
// Call from the once-a-second tick handler -- cheap even when nothing in
// the whole table actually needs it, since it's a no-op scan when no
// slot is currently showing seconds.
void features_layer_refresh_second_slots(Layer *layer);

// Re-resolves ONLY the slot(s) currently showing this specific content
// id, then marks the layer dirty -- for triggers tied to one particular
// content type rather than the clock (e.g. the compass feature's own
// live-heading animation frame, which needs to update far more often
// than the general refresh, but only for whichever slot(s) are actually
// showing the compass).
void features_layer_refresh_content(Layer *layer, uint8_t content);

// See its own comment in features_layer.c -- shared with
// pebble-eclipse-watch.c so the clock text and the digital-mode bottom
// feature can never disagree about which horizontal band they share.
void digital_clock_area(uint8_t bottom_style, int16_t screen_w, int16_t *out_x, int16_t *out_w);

// Loads/unloads the shared corner/edge custom font on demand -- cheap to
// call repeatedly (no-ops if the choice hasn't changed since the last
// call). Used by this module's own value-recompute functions and the
// big-analog hands layer's date-behind-hands readout.
void ensure_corner_custom_font(uint8_t choice);

// Unloads the shared corner/edge custom font, if one is currently
// loaded -- call once from window_unload() on app exit, mirroring how
// pebble-eclipse-watch.c already frees its own clock_font.
void features_layer_unload_fonts(void);

// Shared outline-drawing primitives -- also used by the countdown/status
// label in pebble-eclipse-watch.c, not just this module's own content.
GColor contrasting_outline_color(GColor c);
void draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                         GTextOverflowMode overflow, GTextAlignment alignment,
                         GColor color, uint8_t outline_style);
