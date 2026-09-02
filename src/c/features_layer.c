#include "features_layer.h"
#include "background_layer.h"
#include "font_lookup.h"
#include <string.h>

// See features_layer.h for the module-level design note (metadata cache
// vs. per-redraw layout recompute). This file also owns the shared
// draw_text_outlined()/contrasting_outline_color() outline primitives and
// the corner/edge custom-font plumbing (ensure_corner_custom_font() etc.)
// -- both used outside this module too (the countdown label and the big-
// analog hands/small-analog panel, respectively), which is why they're
// declared in features_layer.h rather than kept private.

#define CORNER_BOX_W 68

static bool point_in_convex_polygon(GPoint *pts, int n, GPoint p) {
  bool has_pos = false, has_neg = false;
  for (int i = 0; i < n; i++) {
    GPoint a = pts[i];
    GPoint b = pts[(i + 1) % n];
    int32_t cross = (int32_t)(b.x - a.x) * (p.y - a.y) - (int32_t)(b.y - a.y) * (p.x - a.x);
    if (cross > 0) has_pos = true;
    if (cross < 0) has_neg = true;
    if (has_pos && has_neg) return false;
  }
  return true;
}

// Fills a convex polygon with a genuine ~50% Bayer-dithered stipple
// of `color`, pixel by pixel -- half the pixels (in the same ordered
// pattern used for the sky/clouds elsewhere, not randomly) get the
// hand's color, the other half are left completely untouched. Since
// Pebble's basic fills have no alpha blending for arbitrary shapes,
// this is how "50% transparent" becomes a real per-pixel compositing
// effect rather than a hollow-outline approximation: whatever the
// sky canvas drew underneath shows through evenly across the whole
// hand, not just around its edges.
static void fill_polygon_dithered(GContext *ctx, GPoint *pts, int n, GColor color) {
  int16_t min_x = pts[0].x, max_x = pts[0].x, min_y = pts[0].y, max_y = pts[0].y;
  for (int i = 1; i < n; i++) {
    if (pts[i].x < min_x) min_x = pts[i].x;
    if (pts[i].x > max_x) max_x = pts[i].x;
    if (pts[i].y < min_y) min_y = pts[i].y;
    if (pts[i].y > max_y) max_y = pts[i].y;
  }
  graphics_context_set_fill_color(ctx, color);
  for (int16_t y = min_y; y <= max_y; y++) {
    for (int16_t x = min_x; x <= max_x; x++) {
      if (BAYER4[y & 3][x & 3] >= 8) continue; // ~50% threshold
      GPoint p = GPoint(x, y);
      if (!point_in_convex_polygon(pts, n, p)) continue;
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    }
  }
}

// Shared by every outline implementation in this file (text, icons,
// hands): draw once shifted in each cardinal direction with a
// contrasting color, then once more normally on top. Cheap and
// guarantees contrast against any background without needing
// per-pixel edge detection.
static const GPoint OUTLINE_OFFSETS[4] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

// White for dark colors, black for bright ones -- the outline has to
// contrast with the text/icon's OWN color to do its job (a dark
// outline on dark text is invisible regardless of what's behind it),
// not with the scheme's background color, which is what this used to
// (incorrectly) use.
//
// "On shake" animation: text/icon outlines can only take one fill
// color per whole draw call (graphics_draw_text()/
// graphics_draw_bitmap_in_rect() don't support anything finer), so
// unlike hand_layer.c's outlines -- which sample the gradient per
// pixel for a true left-to-right sweep across the screen -- every
// caller here just samples the gradient once, at a fixed reference
// point, and shares that single color. shake_outline_color() itself
// is a no-op (returns base unchanged) whenever the animation isn't
// actually running or isn't in a gradient-including mode.
GColor contrasting_outline_color(GColor c) {
  uint8_t r = (c.argb >> 4) & 0x03;
  uint8_t g = (c.argb >> 2) & 0x03;
  uint8_t b = c.argb & 0x03;
  int luma = r * 3 + g * 6 + b; // approximates 0.3/0.6/0.1 luma weights, out of 30
  GColor base = (luma >= 15) ? GColorBlack : GColorWhite;
  return shake_outline_color(base, 0);
}

void draw_text_outlined(GContext *ctx, const char *text, GFont font, GRect box,
                                GTextOverflowMode overflow, GTextAlignment alignment,
                                GColor color, bool outline_enabled) {
  if (outline_enabled) {
    graphics_context_set_text_color(ctx, contrasting_outline_color(color));
    for (int i = 0; i < 4; i++) {
      GRect shifted = GRect(box.origin.x + OUTLINE_OFFSETS[i].x, box.origin.y + OUTLINE_OFFSETS[i].y,
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

// How many of the small-analog info panel's rows actually fit, given
// the currently-selected corner/edge font -- anything taller than
// System Medium (the old default) only leaves room for 3 rows before
// they'd start overlapping; System Small and Medium both stay short
// enough for the full 4. Mirrors the equivalent check in
// config-page.js's computeSlotAvailability() (used to gray out the
// 4th feature button there) -- keep the two in sync.
uint8_t small_analog_feature_count(const EclipseData *data) {
  return font_lookup_height(data->corner_font) > 18 ? 3 : 4;
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
// authored as a 2-color (transparent + opaque) 1-bit-palette PNG (see
// "memoryFormat": "1BitPalette" in package.json), so gbitmap_set_palette()
// remaps palette index 0 to fully transparent and index 1 to whatever
// GColor the caller wants, giving every one of them full-color tinting
// (any GColor, not just black) and alpha transparency for free, with
// no per-icon code. Outline support reuses the exact technique already
// used everywhere else in this file (draw_text_outlined() et al, see
// OUTLINE_OFFSETS' comment near the top): call sites draw the icon 4x
// shifted in a contrasting outline color, then once more in the real
// fill color -- draw_icon_resource() itself doesn't need to know
// about outlines at all, it just draws one already-tinted icon.
static void draw_icon_resource(GContext *ctx, GPoint top_left, uint32_t resource_id, GColor color) {
  GBitmap *bmp = gbitmap_create_with_resource(resource_id);
  if (!bmp) return;
  // Palette array must stay alive for the whole set+draw+destroy
  // sequence below (free_on_destroy=false, so gbitmap_destroy() won't
  // try to free this stack array) -- see gbitmap_set_palette()'s docs:
  // https://developer.rebble.io/docs/c/Graphics/Graphics_Types/#gbitmap_set_palette
  GColor palette[2] = { GColorClear, color };
  gbitmap_set_palette(bmp, palette, false);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(top_left.x, top_left.y, ICON_WIDTH, ICON_ROWS));
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

// Combined width of an icon plus its gap before the text that
// follows it, per icon_kind -- used to position the icon+text group
// as a unit for alignment (see draw_corner_item).
static int16_t icon_plus_gap_width(int icon_kind) { // TODO: This might not be neccessary anymore
  switch (icon_kind) {
    case 1: case 2: case 5: case 6: case 7: case 8: case 9: case 10: case 13:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26:
      return 11; // bitmap icons (7-wide at 140% scale) + gap
    case 3: return 10; // battery + gap
    case 4: return 21; // moon (radius 9, so 2*9+2 diameter box) + gap
    case 11: return 22; // sun-time glyph (fixed 20px, drawn via direct primitives) + gap
    case 14: return 20; // weather icon (16-wide box, worst case a bit wider for the sun's rays) + gap
    case 15: return 12; // pressure trend chevron + gap
    case 16: return 14; // wind direction arrow + gap
    case 17: return 20; // mountain icon (16-wide box) + gap
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
// allow_outline gates data->outline_enabled on top of the user
// setting rather than replacing it -- pass true from every caller
// that draws over the busy sky/hands canvas (corners, edge-middle
// slots), where the outline is what keeps text legible against an
// unpredictable background. The small-analog info panel's rows sit
// on their own solid-color background instead, so they pass false
// and never get one regardless of the outline_enabled setting --
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
      return true;
    default:
      return false;
  }
}

void features_draw_item(GContext *ctx, GRect bounds, const EclipseData *data,
                              uint8_t content, uint8_t color_mode,
                              GColor main_color, GColor accent_color, GColor bg_color,
                              bool is_top, bool is_left, bool is_middle, int16_t top_offset, int16_t bottom_shift,
                              int16_t middle_inset,
                              bool center_horizontal, bool center_vertical, bool allow_outline) {
  if (content == 0) return;

  // 40, not 24 -- the actual longest real content (e.g. "September",
  // "Restful sleep") stays well under 24, but GCC's -Wformat-truncation
  // sizes snprintf's *worst case* off each %d's full possible range (up
  // to 11 characters, for a very negative 32-bit int), not the small
  // calendar-sized values (day/month/year) actually passed in -- the
  // 3-%d date format below is the tightest case, needing up to 36 by
  // that conservative accounting even though real dates need under 12.
  char buf[40];
  int icon_kind = 0; // 0=none, 1=heart, 2=foot, 3=battery, 4=moon phase, 5=umbrella, 6=droplet,
                       // 7=wind, 8=GPS pin, 9=eye, 10=clouds, 11=sunrise/sunset
  bool icon_is_sunrise = false; // only meaningful when icon_kind == 11
  uint8_t icon_weather_category = 0; // only meaningful when icon_kind == 14 -- see weather_icon_category()
  GColor dynamic_color = main_color;

  switch (content) {
    case 1: { // heart rate -- red if a recent reading is available, gray otherwise
      icon_kind = 1;
      int bpm = 0;
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
      }
      if (bpm > 0) {
        snprintf(buf, sizeof(buf), "%d", bpm);
        dynamic_color = GColorFromRGB(220, 20, 20);
      } else {
        snprintf(buf, sizeof(buf), "--");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 2: { // steps today
      icon_kind = 2;
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      snprintf(buf, sizeof(buf), "%d", (int)steps);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 100) pct = 100;
      dynamic_color = red_green_gradient((uint8_t)pct);
      break;
    }
    case 3: { // step goal %
      icon_kind = 2;
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      uint16_t goal = data->daily_step_goal > 0 ? data->daily_step_goal : 10000;
      int32_t pct = (steps * 100) / goal;
      if (pct > 999) pct = 999;
      snprintf(buf, sizeof(buf), "%d%%", (int)pct);
      dynamic_color = red_green_gradient((uint8_t)(pct > 100 ? 100 : pct));
      break;
    }
    case 4: { // high/low temperature -- same readout the fixed bottom-left corner used to show
      int16_t hi = convert_temp(data->temp_high_c, data->temp_unit);
      int16_t lo = convert_temp(data->temp_low_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "H%d L%d", hi, lo);
      // Only used for the mono/accent/translucent color modes, which
      // still share one color across the combined "H.. L.." string --
      // dynamic mode splits high and low into their own separately
      // gradient-colored segments instead (see the special case near
      // the final text draw below), so this particular value goes
      // unused in that combination.
      dynamic_color = seven_stop_gradient(data->temp_high_c, -10, 40);
      break;
    }
    case 5: { // current conditions -- same readout the fixed bottom-right corner used to show
      int16_t temp = convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d%s %s", temp, temp_unit_suffix(data->temp_unit),
               short_condition_text(data->weather_condition, data->cloud_cover_pct));
      dynamic_color = data->valid
        ? weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct)
        : bg_color;
      break;
    }
    case 6: { // UV index
      uint8_t uv = data->uv_index_x10 / 10;
      snprintf(buf, sizeof(buf), "UV%d", uv);
      dynamic_color = seven_stop_gradient(uv, 1, 13);
      break;
    }
    case 7: { // rain chance
      icon_kind = 5;
      snprintf(buf, sizeof(buf), "%d%%", data->rain_chance_pct);
      dynamic_color = white_to_turquoise_gradient(data->rain_chance_pct, 0, 100);
      break;
    }
    case 8: { // humidity
      icon_kind = 6;
      snprintf(buf, sizeof(buf), "%d%%", data->humidity_pct);
      dynamic_color = white_to_turquoise_gradient(data->humidity_pct, 0, 100);
      break;
    }
    case 9: { // wind
      icon_kind = 7;
      snprintf(buf, sizeof(buf), "%d", convert_wind(data->wind_speed_kmh, data->wind_speed_unit));
      dynamic_color = white_to_turquoise_gradient(data->wind_speed_kmh, 0, 60);
      break;
    }
    case 10: { // battery
      icon_kind = 3;
      BatteryChargeState bs = battery_state_service_peek();
      if (bs.is_charging) {
        snprintf(buf, sizeof(buf), "%d%%+", bs.charge_percent);
      } else {
        snprintf(buf, sizeof(buf), "%d%%", bs.charge_percent);
      }
      dynamic_color = red_green_gradient((uint8_t)bs.charge_percent);
      break;
    }
    case 11: { // Moon phase -- icon + short name
      icon_kind = 4;
      snprintf(buf, sizeof(buf), "%s", moon_phase_short_name(data->moon_phase_pct, data->moon_waxing));
      dynamic_color = GColorWhite; // no numeric dimension to grade on; white is the Moon's natural color
      break;
    }
    case 12: { // short date -- no icon, e.g. "Mon 15"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      char day_buf[4], mday_buf[4];
      strftime(day_buf, sizeof(day_buf), "%a", t);
      snprintf(mday_buf, sizeof(mday_buf), "%d", t->tm_mday);
      snprintf(buf, sizeof(buf), "%s %s", day_buf, mday_buf);
      dynamic_color = main_color; // no natural "value" to grade on
      break;
    }
    case 13: { // location name
      icon_kind = 8;
      snprintf(buf, sizeof(buf), "%s", data->location_name[0] != '\0' ? data->location_name : "Unknown");
      dynamic_color = main_color; // no natural "value" to grade on
      break;
    }
    case 14: { // visibility ("chance you'll actually see it" score)
      icon_kind = 9;
      snprintf(buf, sizeof(buf), "%d%%", data->vis_score_pct);
      dynamic_color = red_green_gradient(data->vis_score_pct);
      break;
    }
    case 15: { // cloud cover
      icon_kind = 10;
      snprintf(buf, sizeof(buf), "%d%%", data->cloud_cover_pct);
      dynamic_color = overcast_gray_gradient(data->cloud_cover_pct < OVERCAST_CLOUD_THRESHOLD ? OVERCAST_CLOUD_THRESHOLD : data->cloud_cover_pct);
      break;
    }
    case 16: { // sunrise/sunset -- same event/icon as the digital/analog info panel's row
      icon_kind = 11;
      time_t now = time(NULL);
      time_t sun_event_time = 0;
      if (get_next_sun_event(now, data->sun_rise, data->sun_set, data->sun_rise_tomorrow, &sun_event_time, &icon_is_sunrise)) {
        struct tm *event_t = localtime(&sun_event_time);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", event_t);
      } else {
        snprintf(buf, sizeof(buf), "--:--");
      }
      dynamic_color = main_color; // no natural "value" to grade on
      break;
    }
    case 17: { // pebble battery logo
      icon_kind = 12;
      BatteryChargeState bs = battery_state_service_peek();
      dynamic_color = red_green_gradient((uint8_t)bs.charge_percent);
      break;
    }
    case 18: { // digital time
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
      dynamic_color = seven_stop_gradient((int32_t)(t->tm_hour*60+t->tm_min), 0, 1440);
      break;
    }
    case 19: { // week number
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "WK %V", t);
      dynamic_color = seven_stop_gradient((int32_t)(t->tm_yday), 0, 360);
      break;
    }
    case 20: { // Bluetooth connection status
      icon_kind = 13;
      bool connected = connection_service_peek_pebble_app_connection();
      snprintf(buf, sizeof(buf), "%s", connected ? "Connected" : "No phone");
      // Bright, saturated colors deliberately outside the muted palettes
      // used elsewhere (this is a binary connected/not state, not
      // something to grade smoothly along a gradient).
      dynamic_color = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      break;
    }
    // ---- date format variants (21-30) -- none of these have a natural
    // "value" to grade a color on, so they all just take main_color,
    // same as the original short-date (case 12).
    case 21: { // month + day, e.g. "SEP 11"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      char mon_buf[4];
      strftime(mon_buf, sizeof(mon_buf), "%b", t);
      to_upper_str(mon_buf);
      snprintf(buf, sizeof(buf), "%s %d", mon_buf, t->tm_mday);
      dynamic_color = main_color;
      break;
    }
    case 22: { // day of month only, e.g. "11"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d", t->tm_mday);
      dynamic_color = main_color;
      break;
    }
    case 23: { // weekday, short + all caps, e.g. "MON"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%a", t);
      to_upper_str(buf);
      dynamic_color = main_color;
      break;
    }
    case 24: { // weekday, long, e.g. "Monday"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%A", t);
      dynamic_color = main_color;
      break;
    }
    case 25: { // month, short + all caps, e.g. "SEP"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%b", t);
      to_upper_str(buf);
      dynamic_color = main_color;
      break;
    }
    case 26: { // month, long, e.g. "September"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), "%B", t);
      dynamic_color = main_color;
      break;
    }
    case 27: { // day/month, e.g. "11/9"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d", t->tm_mday, t->tm_mon + 1);
      dynamic_color = main_color;
      break;
    }
    case 28: { // month/day, e.g. "9/11"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d", t->tm_mon + 1, t->tm_mday);
      dynamic_color = main_color;
      break;
    }
    case 29: { // full, day/month/year, e.g. "24/9/2026"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d/%d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
      dynamic_color = main_color;
      break;
    }
    case 30: { // full imperial, month/day/2-digit year, e.g. "9/24/26"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d/%d/%02d", t->tm_mon + 1, t->tm_mday, (t->tm_year + 1900) % 100);
      dynamic_color = main_color;
      break;
    }
    case 31: { // weather icon only, no text
      icon_kind = 14;
      icon_weather_category = weather_icon_category(data->weather_condition, data->cloud_cover_pct);
      buf[0] = '\0';
      dynamic_color = data->valid
        ? weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct)
        : bg_color;
      break;
    }
    case 32: { // temp + weather icon
      icon_kind = 14;
      icon_weather_category = weather_icon_category(data->weather_condition, data->cloud_cover_pct);
      int16_t temp = convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d%s", temp, temp_unit_suffix(data->temp_unit));
      dynamic_color = data->valid
        ? weather_condition_color(data->weather_condition, data->cloud_cover_pct, data->rain_chance_pct)
        : bg_color;
      break;
    }
    // Timezone -- "ABBR H:MM" (or "ABBR HH:MM" in 24h style). Each of
    // the 19 cities is its OWN content id (44-62, right after the
    // highest id already in use, in the same order as TIMEZONES[]
    // below) rather than one "Timezone" id plus a shared setting
    // picking which city -- that's what makes this genuinely
    // independent per slot: two different slots can each pick a
    // different city and show both at once, since the city is baked
    // into which content id was chosen, not a single global choice
    // every "Timezone" slot would otherwise have to share. (id 33,
    // the old single "Timezone" placeholder, is simply retired rather
    // than reused -- ids 34-43 are already taken by other features
    // added since, so 44 is the first id actually free.)
    case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53:
    case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62: {
      const TimezoneInfo *tz = &TIMEZONES[content - 44];
      time_t now = time(NULL);
      int16_t offset_min = timezone_current_offset_min(tz, now);
      time_t local_time = now + (int32_t)offset_min * 60;
      int32_t local_secs_of_day = ((local_time % 86400) + 86400) % 86400;
      int local_hour24 = (int)(local_secs_of_day / 3600);
      int local_min = (int)((local_secs_of_day % 3600) / 60);
      if (clock_is_24h_style()) {
        snprintf(buf, sizeof(buf), "%s %02d:%02d", tz->abbr, local_hour24, local_min);
      } else {
        int hour12 = local_hour24 % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(buf, sizeof(buf), "%s %d:%02d%s", tz->abbr, hour12, local_min, local_hour24 < 12 ? "AM" : "PM");
      }
      dynamic_color = timezone_daylight_color(local_hour24);
      break;
    }
    case 34: { // pressure, with rising/falling/flat trend arrow
      icon_kind = 15;
      snprintf(buf, sizeof(buf), "%d hPa", data->pressure_hpa);
      // Green when in the ordinary ~1000-1025 hPa band, ambering out
      // toward either extreme -- reuses the same 7-stop gradient as
      // temperature/UV, just remapped to a pressure-appropriate range.
      dynamic_color = seven_stop_gradient(data->pressure_hpa, 970, 1050);
      break;
    }
    case 35: { // wind direction, with a rotated compass arrow
      icon_kind = 16;
      static const char *COMPASS_DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
      int compass_idx = ((data->wind_dir_deg + 22) / 45) % 8;
      if (compass_idx < 0) compass_idx += 8;
      snprintf(buf, sizeof(buf), "%s", COMPASS_DIRS[compass_idx]);
      dynamic_color = main_color; // no natural "value" to grade a color on
      break;
    }
    case 36: { // air quality -- unit picked in the Weather settings section
      bool use_eu = (data->aqi_unit == 1);
      uint16_t aqi_value = use_eu ? data->aqi_eu : data->aqi_us;
      snprintf(buf, sizeof(buf), "AQI %d", aqi_value);
      // US AQI: good <=50, moderate <=100, unhealthy >150 (0-500 scale).
      // European AQI: good <=20, moderate <=40, poor >60 (0-100+ scale).
      // Different thresholds per scale, same green->yellow->red shape.
      uint16_t good_max = use_eu ? 20 : 50;
      uint16_t bad_min = use_eu ? 60 : 150;
      GColor good = GColorFromRGB(0, 200, 0), mid = GColorFromRGB(230, 200, 0), bad = GColorFromRGB(220, 0, 0);
      if (aqi_value <= good_max) dynamic_color = good;
      else if (aqi_value >= bad_min) dynamic_color = bad;
      else dynamic_color = mid;
      break;
    }
    case 37: { // dew point -- reuses the humidity feature's droplet icon
      icon_kind = 6;
      int16_t dew = convert_temp(data->dew_point_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d%s", dew, temp_unit_suffix(data->temp_unit));
      dynamic_color = main_color;
      break;
    }
    case 38: { // altitude
      icon_kind = 17;
      if (data->altitude_m <= -32000) { // sentinel: not available
        snprintf(buf, sizeof(buf), "N/A");
      } else if (data->altitude_unit == 1) { // feet
        int32_t feet = ((int32_t)data->altitude_m * 328) / 100; // *3.28084, integer approximation
        snprintf(buf, sizeof(buf), "%ldft", (long)feet);
      } else {
        snprintf(buf, sizeof(buf), "%dm", data->altitude_m);
      }
      dynamic_color = main_color;
      break;
    }
    case 39: { // sleep duration (total)
      icon_kind = 21;
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricSleepSeconds, time(NULL) - 86400, time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue secs = health_service_sum_today(HealthMetricSleepSeconds);
        format_duration_hm(buf, sizeof(buf), (int32_t)secs);
        dynamic_color = seven_stop_gradient_reversed((int32_t)secs, 0, 9 * 3600); // 0-9h
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 40: { // restful (deep) sleep duration
      icon_kind = 22;
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricSleepRestfulSeconds, time(NULL) - 86400, time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue secs = health_service_sum_today(HealthMetricSleepRestfulSeconds);
        format_duration_hm(buf, sizeof(buf), (int32_t)secs);
        dynamic_color = seven_stop_gradient_reversed((int32_t)secs, 0, 3 * 3600); // 0-3h
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 41: { // sleep quality -- restful / total, as a percentage
      icon_kind = 20;
      HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricSleepSeconds, time(NULL) - 86400, time(NULL));
      if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue total = health_service_sum_today(HealthMetricSleepSeconds);
        HealthValue restful = health_service_sum_today(HealthMetricSleepRestfulSeconds);
        int pct = (total > 0) ? (int)((restful * 100) / total) : 0;
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        dynamic_color = red_green_gradient((uint8_t)pct);
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 42: { // bed time -- earliest sleep-activity start in the last 24h
      icon_kind = 18;
      SleepSpan span = get_sleep_span();
      if (span.found) {
        struct tm *t = localtime(&span.earliest_start);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
        dynamic_color = main_color;
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 43: { // wake time -- latest sleep-activity end in the last 24h
      icon_kind = 19;
      SleepSpan span = get_sleep_span();
      if (span.found) {
        struct tm *t = localtime(&span.latest_end);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
        dynamic_color = main_color;
      } else {
        snprintf(buf, sizeof(buf), "N/A");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    // ---- date/time component variants (63-72) -- like the date
    // formats above, none of these have a natural "value" to grade a
    // color on, so they all just take main_color.
    case 63: { // full time, e.g. "14:32:07" / "2:32:07 PM"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S %p", t);
      dynamic_color = main_color;
      break;
    }
    case 64: { // hour, always 2 digits (24h), e.g. "07"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%02d", t->tm_hour);
      dynamic_color = main_color;
      break;
    }
    case 65: { // hour, no leading zero (24h), e.g. "7"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d", t->tm_hour);
      dynamic_color = main_color;
      break;
    }
    case 66: { // hour, 12h mode, no leading zero, e.g. "7"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      int hour12 = t->tm_hour % 12;
      if (hour12 == 0) hour12 = 12;
      snprintf(buf, sizeof(buf), "%d", hour12);
      dynamic_color = main_color;
      break;
    }
    case 67: { // minute, no leading zero, e.g. "5"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d", t->tm_min);
      dynamic_color = main_color;
      break;
    }
    case 68: { // minute, leading zero, e.g. "05"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%02d", t->tm_min);
      dynamic_color = main_color;
      break;
    }
    case 69: { // second, no leading zero, e.g. "8"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d", t->tm_sec);
      dynamic_color = main_color;
      break;
    }
    case 70: { // second, leading zero, e.g. "08"
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%02d", t->tm_sec);
      dynamic_color = main_color;
      break;
    }
    case 71: { // seconds, tens digit only (0-5)
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d", t->tm_sec / 10);
      dynamic_color = main_color;
      break;
    }
    case 72: { // seconds, singles digit only (0-9)
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      snprintf(buf, sizeof(buf), "%d", t->tm_sec % 10);
      dynamic_color = main_color;
      break;
    }
    // ---- weather temperature variants (73-77) -- all graded on the
    // same -10..40C 7-stop gradient the existing temperature content
    // types use ("color" mode), rather than the condition-driven
    // palette the weather-icon content types (31/32) use.
    case 73: { // current temp only, e.g. "22C"
      int16_t temp = convert_temp(data->weather_temp_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d%s", temp, temp_unit_suffix(data->temp_unit));
      dynamic_color = seven_stop_gradient(data->weather_temp_c, -10, 40);
      break;
    }
    case 74: { // high temp only, e.g. "H 28C"
      int16_t hi = convert_temp(data->temp_high_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "H %d%s", hi, temp_unit_suffix(data->temp_unit));
      dynamic_color = seven_stop_gradient(data->temp_high_c, -10, 40);
      break;
    }
    case 75: { // low temp only, e.g. "L 11C"
      int16_t lo = convert_temp(data->temp_low_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "L %d%s", lo, temp_unit_suffix(data->temp_unit));
      dynamic_color = seven_stop_gradient(data->temp_low_c, -10, 40);
      break;
    }
    case 76: { // weather icon + current/high/low all in one line
      icon_kind = 14;
      icon_weather_category = weather_icon_category(data->weather_condition, data->cloud_cover_pct);
      int16_t cur = convert_temp(data->weather_temp_c, data->temp_unit);
      int16_t hi = convert_temp(data->temp_high_c, data->temp_unit);
      int16_t lo = convert_temp(data->temp_low_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "%d H%d L%d%s", cur, hi, lo, temp_unit_suffix(data->temp_unit));
      dynamic_color = seven_stop_gradient(data->weather_temp_c, -10, 40);
      break;
    }
    case 77: { // feels-like temp, e.g. "FL 20C"
      int16_t felt_c = apparent_temp_c(data->weather_temp_c, data->wind_speed_kmh, data->humidity_pct);
      int16_t felt = convert_temp(felt_c, data->temp_unit);
      snprintf(buf, sizeof(buf), "FL %d%s", felt, temp_unit_suffix(data->temp_unit));
      dynamic_color = seven_stop_gradient(felt_c, -10, 40);
      break;
    }
    // Icon-only Bluetooth status -- unlike content 20 (which always
    // shows "Connected"/"No phone" text in whatever color mode was
    // picked), this one only ever draws its dynamic connected/
    // disconnected color: see the color_mode override right after
    // this switch, below.
    case 78: {
      icon_kind = 13;
      buf[0] = '\0';
      bool connected = connection_service_peek_pebble_app_connection();
      dynamic_color = connected ? GColorFromRGB(64, 224, 208) : GColorFromRGB(255, 0, 0);
      break;
    }
    // ---- astronomy features (79-83) -- all built from data already
    // being sent every refresh for the sky-view animation itself
    // (planet altitude samples/rise/set, Saturn's ring angle, the
    // active meteor shower) except 83, which needed a genuinely new
    // phone-side computation (see astro.js's findNextIssPass()) since
    // only a single current-moment ISS snapshot existed before.
    case 79: { // how many of the 5 tracked planets are above the horizon right now
      icon_kind = 23;
      time_t now = time(NULL);
      uint8_t count = background_count_visible_planets(data, now);
      snprintf(buf, sizeof(buf), "%d planet%s", count, count == 1 ? "" : "s");
      dynamic_color = main_color;
      break;
    }
    case 80: { // active meteor shower name, if any -- graded by intensity like a percentage
      if (data->meteor_intensity > 0 && data->meteor_shower_name[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", data->meteor_shower_name);
        dynamic_color = red_green_gradient(data->meteor_intensity);
      } else {
        snprintf(buf, sizeof(buf), "None");
        dynamic_color = GColorLightGray;
      }
      break;
    }
    case 81: { // Saturn's current ring-opening angle (0% = edge-on, 100% = fully open)
      icon_kind = 24;
      snprintf(buf, sizeof(buf), "Rings %d%%", data->saturn_ring_open_pct);
      dynamic_color = main_color;
      break;
    }
    case 82: { // which of the 5 tracked planets rises next today, and when
      time_t now = time(NULL);
      static const char *PLANET_ABBR[PLANET_COUNT] = { "MER", "VEN", "MAR", "JUP", "SAT" };
      int best = -1;
      time_t best_t = 0;
      for (int p = 0; p < PLANET_COUNT; p++) {
        time_t r = data->planet_rise[p];
        if (r > now && (best == -1 || r < best_t)) {
          best = p;
          best_t = r;
        }
      }
      if (best >= 0) {
        struct tm *t = localtime(&best_t);
        char time_buf[8];
        strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
        snprintf(buf, sizeof(buf), "%s %s", PLANET_ABBR[best], time_buf);
      } else {
        snprintf(buf, sizeof(buf), "None");
      }
      dynamic_color = main_color;
      break;
    }
    case 83: { // start time of the next visible ISS pass, if astro.js's
               // findNextIssPass() found one in its search window -- see
               // eclipse_data.h's iss_next_pass comment for what "visible" means
      icon_kind = 25;
      if (data->iss_next_pass > 0) {
        struct tm *t = localtime(&data->iss_next_pass);
        strftime(buf, sizeof(buf), clock_is_24h_style() ? "%H:%M" : "%I:%M %p", t);
      } else {
        snprintf(buf, sizeof(buf), "--:--");
      }
      dynamic_color = main_color;
      break;
    }
    case 84: { // current planetary Kp index -- see eclipse_data.h's aurora_kp_x10/
               // aurora_enabled comments. Shown (with a real Kp reading) even when
               // aurora_visibility_pct says the estimate doesn't reach this latitude --
               // the index itself is informative on its own, only the sky glow's
               // own visibility is gated on that estimate.
      icon_kind = 26;
      snprintf(buf, sizeof(buf), "Kp %d.%d", data->aurora_kp_x10 / 10, data->aurora_kp_x10 % 10);
      dynamic_color = white_to_red_gradient(data->aurora_kp_x10);
      break;
    }
    case 85: { // Compass -- active (real heading, redrawing) for 15s after a shake,
               // then asleep (shows "Z z" and three dashes) until the next one -- see
               // compass_feature_is_asleep()/compass_feature_heading_deg() in
               // pebble-eclipse-watch.c. Its own multi-color north-arrow/other-arrows
               // split is handled separately, right after this switch, rather than
               // through dynamic_color like every other content here -- see that code's
               // own comment for why.
      icon_kind = 27;
      if (compass_feature_is_asleep()) {
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
      dynamic_color = main_color; // unused in practice -- see the north/other split below
      break;
    }
    default:
      return;
  }

  GColor color;
  bool translucent = false;
  switch (color_mode) {
    case 1: color = accent_color; break;
    case 2: color = accent_color; translucent = true; break;
    case 3: color = dynamic_color; break;
    case 0:
    default: color = main_color; break;
  }

  // See content_is_weather_derived()'s own comment above -- overrides
  // whatever the switch above built, uniformly, for any of the 16
  // weather-sourced content ids at once. A deliberately plain, fixed
  // red rather than anything mode/gradient-driven: this is reporting a
  // fetch problem, not a weather reading, so it shouldn't try to blend
  // in as one -- and skips translucent/dithering for the same reason
  // (an error needs to stay legible, not fade like normal content can).
  if (content_is_weather_derived(content) && weather_should_show_error(data)) {
    snprintf(buf, sizeof(buf), "ERR %d", data->weather_error_code);
    color = GColorRed;
    translucent = false;
    icon_kind = 0;
  }
  // Compass (85) needs 2 colors at once (see draw_compass_icon()'s own
  // comment) rather than the single flat `color` every other content
  // uses -- "mono" (color_mode 0) and "acc"/"semi" (1/2) still just
  // mean one flat color for the whole icon, same as `color` above
  // already resolved; only "color" mode (3) is actually special here,
  // splitting into an accent north arrow + main-color other 3 rather
  // than picking one value-driven gradient the way every other
  // content's dynamic_color does. Translucent dithering (color_mode 2)
  // isn't applied to the compass -- its icon is line-drawn, not
  // filled, and Bayer-dithering individual 1px strokes wouldn't read
  // as translucency the way it does on a filled shape.
  GColor compass_north_color = color, compass_other_color = color;
  if (content == 85 && color_mode == 3) {
    compass_north_color = accent_color;
    compass_other_color = main_color;
  }
  // Bluetooth status (78) only ever means anything as its dynamic
  // connected/disconnected color -- MONO/ACC/SEMI would just make it
  // a plain, meaningless-colored icon, so it ignores color_mode
  // entirely and always draws dynamic_color.
  if (content == 78) {
    color = dynamic_color;
    translucent = false;
  }

  int16_t box_x = center_horizontal
    ? bounds.origin.x + (bounds.size.w - CORNER_BOX_W) / 2
    : (is_left ? bounds.origin.x + 2 : bounds.origin.x + bounds.size.w - 2 - CORNER_BOX_W);
  int16_t box_y = center_vertical
    ? bounds.origin.y + (bounds.size.h - CORNER_ROW_H) / 2 + top_offset // top_offset doubles as a
                                                                          // vertical nudge from center here
                                                                          // (middle-left/right's 2-line pairs)
    : (is_top ? bounds.origin.y + top_offset
              : bounds.origin.y + bounds.size.h - CORNER_ROW_H - 2 - bottom_shift);
  
  if (is_middle) {
    if (is_left) {
      box_x += middle_inset;
    } else {
      box_x -= middle_inset;
    }
  }

  if (translucent) {
    // A dithered highlight plate behind the icon+text -- gives
    // "translucent accent" a real visual difference from "solid
    // accent" without needing per-glyph dithering (system fonts
    // can't be inspected pixel-by-pixel the way the procedural tiny
    // digit/icon bitmaps above can).
    GPoint plate_pts[4] = {
      GPoint(box_x, box_y), GPoint(box_x + CORNER_BOX_W, box_y),
      GPoint(box_x + CORNER_BOX_W, box_y + CORNER_ROW_H), GPoint(box_x, box_y + CORNER_ROW_H)
    };
    fill_polygon_dithered(ctx, plate_pts, 4, color);
  }

  GFont font = font_lookup_resolve(&s_corner_font_slot, data->corner_font);
  int16_t font_h = font_lookup_height(data->corner_font);
  GSize text_size = graphics_text_layout_get_content_size(buf, font, GRect(0, 0, 200, font_h + 10),
                                                            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  int16_t icon_gap_w = icon_plus_gap_width(icon_kind);
  int16_t group_w = icon_gap_w + text_size.w;
  if (group_w > CORNER_BOX_W) group_w = CORNER_BOX_W;

  int16_t group_x;
  if (center_horizontal) {
    group_x = box_x + (CORNER_BOX_W - group_w) / 2;
  } else if (!is_left) {
    group_x = box_x + CORNER_BOX_W - group_w;
  } else {
    group_x = box_x;
  }
  int16_t icon_x = group_x;
  int16_t text_x = group_x + icon_gap_w;

  bool do_icon_outline = allow_outline && data->outline_enabled && color_mode != 2;
  GColor icon_outline_color = contrasting_outline_color(color);
  switch (icon_kind) {
    case 1: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_HEART, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_HEART, color);
      break;
    }
    case 2: {
      // Was hand-tuned for the old 8x13 FOOT_ICON bit pattern; now that
      // every icon (including this one) is a standardized 16x12 image
      // resource, it uses the same centering as the heart icon above.
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_FOOT, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_FOOT, color);
      break;
    }
    case 3: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 14) / 2);
      BatteryChargeState bs = battery_state_service_peek();
      
      if (bs.is_charging) {
        color = GColorGreen;
        icon_outline_color = GColorBlack;
      }
      
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_corner_battery_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), icon_outline_color, 0);
        }
      }
      
      draw_corner_battery_icon(ctx, pos, color, bs.charge_percent);
      break;
    }
    case 4: {
      int16_t moon_r = 9;
      GPoint moon_center = GPoint(icon_x + moon_r, box_y + CORNER_ROW_H / 2);
      GRect moon_clip = GRect(icon_x, box_y, moon_r * 2 + 2, CORNER_ROW_H);
      if (do_icon_outline) {
        // A simplified bounding-circle outline rather than 4 extra
        // full phase-shaded passes -- draw_moon_phase's lit/dark
        // split is a per-pixel loop, so replicating it 4x would cost
        // meaningfully more for a detail (the outline's silhouette
        // exactly following the phase terminator) that isn't visible
        // at this size anyway.
        graphics_context_set_fill_color(ctx, icon_outline_color);
        for (int i = 0; i < 4; i++) {
          GPoint shifted = GPoint(moon_center.x + OUTLINE_OFFSETS[i].x, moon_center.y + OUTLINE_OFFSETS[i].y);
          graphics_fill_circle(ctx, shifted, moon_r);
        }
      }
      draw_moon_phase(ctx, moon_clip, moon_center, moon_r, data->moon_phase_pct, data->moon_waxing, color);
      break;
    }
    case 5: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+10, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_UMBRELLA, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_UMBRELLA, color);
      break;
    }
    case 6: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+10, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_DROPLET, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_DROPLET, color);
      break;
    }
    case 7: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+10, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_WIND, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_WIND, color);
      break;
    }
    case 8: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_GPS_PIN, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_GPS_PIN, color);
      break;
    }
    case 9: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_EYE, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_EYE, color);
      break;
    }
    case 10: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_CLOUD, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_CLOUD, color);
      break;
    }
    case 13: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_BLUETOOTH, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_BLUETOOTH, color);
      break;
    }
    case 18: case 19: case 20: case 21: case 22: {
      uint32_t bed_resource = RESOURCE_ID_ICON_BED_ARROW_IN;
      if (icon_kind == 19) bed_resource = RESOURCE_ID_ICON_BED_ARROW_OUT;
      else if (icon_kind == 20) bed_resource = RESOURCE_ID_ICON_BED_CHECK;
      else if (icon_kind == 21) bed_resource = RESOURCE_ID_ICON_BED_CLOCK;
      else if (icon_kind == 22) bed_resource = RESOURCE_ID_ICON_BED_CHECK_CLOCK;
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                              bed_resource, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, bed_resource, color);
      break;
    }
    case 23: case 24: case 25: {
      uint32_t astro_resource = RESOURCE_ID_ICON_PLANETS;
      if (icon_kind == 24) astro_resource = RESOURCE_ID_ICON_SATURN_RING;
      else if (icon_kind == 25) astro_resource = RESOURCE_ID_ICON_ISS;
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                              astro_resource, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, astro_resource, color);
      break;
    }
    case 26: {
      GPoint pos = GPoint(icon_x - ICON_WIDTH+6, box_y + (CORNER_ROW_H - ICON_ROWS) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_icon_resource(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), RESOURCE_ID_ICON_AURORA, icon_outline_color);
        }
      }
      draw_icon_resource(ctx, pos, RESOURCE_ID_ICON_AURORA, color);
      break;
    }
    case 27: { // Compass -- see draw_compass_icon()/draw_compass_sleep_icon()'s
               // own comments, and content-85's own case in the switch above for
               // where compass_north_color/compass_other_color come from.
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      bool asleep = compass_feature_is_asleep();
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          GPoint shifted = GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y);
          if (asleep) draw_compass_sleep_icon(ctx, shifted, icon_outline_color);
          else draw_compass_icon(ctx, shifted, compass_feature_heading_deg(), icon_outline_color, icon_outline_color);
        }
      }
      if (asleep) draw_compass_sleep_icon(ctx, pos, color);
      else draw_compass_icon(ctx, pos, compass_feature_heading_deg(), compass_north_color, compass_other_color);
      break;
    }
    case 11: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 9) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_sun_time_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                              icon_is_sunrise, icon_outline_color, bg_color);
        }
      }
      draw_sun_time_icon(ctx, pos, icon_is_sunrise, color, bg_color);
      break;
    }
    case 14: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - ICON_ROWS) / 2 - 2);
      // No outline pass for style 2 (full color): draw_weather_icon_filled()
      // ignores whatever color it's given (see its own comment), so
      // shifting it 4x in "icon_outline_color" would just redraw the
      // exact same multi-color icon 4x instead of a contrasting
      // silhouette behind it -- a smudgy ghosting effect, not an outline.
      if (do_icon_outline && data->weather_icon_style != 2) {
        for (int i = 0; i < 4; i++) {
          draw_weather_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                             icon_weather_category, data->weather_icon_style, icon_outline_color);
        }
      }
      draw_weather_icon(ctx, pos, icon_weather_category, data->weather_icon_style, color);
      break;
    }
    case 15: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_pressure_trend_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                                    data->pressure_trend, icon_outline_color);
        }
      }
      draw_pressure_trend_icon(ctx, pos, data->pressure_trend, color);
      break;
    }
    case 16: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_wind_direction_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                                    data->wind_dir_deg, icon_outline_color);
        }
      }
      draw_wind_direction_icon(ctx, pos, data->wind_dir_deg, color);
      break;
    }
    case 17: {
      GPoint pos = GPoint(icon_x, box_y + (CORNER_ROW_H - 12) / 2);
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_mountain_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y), icon_outline_color);
        }
      }
      draw_mountain_icon(ctx, pos, color);
      break;
    }
    case 12: {
      GPoint pos = GPoint(icon_x - 15, box_y + (CORNER_ROW_H - 10) / 2);
      BatteryChargeState bs = battery_state_service_peek();
      graphics_context_set_stroke_width(ctx, 1);
      GPoint p1 = GPoint(pos.x + 3, pos.y + 9);
      GPoint p2 = GPoint(pos.x + 3 + (35 * bs.charge_percent / 100), pos.y + 9);
      
      if (bs.is_charging) {
        color = GColorGreen;
        icon_outline_color = GColorBlack;
      }
      
      if (do_icon_outline) {
        for (int i = 0; i < 4; i++) {
          draw_tiny_icon(ctx, GPoint(pos.x + OUTLINE_OFFSETS[i].x, pos.y + OUTLINE_OFFSETS[i].y),
                          PEBBLE_ICON, 10, 40, icon_outline_color);
        }
        graphics_context_set_stroke_color(ctx, icon_outline_color);
        graphics_draw_line(ctx, GPoint(p1.x , p1.y + 1), GPoint(p2.x , p2.y + 1));
        graphics_draw_line(ctx, GPoint(p1.x - 1, p1.y), GPoint(p2.x + 1, p2.y));
        graphics_draw_line(ctx, GPoint(p1.x , p1.y - 1), GPoint(p2.x , p2.y - 1));
      }
      
      draw_tiny_icon(ctx, pos, PEBBLE_ICON, 10, 40, color);
      
      graphics_context_set_stroke_color(ctx, color);
      graphics_draw_line(ctx, p1, p2);
      break;
    }
    default:
      break;
  }

  if (content == 4 && color_mode == 3) {
    // Min/max temperature each get their own gradient color rather
    // than sharing one -- split the remaining box width in half and
    // draw "H.." / "L.." as two independently-colored segments
    // instead of the generic single-color path below.
    int16_t hi = convert_temp(data->temp_high_c, data->temp_unit);
    int16_t lo = convert_temp(data->temp_low_c, data->temp_unit);
    char hi_buf[8], lo_buf[8];
    snprintf(hi_buf, sizeof(hi_buf), "H%d", hi);
    snprintf(lo_buf, sizeof(lo_buf), "L%d", lo);
    GColor hi_color = seven_stop_gradient(data->temp_high_c, -10, 40);
    GColor lo_color = seven_stop_gradient(data->temp_low_c, -10, 40);
    int16_t half_w = (CORNER_BOX_W - (text_x - box_x)) / 2;
    draw_text_outlined(ctx, hi_buf, font,
                        GRect(text_x, box_y + (CORNER_ROW_H - font_h) / 2, half_w, font_h + 2),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                        hi_color, allow_outline && data->outline_enabled);
    draw_text_outlined(ctx, lo_buf, font,
                        GRect(text_x + half_w, box_y + (CORNER_ROW_H - font_h) / 2, half_w, font_h + 2),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                        lo_color, allow_outline && data->outline_enabled);
    return;
  }

  if (content != 17) {
    draw_text_outlined(ctx, buf, font,
                       GRect(text_x, box_y + (CORNER_ROW_H - font_h) / 2, text_size.w + 2, font_h + 2),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft,
                       color, allow_outline && data->outline_enabled);
  }
}


// ---- metadata cache -------------------------------------------------------

// Fixed slot order -- matches the original corners_layer_update_proc's own
// draw order (edge-middles first, then the 4 corners), not that it matters
// for correctness (each slot draws to its own screen position regardless
// of array order), just for readability when stepping through this file.
enum {
  SLOT_UPPER_L1 = 0, SLOT_UPPER_L2,
  SLOT_BOTTOM_L1, SLOT_BOTTOM_L2,
  SLOT_LEFT_L1, SLOT_LEFT_L2,
  SLOT_RIGHT_L1, SLOT_RIGHT_L2,
  SLOT_CORNER_TL, SLOT_CORNER_TR, SLOT_CORNER_BL, SLOT_CORNER_BR,
};

// One fully-resolved feature slot: everything features_draw_item() needs
// to draw it, with zero layout decisions left to make at draw time.
typedef struct {
  bool active;             // false = this slot draws nothing this cycle
  uint8_t content;
  uint8_t color_mode;
  bool is_top;
  bool is_left;
  bool is_middle;
  int16_t top_offset;
  int16_t bottom_shift;
  int16_t middle_inset;    // how far in from the screen edge a middle-left/right slot sits --
                             // defaults to 30 (the fixed inset every non-custom marker style
                             // always used); custom markers (style 8) compute this dynamically
                             // instead, from where the ring's own inner boundary actually lands
                             // at 3/9 o'clock -- see features_recompute_slots()'s own comment.
  bool center_horizontal;
  bool center_vertical;
  bool allow_outline;
} FeatureSlot;

typedef struct {
  EclipseData *data;
  bool labels_visible;
  FeatureSlot slots[FEATURES_MAX_SLOTS];
} FeaturesState;

// Re-derives every slot's active/position fields from the current
// settings (state->data) and shake-label state (state->labels_visible).
// This is the layout logic corners_layer_update_proc used to redo on
// every single redraw; it now runs only from features_layer_set_data()/
// features_layer_set_labels_visible(), i.e. on an actual settings change
// or shake event, not every tick/heartbeat-refresh.
static void features_recompute_slots(FeaturesState *state) {
  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) state->slots[i].active = false;

  EclipseData *d = state->data;
  if (!d) return;

  bool is_big_analog = d->bottom_style == 2;
  uint8_t marker_style = d->big_analog_marker_style;
  bool is_bitmap_style = is_big_analog && marker_style >= 3 && marker_style != 8 && marker_style != 9;

  // Which of the 4 edge-middle slots (upper/bottom/left/right-middle) does
  // the current mode/style actually support? Digital/analog modes use
  // none of them. Big-analogue procedural styles (<3), custom (8), and
  // none (9, no marker ring drawn at all) have no artwork to work around,
  // so all 4 are available alongside the corners. Big-analogue bitmap
  // styles (3-7) are limited to whichever slots that specific mask
  // graphic's design has room for, and always suppress the 4 corners (the
  // mask already fills most of the screen either way).
  bool show_upper = false, show_bottom = false, show_left = false, show_right = false;
  if (is_big_analog) {
    if (marker_style < 3 || marker_style == 8 || marker_style == 9) {
      show_upper = show_bottom = show_left = show_right = true;
    } else {
      switch (marker_style) {
        case 3: case 4: case 6: // Modern, Shadow, Bell -- only top middle and bottom middle
          show_upper = show_bottom = true;
          break;
        case 5: case 7: // tally, brown -- all inside
          show_upper = show_bottom = show_left = show_right = true;
          break;
        default: // swiss, bell -- only the top has clean space
          show_upper = true;
          break;
      }
    }
  }

  // Custom markers (style 8) can have their inner ring boundary sit
  // anywhere at all -- unlike the procedural presets (which never
  // reach far enough in to threaten the middle-edge slots' normal
  // fixed position), a custom ring's own inner_border_pct might be set
  // large enough to actually overlap where upper/bottom/left/right-
  // middle content would otherwise sit. Rather than the fixed 44/40/30
  // margins every other style uses, compute where the ring's own inner
  // edge actually lands at each of the 4 cardinal marks (12/3/6/9
  // o'clock -- same point_on_ring() the ring itself is drawn with, see
  // its own comment in background_layer.c for why it's exposed here),
  // and use whichever of "the normal fixed margin" or "just past the
  // ring's own inner edge" is larger -- so a small/central ring still
  // leaves everything at its normal position, and only a ring that
  // genuinely reaches into that space pushes the affected slot(s)
  // further in to clear it.
  int16_t dyn_upper_offset = 44, dyn_bottom_shift = 40, dyn_middle_inset = 30;
  if (marker_style == 8) {
    GRect screen = GRect(0, 0, 200, 228); // full, unshrunk screen -- same dimensions full_bounds
                                            // itself always is (this app targets emery only), which
                                            // is what the marker ring is actually drawn against
    GPoint center = GPoint(screen.size.w / 2, screen.size.h / 2);
    uint8_t pct = d->custom_hour_marker.inner_border_pct;
    uint8_t ecc = d->custom_hour_marker.inner_eccentricity;
    GPoint top_pt = point_on_ring(center, screen, 0, pct, ecc);
    GPoint right_pt = point_on_ring(center, screen, TRIG_MAX_ANGLE / 4, pct, ecc);
    GPoint bottom_pt = point_on_ring(center, screen, TRIG_MAX_ANGLE / 2, pct, ecc);
    GPoint left_pt = point_on_ring(center, screen, (TRIG_MAX_ANGLE * 3) / 4, pct, ecc);
    int16_t margin = 4; // a few px of breathing room past the ring's own inner edge, not flush against it
    if (top_pt.y + margin > dyn_upper_offset) dyn_upper_offset = top_pt.y + margin;
    if (screen.size.h - bottom_pt.y + margin > dyn_bottom_shift) dyn_bottom_shift = screen.size.h - bottom_pt.y + margin;
    int16_t left_inset = left_pt.x + margin;
    int16_t right_inset = screen.size.w - right_pt.x + margin;
    int16_t dyn_side_inset = (left_inset > right_inset) ? left_inset : right_inset; // one shared inset for both sides, same as the fixed-30 default already was
    if (dyn_side_inset > dyn_middle_inset) dyn_middle_inset = dyn_side_inset;
  }

  // Upper-middle sits further down (34px, was 4px) than the old
  // single-slot version -- too close to the top edge on the modern mask's
  // actual artwork. Bottom-middle mirrors that same ~30px margin up from
  // the bottom edge. Each is now a 2-line pair: when line 2 has no
  // content, line 1 shifts toward the vertical center of where the pair
  // would have sat, rather than staying pinned at the "top line of two"
  // position with an empty gap below/above it.
  if (show_upper) {
    bool has_line2 = d->upper_middle_line2_content != 0;
    int16_t line1_offset = has_line2 ? dyn_upper_offset : dyn_upper_offset + CORNER_ROW_H / 2;
    state->slots[SLOT_UPPER_L1] = (FeatureSlot){
      .active = true, .content = d->upper_middle_line1_content, .color_mode = d->upper_middle_line1_color_mode,
      .is_top = true, .is_left = true, .is_middle = false,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
    };
    if (has_line2) {
      state->slots[SLOT_UPPER_L2] = (FeatureSlot){
        .active = true, .content = d->upper_middle_line2_content, .color_mode = d->upper_middle_line2_color_mode,
        .is_top = true, .is_left = true, .is_middle = false,
        .top_offset = dyn_upper_offset + CORNER_ROW_H, .bottom_shift = 0, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      };
    }
  }
  if (show_bottom) {
    bool has_line2 = d->bottom_middle_line2_content != 0;
    int16_t line1_shift = has_line2 ? dyn_bottom_shift + CORNER_ROW_H : dyn_bottom_shift + CORNER_ROW_H / 2;
    state->slots[SLOT_BOTTOM_L1] = (FeatureSlot){
      .active = true, .content = d->bottom_middle_line1_content, .color_mode = d->bottom_middle_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = false,
      .top_offset = 0, .bottom_shift = line1_shift, .middle_inset = 0,
      .center_horizontal = true, .center_vertical = false, .allow_outline = true,
    };
    if (has_line2) {
      state->slots[SLOT_BOTTOM_L2] = (FeatureSlot){
        .active = true, .content = d->bottom_middle_line2_content, .color_mode = d->bottom_middle_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = false,
        .top_offset = 0, .bottom_shift = dyn_bottom_shift, .middle_inset = 0,
        .center_horizontal = true, .center_vertical = false, .allow_outline = true,
      };
    }
  }
  if (show_left) {
    bool has_line2 = d->middle_left_line2_content != 0;
    int16_t line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_LEFT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_left_line1_content, .color_mode = d->middle_left_line1_color_mode,
      .is_top = false, .is_left = true, .is_middle = true,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_middle_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
    };
    if (has_line2) {
      state->slots[SLOT_LEFT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_left_line2_content, .color_mode = d->middle_left_line2_color_mode,
        .is_top = false, .is_left = true, .is_middle = true,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_middle_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      };
    }
  }
  if (show_right) {
    bool has_line2 = d->middle_right_line2_content != 0;
    int16_t line1_offset = has_line2 ? -(CORNER_ROW_H / 2) : 0;
    state->slots[SLOT_RIGHT_L1] = (FeatureSlot){
      .active = true, .content = d->middle_right_line1_content, .color_mode = d->middle_right_line1_color_mode,
      .is_top = false, .is_left = false, .is_middle = true,
      .top_offset = line1_offset, .bottom_shift = 0, .middle_inset = dyn_middle_inset,
      .center_horizontal = false, .center_vertical = true, .allow_outline = true,
    };
    if (has_line2) {
      state->slots[SLOT_RIGHT_L2] = (FeatureSlot){
        .active = true, .content = d->middle_right_line2_content, .color_mode = d->middle_right_line2_color_mode,
        .is_top = false, .is_left = false, .is_middle = true,
        .top_offset = CORNER_ROW_H / 2, .bottom_shift = 0, .middle_inset = dyn_middle_inset,
        .center_horizontal = false, .center_vertical = true, .allow_outline = true,
      };
    }
  }

  if (is_bitmap_style) return; // corners fully replaced by the slots above

  // Bottom corners shift up out of the way of the "Clouds/visibility/
  // location" bar (background_layer.c's canvas_update_proc) whenever that
  // bar is actually going to be drawn -- which depends on
  // bottom_info_bar_mode, not just whether a shake is currently active:
  // Off (0) never draws it (never shift), Permanent (2) always draws it
  // (always shift), and On shake (1) draws it only while labels_visible
  // is true (shift only then). Also itself suppressed in analog mode
  // (bottom_style == 1), where the bar is redundant with the persistent
  // info panel and never drawn.
  bool bar_will_draw = (d->bottom_info_bar_mode == 2) ||
                        (d->bottom_info_bar_mode == 1 && state->labels_visible);
  int16_t bottom_shift = (bar_will_draw && d->bottom_style != 1) ? 18 : 0;

  state->slots[SLOT_CORNER_TL] = (FeatureSlot){
    .active = true, .content = d->corner_content[0], .color_mode = d->corner_color_mode[0],
    .is_top = true, .is_left = true, .is_middle = false,
    .top_offset = 2, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
  };
  state->slots[SLOT_CORNER_TR] = (FeatureSlot){
    .active = true, .content = d->corner_content[1], .color_mode = d->corner_color_mode[1],
    .is_top = true, .is_left = false, .is_middle = false,
    .top_offset = 2, .bottom_shift = 0,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
  };
  state->slots[SLOT_CORNER_BL] = (FeatureSlot){
    .active = true, .content = d->corner_content[2], .color_mode = d->corner_color_mode[2],
    .is_top = false, .is_left = true, .is_middle = false,
    .top_offset = 0, .bottom_shift = bottom_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
  };
  state->slots[SLOT_CORNER_BR] = (FeatureSlot){
    .active = true, .content = d->corner_content[3], .color_mode = d->corner_color_mode[3],
    .is_top = false, .is_left = false, .is_middle = false,
    .top_offset = 0, .bottom_shift = bottom_shift,
    .center_horizontal = false, .center_vertical = false, .allow_outline = true,
  };
}

// ---- layer lifecycle --------------------------------------------------

// The always-on-top feature overlay. Deliberately a separate layer with
// its own independent refresh timer (see main.c's corners_timer_callback)
// rather than being drawn as part of the sky canvas or tied to its redraw
// cycle -- per the original brief, updating this (e.g. for a fresh heart
// rate reading) should never force the much more expensive sky canvas to
// redraw too. Like the hands layer, never fills its own background, so
// whatever's underneath (sky canvas, and in big-analog mode possibly the
// hands too -- see "draw features beneath hands") shows through
// everywhere except where content is actually drawn.
static void features_layer_update_proc(Layer *layer, GContext *ctx) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  if (!state->data) return;

  GRect bounds = layer_get_unobstructed_bounds(layer);
  time_t now = time(NULL);
  GColor bg, main_color, accent_color;
  get_active_color_scheme(state->data, now, &bg, &main_color, &accent_color);
  ensure_corner_custom_font(state->data->corner_font);

  for (int i = 0; i < FEATURES_MAX_SLOTS; i++) {
    FeatureSlot *s = &state->slots[i];
    if (!s->active) continue;
    features_draw_item(ctx, bounds, state->data, s->content, s->color_mode,
                        main_color, accent_color, bg,
                        s->is_top, s->is_left, s->is_middle, s->top_offset, s->bottom_shift,
                        s->middle_inset,
                        s->center_horizontal, s->center_vertical, s->allow_outline);
  }
}

Layer *features_layer_create(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(FeaturesState));
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  state->data = NULL;
  state->labels_visible = false;
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
  features_recompute_slots(state);
  layer_mark_dirty(layer);
}

void features_layer_set_labels_visible(Layer *layer, bool visible) {
  FeaturesState *state = (FeaturesState *)layer_get_data(layer);
  state->labels_visible = visible;
  features_recompute_slots(state);
  layer_mark_dirty(layer);
}
