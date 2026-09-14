#include "marker_layer.h"
#include "marker_bitmap.h"
#include <string.h>
#include <limits.h>

// ---------------------------------------------------------------------------
// Marker renderer
//
// This module owns all big-analog marker geometry, text numerals, and marker
// reveal animation. Bitmap resources are delegated to marker_bitmap. The
// background canvas only
// supplies the drawing context, current data, geometry, colors, and animation
// progress.
// ---------------------------------------------------------------------------


static inline int32_t marker_div_round(int32_t num, int32_t den) {
  if (den == 0) return 0;
  if ((num ^ den) >= 0) return (num + den / 2) / den;
  return (num - den / 2) / den;
}

static uint8_t marker_lerp8(uint8_t a, uint8_t b, int32_t num, int32_t den) {
  if (den == 0) return a;
  return (uint8_t)(a + ((int32_t)(b - a) * num) / den);
}

static int16_t marker_min(int16_t a, int16_t b) { return a < b ? a : b; }
static int16_t marker_max(int16_t a, int16_t b) { return a > b ? a : b; }

// Converts a 0-100% "reach" into an actual distance from center, in the
// same Q24.8 fixed-point units marker_layer_point_on_ring_fp() below works in
// throughout -- this is what actually keeps everything on-screen: 0% =
// the largest circle that's guaranteed to stay fully within the screen
// at every angle (the shorter half-dimension), 100% = the far edge the
// screen-fitted rectangle reaches along its dominant axis (the longer
// half-dimension). Returns fixed-point directly (rather than rounding
// to a whole pixel first and re-promoting that) so the sub-pixel
// precision survives into the rest of marker_layer_point_on_ring_fp()'s math.
static int32_t marker_reach_fp(GRect screen, uint8_t pct) {
  int16_t reach_min = marker_min(screen.size.w / 4, screen.size.h / 4);
  int16_t reach_max = marker_max(screen.size.w / 2, screen.size.h / 2);
  int32_t reach_min_fp = (int32_t)reach_min << SUBPIXEL_BITS;
  int32_t reach_max_fp = (int32_t)reach_max << SUBPIXEL_BITS;
  return reach_min_fp + marker_div_round((reach_max_fp - reach_min_fp) * pct, 100);
}

// Blends a point on a circle of radius marker_reach_fp(pct) with a point
// on a screen-proportioned rectangle sized so its longer half-extent
// equals that same reach, by eccentricity_pct (0=circle, 100=rectangle)
// -- same sub-pixel fixed-point system (subpixel.h) hand_layer.c's
// compute_hand_geometry_fp() builds hand shapes in, rather than rounding
// each mark's endpoint to a whole pixel before working out its
// thickness. Markers rotate around the dial exactly like hands do, so
// they get the same precision now instead of a coarser plain-integer
// version of the same math. sin_v/cos_v are passed in (rather than an
// angle) since draw_marker_ring() below already looks them up once per
// mark and reuses them for the mark's thickness offset too.
static FGPoint marker_layer_point_on_ring_fp(FGPoint center, GRect screen, int32_t sin_v, int32_t cos_v,
                                 uint8_t pct, uint8_t eccentricity_pct) {
  int16_t screen_hw = screen.size.w / 2, screen_hh = screen.size.h / 2;
  int32_t reach_fp = marker_reach_fp(screen, pct);

  FGPoint circle_pt = fgpoint_new(
    center.x + (int32_t)(((int64_t)reach_fp * sin_v) / TRIG_MAX_RATIO),
    center.y - (int32_t)(((int64_t)reach_fp * cos_v) / TRIG_MAX_RATIO));

  if (eccentricity_pct == 0) return circle_pt;

  int16_t reach_max = marker_max(screen_hw, screen_hh);
  int32_t rect_hw_fp = (int32_t)(((int64_t)reach_fp * screen_hw) / reach_max);
  int32_t rect_hh_fp = (int32_t)(((int64_t)reach_fp * screen_hh) / reach_max);

  int32_t adx = sin_v < 0 ? -sin_v : sin_v;
  int32_t ady = cos_v < 0 ? -cos_v : cos_v;
  int32_t t_x = (adx == 0) ? INT32_MAX : (int32_t)(((int64_t)rect_hw_fp * TRIG_MAX_RATIO) / adx);
  int32_t t_y = (ady == 0) ? INT32_MAX : (int32_t)(((int64_t)rect_hh_fp * TRIG_MAX_RATIO) / ady);
  int32_t t = t_x < t_y ? t_x : t_y;

  FGPoint rect_pt = fgpoint_new(
    center.x + (int32_t)(((int64_t)t * sin_v) / TRIG_MAX_RATIO),
    center.y - (int32_t)(((int64_t)t * cos_v) / TRIG_MAX_RATIO));

  FGPoint result;
  result.x = circle_pt.x + (int32_t)(((int64_t)(rect_pt.x - circle_pt.x) * eccentricity_pct) / 100);
  result.y = circle_pt.y + (int32_t)(((int64_t)(rect_pt.y - circle_pt.y) * eccentricity_pct) / 100);
  return result;
}

// Thin GPoint-returning wrapper for draw_text_markers() below, which
// only ever needs a final whole-pixel position for a numeral -- looks
// up sin/cos from `angle` itself since it doesn't already have them
// the way draw_marker_ring() does. Not static -- also called from
// features_layer.c to find where the custom marker ring's own inner
// boundary sits at the 4 cardinal points, so the middle-edge feature
// slots can shift inside it -- see that file's own comment near
// features_recompute_slots() (or wherever the slot layout actually
// gets built) for how.
GPoint marker_layer_point_on_ring(GPoint center, GRect screen, int32_t angle,
                      uint8_t pct, uint8_t eccentricity_pct) {
  int32_t norm_angle = angle & 0xFFFF; // mask to prevent trig table lookup overflow/underflow
  int32_t sin_v = sin_lookup(norm_angle), cos_v = cos_lookup(norm_angle);
  FGPoint center_fp = fgpoint_from_gpoint(center);
  return fgpoint_to_gpoint(marker_layer_point_on_ring_fp(center_fp, screen, sin_v, cos_v, pct, eccentricity_pct));
}

// Draws one mark as a straight quad from inner to outer, with the
// requested cap style. inner_half_thick_fp/outer_half_thick_fp are
// independent (both simply equal for every style except 4, "tapered",
// which is what actually turns this into a trapezoid instead of a
// uniform-width quad) -- built directly from sin_v/cos_v (the same
// radial direction marker_layer_point_on_ring_fp() placed inner/outer along) rather
// than re-deriving a direction from the two points via vector
// subtraction, exactly mirroring how compute_hand_geometry_fp() in
// hand_layer.c builds a hand's own dot/square body from its own angle.
// style: 0=dot (round caps, via filled circles at both ends, each at
// its own end's thickness), 1=line (flush/butt ends), 2=square (ends
// extended outward by that end's own half-thickness, like SVG's
// stroke-linecap:square), 4=tapered (same square-style extension as 2,
// just per-end so an asymmetric taper still gets flush-looking ends
// rather than a butt cut at an angle). translucent switches every
// fill/stroke in here to subpixel.h's dithered variants.
static void draw_ring_mark_fp(GContext *ctx, FGPoint inner, FGPoint outer, int32_t sin_v, int32_t cos_v,
                               int32_t inner_half_thick_fp, int32_t outer_half_thick_fp,
                               uint8_t style, GColor color, bool translucent,
                               uint8_t inner_thickness_px, uint8_t outer_thickness_px) {
  // See subpixel.h's own comment on fill_polygon_thin_fp() for why:
  // either end being genuinely thin is enough to want the supersampled
  // path, even if the other end (a tapered mark's wide side) is not --
  // fill_polygon_thin_fp() itself works fine on any convex polygon, not
  // just uniform-width ones, so there's no extra cost to reusing it
  // whole-shape rather than only over the thin end.
  bool thin = inner_thickness_px < 3 || outer_thickness_px < 3;
  if (inner.x == outer.x && inner.y == outer.y) {
    // Degenerate zero-length mark (inner/outer border reach configured
    // equal) -- no direction to build a quad from, so just draw a dot
    // at that single point regardless of style, same fallback the
    // pre-fixed-point version of this code used. Sized off the outer
    // (end) thickness, same single value this fallback always used
    // before inner/outer could differ.
    if (thin && !translucent) fill_circle_thin_fp(ctx, inner, outer_half_thick_fp, color);
    else fill_circle_fp(ctx, inner, outer_half_thick_fp, color, translucent);
    return;
  }

  int32_t inner_dx_w = (int32_t)(((int64_t)inner_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
  int32_t inner_dy_w = (int32_t)(((int64_t)inner_half_thick_fp * sin_v) / TRIG_MAX_RATIO);
  int32_t outer_dx_w = (int32_t)(((int64_t)outer_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
  int32_t outer_dy_w = (int32_t)(((int64_t)outer_half_thick_fp * sin_v) / TRIG_MAX_RATIO);

  FGPoint a = inner, b = outer;
  if (style == 2 || style == 4) { // square/tapered caps -- extend each end along the same
                                    // radial direction outer sits on, by that end's own thickness
    int32_t inner_ex = (int32_t)(((int64_t)inner_half_thick_fp * sin_v) / TRIG_MAX_RATIO);
    int32_t inner_ey = (int32_t)(((int64_t)inner_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
    int32_t outer_ex = (int32_t)(((int64_t)outer_half_thick_fp * sin_v) / TRIG_MAX_RATIO);
    int32_t outer_ey = (int32_t)(((int64_t)outer_half_thick_fp * cos_v) / TRIG_MAX_RATIO);
    a = fgpoint_new(inner.x - inner_ex, inner.y + inner_ey);
    b = fgpoint_new(outer.x + outer_ex, outer.y - outer_ey);
  }

  FGPoint points[4] = {
    fgpoint_new(a.x - inner_dx_w, a.y - inner_dy_w), fgpoint_new(a.x + inner_dx_w, a.y + inner_dy_w),
    fgpoint_new(b.x + outer_dx_w, b.y + outer_dy_w), fgpoint_new(b.x - outer_dx_w, b.y - outer_dy_w),
  };

  if (translucent) {
    fill_polygon_dithered_fp(ctx, points, 4, color);
  } else if (thin) {
    fill_polygon_thin_fp(ctx, points, 4, color);
  } else {
    fill_polygon_fp(ctx, points, 4, color);
  }

  if (style == 0) { // dot caps -- each end's own circle, at that end's own thickness
    if (thin && !translucent) {
      fill_circle_thin_fp(ctx, inner, inner_half_thick_fp, color);
      fill_circle_thin_fp(ctx, outer, outer_half_thick_fp, color);
    } else {
      fill_circle_fp(ctx, inner, inner_half_thick_fp, color, translucent);
      fill_circle_fp(ctx, outer, outer_half_thick_fp, color, translucent);
    }
  }
}

// Resolves a MarkerRingConfig's own color choice against the active
// scheme -- same 3-way (main/accent/background) selection used all
// over this app, just not through hand_layer.c's own private
// resolve_scheme_color() (file-local there, and this is the only
// place background_layer.c needs the same lookup).
static GColor marker_ring_color(uint8_t choice, GColor main_color, GColor accent_color, GColor bg_color) {
  switch (choice) {
    case 1: return accent_color;
    case 2: return bg_color;
    case 0: default: return main_color;
  }
}

// "Animate background on start": each mark staggers in starting from
// the fully-reached, fully-eccentric extreme (100%/100 -- effectively
// off the visible screen, or at least hard up against its very edge --
// see MarkerRingConfig's own inner/outer_border_pct/eccentricity
// comments in eclipse_data.h for why those particular values count as
// "off screen") down to its real configured position, beginning with
// the 12-o'clock mark and sweeping clockwise through the rest -- mark
// i's own animation starts MARKER_ANIM_STAGGER_1000*i/marks of the way
// into the overall duration and has whatever's left of it to finish,
// so every mark still lands exactly on its real position by the time
// the overall progress reaches 1.0 regardless of how late it started.
#define MARKER_ANIM_STAGGER_1000 600 // marks' own starts spread across the first 60% of the duration

static int32_t marker_ease_out_1000(int32_t t) {
  int32_t inv = 1000 - t;
  int64_t inv3 = ((int64_t)inv * inv * inv) / 1000000;
  int32_t r = 1000 - (int32_t)inv3;
  return (r > 1000) ? 1000 : r;
}

static int32_t marker_ease_in_1000(int32_t t) {
  int64_t t64 = t;
  int32_t r = (int32_t)((t64 * t64 * t64) / 1000000);
  return (r > 1000) ? 1000 : r;
}

static int32_t marker_anim_mark_progress_1000_raw(int mark_index, int marks, int32_t overall_progress_1000) {
  int32_t start_frac = ((int32_t)mark_index * MARKER_ANIM_STAGGER_1000) / marks;
  int32_t duration_frac = 1000 - start_frac;
  if (duration_frac <= 0) return 1000;
  int32_t local = ((overall_progress_1000 - start_frac) * 1000) / duration_frac;
  if (local < 0) local = 0;
  if (local > 1000) local = 1000;
  return local;
}

static void draw_marker_ring(GContext *ctx, GPoint center, GRect screen, const MarkerRingConfig *cfg,
                              int marks, int skip_step, GColor main_color, GColor accent_color, GColor bg_color,
                              bool anim_active, int32_t anim_overall_progress_1000, uint8_t inner_thickness) {
  if (cfg->thickness == 0) return;
  GColor color = marker_ring_color(cfg->color, main_color, accent_color, bg_color);
  uint8_t inner_pct = cfg->inner_border_pct, outer_pct = cfg->outer_border_pct;
  if (outer_pct < inner_pct) outer_pct = inner_pct;

  // Same 0.5px-minimum floor compute_hand_geometry_fp() uses for a
  // hand's half-width -- without it, a thickness of 1 (half_thick_fp
  // rounding down to 0) would collapse the mark's quad to zero area at
  // every angle except the four cardinal ones, same "second hand only
  // draws at right angles" bug subpixel_round_div()'s comment in subpixel.h
  // describes.
  int32_t outer_half_thick_fp = ((int32_t)cfg->thickness << SUBPIXEL_BITS) / 2;
  if (outer_half_thick_fp < SUBPIXEL_HALF) outer_half_thick_fp = SUBPIXEL_HALF;

  // Only style 4 (tapered) actually uses inner_thickness -- every other
  // style keeps inner and outer at the same width, exactly like before
  // this value existed. inner_thickness itself floors the same way
  // cfg->thickness does just above (and defaults to 0, i.e. "not yet
  // sent by the phone" -- see EclipseData's own comment on it), so an
  // unset value quietly reads as the sharpest possible taper (~1px)
  // rather than needing its own special case.
  int32_t inner_half_thick_fp = outer_half_thick_fp;
  uint8_t inner_thickness_px = cfg->thickness;
  if (cfg->style == 4) {
    inner_half_thick_fp = ((int32_t)inner_thickness << SUBPIXEL_BITS) / 2;
    if (inner_half_thick_fp < SUBPIXEL_HALF) inner_half_thick_fp = SUBPIXEL_HALF;
    inner_thickness_px = inner_thickness;
  }

  FGPoint center_fp = fgpoint_from_gpoint(center);

  for (int i = 0; i < marks; i++) {
    if (skip_step > 0 && i % skip_step == 0) continue;

    // Strictly mask with 0xFFFF to fix missing rotated markers
    int32_t angle = (((int32_t)i * TRIG_MAX_ANGLE) / marks) & 0xFFFF;
    int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);

    uint8_t use_inner_pct = inner_pct, use_outer_pct = outer_pct;
    uint8_t use_inner_ecc = cfg->inner_eccentricity, use_outer_ecc = cfg->outer_eccentricity;
    if (anim_active) {
      int32_t p = marker_ease_out_1000(marker_anim_mark_progress_1000_raw(i, marks, anim_overall_progress_1000));
      use_inner_pct = (uint8_t)marker_lerp8(100, inner_pct, p, 1000);
      use_outer_pct = (uint8_t)marker_lerp8(100, outer_pct, p, 1000);
      use_inner_ecc = (uint8_t)marker_lerp8(100, cfg->inner_eccentricity, p, 1000);
      use_outer_ecc = (uint8_t)marker_lerp8(100, cfg->outer_eccentricity, p, 1000);
    }

    FGPoint outer_fp = marker_layer_point_on_ring_fp(center_fp, screen, sin_v, cos_v, use_outer_pct, use_outer_ecc);
    FGPoint inner_fp = marker_layer_point_on_ring_fp(center_fp, screen, sin_v, cos_v, use_inner_pct, use_inner_ecc);
    draw_ring_mark_fp(ctx, inner_fp, outer_fp, sin_v, cos_v, inner_half_thick_fp, outer_half_thick_fp,
                       cfg->style, color, cfg->translucent, inner_thickness_px, cfg->thickness);
  }
}

// Hardcoded MarkerRingConfig pairs recreating the 3 non-bitmap,
// non-custom marker styles (big_analog_marker_style 0/1/2 -- minimal/
// small/big) through this same shared rasterizer, rather than each
// keeping its own separate procedural-drawing code. Approximated (not
// pixel-identical to the old formula-driven version, which varied hour
// mark length every 3rd hour and positioned everything relative to the
// screen radius directly) -- a deliberate simplification, tuned to look
// reasonably close within the 0-100% reach range every style now shares.
static const MarkerRingConfig MARKER_STYLE_HOUR_PRESETS[3] = {
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 0: minimal
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 60, .outer_border_pct = 85 }, // 1: small
  { .style = 2, .thickness = 5, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 60, .outer_border_pct = 85 }, // 2: big
};
static const MarkerRingConfig MARKER_STYLE_SECOND_PRESETS[3] = {
  { .style = 1, .thickness = 0, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 0: minimal -- thickness 0 = off, matches "hour markers only"
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 1: small
  { .style = 1, .thickness = 1, .inner_eccentricity = 0, .outer_eccentricity = 0, .inner_border_pct = 65, .outer_border_pct = 85 }, // 2: big
};

void marker_layer_inner_reach(uint8_t marker_style, uint8_t *out_pct, uint8_t *out_eccentricity) {
  if (marker_style > 2) { // "none" (9), or any other non-procedural style a caller shouldn't be asking about
    *out_pct = 100; // fully retracted -- never the tighter reach against a caller's own floor margin
    *out_eccentricity = 0;
    return;
  }
  const MarkerRingConfig *hour = &MARKER_STYLE_HOUR_PRESETS[marker_style];
  const MarkerRingConfig *sec = &MARKER_STYLE_SECOND_PRESETS[marker_style];
  // Whichever ring reaches CLOSER to center (the smaller inner_border_pct)
  // is the one that actually constrains the inner empty area -- a
  // thickness-0 ring (style 0's second ring) still has a border_pct
  // set, but draws nothing, so it's excluded from the comparison.
  if (sec->thickness == 0 || hour->inner_border_pct <= sec->inner_border_pct) {
    *out_pct = hour->inner_border_pct;
    *out_eccentricity = hour->inner_eccentricity;
  } else {
    *out_pct = sec->inner_border_pct;
    *out_eccentricity = sec->inner_eccentricity;
  }
}

// Marker text's own font is resolved via font_lookup_resolve()
// (state->text_font_slot) directly at each call site now --
// see font_lookup.c for the shared table every font-selecting system
// in this app draws from.

// Converts 1-59 (our only actual range: hour labels 1-12, second labels
// 0/5.../55) to a Roman numeral string. 0 has no traditional Roman
// numeral -- shown as "0" rather than an empty label, since a blank
// marker would look like a rendering bug rather than a deliberate choice.
static void int_to_roman(int num, char *buf, size_t buf_size) {
  if (num <= 0) { snprintf(buf, buf_size, "%d", num); return; }
  static const int VALUES[] = {50, 40, 10, 9, 5, 4, 1};
  static const char *SYMBOLS[] = {"L", "XL", "X", "IX", "V", "IV", "I"};
  size_t pos = 0;
  buf[0] = '\0';
  for (int i = 0; i < 7 && num > 0; i++) {
    while (num >= VALUES[i]) {
      size_t len = strlen(SYMBOLS[i]);
      if (pos + len + 1 > buf_size) return; // out of room -- truncate rather than overflow
      memcpy(buf + pos, SYMBOLS[i], len);
      pos += len;
      buf[pos] = '\0';
      num -= VALUES[i];
    }
  }
}

static void draw_text_markers(GContext *ctx, GPoint center, GRect screen, MarkerLayerState *state,
                               const MarkerTextConfig *text_cfg, const MarkerRingConfig *hour_cfg,
                               const MarkerRingConfig *second_cfg, GColor color,
                               bool anim_active, int32_t anim_overall_progress_1000,
                               bool draw_debug) {
  if (text_cfg->target == 0) return;
  bool is_hour = (text_cfg->target == 1);
  const MarkerRingConfig *ring = is_hour ? hour_cfg : second_cfg;
  uint16_t mask = is_hour ? text_cfg->hour_mask : text_cfg->second_mask;
  if (mask == 0) return;
  // Same "hour only" scoping as marker_layer_draw()'s own two ring
  // calls: text markers sitting on the second ring's positions don't
  // animate in either, regardless of what the caller passed in.
  if (!is_hour) { anim_active = false; anim_overall_progress_1000 = 0; }

  GFont font = font_lookup_resolve(&state->text_font_slot, text_cfg->font_choice);
  int16_t fh = font_lookup_height(text_cfg->font_choice) + font_lookup_y_offset(text_cfg->font_choice);
  // Sized to the font itself rather than a flat pixel count -- this
  // used to be a fixed 30px, wide enough for a 2-digit number in one
  // of the ~14-20px system/small fonts every marker text font used to
  // be. Now that the corner/edge-style big custom display fonts (up
  // to ~48px tall, e.g. Digital Dream/Minecrafter/Bebas Big) are
  // selectable here too, that flat 30px wasn't even wide enough for a
  // single glyph at that size, let alone a 2-3 character mark like
  // "12" or a Roman numeral ("XII") -- text overflowing its own draw
  // box like that is what was showing up as a trailing "…" instead of
  // the actual mark. 2x the font's own height comfortably fits the
  // widest label this ring ever draws (a 2-digit number or a short
  // Roman numeral) at any font size, small or big alike.
  int16_t box_w = fh * 2 + 8, box_h = fh + 6;

  graphics_context_set_text_color(ctx, color);

  for (int i = 0; i < 12; i++) {
    if (!(mask & (1 << i))) continue;
    
    int32_t angle = (((int32_t)i * TRIG_MAX_ANGLE) / 12) & 0xFFFF;
    int offset_text_pct = ring->thickness == 0 ? 100 + text_cfg->offset_px : ring->inner_border_pct + text_cfg->offset_px;
    if(offset_text_pct>100) {
      offset_text_pct = 100;
    } else if (offset_text_pct < 0) {
      offset_text_pct = 0;
    }
    GPoint pos = marker_layer_point_on_ring(center, screen, angle, offset_text_pct, ring->thickness == 0 ? 100 : ring->inner_eccentricity);
   
//    int32_t sin_v = sin_lookup(angle), cos_v = cos_lookup(angle);
//    GPoint pos = GPoint(
//      base.x + div_round((int32_t)text_cfg->offset_px * sin_v, TRIG_MAX_RATIO),
//      base.y - div_round((int32_t)text_cfg->offset_px * cos_v, TRIG_MAX_RATIO));

    char buf[8];
    int label = is_hour ? (i == 0 ? 12 : i) : (i * 5);
    // "Animate background on start": each label counts up from 0 to
    // its real value, using the same 12-mark clockwise stagger (mark
    // index i is also the mark's own 12-o'clock-relative position
    // here, same as draw_marker_ring's) but an accelerating curve
    // instead of a decelerating one -- see marker_ease_in_1000's own
    // comment for why.
    if (anim_active) {
      int32_t local = marker_anim_mark_progress_1000_raw(i, 12, anim_overall_progress_1000);
      int32_t eased = marker_ease_in_1000(local);
      label = (int)(((int32_t)label * eased) / 1000);
      // Hour markers never legitimately target 0 (i==0 maps to 12
      // above), so label==0 here only ever means this particular
      // mark's own staggered count-up hasn't actually started yet --
      // skip drawing it at all rather than showing a static "0"
      // placeholder for however long its stagger window hasn't opened.
      if (label == 0) continue;
    }
    if (text_cfg->roman_numerals && label > 0) int_to_roman(label, buf, sizeof(buf));
    else if (text_cfg->roman_numerals) buf[0] = '\0'; // roman numerals have no glyph for 0 -- blank rather than garbage mid-count-up
    else snprintf(buf, sizeof(buf), "%d", label);

    GRect box = GRect(pos.x - box_w / 2, pos.y - box_h / 2, box_w, box_h);
    graphics_draw_text(ctx, buf, font, box, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    
    if (draw_debug){
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_stroke_color(ctx, GColorYellow);
      
      graphics_draw_rect(ctx, box);
      graphics_context_set_stroke_color(ctx, GColorOrange);
      graphics_draw_rect(ctx, GRect(pos.x-15, pos.y, 30, 1));
      graphics_draw_rect(ctx, GRect(pos.x, pos.y-15, 1, 30));
    }
  }
}

// ---- bitmap marker styles (moved in from pebble-eclipse-watch.c) --------

// Draws whichever marker style is active (procedural preset, custom, or
// bitmap) into `ctx`, using `screen`/`center` -- called from
// canvas_update_proc() during a full redraw, before the frame gets
// captured, so this only actually runs on this canvas's own throttled
// cadence (once a minute, or immediately on a forced redraw) rather than
// every tick. analog mode only; callers must gate on d->bottom_style.
void marker_layer_init(MarkerLayerState *state) {
  state->bitmap = NULL;
  state->bitmap_style = 255;
  state->bitmap_tinted = false;
  state->bitmap_tint_color = GColorClear;
  state->bitmap_tint_transparent = false;
  state->text_font_slot = FONT_SLOT_EMPTY;
}

void marker_layer_deinit(MarkerLayerState *state) {
  marker_bitmap_release(&state->bitmap, &state->bitmap_style, &state->bitmap_tinted);
  font_lookup_release(&state->text_font_slot);
}

void marker_layer_draw(GContext *ctx, MarkerLayerState *state, GPoint center, GRect screen,
                              const EclipseData *d, GColor main_color, GColor accent_color, GColor bg_color,
                              bool anim_active, int32_t anim_progress_1000, bool draw_debug) {
  uint8_t marker_style = d->big_analog_marker_style;

  if (marker_style == 9) { // none -- no ring, no bitmap, nothing to draw
    marker_bitmap_ensure(&state->bitmap, &state->bitmap_style, &state->bitmap_tinted,
                         &state->bitmap_tint_color, &state->bitmap_tint_transparent, marker_style); // frees any previously-loaded bitmap
    return;
  }

  bool is_bitmap_style = marker_style >= 3 && marker_style != 8;

  marker_bitmap_ensure(&state->bitmap, &state->bitmap_style, &state->bitmap_tinted,
                         &state->bitmap_tint_color, &state->bitmap_tint_transparent, marker_style);

  if (is_bitmap_style) {
    marker_bitmap_tint(state->bitmap, &state->bitmap_tinted, &state->bitmap_tint_color,
                       &state->bitmap_tint_transparent, main_color, d->bitmap_marker_transparent);
    marker_bitmap_draw(ctx, state->bitmap, screen, anim_active, anim_progress_1000);
    return;
  }

  const MarkerRingConfig *hour_cfg, *second_cfg;
  uint8_t hour_inner_thickness, second_inner_thickness;
  if (marker_style == 8) {
    hour_cfg = &d->custom_hour_marker;
    second_cfg = &d->custom_second_marker;
    hour_inner_thickness = d->custom_hour_marker_inner_thickness;
    second_inner_thickness = d->custom_second_marker_inner_thickness;
  } else {
    uint8_t idx = (marker_style <= 2) ? marker_style : 0;
    hour_cfg = &MARKER_STYLE_HOUR_PRESETS[idx];
    second_cfg = &MARKER_STYLE_SECOND_PRESETS[idx];
    // The 3 procedural presets never use style 4 (tapered), so this
    // value is never actually read for them -- passed through anyway
    // for a uniform call shape rather than a separate no-op overload.
    hour_inner_thickness = hour_cfg->thickness;
    second_inner_thickness = second_cfg->thickness;
  }

  // Second ring first so the hour ring's marks draw on top at shared
  // 12-o'clock-aligned slots (matches the original procedural markers'
  // precedent of hour ticks winning at shared positions). Only the
  // hour ring actually animates in "markers" mode -- the second ring
  // always draws at its real, settled position, never off-screen or
  // mid-sweep, per request (a 60-mark ring animating in on top of a
  // 12-mark one was judged too visually busy). It's still drawn fresh
  // every frame during the animation rather than genuinely cached,
  // though -- a real bitmap cache for it is a larger, separate piece
  // of work (see this project's own notes on the broader background-
  // animation caching architecture) than swapping which args get
  // passed here.
  draw_marker_ring(ctx, center, screen, second_cfg, 60, 5, main_color, accent_color, bg_color, false, 0, second_inner_thickness);
  draw_marker_ring(ctx, center, screen, hour_cfg, 12, 0, main_color, accent_color, bg_color, anim_active, anim_progress_1000, hour_inner_thickness);

  if (marker_style == 8) {
    draw_text_markers(ctx, center, screen, state, &d->marker_text, hour_cfg, second_cfg, main_color, anim_active, anim_progress_1000, draw_debug);
  }
}


