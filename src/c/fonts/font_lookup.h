#pragma once

#include <pebble.h>

/**
 * font_lookup.h -- one canonical table of every font this watchface
 * uses (system and custom-resource alike), and one function to
 * resolve a font id into an actual GFont for clock, marker, and
 * corner/edge presentation. The table provides one consistent font
 * identity and resource mapping for all consumers, and its ids are
 * shared with the configuration page.
 *
 * Font ids are defined by the current application table; persisted
 * settings are expected to use these current ids.
 *
 * A font id is a plain uint8_t, FONT_LOOKUP_COUNT-1 or below (see
 * FONT_LOOKUP_COUNT); anything out of range resolves to id 0 (system
 * Gothic 14) rather than crashing.
 */

// Custom fonts require per-slot load/unload; system fonts are ROM-backed.
typedef struct {
  GFont loaded_font;   // NULL if nothing (custom) is currently loaded in this slot
  uint8_t loaded_id;    // which font id loaded_font corresponds to; 255 = none
} FontSlot;

#define FONT_SLOT_EMPTY ((FontSlot){ .loaded_font = NULL, .loaded_id = 255 })

// Resolves `font_id` into a ready-to-use GFont, using `slot` to track
// (and reuse, across repeated calls with the same id -- no redundant
// reload) whatever custom font is currently loaded for this caller.
// Safe to call every redraw: a call with the same id as last time is
// just a cache hit, no allocation. Switching to a different id
// releases any custom resource in the slot before loading or resolving
// the requested font.
GFont font_lookup_resolve(FontSlot *slot, uint8_t font_id);

// Rough export height in px for font_id, for sizing/vertically-
// centering text boxes -- doesn't need to be pixel-exact. Valid to
// call without font_id having been resolved/loaded first.
uint8_t font_lookup_height(uint8_t font_id);

// Per-font vertical fine-tune in px, added to font_lookup_height()'s
// result when placing text. Pulled from the old, separate per-system
// hand-tuned tables this replaced (get_clock_font_height_offset() in
// application entry point, MARKER_FONT_Y_OFFSET in background_layer module)
// -- most fonts had the same
// offset in every context they appeared, but a couple (Digital Dream
// Small, Minecrafter Small) were tuned slightly differently depending
// on which OTHER font they were paired alongside as a clock face's
// small-readout companion. Folded to the majority value for those,
// values are intentionally compact rather than pixel-perfect measurements.
int8_t font_lookup_y_offset(uint8_t font_id);

// Releases whatever custom font `slot` currently holds (if any) and
// resets it to empty -- call when a caller is done needing its font
// entirely (e.g. window_unload), not on every redraw.
void font_lookup_release(FontSlot *slot);

// Total number of valid font ids (0..FONT_LOOKUP_COUNT-1).
extern const uint8_t FONT_LOOKUP_COUNT;
