#include "features_layer.h"
#include "background_layer.h"
#include "font_lookup.h"
#include <string.h>
#include <stdlib.h> // atoi(), for parsing strftime's "%V" week-number string back to an int for grading

// See features_layer.h for the module-level design note (metadata cache
// vs. per-redraw layout recompute). This file also owns the shared
// draw_text_outlined()/contrasting_outline_color() outline primitives and
// the corner/edge custom-font plumbing (ensure_corner_custom_font() etc.)
// -- both used outside this module too (the countdown label and the big-
// analog hands/small-analog panel, respectively), which is why they're
// declared in features_layer.h rather than kept private.

#define CORNER_BOX_W 68

// How far a corner slot's box sits from the screen edge. Was 2px;
// bumped to 4 per request so corners get a bit more breathing room
// from the bezel.
#define CORNER_INSET_PX 4

// The digital mode's bottom (clock) panel's own fixed height -- screen
// height (228) minus the sky canvas's fixed top-of-panel value (152,
// see apply_layout()/unobstructed_change_handler() in
// pebble-eclipse-watch.c, both of which keep the panel at this exact
// height and instead shift its TOP up as a system notification
// obstructs the bottom of the screen, rather than shrinking it). Used
// below purely to keep the two BOTTOM corner slots anchored to the
// sky's own bottom edge in digital mode (see their own comment) --
// the obstruction itself cancels out of that distance (both the sky
// and the full screen shrink by the exact same amount), so this can
// stay a plain constant instead of something recomputed on every
// unobstructed-area change.
#define DIGITAL_PANEL_H 76

// point_in_convex_polygon()/fill_polygon_dithered() used to live here
// for the old "semi" color mode's dithered highlight plate -- removed
// now that Pill mode draws a plain solid-fill capsule instead (see
// draw_pill below), which needs neither. Smaller binary, one less
// per-pixel loop.

// Shared by every outline implementation in this file (text, icons):
// draw once per offset with a contrasting color, then once more
// normally on top. Cheap and guarantees contrast against any
// background without needing per-pixel edge detection.
// Thin is the original/default look (4 cardinal 1px shifts). Thick
// adds 4 cardinal 2px shifts and 4 diagonal 1px shifts on top of
// thin's own 4 -- 12 points total -- for a visibly bolder line, per
// the request's exact offset list.
static const GPoint OUTLINE_OFFSETS_THIN[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
static const GPoint OUTLINE_OFFSETS_THICK[12] = {
  {-1, 0}, {1, 0}, {0, -1}, {0, 1},
  {-2, 0}, {2, 0}, {0, -2}, {0, 2},
  {1, 1}, {-1, 1}, {-1, -1}, {1, -1},
};
// Resolves outline_style (0=none, 1=thin, 2=thick) to the actual
// offset table + count every outline-drawing call site below loops
// over -- style 0 is never passed in here (every caller already
// guards on it separately, same as outline_enabled used to be a plain
// bool guard), so this only ever needs to pick between thin/thick.
static void get_outline_offsets(uint8_t outline_style, const GPoint **out_offsets, int *out_count) {
  if (outline_style >= 2) {
    *out_offsets = OUTLINE_OFFSETS_THICK;
    *out_count = 12;
  } else {
    *out_offsets = OUTLINE_OFFSETS_THIN;
    *out_count = 4;
  }
}

// White for dark colors, black for bright ones -- the outline has to
// contrast with the text/icon's OWN color to do its job (a dark
// outline on dark text is invisible regardless of what's behind it),
// not with the scheme's background color, which is what this used to
// (incorrectly) use.
//
// Threshold is 9, not the "true" midpoint of 15, specifically so a
// fully-saturated primary like pure red (r=3,g=0,b=0, luma 9) lands on
// the black-outline side: the classic 0.3/0.6/0.1 luma weights treat
// red as fairly dark, but on the watch's own display a solid red
// doesn't read as dark the way that formula implies -- a white
// outline on it looked wrong in practice. Weaker/darker reds (and
// blue, which is genuinely dark at any saturation) still correctly
// fall below this and get a white outline.
GColor contrasting_outline_color(GColor c) {
  uint8_t r = (c.argb >> 4) & 0x03;
  uint8_t g = (c.argb >> 2) & 0x03;
  uint8_t b = c.argb & 0x03;
  int luma = r * 3 + g * 6 + b; // approximates 0.3/0.6/0.1 luma weights, out of 30
  return (luma >= 9) ? GColorBlack : GColorWhite;
}

void draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                                GTextOverflowMode overflow, GTextAlignment alignment,
                                GColor color, uint8_t outline_style) {
  if (outline_style != 0) {
    const GPoint *offsets; int offset_count;
    get_outline_offsets(outline_style, &offsets, &offset_count);
    graphics_context_set_text_color(ctx, contrasting_outline_color(color));
    for (int i = 0; i < offset_count; i++) {
      GRect shifted = GRect(box.origin.x + offsets[i].x, box.origin.y + offsets[i].y,
                             box.size.w, box.size.h);
      graphics_draw_text(ctx, text, font, shifted, overflow, alignment, NULL);
    }
  }
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, overflow, alignment, NULL);
}



// Corner/edge feature text and the big-analog date's own font slot --
// resolved via font_lookup_resolve() (see font_lookup.h for the
// shared table every font-selecting system in this app draws from).
static FontSlot s_corner_font_slot = FONT_SLOT_EMPTY;

void ensure_corner_custom_font(uint8_t font_id) {
  font_lookup_resolve(&s_corner_font_slot, font_id);
}

void features_layer_unload_fonts(void) {
  font_lookup_release(&s_corner_font_slot);
}

// ---- corners overlay -------------------------------------------------

// Feature icons used to be procedural bit patterns (1 bit per pixel)
// baked into static const arrays here -- kept in flash/RAM for the
// life of the app whether or not the feature showing them was ever
// on screen. They're now PNG image resources (see package.json's
// "media" list, RESOURCE_ID_ICON_* / RESOURCE_ID_ICON_WEATHER_*),
// standardized to the same 16x12 size these arrays already mostly
// used. draw_icon_resource()/draw_icon_resource_native() below load
// one on demand right before drawing it and free it immediately
// after -- so at any moment, only the icon(s) belonging to whichever
// feature slots are actually on screen this redraw are ever decoded
// into memory (e.g. one weather icon if a weather slot is showing,
// a heart icon only while a heart-rate slot is showing, etc.), never
// the whole icon set at once.
//
// PEBBLE_ICON is the one exception, kept as a static array exactly
// as before -- see draw_tiny_icon() below.
static const uint8_t ICON_WIDTH = 16;
static const uint8_t ICON_ROWS = 12;

static const uint8_t PEBBLE_ICON[62]     = { 0x00, 0x02, 0x04, 0x08, 0x00, 0x00, 0x02, 0x04, 0x08, 0x00, 0xFD, 0xFB,
  0xF7, 0xE9, 0xF8, 0x85, 0x0A, 0x14, 0x29, 0x08, 0x85, 0x0A, 0x14, 0x29,
  0x08, 0x85, 0xFA, 0x14, 0x29, 0xF8, 0x85, 0x02, 0x14, 0x29, 0x00, 0xFD,
  0xFB, 0xF7, 0xED, 0xF8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
  0x00, 0x00}; // 40 x 10px

// The simple/hollow/full-color weather icon sets used to be static
// arrays here too -- now RESOURCE_ID_ICON_WEATHER_SIMPLE_*/HOLLOW_*/
// FULLCOLOR_* image resources, loaded on demand by draw_weather_icon_*()
// below (see draw_icon_resource()/draw_icon_resource_native() and the
// comment above PEBBLE_ICON).


static void draw_tiny_icon(GContext *ctx, GPoint top_left, const uint8_t *pattern, int rows, int width, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  int bytes_per_row = (width + 7) / 8;
  for (int row = 0; row < rows; row++) {
    int16_t y0 = top_left.y + row;
    for (int col = 0; col < width; col++) {
      int byte_index = row * bytes_per_row + col / 8;
      int bit_index = 7 - (col % 8);
      if (pattern[byte_index] & (1 << bit_index)) {
        int16_t x0 = top_left.x + col;
        graphics_fill_rect(ctx, GRect(x0, y0, 1, 1), 0, GCornerNone);
      }
    }
  }
}

// ---- resource-backed feature icons -----------------------------------
//
// Every feature icon except PEBBLE_ICON (see above) now lives as a PNG
// resource instead of a static bit pattern -- see package.json's
// "media" list. Both helpers below load the bitmap right before
// drawing it and destroy it immediately after, so an icon only ever
// occupies memory for the instant it's actually being rendered, and
// only the icon(s) belonging to feature slots that are on screen this
// redraw ever get decoded at all.
//
// draw_icon_resource() is for the monochrome-silhouette icons (heart,
// foot, weather simple/hollow sets, bed icons, etc.) -- these are
// authored as a 2-color (nominally opaque icon + transparent
// background) 1-bit-palette PNG (see "memoryFormat": "1BitPalette" in
// package.json). gbitmap_get_palette() exposes that bitmap's own
// 2-entry palette directly (mutable in place -- no separate "set"
// call needed, same as tint_marker_bitmap() above already relies on),
// which this recolors to whatever GColor the caller wants for the
// icon itself plus GColorClear for the background, giving every one
// of them full-color tinting (any GColor, not just black) and alpha
// transparency for free, with no per-icon code.
//
// Which of the 2 palette entries IS the icon vs the background isn't
// safe to assume by a fixed index (0 or 1) -- that's a detail of how
// each individual PNG happened to get authored/exported, and wasn't
// consistent across every one of these icon resources. Assuming a
// fixed index (this used to always treat index 0 as background,
// index 1 as icon) inverted whichever icons didn't happen to match
// that assumption: a solid tinted box with the icon shape as a
// transparent cutout, instead of a tinted icon on a transparent
// background. Two signals instead, in priority order: (1) if the
// source PNG's own palette already has real per-entry alpha (one
// entry fully transparent, the other opaque) -- same technique
// tint_marker_bitmap() above already uses for the bitmap marker
// styles -- trust that directly; (2) otherwise (1-bit-palette
// resources don't reliably carry per-entry alpha through the build
// pipeline the way the marker bitmaps' richer palette formats do, per
// the request), fall back to treating whichever of the two colors is
// darker as the icon -- these are authored as plain black icon
// shapes on a white/light placeholder background, so the darker entry
// reliably is the icon regardless of which index it's stored at.
//
// Outline support reuses the exact technique already used everywhere
// else in this file (draw_text_outlined() et al, see OUTLINE_OFFSETS'
// comment near the top): call sites draw the icon 4x shifted in a
// contrasting outline color, then once more in the real fill color --
// draw_icon_resource() itself doesn't need to know about outlines at
// all, it just draws one already-tinted icon.
// Recolors and draws an already-loaded palette icon bitmap in place --
// the shared inner step both draw_icon_resource() and draw_icon_
// resource_with_outline() below use, so a 4-shifted-copy outline pass
// doesn't have to decode/reload the same resource an extra 4 times
// just to redraw it in a different color. Safe to call repeatedly on
// the same bmp with different colors: each call re-derives which
// palette entry is "ink" vs "transparent" from the bitmap's current
// palette state (transparent stays reliably GColorClear regardless of
// what the ink entry was last recolored to), so the result is
// identical to loading a fresh copy each time.
static void draw_icon_bitmap_tinted(GContext *ctx, GBitmap *bmp, GPoint top_left, GColor color) {
  GColor *palette = gbitmap_get_palette(bmp);
  if (palette) {
    bool transparent0 = (palette[0].argb & 0xC0) == 0;
    bool transparent1 = (palette[1].argb & 0xC0) == 0;
    int ink;
    if (transparent0 != transparent1) {
      ink = transparent0 ? 1 : 0;
    } else {
      int sum0 = ((palette[0].argb >> 4) & 0x03) + ((palette[0].argb >> 2) & 0x03) + (palette[0].argb & 0x03);
      int sum1 = ((palette[1].argb >> 4) & 0x03) + ((palette[1].argb >> 2) & 0x03) + (palette[1].argb & 0x03);
      ink = (sum0 <= sum1) ? 0 : 1;
    }
    palette[ink] = color;
    palette[1 - ink] = GColorClear;
  }
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, ICON_WIDTH, ICON_ROWS));
}

static void draw_icon_resource(GContext *ctx, GPoint top_left, uint32_t resource_id, GColor color) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  draw_icon_bitmap_tinted(ctx, bmp, top_left, color);
  gbitmap_destroy(bmp);
}

// Loads resource_id exactly once and draws it up to 5 times (4
// outline-shifted copies in outline_color when do_outline, then once
// more in color) -- one shared draw call for every icon that needs an
// outline, instead of a dozen hand-repeated "4-shifted-copy outline,
// then the real icon" blocks -- 5 independent resource loads/decodes
// per icon instead of the 1 this version needs.
static void draw_icon_resource_with_outline(GContext *ctx, GPoint pos, uint32_t resource_id,
                                             uint8_t outline_style, GColor outline_color, GColor color) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  if (outline_style != 0) {
    const GPoint *offs; int offs_n;
    get_outline_offsets(outline_style, &offs, &offs_n);
    for (int i = 0; i < offs_n; i++) {
      GPoint shifted = GPoint(pos.x + offs[i].x, pos.y + offs[i].y);
      draw_icon_bitmap_tinted(ctx, bmp, shifted, outline_color);
    }
  }
  draw_icon_bitmap_tinted(ctx, bmp, pos, color);
  gbitmap_destroy(bmp);
}

// For the full-color weather icon set (style 2, "Full color") -- these
// carry their own baked-in per-pixel color and alpha (authored as a
// true-color+alpha PNG, no palette to remap), so unlike
// draw_icon_resource() there's no tint color to apply. See
// draw_weather_icon_filled()'s own comment for why there's no outline
// pass for this style either.
static void draw_icon_resource_native(GContext *ctx, GPoint top_left, uint32_t resource_id) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, ICON_WIDTH, ICON_ROWS));
  gbitmap_destroy(bmp);
}

// A small battery glyph (outline + nub), drawn with primitives rather
// than a bitmap pattern since it's naturally an outline+fill shape,
// not a solid silhouette like the heart/foot icons.
static void draw_corner_battery_icon(GContext *ctx, GPoint top_left, GColor color, int charge) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y, 4, 2), 0, GCornerNone); // nub
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(top_left.x, top_left.y + 2, 8, 12));
  int16_t charge_pixels = (charge == 0) ? 0 : (8 * charge / 100);
  graphics_fill_rect(ctx, GRect(top_left.x + 2, top_left.y + 4 + (8 - charge_pixels), 4, charge_pixels), 0, GCornerNone); // fill
}

// ---- weather condition icon ("Weather icon" / "Temp + weather icon" -----
// corner content, style-selectable via weather_icon_style) -----------------

// Which of the 7 weather icon categories to show, from the same
// weather_condition + cloud_cover_pct combination short_condition_text()
// already uses -- kept in sync with those exact thresholds so the icon
// and the "Sunny"/"P.Cloudy"/etc. text (when both are visible somewhere)
// never disagree. 0=sunny, 1=partly cloudy, 2=cloudy/overcast, 3=fog,
// 4=rain, 5=snow, 6=storm.
static uint8_t weather_icon_category(uint8_t weather_condition, uint8_t cloud_pct) {
  switch (weather_condition) {
    case 1: return 3; // fog
    case 2: return 4; // rain
    case 3: return 5; // snow
    case 4: return 6; // storm
    default:
      if (cloud_pct < 20) return 0; // sunny
      if (cloud_pct < 60) return 1; // partly cloudy
      return 2; // cloudy/overcast
  }
}

// These three are similar to other icon systems wrapped in separate call for clarity
static void draw_weather_icon_hollow(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_SUN, color);
      return;
    case 1: // partly cloudy
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_PARTLY_CLOUDY, color);
      return;
    case 2: // cloudy / overcast
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_CLOUDY_OVERCAST, color);
      return;
    case 3: // fog
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_FOG, color);
      return;
    case 4: // rain
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_RAIN, color);
      return;
    case 5: // snow
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_SNOW, color);
      return;
    case 6: { // storm
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_HOLLOW_STORM, color);
      return;
    }
  }
}

static void draw_weather_icon_simple(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  switch (category) {
    case 0: // sunny
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_SUN, color);
      return;
    case 1: // partly cloudy
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_PARTLY_CLOUDY, color);
      return;
    case 2: // cloudy / overcast
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_CLOUDY_OVERCAST, color);
      return;
    case 3: // fog
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_FOG, color);
      return;
    case 4: // rain
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_RAIN, color);
      return;
    case 5: // snow
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_SNOW, color);
      return;
    case 6: { // storm
      draw_icon_resource(ctx, top_left, RESOURCE_ID_ICON_WEATHER_SIMPLE_STORM, color);
      return;
    }
  }
}

// Unlike draw_weather_icon_hollow()/draw_weather_icon_simple() above,
// this ignores `color` entirely -- full-color icons carry their own
// baked-in per-pixel colors (see README.md), so there's nothing for
// an external tint to apply to. The parameter stays only so all three
// draw_weather_icon_*() functions share draw_weather_icon()'s common
// dispatch signature.
static void draw_weather_icon_filled(GContext *ctx, GPoint top_left, uint8_t category, GColor color) {
  (void)color;
  switch (category) {
    case 0: // sunny
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SUN);
      return;
    case 1: // partly cloudy
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_PARTLY_CLOUDY);
      return;
    case 2: // cloudy / overcast
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_CLOUDY_OVERCAST);
      return;
    case 3: // fog
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_FOG);
      return;
    case 4: // rain
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_RAIN);
      return;
    case 5: // snow
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_SNOW);
      return;
    case 6: { // storm
      draw_icon_resource_native(ctx, top_left, RESOURCE_ID_ICON_WEATHER_FULLCOLOR_STORM);
      return;
    }
  }
}

static void draw_weather_icon(GContext *ctx, GPoint top_left, uint8_t category, uint8_t style, GColor color) {
  switch (style) {
    case 0: draw_weather_icon_simple(ctx, top_left, category, color); return;
    case 2: draw_weather_icon_filled(ctx, top_left, category, color); return;
    case 1:
    default: draw_weather_icon_hollow(ctx, top_left, category, color); return;
  }
}

// ---- timezone feature -----------------------------------------------------
// A curated list of major cities (not the full IANA database) with a
// fixed, always-shown 3-letter city code (not "GMT"/"BST"-style, per
// the brief -- "LON"/"TOK" stay the same year-round even though the
// underlying UTC offset shifts with DST) plus enough to compute the
// CURRENT actual offset: a standard-time UTC offset in minutes, and
// which DST rule (if any) applies. DST is modeled for the two rules
// covering most of what's likely to be picked -- current-era US
// (2nd Sunday March - 1st Sunday November) and EU (last Sunday March -
// last Sunday October) -- both computed exactly from the actual date,
// not a lookup table, so they stay correct in future years. Southern-
// hemisphere DST (Sydney, Auckland) is NOT modeled -- those two just
// use their fixed standard-time offset year-round, a known simplification.
typedef struct {
  const char *abbr;         // fixed on-watch label, e.g. "LON"
  int16_t base_offset_min;  // standard-time UTC offset, in minutes (can be negative)
  uint8_t dst_rule;         // 0=none, 1=US, 2=EU
} TimezoneInfo;

static const TimezoneInfo TIMEZONES[] = {
  { "LON",    0, 2 }, // London
  { "PAR",   60, 2 }, // Paris/Berlin/Madrid (Central European Time)
  { "CAI",  120, 0 }, // Cairo
  { "MOW",  180, 0 }, // Moscow
  { "DXB",  240, 0 }, // Dubai
  { "DEL",  330, 0 }, // Delhi/Mumbai (UTC+5:30)
  { "DAC",  360, 0 }, // Dhaka
  { "BKK",  420, 0 }, // Bangkok/Jakarta
  { "BJS",  480, 0 }, // Beijing/Shanghai/Singapore
  { "TOK",  540, 0 }, // Tokyo
  { "SYD",  600, 0 }, // Sydney (DST not modeled -- see note above)
  { "AKL",  720, 0 }, // Auckland (DST not modeled -- see note above)
  { "NYC", -300, 1 }, // New York
  { "CHI", -360, 1 }, // Chicago
  { "DEN", -420, 1 }, // Denver
  { "LAX", -480, 1 }, // Los Angeles
  { "ANC", -540, 1 }, // Anchorage
  { "HNL", -600, 0 }, // Honolulu
  { "SAO", -180, 0 }, // Sao Paulo
};
#define TIMEZONE_COUNT (int)(sizeof(TIMEZONES) / sizeof(TIMEZONES[0]))

// civil_from_days()/days_from_civil() -- the well-known constant-time
// Gregorian-calendar<->epoch-days conversion (Howard Hinnant's
// "civil_from_days"/"days_from_civil"), used instead of gmtime() so this
// doesn't depend on anything beyond plain integer arithmetic. Verified
// numerically against Python's datetime for round-trips across leap
// years and the epoch boundary before use.
static void civil_from_days(int32_t z, int *y, int *m, int *d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t year = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  uint32_t month = mp + (mp < 10 ? 3 : (uint32_t)-9);
  *y = year + (month <= 2 ? 1 : 0);
  *m = (int)month;
  *d = (int)day;
}

static int32_t days_from_civil(int y, int m, int d) {
  y -= (m <= 2) ? 1 : 0;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// 0=Sunday..6=Saturday -- day 0 (1970-01-01) was a Thursday.
static int day_of_week_from_days(int32_t days) {
  int32_t d = (days + 4) % 7;
  return (int)(d < 0 ? d + 7 : d);
}

// The Nth Sunday of a month as epoch days (nth=1 => first Sunday,
// nth=-1 => last Sunday).
static int32_t nth_sunday_epoch_days(int year, int month, int nth) {
  if (nth > 0) {
    int32_t d1 = days_from_civil(year, month, 1);
    int dow1 = day_of_week_from_days(d1);
    int first_sunday_day = (dow1 == 0) ? 1 : (8 - dow1);
    return days_from_civil(year, month, first_sunday_day + (nth - 1) * 7);
  }
  static const int DAYS_IN_MONTH[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int last_day = DAYS_IN_MONTH[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) last_day = 29;
  int32_t d_last = days_from_civil(year, month, last_day);
  return d_last - day_of_week_from_days(d_last);
}

// Whether US-rule DST is active at this exact UTC instant. Transition
// hours are approximated with a single fixed UTC hour common to
// continental US zones (2am local standard time is ~7am UTC for the
// March start, ~6am UTC for the November end) -- exact for the correct
// calendar day either way, could be off by up to a couple hours right
// at the transition instant itself for the westernmost zones.
static bool is_us_dst(int32_t epoch_days, int32_t secs_of_day, int year) {
  int32_t start = nth_sunday_epoch_days(year, 3, 2) * 86400 + 7 * 3600;
  int32_t end = nth_sunday_epoch_days(year, 11, 1) * 86400 + 6 * 3600;
  int32_t now = epoch_days * 86400 + secs_of_day;
  return now >= start && now < end;
}

// EU-rule DST -- exact, since the EU rule is itself defined in UTC
// terms (01:00 UTC on the last Sunday of March/October).
static bool is_eu_dst(int32_t epoch_days, int32_t secs_of_day, int year) {
  int32_t start = nth_sunday_epoch_days(year, 3, -1) * 86400 + 3600;
  int32_t end = nth_sunday_epoch_days(year, 10, -1) * 86400 + 3600;
  int32_t now = epoch_days * 86400 + secs_of_day;
  return now >= start && now < end;
}

// Resolves a TimezoneInfo's actual current UTC offset in minutes,
// including DST if applicable right now.
static int16_t timezone_current_offset_min(const TimezoneInfo *tz, time_t utc_now) {
  int32_t epoch_days = (int32_t)(utc_now / 86400);
  int32_t secs_of_day = (int32_t)(utc_now % 86400);
  int y, m, d;
  civil_from_days(epoch_days, &y, &m, &d);
  bool dst = false;
  if (tz->dst_rule == 1) dst = is_us_dst(epoch_days, secs_of_day, y);
  else if (tz->dst_rule == 2) dst = is_eu_dst(epoch_days, secs_of_day, y);
  return tz->base_offset_min + (dst ? 60 : 0);
}

// Discrete three-band read of a remote timezone's local hour: white
// through the day, black overnight, and a light-gray "twilight" band
// around sunrise/sunset -- deliberately a simple fixed-hour heuristic
// (06:00-08:00 sunrise, 18:00-20:00 sunset) rather than real sun-
// altitude astronomy, which isn't available for an arbitrary remote
// timezone the way it is for the user's own location via
// eclipse_sky_is_bright().
static GColor timezone_daylight_color(int local_hour24) {
  if (local_hour24 >= 8 && local_hour24 < 18) return GColorWhite;  // day
  if (local_hour24 < 6 || local_hour24 >= 20) return GColorBlack;  // night
  return GColorLightGray; // 06-08 sunrise, 18-20 sunset -- twilight
}

// ---- pressure trend / wind direction icons -------------------------------

// A small up/down chevron (rising/falling) or a flat horizontal line
// (flat), drawn with plain line primitives -- no bitmap needed.
static void draw_pressure_trend_icon(GContext *ctx, GPoint top_left, uint8_t trend, GColor color) {
  GPoint center = GPoint(top_left.x + 5, top_left.y + 6);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  if (trend == 1) { // rising
    graphics_draw_line(ctx, GPoint(center.x, center.y + 5), GPoint(center.x, center.y - 5));
    graphics_draw_line(ctx, GPoint(center.x, center.y - 5), GPoint(center.x - 3, center.y - 2));
    graphics_draw_line(ctx, GPoint(center.x, center.y - 5), GPoint(center.x + 3, center.y - 2));
  } else if (trend == 2) { // falling
    graphics_draw_line(ctx, GPoint(center.x, center.y - 5), GPoint(center.x, center.y + 5));
    graphics_draw_line(ctx, GPoint(center.x, center.y + 5), GPoint(center.x - 3, center.y + 2));
    graphics_draw_line(ctx, GPoint(center.x, center.y + 5), GPoint(center.x + 3, center.y + 2));
  } else { // flat
    graphics_draw_line(ctx, GPoint(center.x - 5, center.y), GPoint(center.x + 5, center.y));
  }
}

// A small compass arrow, rotated via sin/cos (same technique
// hand_layer.c uses for the analog hands). `from_deg` is the direction
// the wind blows FROM (standard meteorological convention, e.g. Open-
// Meteo's winddirection field) -- the arrow itself points the other
// way, toward where the wind is actually blowing, since that reads as
// more immediately useful at a glance than the source bearing would.
static void draw_wind_direction_icon(GContext *ctx, GPoint top_left, int16_t from_deg, GColor color) {
  GPoint center = GPoint(top_left.x + 6, top_left.y + 6);
  int32_t angle = (int32_t)(((from_deg + 180) % 360) * TRIG_MAX_ANGLE) / 360;
  int16_t len = 6;
  GPoint tip = GPoint(center.x + (len * sin_lookup(angle)) / TRIG_MAX_RATIO,
                       center.y - (len * cos_lookup(angle)) / TRIG_MAX_RATIO);
  GPoint tail = GPoint(center.x - (len * sin_lookup(angle)) / TRIG_MAX_RATIO,
                        center.y + (len * cos_lookup(angle)) / TRIG_MAX_RATIO);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, tail, tip);
  int32_t back_angle1 = angle + (TRIG_MAX_ANGLE * 150) / 360;
  int32_t back_angle2 = angle - (TRIG_MAX_ANGLE * 150) / 360;
  GPoint h1 = GPoint(tip.x + (4 * sin_lookup(back_angle1)) / TRIG_MAX_RATIO, tip.y - (4 * cos_lookup(back_angle1)) / TRIG_MAX_RATIO);
  GPoint h2 = GPoint(tip.x + (4 * sin_lookup(back_angle2)) / TRIG_MAX_RATIO, tip.y - (4 * cos_lookup(back_angle2)) / TRIG_MAX_RATIO);
  graphics_draw_line(ctx, tip, h1);
  graphics_draw_line(ctx, tip, h2);
}

// A simple two-peak mountain silhouette, drawn as two filled triangles
// -- used by the "Altitude" corner content.

// Compass rose for the "Compass" corner/edge content (id 85) -- 4
// arrows from center, rotated so they point toward their true compass
// direction given the watch's current heading (0deg = the watch's own
// "up" is pointing true north, so nothing needs to rotate; as heading
// increases, true north swings counterclockwise relative to the
// watch's own "up", hence the (360-heading_deg) below). North gets
// its own longer arrow with a distinct head, drawn in north_color;
// the other 3 (E/S/W) are shorter, plainer, and share other_color --
// for the outline pass both params are just the same flat outline
// color, same as every other multi-part icon here.
static void draw_compass_icon(GContext *ctx, GPoint top_left, int16_t heading_deg,
                               GColor north_color, GColor other_color) {
  GPoint center = GPoint(top_left.x + 6, top_left.y + 6);
  int32_t north_angle = (int32_t)((((360 - (heading_deg % 360)) % 360) * TRIG_MAX_ANGLE)) / 360;

  for (int k = 0; k < 4; k++) {
    int32_t angle = north_angle + (int32_t)((int64_t)k * TRIG_MAX_ANGLE / 4);
    bool is_north = (k == 0);
    int16_t len = is_north ? 6 : 4;
    GColor color = is_north ? north_color : other_color;
    GPoint tip = GPoint(center.x + (len * sin_lookup(angle)) / TRIG_MAX_RATIO,
                         center.y - (len * cos_lookup(angle)) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, center, tip);
    int32_t head_len = is_north ? 3 : 2;
    int32_t back_angle1 = angle + (TRIG_MAX_ANGLE * 150) / 360;
    int32_t back_angle2 = angle - (TRIG_MAX_ANGLE * 150) / 360;
    GPoint h1 = GPoint(tip.x + (head_len * sin_lookup(back_angle1)) / TRIG_MAX_RATIO, tip.y - (head_len * cos_lookup(back_angle1)) / TRIG_MAX_RATIO);
    GPoint h2 = GPoint(tip.x + (head_len * sin_lookup(back_angle2)) / TRIG_MAX_RATIO, tip.y - (head_len * cos_lookup(back_angle2)) / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, tip, h1);
    graphics_draw_line(ctx, tip, h2);
  }
}

// The compass's own "sleep mode" replacement icon -- two simple
// zigzag "Z"/"z" shapes (a bigger one upper-left, a smaller one
// lower-right, like a comic-strip "sleeping" indicator) rather than
// the rose above, shown whenever compass_feature_is_asleep() is true.
static void draw_compass_sleep_icon(GContext *ctx, GPoint top_left, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  // Big Z, roughly 7x7, upper-left of the icon box.
  GPoint bz[4] = { GPoint(top_left.x, top_left.y), GPoint(top_left.x + 6, top_left.y),
                    GPoint(top_left.x, top_left.y + 6), GPoint(top_left.x + 6, top_left.y + 6) };
  graphics_draw_line(ctx, bz[0], bz[1]);
  graphics_draw_line(ctx, bz[1], bz[2]);
  graphics_draw_line(ctx, bz[2], bz[3]);
  // Small z, roughly 4x4, lower-right, overlapping the big one's tail like a real "Zz" sleep glyph.
  GPoint sz_origin = GPoint(top_left.x + 5, top_left.y + 6);
  GPoint sz[4] = { sz_origin, GPoint(sz_origin.x + 4, sz_origin.y), GPoint(sz_origin.x, sz_origin.y + 4), GPoint(sz_origin.x + 4, sz_origin.y + 4) };
  graphics_draw_line(ctx, sz[0], sz[1]);
  graphics_draw_line(ctx, sz[1], sz[2]);
  graphics_draw_line(ctx, sz[2], sz[3]);
}

static void draw_mountain_icon(GContext *ctx, GPoint top_left, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  GPoint peak1[3] = {
    GPoint(top_left.x + 4, top_left.y + 1),
    GPoint(top_left.x, top_left.y + 11),
    GPoint(top_left.x + 9, top_left.y + 11),
  };
  GPathInfo info1 = { .num_points = 3, .points = peak1 };
  GPath *path1 = gpath_create(&info1);
  gpath_draw_filled(ctx, path1);
  gpath_destroy(path1);

  GPoint peak2[3] = {
    GPoint(top_left.x + 11, top_left.y + 4),
    GPoint(top_left.x + 6, top_left.y + 11),
    GPoint(top_left.x + 15, top_left.y + 11),
  };
  GPathInfo info2 = { .num_points = 3, .points = peak2 };
  GPath *path2 = gpath_create(&info2);
  gpath_draw_filled(ctx, path2);
  gpath_destroy(path2);
}

// 7-stop gradient: turquoise (cold/low end) -> light blue -> green ->
// yellow -> orange -> red -> violet (hot/high end). Used for both
// temperature (-10..40C) and UV index (1..13) by passing different
// min/max, per the brief's request for "more colors" than a simple
// 2-stop blend.
static GColor seven_stop_gradient(int32_t value, int32_t min_v, int32_t max_v) {
  static const int16_t STOPS[7][3] = {
    {  64, 224, 208 },  // turquoise
    { 173, 216, 230 },  // light blue
    {   0, 200,   0 },  // green
    { 255, 220,   0 },  // yellow
    { 255, 140,   0 },  // orange
    { 220,  20,  20 },  // red
    { 148,   0, 211 },  // violet
  };
  if (max_v <= min_v || value <= min_v) return GColorFromRGB(STOPS[0][0], STOPS[0][1], STOPS[0][2]);
  if (value >= max_v) return GColorFromRGB(STOPS[6][0], STOPS[6][1], STOPS[6][2]);

  int32_t pos_x6000 = ((value - min_v) * 6000) / (max_v - min_v); // 0..6000 across 6 segments
  int seg = (int)(pos_x6000 / 1000);
  if (seg > 5) seg = 5;
  int32_t seg_frac = pos_x6000 - (int32_t)seg * 1000; // 0..1000 within the segment

  int16_t r = STOPS[seg][0] + (int16_t)(((STOPS[seg + 1][0] - STOPS[seg][0]) * seg_frac) / 1000);
  int16_t g = STOPS[seg][1] + (int16_t)(((STOPS[seg + 1][1] - STOPS[seg][1]) * seg_frac) / 1000);
  int16_t b = STOPS[seg][2] + (int16_t)(((STOPS[seg + 1][2] - STOPS[seg][2]) * seg_frac) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// Simple white (low) -> turquoise (high) gradient, used for humidity,
// wind, and rain chance -- these don't need the full 7-stop range,
// just "more of this = more teal".
static GColor white_to_turquoise_gradient(int32_t value, int32_t min_v, int32_t max_v) {
  if (max_v <= min_v) return GColorWhite;
  int32_t clamped = value < min_v ? min_v : (value > max_v ? max_v : value);
  int32_t frac1000 = ((clamped - min_v) * 1000) / (max_v - min_v);
  int16_t r = 255 - (int16_t)(((255 - 64) * frac1000) / 1000);
  int16_t g = 255 - (int16_t)(((255 - 224) * frac1000) / 1000);
  int16_t b = 255 - (int16_t)(((255 - 208) * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// Same 7-stop rainbow as seven_stop_gradient(), but reversed: the
// high end of the value range maps to the gradient's "calm" turquoise/
// green side instead of its "alarming" red/violet side. Temperature
// and UV use the gradient the normal way round (more = hotter/worse);
// the sleep-duration features below want the opposite sense, since
// more sleep is the good result.
static GColor seven_stop_gradient_reversed(int32_t value, int32_t min_v, int32_t max_v) {
  return seven_stop_gradient(min_v + (max_v - value), min_v, max_v);
}

// Red (0%) -> green (100%+). Shared by "steps today"/"step goal %"
// (percent of daily goal) and "battery" (percent charged) -- same
// red-is-low, green-is-high convention makes sense for both.
static GColor red_green_gradient(uint8_t pct) {
  if (pct >= 100) return GColorFromRGB(0, 200, 0);
  int32_t frac1000 = ((int32_t)pct * 1000) / 100;
  int16_t r = 220 - (int16_t)((220 * frac1000) / 1000);
  int16_t g = (int16_t)((200 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, 0);
}

// White (Kp 0, geomagnetically quiet) -> red (Kp 9, extreme storm) --
// the aurora Kp index feature's own dynamic color. Runs the opposite
// direction from red_green_gradient above (there, red is the BAD end;
// here, red is the exciting "aurora reaching further south than
// usual" end), so it's its own function rather than a reversed reuse.
static GColor white_to_red_gradient(uint8_t kp_x10) {
  if (kp_x10 >= 90) return GColorFromRGB(220, 0, 0);
  int32_t frac1000 = ((int32_t)kp_x10 * 1000) / 90;
  // Capped at 170, not 255 -- Pebble's display quantizes each RGB
  // channel to just 4 levels (0/85/170/255), so anything above ~213
  // rounds straight back up to 255 anyway. A genuinely calm Kp (or,
  // more often in practice, no reading fetched yet, which also reads
  // as 0) used to render as pure white text, which vanishes into any
  // light/white-background color scheme whenever the outline setting
  // is off. 170 quantizes cleanly to a pale pink-white that's never
  // fully invisible, while still reading as "white-ish" per the
  // original white-to-red design.
  int16_t g = 170 - (int16_t)((170 * frac1000) / 1000);
  int16_t b = g;
  return GColorFromRGB(255, (uint8_t)g, (uint8_t)b);
}

// Pink (calm/resting) -> red -> violet (dangerously high), scaled by
// actual BPM. The 3 thresholds below are a reasonable generic
// resting/exertion/danger split, not personalized -- tune them once
// you've seen real readings against this on the watch.
#define HR_LOW_BPM 60
#define HR_HIGH_BPM 120
#define HR_DANGER_BPM 180
static GColor heart_rate_gradient(int bpm) {
  const int16_t pink[3]   = { 255, 105, 180 };
  const int16_t red[3]    = { 220,  20,  20 };
  const int16_t violet[3] = { 148,   0, 211 };
  const int16_t *from, *to;
  int32_t frac1000;
  if (bpm <= HR_LOW_BPM) {
    return GColorFromRGB((uint8_t)pink[0], (uint8_t)pink[1], (uint8_t)pink[2]);
  } else if (bpm >= HR_DANGER_BPM) {
    return GColorFromRGB((uint8_t)violet[0], (uint8_t)violet[1], (uint8_t)violet[2]);
  } else if (bpm <= HR_HIGH_BPM) {
    from = pink; to = red;
    frac1000 = ((int32_t)(bpm - HR_LOW_BPM) * 1000) / (HR_HIGH_BPM - HR_LOW_BPM);
  } else {
    from = red; to = violet;
    frac1000 = ((int32_t)(bpm - HR_HIGH_BPM) * 1000) / (HR_DANGER_BPM - HR_HIGH_BPM);
  }
  int16_t r = from[0] + (int16_t)(((to[0] - from[0]) * frac1000) / 1000);
  int16_t g = from[1] + (int16_t)(((to[1] - from[1]) * frac1000) / 1000);
  int16_t b = from[2] + (int16_t)(((to[2] - from[2]) * frac1000) / 1000);
  return GColorFromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// White (sea level) -> turquoise (high), for the "Altitude" content --
// default 0-4000m range, adjust ALTITUDE_GRADIENT_MAX_M once you've
// seen real readings.
#define ALTITUDE_GRADIENT_MAX_M 4000
static GColor altitude_gradient(int16_t altitude_m) {
  return white_to_turquoise_gradient(altitude_m, 0, ALTITUDE_GRADIENT_MAX_M);
}

// Dim gray (faint) -> white (strong), for the meteor-shower intensity
// reading -- "more meteors = whiter", replacing the old red/green read.
static GColor meteor_intensity_gradient(uint8_t pct) {
  int32_t frac1000 = ((int32_t)pct * 1000) / 100;
  int16_t v = 90 + (int16_t)((165 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// White during the day, black at night, blending linearly across the
// hour either side of the actual sunrise/sunset moment -- shared by
// the digital-time/full-time content and the bed-time/wake-time
// content below. Falls back to a flat white if sun data hasn't
// arrived yet (0 is never a real sunrise/sunset timestamp).
#define DAYNIGHT_TRANSITION_SECS 3600
static GColor daynight_gradient(time_t at, time_t sun_rise, time_t sun_set) {
  if (sun_rise <= 0 || sun_set <= 0) return GColorWhite;
  bool is_daytime = (at >= sun_rise && at < sun_set);
  int32_t dist_to_rise = (int32_t)(at - sun_rise); // negative before rise, positive after
  int32_t dist_to_set  = (int32_t)(at - sun_set);
  int32_t abs_rise = dist_to_rise < 0 ? -dist_to_rise : dist_to_rise;
  int32_t abs_set  = dist_to_set  < 0 ? -dist_to_set  : dist_to_set;
  bool near_rise = abs_rise <= abs_set;
  int32_t dist = near_rise ? dist_to_rise : dist_to_set;
  int32_t abs_dist = near_rise ? abs_rise : abs_set;
  if (abs_dist >= DAYNIGHT_TRANSITION_SECS) return is_daytime ? GColorWhite : GColorBlack;
  // Within an hour of the nearer transition: blend across it. At
  // sunrise `dist` runs -3600 (an hour before, still black) through 0
  // (right at sunrise) to +3600 (an hour after, fully white); sunset
  // is the same shape, inverted (white -> black).
  int32_t frac1000 = ((dist + DAYNIGHT_TRANSITION_SECS) * 1000) / (2 * DAYNIGHT_TRANSITION_SECS);
  if (frac1000 < 0) frac1000 = 0;
  if (frac1000 > 1000) frac1000 = 1000;
  int16_t v = near_rise ? (int16_t)((255 * frac1000) / 1000) : 255 - (int16_t)((255 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// Plain black->white ramp across a field's own numeric range, no
// sunrise/sunset involved -- used for the standalone hour/minute/
// second components ("single time values"), as opposed to the full
// clock-time content above which uses daynight_gradient() instead.
static GColor linear_white_black(int32_t value, int32_t min_v, int32_t max_v) {
  if (max_v <= min_v) return GColorWhite;
  int32_t clamped = value < min_v ? min_v : (value > max_v ? max_v : value);
  int32_t frac1000 = ((clamped - min_v) * 1000) / (max_v - min_v);
  int16_t v = (int16_t)((255 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// Multi-value dates (weekday+day, day/month, full dates, "long date",
// ...) are graded by how far the year has progressed -- Jan 1 sits at
// the gradient's cold end, Dec 31 at its hot end.
static GColor date_year_progress_gradient(const struct tm *t) {
  return seven_stop_gradient(t->tm_yday, 0, 365);
}

// White (just updated) -> full red at 2h+ stale, for the "last
// weather update" content types.
#define WEATHER_STALE_RED_SECS (2 * 3600)
static GColor weather_staleness_gradient(time_t now, time_t last_update) {
  if (last_update <= 0) return GColorRed; // never updated at all -- treat like fully stale
  int32_t age = (int32_t)(now - last_update);
  if (age <= 0) return GColorWhite;
  if (age >= WEATHER_STALE_RED_SECS) return GColorRed;
  int32_t frac1000 = (age * 1000) / WEATHER_STALE_RED_SECS;
  int16_t gb = 255 - (int16_t)((255 * frac1000) / 1000);
  return GColorFromRGB(255, (uint8_t)gb, (uint8_t)gb);
}

// The four weather-family color gradients "current conditions" (and
// nothing else) uses, each scaled by that condition's own intensity
// rather than a single flat color -- clearer sky/heavier rain/etc.
// reads as a visibly different shade, not just a different icon.
#define OVERCAST_CLOUD_THRESHOLD 40 // cloud_pct at/above this reads as "overcast" rather than "sunny"

// Clear/sunny: white fading toward a warm golden-white as skies get
// clearer (lower cloud_pct).
static GColor sunny_yellow_white_gradient(uint8_t cloud_pct) {
  uint8_t clamped = cloud_pct > OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct;
  int32_t frac1000 = ((int32_t)(OVERCAST_CLOUD_THRESHOLD - clamped) * 1000) / OVERCAST_CLOUD_THRESHOLD;
  int16_t b = 255 - (int16_t)((85 * frac1000) / 1000);
  return GColorFromRGB(255, 255, (uint8_t)b);
}

// Overcast/fog: light gray darkening toward a heavier gray as cloud
// cover thickens.
static GColor overcast_gray_gradient(uint8_t cloud_pct) {
  uint8_t clamped = cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct;
  int32_t frac1000 = ((int32_t)(clamped - OVERCAST_CLOUD_THRESHOLD) * 1000) / (100 - OVERCAST_CLOUD_THRESHOLD);
  int16_t v = 200 - (int16_t)((115 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
}

// Snow: white gaining a faint blue-white cast as it gets heavier
// (denser cloud cover generally means heavier snowfall).
static GColor snow_white_gradient(uint8_t cloud_pct) {
  int32_t frac1000 = ((int32_t)cloud_pct * 1000) / 100;
  int16_t rg = 255 - (int16_t)((85 * frac1000) / 1000);
  return GColorFromRGB((uint8_t)rg, (uint8_t)rg, 255);
}

// weather_condition: 0=clear/cloudy (cloud_pct alone decides sunny vs
// overcast), 1=fog (treated like overcast), 2=rain (reuses the
// existing white->turquoise gradient, driven by rain chance),
// 3=snow, 4=thunderstorm -- an extreme-weather warning that
// overrides everything else with a flat bright red regardless of any
// other value.
static GColor weather_condition_color(uint8_t condition, uint8_t cloud_pct, uint8_t rain_chance_pct) {
  if (condition == 4) return GColorFromRGB(255, 0, 0);
  if (condition == 3) return snow_white_gradient(cloud_pct);
  if (condition == 2) return white_to_turquoise_gradient(rain_chance_pct, 0, 100);
  if (condition == 1) return overcast_gray_gradient(cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : cloud_pct);
  return (cloud_pct < OVERCAST_CLOUD_THRESHOLD) ? sunny_yellow_white_gradient(cloud_pct) : overcast_gray_gradient(cloud_pct);
}

// temp_unit: 0=Celsius (input is already Celsius, passed through),
// 1=Fahrenheit, 2=Kelvin (whole-degree precision throughout this app,
// so +273 rather than +273.15 -- the .15 essentially never changes
// the rounded result at this precision).
static int16_t convert_temp(int16_t celsius, uint8_t temp_unit) {
  if (temp_unit == 1) return (int16_t)((celsius * 9) / 5 + 32);
  if (temp_unit == 2) return (int16_t)(celsius + 273);
  return celsius;
}

// ---- sleep data (Pebble HealthService, entirely on-watch -- no phone
// involvement, unlike the weather/location features above) ---------------

// "Xh Ym" -- shared by the sleep-duration and restful-sleep-duration
// corner content types.
static void format_duration_hm(char *buf, size_t buf_size, int32_t total_seconds) {
  if (total_seconds < 0) total_seconds = 0;
  int hours = (int)(total_seconds / 3600);
  int minutes = (int)((total_seconds % 3600) / 60);
  snprintf(buf, buf_size, "%dh %dm", hours, minutes);
}

typedef struct {
  time_t earliest_start;
  time_t latest_end;
  bool found;
} SleepSpan;

static bool sleep_span_iterator_cb(HealthActivity activity, time_t time_start, time_t time_end, void *context) {
  SleepSpan *span = (SleepSpan *)context;
  if (!span->found || time_start < span->earliest_start) span->earliest_start = time_start;
  if (!span->found || time_end > span->latest_end) span->latest_end = time_end;
  span->found = true;
  return true; // keep going -- want the full extent, not just the first segment
}

// Earliest sleep-activity start and latest end within the last 24
// hours, used for the "Bed time"/"Wake time" corner content types.
// Segments (there can be more than one per night, e.g. brief wake-ups)
// are merged into one overall span rather than tracked individually.
static SleepSpan get_sleep_span(void) {
  SleepSpan span = { 0, 0, false };
  time_t now = time(NULL);
  time_t day_ago = now - 24 * 3600;
  // HealthActivitySleep is already a single-bit mask value (see the
  // HealthActivityMaskAll macro in the SDK docs, and the SDK's own
  // "if (activities & HealthActivitySleep)" example) -- no extra
  // shifting needed, unlike some other Pebble bitmask enums.
  health_service_activities_iterate(HealthActivitySleep, day_ago, now, HealthIterationDirectionPast,
                                     sleep_span_iterator_cb, &span);
  return span;
}

// Shared by every HealthService-backed content (heart rate, steps,
// the 3 sleep readouts, sleep times) -- replaces each one's own
// "HealthServiceAccessibilityMask mask = ...; if (mask & ...Available)"
// pair with a single boolean call.
static bool health_metric_available(HealthMetric metric) {
  time_t now = time(NULL);
  return (health_service_metric_accessible(metric, now - 86400, now) & HealthServiceAccessibilityMaskAvailable) != 0;
}

// Shared by the heart-rate content (1) and the heart-rate segment of
// "heart rate + steps" (97) -- both used to repeat the exact same
// "check accessibility right now, then peek the value" pair. Point-in-
// time window (not the 24h one health_metric_available() above uses),
// since a heart-rate reading from anywhere in the last day would
// still read as "available" long after it's gone stale.
static int peek_current_bpm(void) {
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
  if (!(mask & HealthServiceAccessibilityMaskAvailable)) return 0;
  return (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
}

static const char *temp_unit_suffix(uint8_t temp_unit) {
  if (temp_unit == 1) return "F";
  if (temp_unit == 2) return "K";
  return "C";
}

// Simple apparent-temperature ("feels like") estimate, computed
// entirely on-watch from data already being sent (temperature, wind,
// humidity) rather than plumbing a whole new field through the
// phone-side fetch pipeline. Applies a simplified wind-chill
// adjustment when it's cold and windy, and a simplified humidity
// adjustment when it's warm and humid -- deliberately approximate
// integer arithmetic, not an exact NWS/Rothfusz regression. "Feels
// like" readings are inherently fuzzy even on dedicated weather
// services.
static int16_t apparent_temp_c(int16_t temp_c, int16_t wind_kmh, uint8_t humidity_pct) {
  if (temp_c <= 10 && wind_kmh > 4) {
    int16_t chill = (int16_t)((wind_kmh - 4) / 5);
    if (chill > 12) chill = 12;
    return temp_c - chill;
  }
  if (temp_c >= 27 && humidity_pct > 40) {
    int16_t bump = (int16_t)(((int32_t)(humidity_pct - 40) * 3) / 20);
    if (bump > 8) bump = 8;
    return temp_c + bump;
  }
  return temp_c;
}

// wind_speed_unit: 0=km/h (input is already km/h, passed through),
// 1=mph, 2=m/s, 3=knots.
static int16_t convert_wind(int16_t kmh, uint8_t wind_speed_unit) {
  if (wind_speed_unit == 1) return (int16_t)((kmh * 621) / 1000);  // mph
  if (wind_speed_unit == 2) return (int16_t)((kmh * 1000) / 3600); // m/s
  if (wind_speed_unit == 3) return (int16_t)((kmh * 540) / 1000);  // knots
  return kmh;
}

// Combined width of an icon plus its gap before whatever segment
// follows it (another icon or text), per icon_kind -- used to
// position multi-segment icon+text/icon+icon groups so consecutive
// segments land 5px apart instead of overlapping (see draw_corner_item
// and this function's own per-kind comments for what each icon
// actually measures out to).
static int16_t icon_plus_gap_width(int icon_kind) { // TODO: This might not be neccessary anymore
  switch (icon_kind) {
    case 1: case 2: case 5: case 6: case 7: case 8: case 9: case 10:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26:
      return 15; // bitmap icons (7-wide at 140% scale, ~10px) + 5px gap
    case 3: return 13; // battery (8px wide outlined body) + 5px gap
    case 4: return 21; // moon (radius 9, so 2*9+2 diameter box) + gap
    case 11: return 22; // sun-time glyph (fixed 20px, drawn via direct primitives) + gap
    case 13: return 15; // bluetooth (own case now -- see its own comment in draw_render_icon; same ~10px bitmap width as the SIMPLE_ICONS bucket) + 5px gap
    case 14: return 20; // weather icon (16-wide box, worst case a bit wider for the sun's rays) + gap
    case 15: return 12; // pressure trend chevron + gap
    case 16: return 14; // wind direction arrow + gap
    case 17: return 20; // mountain icon (16-wide box) + gap
    case 27: return 21; // compass rose (~16px-wide box, same footprint class as moon/mountain) + 5px gap
    default: return 0; // no icon
  }
}

// Renders one corner's chosen content type in one of the four color
// In-place uppercase -- used by the short weekday/month date formats
// below, since strftime's %a/%b give "Mon"/"Sep" (title case) and these
// are deliberately styled ALL CAPS instead (matching the long forms,
// which stay in strftime's natural title case: "Monday"/"September").
static void to_upper_str(char *s) {
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z') *s -= 32;
  }
}

// Renders one corner's chosen content type in one of the four color
// modes. The icon+text group is measured and positioned as a unit:
// left-anchored slots (TL/BL/middle-left) keep it flush left, right-
// anchored slots (TR/BR/middle-right) flush it against the box's own
// right edge (which is itself already anchored near the screen edge)
// so short content doesn't leave an empty gap before the edge, and
// center-anchored slots (upper/bottom-middle) center it within the
// box. The icon (when present) always precedes the text in reading
// order regardless of alignment -- only the whole group's position
// changes, not the icon/text order within it.
// allow_outline gates data->outline_style on top of the user
// setting rather than replacing it -- pass true from every caller
// that draws over the busy sky/hands canvas (corners, edge-middle
// slots), where the outline is what keeps text legible against an
// unpredictable background. The small-analog info panel's rows sit
// on their own solid-color background instead, so they pass false
// and never get one regardless of the outline_style setting --
// there's nothing there for it to contrast against.

// Weather-derived corner content (see weather_should_show_error()'s own
// comment in eclipse_data.h for the 10-refresh-streak/never-had-data
// reasoning) shows "ERR ###" instead of its normal reading once
// that's true -- checked once here, after the big content switch
// above has already built its normal buf/dynamic_color/icon_kind,
// rather than duplicating the check in all 16 cases that touch
// weather data below.
static bool content_is_weather_derived(uint8_t content) {
  switch (content) {
    case 4:  // high/low temperature
    case 5:  // current conditions
    case 6:  // UV index
    case 104: // current UV index
    case 7:  // rain chance
    case 8:  // humidity
    case 9:  // wind speed
    case 14: // visibility score
    case 15: // cloud cover
    case 31: // weather icon only
    case 32: // temp + weather icon
    case 34: // pressure
    case 35: // wind direction
    case 37: // dew point
    case 73: // current temp only
    case 76: // weather icon + current/high/low
    case 77: // feels-like temp
    case 87: case 88: case 89: case 90: case 91: case 92: // weather in 1-6h
      return true;
    default:
      return false;
  }
}

// =========================================================================
// TABLE-DRIVEN FEATURE SLOTS
// =========================================================================
//
// Every one of the 12 feature slots (4 corners + 8 middle-edge lines) is
// represented by one FeatureSlot below. A slot is split into two halves
// that change at very different rates, and are recomputed independently:
//
//   - LAYOUT (is_top/is_left/is_middle/top_offset/bottom_shift/
//     middle_inset/center_horizontal/center_vertical/content/color_mode)
//     only ever changes when a setting or the shake-label state changes
//     -- resolved once by features_recompute_layout(), exactly like the
//     old FeatureSlot always did.
//
//   - VALUE (the actual text/icon/color to draw, plus the exact pixel
//     offsets where each piece goes) changes far more often -- a health
//     reading, the weather, the clock, a compass heading -- and is
//     resolved by features_recompute_slot_value(), grouped into a
//     handful of per-category functions (compute_health_value(),
//     compute_weather_value(), compute_date_value(),
//     compute_timezone_value(), compute_sky_value(),
//     compute_combo_value()) instead of one giant per-content switch.
//     Called from three places: a full settings/shake change (every
//     slot), the periodic ~1-minute refresh (every active slot), and
//     the once-a-second tick (ONLY the slot(s) whose content actually
//     needs second-by-second updates, tracked per-slot via
//     needs_second_refresh -- never all 12 just because one shows
//     seconds).
//
// Either way, by the time features_layer_update_proc() actually runs,
// every pixel position, color, and string a slot needs is already
// sitting in its FeatureSlot -- the update proc itself does no
// formatting, no gradient math, and no alignment/width measurement at
// all, just a straight loop blitting whatever's already resolved.

// One icon or one run of text within a slot, at a resolved offset from
// the slot's own box_x -- used both by the single icon+text path (as a
// 2-element case: an optional icon segment, then the text segment) and
// by the multi-icon combo content (heart rate + steps, battery + BT,
// ...), so both paths share the exact same draw-time code.
#define MAX_RENDER_SEGMENTS 4
typedef struct {
  bool is_icon;
  uint8_t icon_kind;    // 0 = none; same icon_kind numbering the old single-icon path always used
  int16_t icon_extra;    // weather category / battery charge% / compass heading-degrees / pressure trend / wind degrees / moon phase %, depending on icon_kind
  bool icon_flag;         // is_charging / is_sunrise / moon_waxing / compass_asleep, depending on icon_kind
  char text[20];           // when !is_icon
  GColor color;
  GColor color2;            // compass "other 3 arrows" color only; equals color everywhere else
  int16_t x_offset;          // resolved once, relative to the slot's box_x
  int16_t width;               // resolved once (icon: fixed box width; text: measured width)
} RenderSegment;

typedef struct {
  bool active;             // false = this slot draws nothing this cycle
  uint8_t content;
  uint8_t color_mode;

  // ---- layout: settings/shake-driven, resolved by features_recompute_layout() ----
  bool is_top;
  bool is_left;
  bool is_middle;
  int16_t top_offset;
  int16_t bottom_shift;
  int16_t middle_inset;
  bool center_horizontal;
  bool center_vertical;
  bool allow_outline;
  bool needs_second_refresh; // true if this content must be recomputed every second (time-with-seconds displays)
  // Digital-mode's single bottom feature is wider than a normal 68px
  // slot and needs to shift/resize with the clock itself (see
  // digital_clock_area()) rather than sit flush against a screen edge
  // or centered across the full screen width -- when true, box_x/box_w
  // below are used verbatim (relative to bounds.origin) instead of the
  // normal is_left/center_horizontal+CORNER_BOX_W math, and the slot's
  // content is always centered within that box.
  bool custom_box;
  int16_t box_x;
  int16_t box_w;

  // ---- value: resolved by features_recompute_slot_value() ----
  bool draw_pill;
  GColor pill_bg;
  int16_t segment_count;      // > 0 means "use segments[] below"; 0 means this slot draws nothing
  RenderSegment segments[MAX_RENDER_SEGMENTS];
} FeatureSlot;

typedef struct {
  EclipseData *data;
  FeatureSlot slots[FEATURES_MAX_SLOTS];
} FeaturesState;

enum {
  SLOT_UPPER_L1 = 0, SLOT_UPPER_L2,
  SLOT_BOTTOM_L1, SLOT_BOTTOM_L2,
  SLOT_LEFT_L1, SLOT_LEFT_L2,
  SLOT_RIGHT_L1, SLOT_RIGHT_L2,
  SLOT_CORNER_TL, SLOT_CORNER_TR, SLOT_CORNER_BL, SLOT_CORNER_BR,
};

// ---- content classification -------------------------------------------

// Only the "Time"/full-clock-with-seconds and the standalone second
// components actually need re-resolving every single second -- every
// other content type (weather, health, dates, astronomy, timezones)
// only changes on its own slower schedule (the periodic refresh
// already covers it) or on a settings change.
static bool content_needs_second_refresh(uint8_t content) {
  switch (content) {
    case 63: // full time with seconds
    case 69: case 70: case 71: case 72: // second components
      return true;
    default:
      return false;
  }
}


// Resolves the shared "one flat color" every content type not doing
// its own per-segment gradient split uses: mono (0) -> main, accent
// (1) -> accent, Pill (2) -> main (drawn over its own solid-bg-color
// plate), color (3) -> whatever gradient/rule that content computed.
// The one and only color_mode switch in this entire file -- every
// cluster function below just calls this once per segment instead of
// re-implementing the same 4-way branch.
static GColor resolve_flat_color(uint8_t color_mode, GColor dynamic_color, GColor main_color, GColor accent_color) {
  switch (color_mode) {
    case 1: return accent_color;
    case 3: return dynamic_color;
    case 0: case 2: default: return main_color;
  }
}

// Write directly into slot->segments[i] instead of building a 32-byte
// RenderSegment on the stack and block-copying it in -- see the size
// analysis's item B. Callers that need icon_extra/icon_flag set them
// on slot->segments[i] themselves right after calling this, same as
// they used to set them on the local RenderSegment before assigning it.
static void set_icon_seg(FeatureSlot *slot, int i, uint8_t icon_kind, GColor color) {
  RenderSegment *g = &slot->segments[i];
  g->is_icon = true;
  g->icon_kind = icon_kind;
  g->icon_extra = 0;
  g->icon_flag = false;
  g->color = color;
  g->color2 = color;
}

static void set_text_seg(FeatureSlot *slot, int i, const char *text, GColor color) {
  RenderSegment *g = &slot->segments[i];
  g->is_icon = false;
  g->icon_kind = 0;
  g->icon_extra = 0;
  g->icon_flag = false;
  g->color = color;
  g->color2 = color;
  snprintf(g->text, sizeof(g->text), "%s", text);
}

// One helper covers the common "icon_kind 0 => text only, else icon+text"
// 1- or 2-segment shape most content-cluster cases use.
static void slot_set(FeatureSlot *slot, uint8_t icon_kind, const char *text, GColor color) {
  if (icon_kind == 0) {
    slot->segment_count = 1;
    set_text_seg(slot, 0, text, color);
  } else {
    slot->segment_count = 2;
    set_icon_seg(slot, 0, icon_kind, color);
    set_text_seg(slot, 1, text, color);
  }
}

// ---- health cluster: heart rate, steps, battery, Bluetooth, sleep -----

static void __attribute__((noinline)) compute_health_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                  uint8_t color_mode, GColor main_color, GColor accent_color) {
  char buf[24];
  switch (content) {
    case 1: { // heart rate -- pink(low)->red->violet(dangerously high) gradient by actual BPM
      int bpm = peek_current_bpm();
      GColor dyn = (bpm > 0) ? heart_rate_gradient(bpm) : GColorLightGray;
      snprintf(buf, sizeof(buf), bpm > 0 ? "%d" : "N/A", bpm);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 1, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      set_text_seg(slot, 1, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 2: { // steps today
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 100) pct = 100;
      GColor dyn = red_green_gradient((uint8_t)pct);
      snprintf(buf, sizeof(buf), "%d", (int)steps);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 2, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      set_text_seg(slot, 1, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 3: { // step goal %
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 999) pct = 999;
      GColor dyn = red_green_gradient((uint8_t)(pct > 100 ? 100 : pct));
      snprintf(buf, sizeof(buf), "%d%%", (int)pct);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 2, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      set_text_seg(slot, 1, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    case 10: { // battery %
      BatteryChargeState bs = battery_state_service_peek();
      GColor dyn = bs.is_charging ? GColorGreen : red_green_gradient((uint8_t)bs.charge_percent);
      snprintf(buf, sizeof(buf), bs.is_charging ? "%d%%+" : "%d%%", bs.charge_percent);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 3, c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      // battery is the one icon that also always shows its own text (percentage) --
      // matches the icon+text shape every other health content uses.
      slot->segment_count = 2;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 17: { // pebble battery logo -- icon draws its own fill bar, no separate text
      BatteryChargeState bs = battery_state_service_peek();
      GColor dyn = bs.is_charging ? GColorGreen : red_green_gradient((uint8_t)bs.charge_percent);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 12, c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;
      return;
    }
    case 20: { // Bluetooth connection status -- always its own dynamic color, ignores color_mode
      bool connected = connection_service_peek_pebble_app_connection();
      GColor c = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      slot_set(slot, 13, connected ? "Connected" : "No phone", c);
      return;
    }
    case 78: { // Bluetooth, icon only -- same always-dynamic color as 20
      bool connected = connection_service_peek_pebble_app_connection();
      GColor c = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 13, c);
      return;
    }
    case 39: case 40: { // sleep duration (total, 39) / restful (deep) sleep duration (40)
      HealthMetric metric = (content == 39) ? HealthMetricSleepSeconds : HealthMetricSleepRestfulSeconds;
      int32_t range_secs = (content == 39) ? 9 * 3600 : 3 * 3600;
      uint8_t icon_kind = (content == 39) ? 21 : 22;
      GColor c;
      if (health_metric_available(metric)) {
        HealthValue secs = health_service_sum_today(metric);
        format_duration_hm(buf, sizeof(buf), (int32_t)secs);
        c = resolve_flat_color(color_mode, seven_stop_gradient_reversed((int32_t)secs, 0, range_secs), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, icon_kind, buf, c);
      return;
    }
    case 41: { // sleep quality -- restful / total, as a percentage
      GColor c;
      if (health_metric_available(HealthMetricSleepSeconds)) {
        HealthValue total = health_service_sum_today(HealthMetricSleepSeconds);
        HealthValue restful = health_service_sum_today(HealthMetricSleepRestfulSeconds);
        int pct = (total > 0) ? (int)((restful * 100) / total) : 0;
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        c = resolve_flat_color(color_mode, red_green_gradient((uint8_t)pct), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, 20, buf, c);
      return;
    }
    case 42: case 43: { // bed time (42) / wake time (43) -- day/night graded
      SleepSpan span = get_sleep_span();
      time_t event = (content == 42) ? span.earliest_start : span.latest_end;
      uint8_t icon_kind = (content == 42) ? 18 : 19;
      GColor c;
      if (span.found) {
        struct tm *et = localtime(&event);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", et);
        c = resolve_flat_color(color_mode, daynight_gradient(event, data->sun_rise, data->sun_set), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, icon_kind, buf, c);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- weather cluster: temperature, conditions, UV, rain/wind/humidity,
// pressure/AQI/visibility/cloud cover, and the "last weather update"
// readouts. All of these (per content_is_weather_derived() below) can
// be overridden wholesale to a red "ERR ###" by
// features_recompute_slot_value()'s shared tail once this function
// returns, so nothing in here needs to check for a fetch error itself.

static void __attribute__((noinline)) compute_weather_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                   uint8_t color_mode, GColor main_color, GColor accent_color, GColor bg_color) {
  char buf[24];
  GColor cond_color = data->valid
    ? weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct)
    : bg_color;

  switch (content) {
    case 4: { // high/low temperature
      int16_t hi = convert_temp(data->temp_high_c, data->temp_unit);
      int16_t lo = convert_temp(data->temp_low_c, data->temp_unit);
      if (color_mode == 3) {
        // "color" mode splits high and low into their own independently
        // gradient-colored segments instead of sharing one flat color.
        char hi_buf[8], lo_buf[8];
        snprintf(hi_buf, sizeof(hi_buf), "H%d", hi);
        snprintf(lo_buf, sizeof(lo_buf), "L%d", lo);
        slot->segment_count = 2;
        set_text_seg(slot, 0, hi_buf, seven_stop_gradient(data->temp_high_c, -10, 40));
        set_text_seg(slot, 1, lo_buf, seven_stop_gradient(data->temp_low_c, -10, 40));
      } else {
        snprintf(buf, sizeof(buf), "H%d L%d", hi, lo);
        slot->segment_count = 1;
        set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, main_color, main_color, accent_color));
      }
      return;
    }
    case 5: { // current conditions
      int16_t temp = convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d%s %s", temp, temp_unit_suffix(data->temp_unit),
               short_condition_text(data->weather_condition, data->cloud_cover_pct));
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, cond_color, main_color, accent_color));
      return;
    }
    case 6: { // UV index (today's daily max -- see 104 for the current-hour value)
      uint8_t uv = data->uv_index_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, seven_stop_gradient(uv, 1, 13), main_color, accent_color));
      return;
    }
    case 104: { // current UV index (this hour, as opposed to 6's daily max)
      uint8_t uv = data->uv_index_current_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, seven_stop_gradient(uv, 1, 13), main_color, accent_color));
      return;
    }
    case 7: { // rain chance
      snprintf(buf, sizeof(buf), "%d%%", data->rain_chance_pct);
      GColor c = resolve_flat_color(color_mode, white_to_turquoise_gradient(data->rain_chance_pct, 0, 100), main_color, accent_color);
      slot_set(slot, 5, buf, c);
      return;
    }
    case 8: { // humidity
      snprintf(buf, sizeof(buf), "%d%%", data->humidity_pct);
      GColor c = resolve_flat_color(color_mode, white_to_turquoise_gradient(data->humidity_pct, 0, 100), main_color, accent_color);
      slot_set(slot, 6, buf, c);
      return;
    }
    case 9: { // wind speed
      snprintf(buf, sizeof(buf), "%d", convert_wind(data->wind_speed_kmh, data->wind_speed_unit));
      GColor c = resolve_flat_color(color_mode, white_to_turquoise_gradient(data->wind_speed_kmh, 0, 60), main_color, accent_color);
      slot_set(slot, 7, buf, c);
      return;
    }
    case 14: { // visibility -- grayscale, like cloud cover (vis_score_pct is sent as 100-cloud%)
      snprintf(buf, sizeof(buf), "%d%%", data->vis_score_pct);
      uint8_t equiv_cloud_pct = 100 - data->vis_score_pct;
      GColor dyn = overcast_gray_gradient(equiv_cloud_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : equiv_cloud_pct);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot_set(slot, 9, buf, c);
      return;
    }
    case 15: { // cloud cover
      snprintf(buf, sizeof(buf), "%d%%", data->cloud_cover_pct);
      GColor dyn = overcast_gray_gradient(data->cloud_cover_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : data->cloud_cover_pct);
      GColor c = resolve_flat_color(color_mode, dyn, main_color, accent_color);
      slot_set(slot, 10, buf, c);
      return;
    }
    case 31: { // weather icon only, no text
      GColor c = resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 1;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = weather_icon_category(data->weather_condition, data->cloud_cover_pct);
      return;
    }
    case 32: { // temp + weather icon -- condition-based color, same as 5/31
      int16_t temp = convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d%s", temp, temp_unit_suffix(data->temp_unit));
      GColor c = resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = weather_icon_category(data->weather_condition, data->cloud_cover_pct);
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 34: { // pressure, with rising/falling/flat trend arrow
      snprintf(buf, sizeof(buf), "%d hPa", data->pressure_hpa);
      GColor c = resolve_flat_color(color_mode, seven_stop_gradient(data->pressure_hpa, 970, 1050), main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 15, c);
      slot->segments[0].icon_extra = data->pressure_trend;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 35: { // wind direction, with a rotated compass arrow
      static const char *COMPASS_DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
      int idx = ((data->wind_dir_deg + 22) / 45) % 8;
      if (idx < 0) idx += 8;
      snprintf(buf, sizeof(buf), "%s", COMPASS_DIRS[idx]);
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 16, c);
      slot->segments[0].icon_extra = data->wind_dir_deg;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 36: { // air quality -- shared 7-stop gradient, remapped per scale
      bool use_eu = (data->aqi_unit == 1);
      uint16_t aqi_value = use_eu ? data->aqi_eu : data->aqi_us;
      snprintf(buf, sizeof(buf), "AQI %d", aqi_value);
      GColor c = resolve_flat_color(color_mode, seven_stop_gradient(aqi_value, 0, use_eu ? 100 : 300), main_color, accent_color);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 37: { // dew point -- reuses the humidity feature's droplet icon
      int16_t dew = convert_temp(data->dew_point_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d%s", dew, temp_unit_suffix(data->temp_unit));
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot_set(slot, 6, buf, c);
      return;
    }
    case 38: { // altitude -- white(sea level)->turquoise(high) gradient
      GColor c;
      if (data->altitude_m <= -32000) { // sentinel: not available
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      } else {
        if (data->altitude_unit == 1) { // feet
          int32_t feet = ((int32_t)data->altitude_m * 328) / 100; // *3.28084, integer approximation
          snprintf(buf, sizeof(buf), "%ldft", (long)feet);
        } else {
          snprintf(buf, sizeof(buf), "%dm", data->altitude_m);
        }
        c = resolve_flat_color(color_mode, altitude_gradient(data->altitude_m), main_color, accent_color);
      }
      slot_set(slot, 17, buf, c);
      return;
    }
    case 73: case 74: case 75: case 77: { // current/high/low/feels-like temp only -- all 7-stop, -10..40C
      int16_t temp_c, shown;
      const char *prefix = "";
      if (content == 73) { temp_c = data->weather_temp_c; shown = convert_temp(temp_c, data->temp_unit); }
      else if (content == 74) { temp_c = data->temp_high_c; shown = convert_temp(temp_c, data->temp_unit); prefix = "H "; }
      else if (content == 75) { temp_c = data->temp_low_c; shown = convert_temp(temp_c, data->temp_unit); prefix = "L "; }
      else { temp_c = apparent_temp_c(data->weather_temp_c, data->wind_speed_kmh, data->humidity_pct); shown = convert_temp(temp_c, data->temp_unit); prefix = "FL "; }
      snprintf(buf, sizeof(buf), "%s%d%s", prefix, shown, temp_unit_suffix(data->temp_unit));
      GColor c = resolve_flat_color(color_mode, seven_stop_gradient(temp_c, -10, 40), main_color, accent_color);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 76: { // weather icon + current/high/low all in one line -- "mixed" value, condition-based color
      int16_t cur = convert_temp(data->weather_temp_c, data->temp_unit);
      int16_t hi = convert_temp(data->temp_high_c, data->temp_unit);
      int16_t lo = convert_temp(data->temp_low_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d H%d L%d%s", cur, hi, lo, temp_unit_suffix(data->temp_unit));
      GColor c = resolve_flat_color(color_mode, cond_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = weather_icon_category(data->weather_condition, data->cloud_cover_pct);
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 87: case 88: case 89: case 90: case 91: case 92: { // weather in 1-6 hours, e.g. "+3h <icon> 28C"
      int hrs_ahead = content - 86; // 1-6
      int idx = hrs_ahead - 1;
      GColor c;
      if (data->forecast_temp_c[idx] <= -128) {
        snprintf(buf, sizeof(buf), "+%dh N/A", hrs_ahead);
        c = GColorLightGray;
        slot->segment_count = 1;
        set_text_seg(slot, 0, buf, c);
        return;
      }
      int16_t shown = convert_temp(data->forecast_temp_c[idx], data->temp_unit);
      snprintf(buf, sizeof(buf), "+%dh %d%s", hrs_ahead, shown, temp_unit_suffix(data->temp_unit));
      // Same shape as "temp + weather icon" (32): plain 7-stop gradient,
      // not the condition-based color -- per the "Temperature readouts
      // (including temp+weather icon)" rule.
      c = resolve_flat_color(color_mode, seven_stop_gradient(data->forecast_temp_c[idx], -10, 40), main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 14, c);
      slot->segments[0].icon_extra = weather_icon_category(data->forecast_condition[idx], 50); // no forecast cloud% sent separately -- 50 is a neutral middle guess, only affects which of a few near-identical icon glyphs gets picked
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 93: case 94: { // last weather update time, long (93, "Last updated 12:34") / short (94, "12:34")
      GColor dyn;
      time_t now = time(NULL);
      if (data->weather_last_update > 0) {
        struct tm *ut = localtime(&data->weather_last_update);
        char time_buf[8];
        strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", ut);
        if (content == 93) snprintf(buf, sizeof(buf), "Last updated %s", time_buf);
        else snprintf(buf, sizeof(buf), "%s", time_buf);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
      }
      dyn = weather_staleness_gradient(now, data->weather_last_update);
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- date/time cluster -------------------------------------------------

static void __attribute__((noinline)) compute_date_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                uint8_t color_mode, GColor main_color, GColor accent_color, time_t now, struct tm *t) {
  char buf[24];
  GColor dyn = main_color;

  switch (content) {
    case 12: { // short date (multi-value), e.g. "Mon 15"
      char day_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      snprintf(buf, sizeof(buf), "%s %d", day_buf, t->tm_mday);
      dyn = date_year_progress_gradient(t);
      break;
    }
    case 18: { // digital time ("Time") -- day/night gradient off the actual sunrise/sunset
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
      dyn = daynight_gradient(now, data->sun_rise, data->sun_set);
      break;
    }
    case 19: { // week number (single-value) -- 7-stop gradient over its own 1-52 range
      char wk_buf[4];
      strftime(wk_buf, sizeof(wk_buf), "%V", t);
      snprintf(buf, sizeof(buf), "WK %s", wk_buf);
      dyn = seven_stop_gradient(atoi(wk_buf), 1, 52);
      break;
    }
    case 21: { // month + day (multi-value), e.g. "SEP 11"
      char mon_buf[4];
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      to_upper_str(mon_buf);
      snprintf(buf, sizeof(buf), "%s %d", mon_buf, t->tm_mday);
      dyn = date_year_progress_gradient(t);
      break;
    }
    case 22: snprintf(buf, sizeof(buf), "%d", t->tm_mday); dyn = seven_stop_gradient(t->tm_mday, 1, 31); break;
    case 23: strftime(buf, sizeof(buf), "%a", t); to_upper_str(buf); dyn = seven_stop_gradient(t->tm_wday, 0, 6); break;
    case 24: strftime(buf, sizeof(buf), "%A", t); dyn = seven_stop_gradient(t->tm_wday, 0, 6); break;
    case 25: strftime(buf, sizeof(buf), "%b", t); to_upper_str(buf); dyn = seven_stop_gradient(t->tm_mon, 0, 11); break;
    case 26: strftime(buf, sizeof(buf), "%B", t); dyn = seven_stop_gradient(t->tm_mon, 0, 11); break;
    case 27: snprintf(buf, sizeof(buf), "%d/%d", t->tm_mday, t->tm_mon + 1); dyn = date_year_progress_gradient(t); break;
    case 28: snprintf(buf, sizeof(buf), "%d/%d", t->tm_mon + 1, t->tm_mday); dyn = date_year_progress_gradient(t); break;
    case 29: snprintf(buf, sizeof(buf), "%d/%d/%d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900); dyn = date_year_progress_gradient(t); break;
    case 30: snprintf(buf, sizeof(buf), "%d/%d/%02d", t->tm_mon + 1, t->tm_mday, (t->tm_year + 1900) % 100); dyn = date_year_progress_gradient(t); break;
    case 63: { // full time with seconds -- day/night gradient, same as digital time
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S %p", t);
      dyn = daynight_gradient(now, data->sun_rise, data->sun_set);
      break;
    }
    case 64: snprintf(buf, sizeof(buf), "%02d", t->tm_hour); dyn = linear_white_black(t->tm_hour, 0, 23); break;
    case 65: snprintf(buf, sizeof(buf), "%d", t->tm_hour); dyn = linear_white_black(t->tm_hour, 0, 23); break;
    case 66: {
      int hour12 = t->tm_hour % 12; if (hour12 == 0) hour12 = 12;
      snprintf(buf, sizeof(buf), "%d", hour12); dyn = linear_white_black(hour12, 1, 12);
      break;
    }
    case 67: snprintf(buf, sizeof(buf), "%d", t->tm_min); dyn = linear_white_black(t->tm_min, 0, 59); break;
    case 68: snprintf(buf, sizeof(buf), "%02d", t->tm_min); dyn = linear_white_black(t->tm_min, 0, 59); break;
    case 69: snprintf(buf, sizeof(buf), "%d", t->tm_sec); dyn = linear_white_black(t->tm_sec, 0, 59); break;
    case 70: snprintf(buf, sizeof(buf), "%02d", t->tm_sec); dyn = linear_white_black(t->tm_sec, 0, 59); break;
    case 71: snprintf(buf, sizeof(buf), "%d", t->tm_sec / 10); dyn = linear_white_black(t->tm_sec / 10, 0, 5); break;
    case 72: snprintf(buf, sizeof(buf), "%d", t->tm_sec % 10); dyn = linear_white_black(t->tm_sec % 10, 0, 9); break;
    case 86: snprintf(buf, sizeof(buf), "%s", t->tm_hour < 12 ? "AM" : "PM"); dyn = (t->tm_hour < 12) ? GColorBlack : GColorWhite; break;
    case 95: { // weekday + day/month (multi-value), e.g. "MON 24/9"
      char day_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t); to_upper_str(day_buf);
      snprintf(buf, sizeof(buf), "%s %d/%d", day_buf, t->tm_mday, t->tm_mon + 1);
      dyn = date_year_progress_gradient(t);
      break;
    }
    case 96: { // weekday + month/day (multi-value), e.g. "MON 9/24"
      char day_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t); to_upper_str(day_buf);
      snprintf(buf, sizeof(buf), "%s %d/%d", day_buf, t->tm_mon + 1, t->tm_mday);
      dyn = date_year_progress_gradient(t);
      break;
    }
    case 103: { // "long date" + week number (multi-value), e.g. "Mon 23 Sep WK34"
      char day_buf[4], mon_buf[4], wk_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      strftime(wk_buf, sizeof(wk_buf), "%V", t);
      snprintf(buf, sizeof(buf), "%s %d %s WK%s", day_buf, t->tm_mday, mon_buf, wk_buf);
      dyn = date_year_progress_gradient(t);
      break;
    }
    default:
      slot->segment_count = 0;
      return;
  }
  slot->segment_count = 1;
  set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, dyn, main_color, accent_color));
}

// ---- timezone cluster ---------------------------------------------------

static void __attribute__((noinline)) compute_timezone_value(FeatureSlot *slot, uint8_t content, uint8_t color_mode,
                                    GColor main_color, GColor accent_color, time_t now) {
  const TimezoneInfo *tz = &TIMEZONES[content - 44];
  int16_t offset_min = timezone_current_offset_min(tz, now);
  time_t local_time = now + (int32_t)offset_min * 60;
  int32_t local_secs_of_day = ((local_time % 86400) + 86400) % 86400;
  int local_hour24 = (int)(local_secs_of_day / 3600);
  int local_min = (int)((local_secs_of_day % 3600) / 60);
  char buf[16];
  if (clock_is_24h_style()) {
    snprintf(buf, sizeof(buf), "%s %02d:%02d", tz->abbr, local_hour24, local_min);
  } else {
    int hour12 = local_hour24 % 12; if (hour12 == 0) hour12 = 12;
    snprintf(buf, sizeof(buf), "%s %d:%02d%s", tz->abbr, hour12, local_min, local_hour24 < 12 ? "AM" : "PM");
  }
  slot->segment_count = 1;
  set_text_seg(slot, 0, buf, resolve_flat_color(color_mode, timezone_daylight_color(local_hour24), main_color, accent_color));
}

// ---- sky/astronomy cluster: moon phase, location, sunrise/sunset,
// planets, meteor shower, Saturn rings, ISS, aurora, compass ---------

static void __attribute__((noinline)) compute_sky_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                               uint8_t color_mode, GColor main_color, GColor accent_color, time_t now) {
  char buf[24];

  switch (content) {
    case 11: { // Moon phase -- icon + short name, no natural "value" to grade -- always white
      snprintf(buf, sizeof(buf), "%s", moon_phase_short_name(data->moon_phase_pct, data->moon_waxing));
      GColor c = resolve_flat_color(color_mode, GColorWhite, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 4, c);
      slot->segments[0].icon_extra = data->moon_phase_pct;
      slot->segments[0].icon_flag = data->moon_waxing;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 13: { // location name
      snprintf(buf, sizeof(buf), "%s", data->location_name[0] != '\0' ? data->location_name : "Unknown");
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot_set(slot, 8, buf, c);
      return;
    }
    case 16: { // sunrise/sunset -- same event/icon as the digital/analog info panel's row
      bool is_sunrise = false;
      time_t sun_event_time = 0;
      if (get_next_sun_event(now, data->sun_rise, data->sun_set, data->sun_rise_tomorrow, &sun_event_time, &is_sunrise)) {
        struct tm *event_t = localtime(&sun_event_time);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", event_t);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
      }
      GColor c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 11, c);
      slot->segments[0].icon_flag = is_sunrise;
      set_text_seg(slot, 1, buf, c);
      return;
    }
    case 79: { // how many of the 5 tracked planets are above the horizon right now
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else {
        uint8_t count = background_count_visible_planets(data, now);
        snprintf(buf, sizeof(buf), "%d planet%s", count, count == 1 ? "" : "s");
        c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      }
      slot_set(slot, 23, buf, c);
      return;
    }
    case 80: { // active meteor shower name, if any -- grayscale by intensity (more meteors = whiter)
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else if (data->meteor_intensity > 0 && data->meteor_shower_name[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->meteor_shower_name);
        c = resolve_flat_color(color_mode, meteor_intensity_gradient(data->meteor_intensity), main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 81: { // Saturn's current ring-opening angle
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else {
        snprintf(buf, sizeof(buf), "Rings %d%%", data->saturn_ring_open_pct);
        c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      }
      slot_set(slot, 24, buf, c);
      return;
    }
    case 82: { // which of the 5 tracked planets rises next today, and when
      GColor c;
      if (data->error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->error_code);
        c = GColorRed;
      } else {
        static const char *PLANET_ABBR[PLANET_COUNT] = { "MER", "VEN", "MAR", "JUP", "SAT" };
        int best = -1;
        time_t best_t = 0;
        for (int p = 0; p < PLANET_COUNT; p++) {
          time_t r = data->planet_rise[p];
          if (r > now && (best == -1 || r < best_t)) { best = p; best_t = r; }
        }
        if (best >= 0) {
          struct tm *bt = localtime(&best_t);
          char time_buf[8];
          strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", bt);
          snprintf(buf, sizeof(buf), "%s %s", PLANET_ABBR[best], time_buf);
          c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
        } else {
          snprintf(buf, sizeof(buf), "N/A");
          c = GColorLightGray;
        }
      }
      slot->segment_count = 1;
      set_text_seg(slot, 0, buf, c);
      return;
    }
    case 83: { // start time of the next visible ISS pass
      GColor c;
      if (data->iss_error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->iss_error_code);
        c = GColorRed;
      } else if (data->iss_next_pass > 0) {
        struct tm *it = localtime(&data->iss_next_pass);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", it);
        c = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        c = GColorLightGray;
      }
      slot_set(slot, 25, buf, c);
      return;
    }
    case 84: { // current planetary Kp index
      GColor c;
      if (data->aurora_error_code != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", data->aurora_error_code);
        c = GColorRed;
      } else {
        snprintf(buf, sizeof(buf), "Kp %d.%d", data->aurora_kp_x10 / 10, data->aurora_kp_x10 % 10);
        c = resolve_flat_color(color_mode, white_to_red_gradient(data->aurora_kp_x10), main_color, accent_color);
      }
      slot_set(slot, 26, buf, c);
      return;
    }
    case 85: { // Compass -- active (real heading) for 15s after a shake, then asleep until the next one.
               // Needs 2 colors at once (north arrow vs the other 3) rather than one flat color --
               // "mono"/"accent"/Pill still mean one shared color for the whole icon; only "color"
               // mode splits into accent (north) + main (other 3).
      bool asleep = compass_feature_is_asleep();
      if (asleep) {
        snprintf(buf, sizeof(buf), "---");
      } else {
        static const char *COMPASS_DIRS[16] = {
          "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
          "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
        };
        int32_t heading = compass_feature_heading_deg();
        int idx = (int)(((heading * 2 + 22) / 45) % 16);
        if (idx < 0) idx += 16;
        snprintf(buf, sizeof(buf), "%s", COMPASS_DIRS[idx]);
      }
      GColor flat = resolve_flat_color(color_mode, main_color, main_color, accent_color);
      slot->segment_count = 2;
      set_icon_seg(slot, 0, 27, flat);
      slot->segments[0].icon_extra = (int16_t)(compass_feature_heading_deg() % 360);
      slot->segments[0].icon_flag = asleep;
      if (color_mode == 3) {
        slot->segments[0].color = accent_color;  // north arrow
        slot->segments[0].color2 = main_color;   // other 3 arrows
      }
      set_text_seg(slot, 1, buf, flat);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- combo cluster: multi-icon/multi-value content (97-102) -----------
//
// Per request, these share ONE flat color (always main_color, not
// accent -- there's no single sensible "accent" reading across a
// multi-icon combo) for mono/accent/Pill modes, and only split into
// independently-gradient-colored segments under "color" mode (3).

static void __attribute__((noinline)) compute_combo_value(FeatureSlot *slot, uint8_t content, const EclipseData *data,
                                 uint8_t color_mode, GColor main_color, time_t now) {
  bool dynamic = (color_mode == 3);
  GColor flat = main_color;
  char buf1[16], buf2[16];

  switch (content) {
    case 97: { // heart rate + steps
      int bpm = peek_current_bpm();
      GColor hr_c = dynamic ? (bpm > 0 ? heart_rate_gradient(bpm) : GColorLightGray) : flat;
      snprintf(buf1, sizeof(buf1), bpm > 0 ? "%d" : "N/A", bpm);

      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 100) pct = 100;
      GColor step_c = dynamic ? red_green_gradient((uint8_t)pct) : flat;
      snprintf(buf2, sizeof(buf2), "%d", (int)steps);

      slot->segment_count = 4;
      set_icon_seg(slot, 0, 1, hr_c);
      set_text_seg(slot, 1, buf1, hr_c);
      set_icon_seg(slot, 2, 2, step_c);
      set_text_seg(slot, 3, buf2, step_c);
      return;
    }
    case 98: { // bed time + wake time, day/night graded independently
      SleepSpan span = get_sleep_span();
      GColor bed_c = flat, wake_c = flat;
      if (span.found) {
        struct tm *bt = localtime(&span.earliest_start);
        strftime(buf1, sizeof(buf1), clock_is_24h_style() ? "%H:%M" : "%I:%M", bt);
        struct tm *wt = localtime(&span.latest_end);
        char wake_time[8];
        strftime(wake_time, sizeof(wake_time), clock_is_24h_style() ? "%H:%M" : "%I:%M", wt);
        snprintf(buf2, sizeof(buf2), "/%s", wake_time);
        if (dynamic) {
          bed_c = daynight_gradient(span.earliest_start, data->sun_rise, data->sun_set);
          wake_c = daynight_gradient(span.latest_end, data->sun_rise, data->sun_set);
        }
      } else {
        snprintf(buf1, sizeof(buf1), "N/A");
        buf2[0] = '\0';
        if (dynamic) bed_c = wake_c = GColorLightGray;
      }
      slot->segment_count = 3;
      set_icon_seg(slot, 0, 21, flat);
      set_text_seg(slot, 1, buf1, bed_c);
      set_text_seg(slot, 2, buf2, wake_c);
      return;
    }
    case 99: case 100: { // battery + BT (icons only, 99), battery % + BT (100)
      BatteryChargeState bs = battery_state_service_peek();
      GColor batt_c = dynamic ? (bs.is_charging ? GColorGreen : red_green_gradient((uint8_t)bs.charge_percent)) : flat;
      bool connected = connection_service_peek_pebble_app_connection();
      GColor bt_c = dynamic ? (connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0)) : flat;

      set_icon_seg(slot, 0, 3, batt_c);
      slot->segments[0].icon_extra = bs.charge_percent;
      slot->segments[0].icon_flag = bs.is_charging;

      if (content == 99) {
        slot->segment_count = 2;
        set_icon_seg(slot, 1, 13, bt_c);
      } else {
        snprintf(buf1, sizeof(buf1), "%d%%", bs.charge_percent);
        slot->segment_count = 3;
        set_text_seg(slot, 1, buf1, batt_c);
        set_icon_seg(slot, 2, 13, bt_c);
      }
      return;
    }
    case 101: { // sleep times: sleep icon, total duration, (restful duration), quality%
      if (health_metric_available(HealthMetricSleepSeconds)) {
        HealthValue total = health_service_sum_today(HealthMetricSleepSeconds);
        HealthValue restful = health_service_sum_today(HealthMetricSleepRestfulSeconds);
        char total_buf[12], restful_buf[12], quality_buf[6];
        format_duration_hm(total_buf, sizeof(total_buf), (int32_t)total);
        format_duration_hm(restful_buf, sizeof(restful_buf), (int32_t)restful);
        int pct = (total > 0) ? (int)((restful * 100) / total) : 0;
        if (pct > 100) pct = 100;
        snprintf(quality_buf, sizeof(quality_buf), "%d%%", pct);
        char restful_paren[14];
        snprintf(restful_paren, sizeof(restful_paren), "(%s)", restful_buf);

        GColor total_c = dynamic ? seven_stop_gradient_reversed((int32_t)total, 0, 9 * 3600) : flat;
        GColor restful_c = dynamic ? seven_stop_gradient_reversed((int32_t)restful, 0, 3 * 3600) : flat;
        GColor quality_c = dynamic ? red_green_gradient((uint8_t)pct) : flat;

        slot->segment_count = 4;
        set_icon_seg(slot, 0, 21, flat);
        set_text_seg(slot, 1, total_buf, total_c);
        set_text_seg(slot, 2, restful_paren, restful_c);
        set_text_seg(slot, 3, quality_buf, quality_c);
      } else {
        slot->segment_count = 2;
        set_icon_seg(slot, 0, 21, flat);
        set_text_seg(slot, 1, "N/A", dynamic ? GColorLightGray : flat);
      }
      return;
    }
    case 102: { // "long date" + sunset/sunrise, e.g. "Mon 23 Sep <icon> 19:45"
      struct tm *t = localtime(&now);
      char day_buf[4], mon_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      snprintf(buf1, sizeof(buf1), "%s %d %s", day_buf, t->tm_mday, mon_buf);
      GColor date_c = dynamic ? date_year_progress_gradient(t) : flat;

      bool is_sunrise = false;
      time_t sun_event_time = 0;
      char time_buf[8];
      if (get_next_sun_event(now, data->sun_rise, data->sun_set, data->sun_rise_tomorrow, &sun_event_time, &is_sunrise)) {
        struct tm *et = localtime(&sun_event_time);
        strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", et);
      } else {
        snprintf(time_buf, sizeof(time_buf), "N/A");
      }

      slot->segment_count = 3;
      set_text_seg(slot, 0, buf1, date_c);
      set_icon_seg(slot, 1, 11, flat);
      slot->segments[1].icon_flag = is_sunrise;
      set_text_seg(slot, 2, time_buf, flat);
      return;
    }
    default:
      slot->segment_count = 0;
      return;
  }
}

// ---- unified position resolution ---------------------------------------
//
// The one and only place any slot's content gets positioned: measures
// each already-filled-in segment (icon widths are the same fixed
// per-icon-kind lookup every icon always used, text is measured
// against the corner font) and resolves every segment's x_offset/width
// relative to the slot's own box_x, according to the slot's left/
// center/right alignment. Runs once per recompute, never at draw time --
// features_draw_slot() just reads x_offset/width straight off each
// segment.
static void resolve_segment_offsets(FeatureSlot *slot, GFont font, int16_t font_h) {
  int16_t box_w = slot->custom_box ? slot->box_w : CORNER_BOX_W;
  int16_t advance[MAX_RENDER_SEGMENTS]; // width INCLUDING this segment's own trailing gap
  int16_t total_w = 0;
  for (int i = 0; i < slot->segment_count; i++) {
    RenderSegment *seg = &slot->segments[i];
    if (seg->is_icon) {
      seg->width = ICON_WIDTH;
      advance[i] = icon_plus_gap_width(seg->icon_kind);
    } else {
      GSize sz = graphics_text_layout_get_content_size(seg->text, font, GRect(0, 0, 200, font_h + 10),
                                                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
      seg->width = sz.w + 2;
      advance[i] = seg->width + 5; // 5px gap before whatever segment follows (icon or more text) -- matches icon_plus_gap_width()'s own gap
    }
    total_w += advance[i];
  }
  if (total_w > box_w) total_w = box_w;

  // A custom-box slot (currently just the digital-mode bottom feature)
  // is always centered within its own box_w -- it has no left/right
  // edge of its own to hug, unlike every normal corner/edge slot.
  int16_t start_x = slot->custom_box ? (box_w - total_w) / 2
                    : (slot->center_horizontal ? (box_w - total_w) / 2
                       : (!slot->is_left ? box_w - total_w : 0));
  int16_t x = start_x;
  for (int i = 0; i < slot->segment_count; i++) {
    slot->segments[i].x_offset = x;
    x += advance[i];
  }
}

// ---- value recompute dispatcher ----------------------------------------
//
// Routes a slot's already-settings-resolved content/color_mode to the
// right cluster function above, applies the one shared override every
// weather-derived content needs (a service error blanks everything to
// a flat red "ERR ###", overriding whatever the cluster function just
// computed), then resolves positions. This is the only place that
// looks at `content` as a giant range of numbers -- every cluster
// function above only ever sees the handful of ids it actually owns.
static void features_recompute_slot_value(FeatureSlot *slot, const EclipseData *data,
                                           GColor main_color, GColor accent_color, GColor bg_color,
                                           GFont font, int16_t font_h, time_t now, struct tm *t) {
  slot->draw_pill = (slot->color_mode == 2);
  slot->pill_bg = bg_color;

  if (slot->content == 0) {
    slot->segment_count = 0;
    return;
  }

  uint8_t content = slot->content, color_mode = slot->color_mode;
  switch (content) {
    case 97: case 98: case 99: case 100: case 101: case 102:
      compute_combo_value(slot, content, data, color_mode, main_color, now);
      break;
    case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53:
    case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62:
      compute_timezone_value(slot, content, color_mode, main_color, accent_color, now);
      break;
    case 1: case 2: case 3: case 10: case 17: case 20: case 39: case 40: case 41: case 42: case 43: case 78:
      compute_health_value(slot, content, data, color_mode, main_color, accent_color);
      break;
    case 11: case 13: case 16: case 79: case 80: case 81: case 82: case 83: case 84: case 85:
      compute_sky_value(slot, content, data, color_mode, main_color, accent_color, now);
      break;
    case 4: case 5: case 6: case 7: case 8: case 9: case 14: case 15: case 31: case 32: case 34:
    case 35: case 36: case 37: case 38: case 73: case 74: case 75: case 76: case 77: case 87: case 88:
    case 89: case 90: case 91: case 92: case 93: case 94:
      compute_weather_value(slot, content, data, color_mode, main_color, accent_color, bg_color);
      break;
    default: // every date/time format variant (12, 18-19, 21-30, 63-72, 86, 95-96, 103)
      compute_date_value(slot, content, data, color_mode, main_color, accent_color, now, t);
      break;
  }

  // A weather fetch error blanks the whole slot to a flat red "ERR ###" --
  // uniformly, regardless of which weather cluster case built it, and
  // regardless of color_mode (an error needs to stay legible, not blend
  // in as a normal reading would).
  if (content_is_weather_derived(content) && weather_should_show_error(data)) {
    char err_buf[10];
    snprintf(err_buf, sizeof(err_buf), "ERR %d", data->weather_error_code);
    slot->segment_count = 1;
    set_text_seg(slot, 0, err_buf, GColorRed);
    slot->draw_pill = false;
  }

  if (slot->segment_count > 0) resolve_segment_offsets(slot, font, font_h);
}

// ---- drawing: icon dispatch --------------------------------------------
//
// Every simple palette icon (heart/foot/umbrella/droplet/wind/GPS/eye/
// cloud/bluetooth/the 5 bed icons/the 3 astronomy icons/aurora) shares
// one draw call via this table -- exactly the resource + per-icon
// horizontal nudge every one of them always used, just looked up
// instead of hand-copied per case. `icon_x` below is always the pixel
// position resolve_segment_offsets() already worked out (box_x +
// segment x_offset) -- nothing here recomputes a position, only where
// *within* that already-resolved x an individual bitmap's own ink
// should sit, which is a fixed constant per icon, not a calculation.
static const struct { uint8_t kind; uint32_t resource_id; int16_t x_nudge; } SIMPLE_ICONS[] = {
  { 1,  RESOURCE_ID_ICON_HEART,           12 },
  { 2,  RESOURCE_ID_ICON_FOOT,             6 },
  { 5,  RESOURCE_ID_ICON_UMBRELLA,        10 },
  { 6,  RESOURCE_ID_ICON_DROPLET,         10 },
  { 7,  RESOURCE_ID_ICON_WIND,            10 },
  { 8,  RESOURCE_ID_ICON_GPS_PIN,          6 },
  { 9,  RESOURCE_ID_ICON_EYE,              6 },
  { 10, RESOURCE_ID_ICON_CLOUD,            6 },
  { 18, RESOURCE_ID_ICON_BED_ARROW_IN,     6 },
  { 19, RESOURCE_ID_ICON_BED_ARROW_OUT,    6 },
  { 20, RESOURCE_ID_ICON_BED_CHECK,        6 },
  { 21, RESOURCE_ID_ICON_BED_CLOCK,        6 },
  { 22, RESOURCE_ID_ICON_BED_CHECK_CLOCK,  6 },
  { 23, RESOURCE_ID_ICON_PLANETS,          6 },
  { 24, RESOURCE_ID_ICON_SATURN_RING,      6 },
  { 25, RESOURCE_ID_ICON_ISS,              6 },
  { 26, RESOURCE_ID_ICON_AURORA,           6 },
};

static void draw_debug_marker_point(GContext *ctx, bool draw_debug, GPoint pos, GColor color) {
  if (draw_debug) {
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, color);
    graphics_draw_rect(ctx, GRect(pos.x, pos.y-15, 1, 30));
    graphics_draw_rect(ctx, GRect(pos.x-15, pos.y, 30, 1));
  }
}

static void draw_render_icon(GContext *ctx, const RenderSegment *seg, int16_t icon_x, int16_t box_y, uint8_t outline_style, uint8_t weather_icon_style, GColor bg_color, bool draw_debug) {
  GColor color = seg->color;
  GColor outline_color = contrasting_outline_color(color);
  bool do_outline = outline_style != 0;
  const GPoint *offs = NULL; int offs_n = 0;
  if (do_outline) get_outline_offsets(outline_style, &offs, &offs_n);
  draw_debug_marker_point(ctx, draw_debug, GPoint(icon_x, box_y), GColorRed);
  for (size_t i = 0; i < sizeof(SIMPLE_ICONS) / sizeof(SIMPLE_ICONS[0]); i++) {
    if (SIMPLE_ICONS[i].kind != seg->icon_kind) continue;
    GPoint pos = GPoint(icon_x - ICON_WIDTH + SIMPLE_ICONS[i].x_nudge, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
    draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
    draw_icon_resource_with_outline(ctx, pos, SIMPLE_ICONS[i].resource_id, outline_style, outline_color, color);
    return;
  }

  switch (seg->icon_kind) {
    case 3: { // battery
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 14) / 2);
      GColor c = seg->icon_flag ? GColorGreen : color; // charging -> always green, matching the old special case
      GColor oc = seg->icon_flag ? GColorBlack : outline_color;
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_corner_battery_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), oc, 0);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_corner_battery_icon(ctx, pos, c, seg->icon_extra);
      return;
    }
    case 4: { // moon phase
      int16_t moon_r = 9;
      GPoint center = GPoint(icon_x + moon_r, box_y + CORNER_ROW_H / 2);
      GRect clip = GRect(icon_x, box_y, moon_r * 2 + 2, CORNER_ROW_H);
      if (do_outline) {
        graphics_context_set_fill_color(ctx, outline_color);
        for (int i = 0; i < offs_n; i++) {
          graphics_fill_circle(ctx, GPoint(center.x + offs[i].x, center.y + offs[i].y), moon_r);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, center, GColorMagenta);
      draw_moon_phase(ctx, clip, center, moon_r, (uint8_t)seg->icon_extra, seg->icon_flag, color);
      return;
    }
    case 11: { // sunrise/sunset glyph
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 9) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_sun_time_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), seg->icon_flag, outline_color, bg_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_sun_time_icon(ctx, pos, seg->icon_flag, color, bg_color);
      return;
    }
    case 12: { // Pebble battery logo
      GPoint pos = GPoint(icon_x - 15, box_y + (CORNER_ROW_H - 10) / 2);
      GColor c = seg->icon_flag ? GColorGreen : color;
      GColor oc = seg->icon_flag ? GColorBlack : outline_color;
      GPoint p1 = GPoint(pos.x + 3, pos.y + 9);
      GPoint p2 = GPoint(pos.x + 3 + (35 * seg->icon_extra / 100), pos.y + 9);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), PEBBLE_ICON, 10, 40, oc);
        }
        graphics_context_set_stroke_color(ctx, oc);
        graphics_draw_line(ctx, GPoint(p1.x, p1.y + 1), GPoint(p2.x, p2.y + 1));
        graphics_draw_line(ctx, GPoint(p1.x - 1, p1.y), GPoint(p2.x + 1, p2.y));
        graphics_draw_line(ctx, GPoint(p1.x, p1.y - 1), GPoint(p2.x, p2.y - 1));
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_tiny_icon(ctx, pos, PEBBLE_ICON, 10, 40, c);
      graphics_context_set_stroke_color(ctx, c);
      graphics_draw_line(ctx, p1, p2);
      return;
    }
    case 14: { // weather condition icon -- style picked in settings (simple/hollow/full color)
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - ICON_ROWS) / 2 - 2);
      uint8_t category = (uint8_t)seg->icon_extra;
      // Full color (style 2) draws its outline pass using style 1
      // (hollow)'s own silhouette instead of its real style --
      // draw_weather_icon_filled() ignores whatever color it's given
      // (it's a true-color+alpha image, no tint to apply), so shifting
      // IT around would just stack identical copies of the same
      // multi-color icon instead of a contrasting silhouette behind
      // it. Hollow's outline shape in outline_color gives it a real
      // outline without needing a second baked asset.
      if (do_outline) {
        uint8_t outline_icon_style = (weather_icon_style == 2) ? 1 : weather_icon_style;
        for (int i = 0; i < offs_n; i++) {
          draw_weather_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), category, outline_icon_style, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_weather_icon(ctx, pos, category, weather_icon_style, color);
      return;
    }
    case 15: { // pressure trend chevron
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_pressure_trend_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), (uint8_t)seg->icon_extra, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_pressure_trend_icon(ctx, pos, (uint8_t)seg->icon_extra, color);
      return;
    }
    case 16: { // wind direction arrow
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_wind_direction_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), seg->icon_extra, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_wind_direction_icon(ctx, pos, seg->icon_extra, color);
      return;
    }
    case 17: { // altitude mountain glyph
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          draw_mountain_icon(ctx, GPoint(pos.x + offs[i].x, pos.y + offs[i].y), outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      draw_mountain_icon(ctx, pos, color);
      return;
    }
    case 13: { // bluetooth -- own case rather than the generic SIMPLE_ICONS
               // bucket above: that bucket draws each bitmap right-anchored
               // within its own 16px box (icon_x - ICON_WIDTH + x_nudge),
               // which is fine for a bitmap that's always the FIRST/only
               // segment in its slot, but bluetooth routinely follows
               // another segment (battery icon, a "NN%" text) -- and a
               // right-anchored draw there lands up to (ICON_WIDTH -
               // x_nudge) px to the LEFT of icon_x, i.e. back on top of
               // whatever precedes it, cancelling out that segment's own
               // trailing gap entirely. Left-anchored at icon_x instead,
               // matching every other multi-segment-capable icon kind
               // (battery, moon, compass, ...) below.
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      draw_icon_resource_with_outline(ctx, pos, RESOURCE_ID_ICON_BLUETOOTH, outline_style, outline_color, color);
      return;
    }
    case 27: { // compass -- asleep (Zz glyph) or a live heading rose with a distinct north arrow
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_outline) {
        for (int i = 0; i < offs_n; i++) {
          GPoint shifted = GPoint(pos.x + offs[i].x, pos.y + offs[i].y);
          if (seg->icon_flag) draw_compass_sleep_icon(ctx, shifted, outline_color);
          else draw_compass_icon(ctx, shifted, seg->icon_extra, outline_color, outline_color);
        }
      }
      draw_debug_marker_point(ctx, draw_debug, pos, GColorMagenta);
      if (seg->icon_flag) draw_compass_sleep_icon(ctx, pos, color);
      else draw_compass_icon(ctx, pos, seg->icon_extra, seg->color, seg->color2);
      return;
    }
    default:
      return;
  }
}

// ---- drawing: the trivial per-slot blit --------------------------------
//
// Everything this needs -- position, color, text, which icon -- was
// already resolved by features_recompute_slot_value() above. This
// function does no formatting, no gradient math, and no alignment or
// width measurement: box_x/box_y is the one bit of arithmetic left
// (added back at draw time, not cached, purely so a system
// notification's screen obstruction can shrink `bounds` without
// needing its own recompute pass -- see the module-level comment up
// top), and every segment inside the slot just gets blitted at its
// already-resolved x_offset.
static void features_draw_slot(GContext *ctx, GRect bounds, const FeatureSlot *slot,
                                GFont font, int16_t font_h, int16_t font_offset,
                                uint8_t outline_style, uint8_t weather_icon_style, GColor bg_color, bool draw_debug) {
  if (!slot->active || slot->segment_count == 0) return;

  int16_t box_w = slot->custom_box ? slot->box_w : CORNER_BOX_W;
  int16_t box_x = slot->custom_box ? bounds.origin.x + slot->box_x
    : (slot->center_horizontal
       ? bounds.origin.x + (bounds.size.w - CORNER_BOX_W) / 2
       : (slot->is_left ? bounds.origin.x + CORNER_INSET_PX : bounds.origin.x + bounds.size.w - CORNER_INSET_PX - CORNER_BOX_W));
  int16_t box_y = slot->center_vertical
    ? bounds.origin.y + (bounds.size.h - CORNER_ROW_H) / 2 + slot->top_offset
    : (slot->is_top ? bounds.origin.y + slot->top_offset
                     : bounds.origin.y + bounds.size.h - CORNER_ROW_H - CORNER_INSET_PX - slot->bottom_shift);
  if (slot->is_middle) {
    if (slot->is_left) box_x += slot->middle_inset; else box_x -= slot->middle_inset;
  }

  if (slot->draw_pill) {
    graphics_context_set_fill_color(ctx, slot->pill_bg);
    graphics_fill_rect(ctx, GRect(box_x, box_y, box_w, CORNER_ROW_H), CORNER_ROW_H / 2, GCornersAll);
  }

  uint8_t effective_outline_style = slot->allow_outline ? outline_style : 0;
  for (int i = 0; i < slot->segment_count; i++) {
    const RenderSegment *seg = &slot->segments[i];
    int16_t seg_x = box_x + seg->x_offset;
    if (seg->is_icon) {
      draw_render_icon(ctx, seg, seg_x, box_y, effective_outline_style, weather_icon_style, bg_color, draw_debug);
    } else {
      draw_text_outlined(ctx, seg->text, font,
                          GRect(seg_x, box_y + (CORNER_ROW_H - font_h) / 2 + font_offset, seg->width + 2, font_h + 2),
                          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, seg->color, effective_outline_style);
      
      if (draw_debug){
        graphics_context_set_stroke_width(ctx, 1);
        graphics_context_set_stroke_color(ctx, GColorGreen);
        graphics_draw_rect(ctx, GRect(seg_x, box_y + (CORNER_ROW_H - font_h) / 2 + font_offset, seg->width + 2, font_h + 2));
      }
    }
  }
}

// ---- layout resolution (settings-driven) -------------------------
//
// Which of the 12 slots are even active, and where each one's box
// sits, depends only on settings (bottom_style, big_analog_marker_style,
// the corner/edge content+color-mode fields) -- never on the current
// time, any live reading, or the shake-to-reveal state. Resolved here,
// then features_recompute_slot_value() is called once per newly-active
// slot to fill in its actual content -- see features_layer_set_data()
// below for when this whole function runs (a settings change) versus
// features_layer_refresh_values()/refresh_second_slots() (which only
// re-run the VALUE half, for slots whose layout hasn't changed at all).
// Which horizontal band of the digital clock panel's full width the
// clock text (and the single "bottom" feature under it, which always
// shifts in lockstep with it) actually occupies, given which side
// feature columns are active. One side active -> the clock shifts
// away from it, into the freed-up space; both or neither active ->
// centered across the full width, exactly as if no sides existed --
// per the request, a font enabled for both-sides use is expected to
// already be narrow enough to coexist with a centered clock without
// needing to shrink further. Shared between pebble-eclipse-watch.c
// (positions the clock text itself) and this file's own
// features_recompute_layout() (positions the bottom feature to match)
// so the two can never drift out of sync with each other.
void digital_clock_area(uint8_t bottom_style, int16_t screen_w, int16_t *out_x, int16_t *out_w) {
  if (bottom_style == 2) { // right side only -- shift left
    *out_x = 0;
    *out_w = screen_w - CORNER_BOX_W;
  } else if (bottom_style == 3) { // left side only -- shift right
    *out_x = CORNER_BOX_W;
    *out_w = screen_w - CORNER_BOX_W;
  } else { // 0 (no sides) or 4 (both sides) -- centered, full width
    *out_x = 0;
    *out_w = screen_w;
  }
}

static void features_recompute_layout(FeaturesState *state) {
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) state->slots[i].active = false;

  EclipseData *d = state->data;
  if (!d) return;

  bool is_analog = d->bottom_style == 1;
  uint8_t marker_style = d->big_analog_marker_style;
  bool is_bitmap_style = is_analog && marker_style >= 3 && marker_style != 8 && marker_style != 9;

  // Which of the 12 slots actually apply for the current marker/
  // bottom_style is decided entirely on the phone (see
  // computeSlotAvailability()/CORNER_CATEGORIES in config-page.js and
  // the availability check in index.js's dict-building step) -- by the
  // time a content field reaches here, it's already 0 ("None") for any
  // slot that shouldn't show for the current style, so this file just
  // builds every slot from whatever content it was given, unconditionally,
  // and trusts a content of 0 to mean "draws nothing" (already true --
  // see features_recompute_slot_value()'s own content==0 early return)
  // rather than keeping its own separate copy of "which styles support
  // which slots" to decide that upfront.

  // Inner-empty-area margins: procedural presets (0/1/2) and "none" (9)
  // are calculated from that style's own marker-ring geometry via
  // background_marker_inner_reach() (same point_on_ring() technique
  // custom (8) uses below, just fed a fixed preset instead of a live
  // user config); bitmap styles (3-7) have no ring geometry at all, so
  // they use a fixed per-style table instead, each side independent.
  typedef struct { int16_t top, bottom, left, right; } EdgeMargins;
  static const EdgeMargins BITMAP_STYLE_MARGINS[5] = {
    { 34, 30, 35, 30 }, // 3: Modern
    { 34, 30, 35, 30 }, // 4: Shadow
    { 44, 40, 25, 15 }, // 5: Tally
    { 44, 40, 40, 40 }, // 6: Bell
    { 44, 40, 30, 20 }, // 7: Fancy
  };
  int16_t dyn_upper_offset = 44, dyn_bottom_shift = 40, dyn_left_inset = 30, dyn_right_inset = 30;
  if (is_bitmap_style && marker_style >= 3 && marker_style <= 7) {
    const EdgeMargins *m = &BITMAP_STYLE_MARGINS[marker_style - 3];
    dyn_upper_offset = m->top; dyn_bottom_shift = m->bottom; dyn_left_inset = m->left; dyn_right_inset = m->right;
  } else if (marker_style == 8) {
    GRect screen = GRect(0, 0, 200, 228);
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    uint8_t pct = d->custom_hour_marker.inner_border_pct;
    uint8_t ecc = d->custom_hour_marker.inner_eccentricity;
    GPoint top_pt = point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    int16_t margin = 4;
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_reach = left_pt.x + margin, right_reach = screen.size.w - right_pt.x + margin;
    if (left_reach > dyn_left_inset) dyn_left_inset = left_reach;
    if (right_reach > dyn_right_inset) dyn_right_inset = right_reach;
  } else if (marker_style <= 2 || marker_style == 9) {
    uint8_t pct, ecc;
    background_marker_inner_reach(marker_style, &pct, &ecc);
    GRect screen = GRect(0, 0, 200, 228);
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    GPoint top_pt = point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    int16_t margin = 4;
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_reach = left_pt.x + margin, right_reach = screen.size.w - right_pt.x + margin;
    if (left_reach > dyn_left_inset) dyn_left_inset = left_reach;
    if (right_reach > dyn_right_inset) dyn_right_inset = right_reach;
  }

  if (is_analog) {
    bool has_line2 = d->upper_middle_line2_content != 0;
    int16_t line1_offset = has_line2 ? dyn_upper_offset : dyn_upper_offset + CORNER_ROW_H / 2;
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = true, .is_left = true, .is_middle = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->upper_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_UPPER_L2] = (FeatureSlot){
        .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
        .is_top = true, .is_left = true, .is_middle = false,
        .top_offset = dyn_upper_offset + CORNER_ROW_H, .bottom_shift = 0, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = content_needs_second_refresh(d->upper_middle_line2_content),
      };
    }

    has_line2 = d->bottom_middle_line2_content != 0;
    int16_t line1_shift = has_line2 ? dyn_bottom_shift + CORNER_ROW_H : dyn_bottom_shift + CORNER_ROW_H / 2;
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false,
      .top_offset = 0, .bottom_shift = line1_shift, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->bottom_middle_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_BOTTOM_L2] = (FeatureSlot){
        .active = true, .content = d->bottom_middle_line2_content, .color_mode = d->bottom_middle_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = false,
        .top_offset = 0, .bottom_shift = dyn_bottom_shift, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
        .needs_second_refresh = content_needs_second_refresh(d->bottom_middle_line2_content),
      };
    }

    has_line2 = d->middle_left_line2_content != 0;
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = true,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_left_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->middle_left_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_LEFT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = true,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_left_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = content_needs_second_refresh(d->middle_left_line2_content),
      };
    }

    has_line2 = d->middle_right_line2_content != 0;
    line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = false, .is_left = false, .is_middle = true,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_right_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->middle_right_line1_content),
    };
    if (has_line2) {
      state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
        .is_top = false, .is_left = false, .is_middle = true,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_right_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
        .needs_second_refresh = content_needs_second_refresh(d->middle_right_line2_content),
      };
    }
  }

  // Digital-mode-only equivalent of the 4 blocks above -- the 8 edge
  // slot indices are unused by any analog "show_*" block when
  // !is_analog (none of those ran), so it's safe to repurpose 7 of
  // them here: SLOT_LEFT_L1/L2 + SLOT_UPPER_L1 as the 3-line left
  // column, SLOT_RIGHT_L1/L2 + SLOT_UPPER_L2 as the 3-line right
  // column, SLOT_BOTTOM_L1 as the single bottom feature (SLOT_BOTTOM_L2
  // stays unused). Deliberately reuses the SAME 8 EclipseData fields
  // analog mode's upper/bottom/left/right-middle content uses (see
  // their own dual-purpose comment in eclipse_data.h) rather than a
  // separate set of digital-only fields -- the two modes never run at
  // once, so there's nothing to actually preserve by keeping them
  // apart, and sharing saves both the extra bytes on the watch and the
  // extra AppMessage keys/traffic a second set would cost. Which side
  // columns actually apply for the current bottom_style (0/2/3/4) is
  // decided phone-side same as everything else here -- the content
  // fields already arrive zeroed for whichever side isn't turned on,
  // so both columns are just built unconditionally below.
  if (!is_analog) {
    int16_t clock_x, clock_w;
    digital_clock_area(d->bottom_style, 200, &clock_x, &clock_w);

    // 1 = top (nearest the clock), 3 = bottom (nearest the screen
    // edge) -- all bottom-anchored (not top-anchored off a fixed
    // panel offset) so a shrinking screen during a system
    // notification shifts the whole stack up together, same as the
    // corners already do, rather than the top row drifting away from
    // the panel it's meant to sit inside.
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false,
      .top_offset = 0, .bottom_shift = CORNER_ROW_H * 2, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->middle_left_line1_content),
    };
    state->slots[SLOT_LEFT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
      .is_top = false, .is_left = true, .is_middle = false,
      .top_offset = 0, .bottom_shift = CORNER_ROW_H, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->middle_left_line2_content),
    };
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){ // reused: digital left column, row 3 -- reads upper_middle_line1
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false,
      .top_offset = 0, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->upper_middle_line1_content),
    };

    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = false, .is_left = false, .is_middle = false,
      .top_offset = 0, .bottom_shift = CORNER_ROW_H * 2, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->middle_right_line1_content),
    };
    state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
      .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
      .is_top = false, .is_left = false, .is_middle = false,
      .top_offset = 0, .bottom_shift = CORNER_ROW_H, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->middle_right_line2_content),
    };
    state->slots[SLOT_UPPER_L2] = (FeatureSlot){ // reused: digital right column, row 3 -- reads upper_middle_line2
      .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
      .is_top = false, .is_left = false, .is_middle = false,
      .top_offset = 0, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .needs_second_refresh = content_needs_second_refresh(d->upper_middle_line2_content),
    };

    // Single bottom feature -- reuses bottom_middle_line1 (analog's
    // upper of its own 2-line pair; bottom_middle_line2 has no
    // digital-mode role, 7 slots needed against 8 available fields).
    // Shares clock_x/clock_w with the clock text itself
    // (bottom_canvas_update_proc() in pebble-eclipse-watch.c uses the
    // exact same digital_clock_area() call), always centered within
    // that band, anchored to the screen's own bottom edge.
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false,
      .top_offset = 0, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = false, .center_vertical = false, .allow_outline = true,
      .custom_box = true, .box_x = clock_x, .box_w = clock_w,
      .needs_second_refresh = content_needs_second_refresh(d->bottom_middle_line1_content),
    };
  }

  // Corners always just draw whatever d->corner_content[] says, for
  // every marker style including bitmap ones -- defaulting that to
  // "off" for bitmap styles (and offering an "enable corner features"
  // override) is the settings page's job, not this file's.
  state->slots[SLOT_CORNER_TL] = (FeatureSlot){
    .active = true, .content = d->corner_content[0], .color_mode = d->corner_color_mode[0],
    .is_top = true, .is_left = true, .is_middle = false,
    .top_offset = CORNER_INSET_PX, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = content_needs_second_refresh(d->corner_content[0]),
  };
  state->slots[SLOT_CORNER_TR] = (FeatureSlot){
    .active = true, .content = d->corner_content[1], .color_mode = d->corner_color_mode[1],
    .is_top = true, .is_left = false, .is_middle = false,
    .top_offset = CORNER_INSET_PX, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = content_needs_second_refresh(d->corner_content[1]),
  };
  // Bottom corners (BL/BR): stay anchored to the SKY's own bottom
  // edge, not the full screen's -- meaningfully different only in
  // digital mode, where the sky canvas only occupies the screen's top
  // portion and the digital clock's own bottom panel fills the rest.
  // features_layer's own frame spans the FULL screen in both modes
  // (see apply_layout()), so a plain bottom_shift of 0 here would put
  // these two corners down inside the digital panel instead of at the
  // sky's own bottom-left/-right -- adding DIGITAL_PANEL_H's worth of
  // shift pulls them back up to the sky boundary. Analog mode has no
  // separate panel (sky already fills the screen), so bottom_shift
  // stays 0 there, same as before.
  int16_t bottom_corner_shift = is_analog ? 0 : DIGITAL_PANEL_H;
  state->slots[SLOT_CORNER_BL] = (FeatureSlot){
    .active = true, .content = d->corner_content[2], .color_mode = d->corner_color_mode[2],
    .is_top = false, .is_left = true, .is_middle = false,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = content_needs_second_refresh(d->corner_content[2]),
  };
  state->slots[SLOT_CORNER_BR] = (FeatureSlot){
    .active = true, .content = d->corner_content[3], .color_mode = d->corner_color_mode[3],
    .is_top = false, .is_left = false, .is_middle = false,
    .top_offset = 0, .bottom_shift = bottom_corner_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
    .needs_second_refresh = content_needs_second_refresh(d->corner_content[3]),
  };
}

// ---- value refresh (recomputes VALUE only, layout stays as-is) --------

// Recomputes every active slot's value -- called after a full layout
// change (so newly-active slots get real content right away) and from
// the periodic ~1-minute refresh (so slower-changing readings --
// weather, health, battery, dates -- actually update).
static void features_recompute_all_values(FeaturesState *state) {
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  ensure_corner_custom_font(state->data->corner_font);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active) continue;
    features_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

// Recomputes ONLY the slot(s) whose content needs second-by-second
// updating (a seconds-showing time display, or nothing at all most of
// the time) -- if only one feature slot shows seconds, only that one
// slot gets touched, not all 12.
static void features_recompute_second_slots(FeaturesState *state) {
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active || !state->slots[i].needs_second_refresh) continue;
    features_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
}

// ---- layer lifecycle --------------------------------------------------

// The always-on-top feature overlay. Deliberately a separate layer with
// its own independent refresh timer (see main.c's corners_timer_callback)
// rather than being drawn as part of the sky canvas or tied to its redraw
// cycle. Never fills its own background, so whatever's underneath (sky
// canvas, and in big-analog mode possibly the hands too) shows through
// everywhere except where content is actually drawn.
//
// Does no computation of its own at all -- every slot's position,
// color, icon, and text was already resolved by whichever of
// features_layer_set_data()/refresh_values()/refresh_second_slots()
// last ran. This is purely a blit loop.
static void features_layer_update_proc(Layer *layer, GContext *ctx) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  if (!state->data) return;

  GRect bounds = layer_get_unobstructed_bounds(layer);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);
  int16_t font_offset = font_lookup_y_offset(state->data->corner_font);
  // Only needed here for the sunrise/sunset glyph's halo -- everything
  // else's color was already fully resolved back at recompute time.
  GColor bg, main_color, accent_color;
  get_active_color_scheme(state->data, time(NULL), &bg, &main_color, &accent_color);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    features_draw_slot(ctx, bounds, &state->slots[i], font, font_h, font_offset,
                        state->data->outline_style, state->data->weather_icon_style, bg, state->data->draw_debug);
  }
}

Layer *features_layer_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(FeaturesState));
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  state->data = NULL;
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) state->slots[i].active = false;
  layer_set_update_proc(layer, features_layer_update_proc);
  return layer;
}

void features_layer_destroy(Layer *layer) {
  layer_destroy(layer);
}

void features_layer_set_data(Layer *layer, EclipseData *data) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  state->data = data;
  features_recompute_layout(state);
  features_recompute_all_values(state);
  layer_mark_dirty(layer);
}

void features_layer_refresh_values(Layer *layer) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  features_recompute_all_values(state);
  layer_mark_dirty(layer);
}

void features_layer_refresh_second_slots(Layer *layer) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  features_recompute_second_slots(state);
  layer_mark_dirty(layer);
}

void features_layer_refresh_content(Layer *layer, uint8_t content) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  if (!state->data) return;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  GFont font = font_lookup_resolve(&s_corner_font_slot, state->data->corner_font);
  int16_t font_h = font_lookup_height(state->data->corner_font);
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    if (!state->slots[i].active || state->slots[i].content != content) continue;
    features_recompute_slot_value(&state->slots[i], state->data, main_color, accent_color, bg, font, font_h, now, t);
  }
  layer_mark_dirty(layer);
}
