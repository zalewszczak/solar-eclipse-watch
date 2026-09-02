#include "font_lookup.h"

typedef struct {
  bool is_custom;
  uint32_t resource_id;     // valid iff is_custom
  const char *system_key;    // valid iff !is_custom -- one of Pebble's own FONT_KEY_* macros
  uint8_t height;             // rough export height in px, for text-box sizing
  int8_t y_offset;            // vertical fine-tune, see font_lookup_y_offset()'s own header comment
  bool wide;                  // true if this font runs too wide for a full HH:MM:SS digital
                                // readout at its normal size -- see font_lookup_is_wide()
} FontLookupEntry;

// One row per distinct font this app uses anywhere -- deliberately
// deduplicated: e.g. Bebas at 20px used to be its own separate
// "choice" in 3 different places (marker text choice 14,
// corner_custom_font 5, and the clock face's small-readout companion
// for several clock_font choices), each with independently-loaded
// copies and independently-hand-picked ids. Now it's just id 24,
// resolved once here.
//
// Heights are unmeasured estimates (same caveat the older per-system
// tables already carried) -- good enough to keep text roughly
// centered, not claimed to be pixel-exact.
static const FontLookupEntry FONT_TABLE[] = {
  [0]  = { .system_key = FONT_KEY_GOTHIC_14,                .height = 14 }, // System Small
  [1]  = { .system_key = FONT_KEY_GOTHIC_14_BOLD,           .height = 18 }, // System Medium
  [2]  = { .system_key = FONT_KEY_GOTHIC_18_BOLD,           .height = 24 }, // System Large
  [3]  = { .system_key = FONT_KEY_GOTHIC_24_BOLD,           .height = 32 }, // System XL
  [4]  = { .system_key = FONT_KEY_GOTHIC_28_BOLD,           .height = 36 }, // System XXL
  [5]  = { .system_key = FONT_KEY_LECO_28_LIGHT_NUMBERS,    .height = 17 }, // Leco Small
  [6]  = { .system_key = FONT_KEY_LECO_32_BOLD_NUMBERS,     .height = 20 }, // Leco Medium
  [7]  = { .system_key = FONT_KEY_LECO_36_BOLD_NUMBERS,     .height = 23 }, // Leco Large
  [8]  = { .system_key = FONT_KEY_LECO_42_NUMBERS,          .height = 26 }, // Leco XL (main clock's own default)
  [9]  = { .system_key = FONT_KEY_DROID_SERIF_28_BOLD,      .height = 17 }, // Droid Serif
  [10] = { .system_key = FONT_KEY_ROBOTO_CONDENSED_21,      .height = 15, .y_offset = -2 }, // Roboto Condensed
  [11] = { .system_key = FONT_KEY_ROBOTO_BOLD_SUBSET_49,    .height = 30, .y_offset = -2 }, // Roboto Bold (big)
  [12] = { .system_key = FONT_KEY_BITHAM_30_BLACK,          .height = 19 }, // Bitham Bold 30
  [13] = { .system_key = FONT_KEY_BITHAM_34_MEDIUM_NUMBERS, .height = 21 }, // Bitham Medium 34
  [14] = { .system_key = FONT_KEY_BITHAM_42_LIGHT,          .height = 26 }, // Bitham Light (big)
  [15] = { .system_key = FONT_KEY_BITHAM_42_BOLD,           .height = 26 }, // Bitham Bold (big)

  [16] = { .is_custom = true, .resource_id = RESOURCE_ID_DIGITALDREAM_FONT_12,  .height = 12, .y_offset = -2 }, // Digital Dream Small
  [17] = { .is_custom = true, .resource_id = RESOURCE_ID_DIGITALDREAM_FONT_48,  .height = 40, .y_offset = -2 }, // Digital Dream Big
  [18] = { .is_custom = true, .resource_id = RESOURCE_ID_MINECRAFTER_FONT_12,   .height = 12, .y_offset = -2 }, // Minecrafter Small
  [19] = { .is_custom = true, .resource_id = RESOURCE_ID_MINECRAFTER_FONT_48,   .height = 40, .y_offset = 4, .wide = true }, // Minecrafter Big
  [20] = { .is_custom = true, .resource_id = RESOURCE_ID_SFPIXELATE_FONT_14,    .height = 14 }, // SF Pixelate Small
  [21] = { .is_custom = true, .resource_id = RESOURCE_ID_SFPIXELATE_FONT_48,    .height = 40, .wide = true }, // SF Pixelate Big
  [22] = { .is_custom = true, .resource_id = RESOURCE_ID_MISO_FONT_19,          .height = 19, .y_offset = -4 }, // Miso Small
  [23] = { .is_custom = true, .resource_id = RESOURCE_ID_MISO_FONT_48,          .height = 40, .y_offset = -4 }, // Miso Big
  [24] = { .is_custom = true, .resource_id = RESOURCE_ID_BEBAS_FONT_20,         .height = 20, .y_offset = -6 }, // Bebas Small
  [25] = { .is_custom = true, .resource_id = RESOURCE_ID_BEBAS_FONT_48,         .height = 40 }, // Bebas Big
  [26] = { .is_custom = true, .resource_id = RESOURCE_ID_CLOCKFORGE_FONT_48,    .height = 40 }, // ClockForge
  [27] = { .is_custom = true, .resource_id = RESOURCE_ID_RADIOLAND_FONT_48,     .height = 40, .wide = true }, // Radioland
  [28] = { .is_custom = true, .resource_id = RESOURCE_ID_MINISYSTEM_FONT_48,    .height = 40, .wide = true }, // Mini System
  [29] = { .is_custom = true, .resource_id = RESOURCE_ID_KITCHENPOLICE_FONT_48, .height = 40, .wide = true }, // Kitchen Police
  [30] = { .is_custom = true, .resource_id = RESOURCE_ID_DSDIGIB_FONT_48,       .height = 40, .y_offset = -4 }, // DS Digital
  [31] = { .is_custom = true, .resource_id = RESOURCE_ID_DISTGRG_FONT_48,       .height = 40, .y_offset = -6 }, // Distant Galaxy
  [32] = { .is_custom = true, .resource_id = RESOURCE_ID_DIMITRI_FONT_48,       .height = 40, .y_offset = -2 }, // Dimitri
  [33] = { .is_custom = true, .resource_id = RESOURCE_ID_BLACKOUT_FONT_48,      .height = 40, .y_offset = -2 }, // Blackout
  [34] = { .is_custom = true, .resource_id = RESOURCE_ID_AUDIOWIDE_FONT_48,     .height = 40, .y_offset = -2, .wide = true }, // Audiowide
  [35] = { .is_custom = true, .resource_id = RESOURCE_ID_FORMATION_FONT_48,     .height = 40, .y_offset = -2 }, // Formation
  [36] = { .is_custom = true, .resource_id = RESOURCE_ID_KOMIKAHB_FONT_48,      .height = 40, .wide = true }, // Komika
  [37] = { .is_custom = true, .resource_id = RESOURCE_ID_PRICEDOWN_FONT_48,     .height = 40 }, // Pricedown
};

const uint8_t FONT_LOOKUP_COUNT = sizeof(FONT_TABLE) / sizeof(FONT_TABLE[0]);

static const FontLookupEntry *entry_for(uint8_t font_id) {
  if (font_id >= FONT_LOOKUP_COUNT) font_id = 0; // out-of-range -- fall back rather than crash
  return &FONT_TABLE[font_id];
}

GFont font_lookup_resolve(FontSlot *slot, uint8_t font_id) {
  if (font_id >= FONT_LOOKUP_COUNT) font_id = 0;
  const FontLookupEntry *e = &FONT_TABLE[font_id];

  if (!e->is_custom) {
    // Switching from a custom font to a system one -- free the slot's
    // old custom font, since system fonts don't use the slot at all.
    if (slot->loaded_font) {
      fonts_unload_custom_font(slot->loaded_font);
      slot->loaded_font = NULL;
    }
    slot->loaded_id = font_id;
    return fonts_get_system_font(e->system_key);
  }

  if (slot->loaded_id == font_id && slot->loaded_font) return slot->loaded_font; // already the right one

  if (slot->loaded_font) {
    fonts_unload_custom_font(slot->loaded_font);
    slot->loaded_font = NULL;
  }
  slot->loaded_font = fonts_load_custom_font(resource_get_handle(e->resource_id));
  slot->loaded_id = font_id;
  return slot->loaded_font;
}

uint8_t font_lookup_height(uint8_t font_id) {
  return entry_for(font_id)->height;
}

int8_t font_lookup_y_offset(uint8_t font_id) {
  return entry_for(font_id)->y_offset;
}

bool font_lookup_is_wide(uint8_t font_id) {
  return entry_for(font_id)->wide;
}

void font_lookup_release(FontSlot *slot) {
  if (slot->loaded_font) {
    fonts_unload_custom_font(slot->loaded_font);
    slot->loaded_font = NULL;
  }
  slot->loaded_id = 255;
}
