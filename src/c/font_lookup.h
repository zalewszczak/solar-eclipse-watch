#pragma once

#include <pebble.h>

/**
 * font_lookup.h -- one canonical table of every font this watchface
 * uses (system and custom-resource alike), and one function to
 * resolve a font id into an actual GFont, replacing what used to be
 * three separate, mostly-duplicate font-selection systems: the main
 * clock face (apply_clock_font() in pebble-eclipse-watch.c), marker
 * text (marker_text_font_resource_id()/get_marker_text_font() in
 * background_layer.c), and corner/edge content
 * (corner_custom_font_resource_id()/get_corner_font() in
 * features_layer.c). All three used to hand-pick from mostly the same
 * pool of fonts under different, uncoordinated numbering -- e.g.
 * "Bebas" was font_choice 14 to the marker system, corner_custom_font
 * 5 to the corner system, and clock_font 20 (the 48px one, a
 * different resource entirely) to the clock, with no shared code
 * between any of them. Now every one of those places -- and the
 * settings page, via CLOCK_FONTS_BIG in config-page.js's font
 * picker, which is generated from the exact same list this table
 * encodes -- refers to the same font by the same id.
 *
 * There is deliberately no attempt to keep old numeric ids meaning
 * the same thing they used to (clock_font/marker_text_font/
 * corner_font_size+corner_custom_font are all being renumbered from
 * scratch) -- anyone with a saved custom font selection will need to
 * re-pick it once after updating.
 *
 * A font id is a plain uint8_t, FONT_LOOKUP_COUNT-1 or below (see
 * FONT_LOOKUP_COUNT); anything out of range resolves to id 0 (system
 * Gothic 14) rather than crashing.
 */

// A resource-backed custom font needs an explicit load/unload
// lifecycle (fonts_load_custom_font()/fonts_unload_custom_font());
// a system font doesn't (fonts_get_system_font() just returns a
// pointer into ROM, and it's always safe to call). Every caller that
// resolves a font id owns one of these -- there's no single shared
// "currently loaded" slot, because the clock face, corner content,
// and marker text can each have a genuinely different custom font
// loaded at the very same time, and Pebble happily allows that (each
// costs its own share of heap, nothing more).
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
// unloads the slot's old custom font (if it had one) before loading
// or resolving the new one.
GFont font_lookup_resolve(FontSlot *slot, uint8_t font_id);

// Rough export height in px for font_id, for sizing/vertically-
// centering text boxes -- doesn't need to be pixel-exact. Valid to
// call without font_id having been resolved/loaded first.
uint8_t font_lookup_height(uint8_t font_id);

// Per-font vertical fine-tune in px, added to font_lookup_height()'s
// result when placing text. Pulled from the old, separate per-system
// hand-tuned tables this replaced (get_clock_font_height_offset()/
// get_small_font_height_offset() in pebble-eclipse-watch.c, MARKER_
// FONT_Y_OFFSET in background_layer.c) -- most fonts had the same
// offset in every context they appeared, but a couple (Digital Dream
// Small, Minecrafter Small) were tuned slightly differently depending
// on which OTHER font they were paired alongside as a clock face's
// small-readout companion. Folded to the majority value for those,
// same "good enough, not pixel-exact" spirit the old tables already
// had (several were marked TODO/unmeasured before this).
int8_t font_lookup_y_offset(uint8_t font_id);

// Releases whatever custom font `slot` currently holds (if any) and
// resets it to empty -- call when a caller is done needing its font
// entirely (e.g. window_unload), not on every redraw.
void font_lookup_release(FontSlot *slot);

// True if this font runs too wide for a full HH:MM:SS digital
// readout at its normal size (some of the wider display fonts need
// the seconds digits shrunk into their own small-font box instead).
bool font_lookup_is_wide(uint8_t font_id);

// Total number of valid font ids (0..FONT_LOOKUP_COUNT-1).
extern const uint8_t FONT_LOOKUP_COUNT;
