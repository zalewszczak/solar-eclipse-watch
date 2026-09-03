#pragma once

#include <pebble.h>
#include "eclipse_data.h"

// ---------------------------------------------------------------------------
// The always-on-top text/icon overlay for the corner and edge-middle
// "feature" slots (health, weather, timezones, astronomy, ...) -- split out
// of pebble-eclipse-watch.c's corners_layer_update_proc into its own module,
// mirroring background_layer.c/hand_layer.c.
//
// Which content goes in which of the up to 12 slots, and exactly where on
// screen each one lands, depends only on settings (bottom_style,
// big_analog_marker_style, bottom_info_bar_mode, and the corner/edge
// content+color-mode fields themselves) plus the shake-triggered label
// state -- NOT on the current time or any live sensor reading. So rather
// than re-deriving all of that layout on every redraw (as the old single
// corners_layer_update_proc did), this module caches it as a small
// FeatureSlot[] array -- recomputed only from features_layer_set_data()/
// features_layer_set_labels_visible() (i.e. on watchface start and
// whenever a relevant setting or the shake state actually changes) -- and
// just walked and drawn from on every real redraw. The live per-item
// values (heart rate, weather, ...) still get formatted at draw time in
// features_draw_item() itself, same as before -- those inherently change
// independent of settings and can't be precomputed.
// ---------------------------------------------------------------------------

#define FEATURES_MAX_SLOTS 12

// The height of one feature row -- shared with the small-analog bottom
// panel's own inline feature rows (see bottom_canvas_update_proc()),
// which reuse features_draw_item() directly rather than going through
// this module's own layer/slot machinery (small-analog mode has no
// separate always-on-top overlay layer to begin with).
#define CORNER_ROW_H 24

Layer *features_layer_create(GRect frame);
void features_layer_destroy(Layer *layer);

// Recomputes every slot's visibility/position from the current settings
// in `data` and marks the layer dirty. Call once right after creating the
// layer, and again whenever an inbox message may have changed a setting
// that affects feature layout (style, marker style, bottom-info-bar mode,
// or any of the corner/edge content fields) -- NOT on every tick.
void features_layer_set_data(Layer *layer, EclipseData *data);

// The shake-to-reveal ground bar shifts the two bottom corners up out of
// its way while it's showing -- affects slot position like a settings
// change would, so (like features_layer_set_data()) this recomputes the
// cached slots rather than just marking the layer dirty.
void features_layer_set_labels_visible(Layer *layer, bool visible);

// ---- shared with the small-analog bottom panel (main .c file) ----------
// bottom_canvas_update_proc() draws its own 4 feature rows directly --
// small-analog mode has no separate overlay layer, its rows live inline
// in the digital/analog bottom panel instead -- so it needs the same
// per-item drawing primitive and font plumbing this module owns for the
// corners/edges overlay.

// Draws one feature slot's content/icon/text at an already fully-resolved
// position -- no layout decisions happen in here, just formatting live
// values (health, weather, ...) and drawing them. `data` supplies the
// settings/live-data fields the content type needs (temp units, step
// goal, and so on).
void features_draw_item(GContext *ctx, GRect bounds, const EclipseData *data,
                         uint8_t content, uint8_t color_mode,
                         GColor main_color, GColor accent_color, GColor bg_color,
                         bool is_top, bool is_left, bool is_middle, int16_t top_offset, int16_t bottom_shift,
                         int16_t middle_inset,
                         bool center_horizontal, bool center_vertical, bool allow_outline);

// Loads/unloads the shared corner/edge custom font on demand -- cheap to
// call every redraw (no-ops if the choice hasn't changed since the last
// call). Used by this module's own update proc, the big-analog hands
// layer's date-behind-hands readout, and the small-analog panel's rows.
void ensure_corner_custom_font(uint8_t choice);

// Unloads the shared corner/edge custom font, if one is currently
// loaded -- call once from window_unload() on app exit, mirroring how
// pebble-eclipse-watch.c already frees its own clock_font/small_font.
void features_layer_unload_fonts(void);

// Shared outline-drawing primitives -- also used by the countdown/status
// label in pebble-eclipse-watch.c, not just this module's own content.
GColor contrasting_outline_color(GColor c);
void draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                         GTextOverflowMode overflow, GTextAlignment alignment,
                         GColor color, bool outline_enabled);
