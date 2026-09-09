#include "font_lookup.h"

// G: FONT_TABLE used to hold a `const char *system_key` pointer per row.
// Even though the table itself is `const`, a pointer to a string needs
// load-time relocation in this platform's PIC image, so the whole table
// ended up in .data.rel.ro (with .got entries to match) instead of plain
// .rodata -- and the pointer was dead weight in the 27 custom rows that
// use resource_id instead. Splitting the (still-relocated, but much
// smaller) pointer array out from the rest lets the main table -- now
// holding only compile-time-constant integers (RESOURCE_ID_* are
// preprocessor #defines) -- become true .rodata.

// One entry per system font FONT_TABLE's rows 0-15 reference, in the
// same order -- SYSTEM_FONT_KEYS[e->sys_idx] recovers the FONT_KEY_*
// a system-font row used to store inline.
static const char *const SYSTEM_FONT_KEYS[] = {
  FONT_KEY_GOTHIC_14,                // 0: System Small
  FONT_KEY_GOTHIC_14_BOLD,           // 1: System Medium
  FONT_KEY_GOTHIC_18_BOLD,           // 2: System Large
  FONT_KEY_GOTHIC_24_BOLD,           // 3: System XL
  FONT_KEY_GOTHIC_28_BOLD,           // 4: System XXL
  FONT_KEY_LECO_28_LIGHT_NUMBERS,    // 5: Leco Small
  FONT_KEY_LECO_32_BOLD_NUMBERS,     // 6: Leco Medium
  FONT_KEY_LECO_36_BOLD_NUMBERS,     // 7: Leco Large
  FONT_KEY_LECO_42_NUMBERS,          // 8: Leco XL (main clock's own default)
  FONT_KEY_DROID_SERIF_28_BOLD,      // 9: Droid Serif
  FONT_KEY_ROBOTO_CONDENSED_21,      // 10: Roboto Condensed
  FONT_KEY_ROBOTO_BOLD_SUBSET_49,    // 11: Roboto Bold (big)
  FONT_KEY_BITHAM_30_BLACK,          // 12: Bitham Bold 30
  FONT_KEY_BITHAM_34_MEDIUM_NUMBERS, // 13: Bitham Medium 34
  FONT_KEY_BITHAM_42_LIGHT,          // 14: Bitham Light (big)
  FONT_KEY_BITHAM_42_BOLD,           // 15: Bitham Bold (big)
};

typedef struct {
  uint32_t resource_id;   // 0 => system font, look up SYSTEM_FONT_KEYS[sys_idx] instead
  uint8_t  sys_idx;        // valid iff resource_id == 0 -- index into SYSTEM_FONT_KEYS above
  uint8_t  height;         // rough export height in px, for text-box sizing
  int8_t   y_offset;       // vertical fine-tune, see font_lookup_y_offset()'s own header comment
} FontLookupEntry; // 8 bytes (was 12 with the system_key pointer inline)

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
  [0]  = { .sys_idx = 0,  .height = 14 }, // drop this?
  [1]  = { .sys_idx = 1,  .height = 18 }, // call this gothic small
  [2]  = { .sys_idx = 2,  .height = 24, .y_offset = -6  }, // call this gothic medium
  [3]  = { .sys_idx = 3,  .height = 32, .y_offset = -8 }, // call this gothic large
  [4]  = { .sys_idx = 4,  .height = 36, .y_offset = -10 }, // call this gothic x-large
  [5]  = { .sys_idx = 5,  .height = 17 }, // Leco Small
  [6]  = { .sys_idx = 6,  .height = 20 }, // Leco Medium
  [7]  = { .sys_idx = 7,  .height = 23 }, // Leco Large
  [8]  = { .sys_idx = 8,  .height = 26 }, // Leco XL (main clock's own default)
  [9]  = { .sys_idx = 9,  .height = 17, .y_offset = -12 }, // Droid Serif - MARK THIS INCOMPATIBLE
  [10] = { .sys_idx = 10, .height = 15, .y_offset = -10 }, // Roboto Condensed
  [11] = { .sys_idx = 11, .height = 30, .y_offset = -2 }, // Roboto Bold (big)
  [12] = { .sys_idx = 12, .height = 30 }, // Bitham Bold 30  - MARK THIS INCOMPATIBLE
  [13] = { .sys_idx = 13, .height = 21 }, // Bitham Medium 34
  [14] = { .sys_idx = 14, .height = 26 }, // Bitham Light (big)
  [15] = { .sys_idx = 15, .height = 26 }, // Bitham Bold (big)

  [16] = { .resource_id = RESOURCE_ID_DIGITALDREAM_FONT_12,  .height = 12, .y_offset = -2 }, // Digital Dream Small
  [17] = { .resource_id = RESOURCE_ID_DIGITALDREAM_FONT_48,  .height = 40, .y_offset = -2 }, // Digital Dream Big
  [18] = { .resource_id = RESOURCE_ID_MINECRAFTER_FONT_12,   .height = 12, .y_offset = -2 }, // Minecrafter Small
  [19] = { .resource_id = RESOURCE_ID_MINECRAFTER_FONT_48,   .height = 40, .y_offset = 4 }, // Minecrafter Big
  [20] = { .resource_id = RESOURCE_ID_SFPIXELATE_FONT_48,    .height = 40 }, // SF Pixelate Big
  [21] = { .resource_id = RESOURCE_ID_ALAGARD_FONT_19,       .height = 19, .y_offset = -4 }, // Alagard Small
  [22] = { .resource_id = RESOURCE_ID_ALAGARD_FONT_48,       .height = 40, .y_offset = -4 }, // Alagard Big
  [23] = { .resource_id = RESOURCE_ID_BEBAS_FONT_20,         .height = 20, .y_offset = -6 }, // Bebas Small
  [24] = { .resource_id = RESOURCE_ID_BEBAS_FONT_48,         .height = 40 }, // Bebas Big
  [25] = { .resource_id = RESOURCE_ID_AMITA_FONT_48,         .height = 40 }, // Amita
  [26] = { .resource_id = RESOURCE_ID_AVERIA_FONT_48,        .height = 40 }, // AveriaSerifLibre
  [27] = { .resource_id = RESOURCE_ID_BAGEL_FONT_48,         .height = 40 }, // Bagel
  [28] = { .resource_id = RESOURCE_ID_BRICOLAGE_FONT_48,     .height = 40 }, // Bricolage Grotesque
  [29] = { .resource_id = RESOURCE_ID_CHANGO_FONT_48,        .height = 40, .y_offset = -4 }, // Chango
  [30] = { .resource_id = RESOURCE_ID_EMBLEMA_FONT_48,       .height = 40, .y_offset = -6 }, // EmblemaOne
  [31] = { .resource_id = RESOURCE_ID_FRAUNCES_FONT_48,      .height = 40, .y_offset = -2 }, // Fraunces
  [32] = { .resource_id = RESOURCE_ID_GEOSTAR_FONT_48,       .height = 40, .y_offset = -2 }, // Geostar Fill
  [33] = { .resource_id = RESOURCE_ID_MICHROMA_FONT_48,      .height = 40, .y_offset = -2 }, // Michroma
  [34] = { .resource_id = RESOURCE_ID_NATIONALPARK_FONT_48,  .height = 40, .y_offset = -2 }, // National Park
  [35] = { .resource_id = RESOURCE_ID_KOMIKAHB_FONT_48,      .height = 40 }, // Komika
  [36] = { .resource_id = RESOURCE_ID_QUANTICO_FONT_48,      .height = 40 }, // Quantico
  [37] = { .resource_id = RESOURCE_ID_SILKSCREEN_FONT_48,    .height = 40 }, // Silkscreen
  [38] = { .resource_id = RESOURCE_ID_STACKSANSHEADLINE_FONT_48, .height = 40 }, // StackSansHeadline
  [39] = { .resource_id = RESOURCE_ID_UNBOUNDED_FONT_48,     .height = 40 }, // Unbounded
  [40] = { .resource_id = RESOURCE_ID_WALLPOET_FONT_48,      .height = 40 }, // Wallpoet
  [41] = { .resource_id = RESOURCE_ID_ZALANDOSANS_FONT_48,   .height = 40 }, // ZalandoSans
  
  [42] = { .resource_id = RESOURCE_ID_BYTESIZED_FONT_16,   .height = 16 }, // tiniest option? looks legible and takes almost no space
  [43] = { .resource_id = RESOURCE_ID_MPLUSBLACK_FONT_16,   .height = 16 }, // this is great!
  [44] = { .resource_id = RESOURCE_ID_NOTOITALIC_FONT_18,   .height = 18 }, // this is even better than above, 18 seems fine
  
  [45] = { .resource_id = RESOURCE_ID_MPLUSBLACK_FONT_48,   .height = 40 }, // good for main clock, very bold
  [46] = { .resource_id = RESOURCE_ID_REDITBOLD_FONT_48,   .height = 40 }, // neat for main!
  
  [47] = { .resource_id = RESOURCE_ID_ARCADE_FONT_18,   .height = 18 }, // kinda like it
  [48] = { .resource_id = RESOURCE_ID_DSDIGIB_FONT_20,   .height = 20 }, // this is really good as well!
  [49] = { .resource_id = RESOURCE_ID_DSDIGIT_FONT_20,   .height = 20 }, // this is perfect
  [50] = { .resource_id = RESOURCE_ID_LCDSOLID_FONT_18,   .height = 18 }, // this is great! 18 seems perfect
  [51] = { .resource_id = RESOURCE_ID_RADIOLAND_FONT_16,   .height = 16 }, // 16 looks really good
  
  [52] = { .resource_id = RESOURCE_ID_ZEROZERO_FONT_48,   .height = 48 }, // kinda cool
  [53] = { .resource_id = RESOURCE_ID_DSDIGIT_FONT_48,   .height = 48 }, // why not?
  [54] = { .resource_id = RESOURCE_ID_LCDSOLID_FONT_48,   .height = 48 }, // i like this too
  [55] = { .resource_id = RESOURCE_ID_REBELREDUX_FONT_48,   .height = 48 }, // could be good for big
};

//
//
//ZEROZERO_FONT_18
//ACKNOWS_FONT_18
//ALPHABETA_FONT_18
//ARCADE_FONT_18
//CRYSTA_FONT_18
//DING_FONT_18
//DIN_FONT_18
//DSDIGIB_FONT_18
//DSDIGIT_FONT_18
//INFO3_FONT_18
//INFO6_FONT_18
//LCDSOLID_FONT_18
//RADIOLAND_FONT_18
//REBELREDUX_FONT_18

const uint8_t FONT_LOOKUP_COUNT = sizeof(FONT_TABLE) / sizeof(FONT_TABLE[0]);

static const FontLookupEntry *entry_for(uint8_t font_id) {
  if (font_id >= FONT_LOOKUP_COUNT) font_id = 0; // out-of-range -- fall back rather than crash
  return &FONT_TABLE[font_id];
}

GFont font_lookup_resolve(FontSlot *slot, uint8_t font_id) {
  if (font_id >= FONT_LOOKUP_COUNT) font_id = 0;
  const FontLookupEntry *e = &FONT_TABLE[font_id];

  if (e->resource_id == 0) {
    // Switching from a custom font to a system one -- free the slot's
    // old custom font, since system fonts don't use the slot at all.
    if (slot->loaded_font) {
      fonts_unload_custom_font(slot->loaded_font);
      slot->loaded_font = NULL;
    }
    slot->loaded_id = font_id;
    return fonts_get_system_font(SYSTEM_FONT_KEYS[e->sys_idx]);
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

void font_lookup_release(FontSlot *slot) {
  if (slot->loaded_font) {
    fonts_unload_custom_font(slot->loaded_font);
    slot->loaded_font = NULL;
  }
  slot->loaded_id = 255;
}
