#pragma once

#include <pebble.h>
#include "hand_layer.h"

// One ring's procedural shape -- used twice (hour ring, second ring). Each
// mark is drawn directly BETWEEN its inner and outer border points -- no
// separate "length": the border points themselves are the mark's start
// and end, so eccentricity ("bending" the ring from a circle towards the
// screen-fitted rectangle) directly changes how long each mark is as it
// goes around, same as it changes the ring's overall shape. Drawing lives
// in background_layer.c now (merged with the sky canvas -- see the design
// note at the top of that file); this struct stays here, in the data
// header both background_layer.h and eclipse_data.h (this file) need,
// rather than in a leaf header of its own, to avoid a circular include
// between the two.
typedef struct {
  uint8_t style;          // 0=dot (round caps), 1=line (thin stroke), 2=square (sharp caps)
  uint8_t thickness;      // width of the mark, across the ring, in px.
                           // Hour: 1-20. Second: 1-10 (clamped by the caller/settings UI).
  uint8_t inner_eccentricity; // 0-100: 0 = the inner edge follows a circle, 100 = it follows
                               // the screen-fitted rectangle (see background_layer.c:
                               // point_on_ring()) at the inner_border_pct "reach".
  uint8_t outer_eccentricity; // same, for the outer edge, at outer_border_pct.
  uint8_t inner_border_pct; // 0-100: how far out the inner edge sits. 0% = the largest circle
                              // guaranteed to stay fully on-screen (min(screen_w,screen_h)/2),
                              // 100% = the screen-fitted rectangle's own far edge
                              // (max(screen_w,screen_h)/2, along its dominant axis) -- see
                              // marker_reach_px() in background_layer.c for the exact mapping.
  uint8_t outer_border_pct; // ring's outer reach, same 0-100% scale. Never allowed below
                              // inner_border_pct (enforced defensively again in background_layer.c).
  bool translucent;       // this ring's own ~50% Bayer-dithered stipple (see
                            // fill_polygon_dithered_marker() in background_layer.c) -- only
                            // meaningful for the custom marker style (big_analog_marker_style
                            // == 8), where the hour ring and second ring each get their own
                            // independent checkbox in their own marker-editor popup. The 3
                            // procedural presets (minimal/small/big) never set this and stay
                            // opaque; bitmap marker styles use bitmap_marker_transparent
                            // instead, a single setting rather than per-ring, since a bitmap
                            // mask isn't split into a separate hour/second ring to begin with.
  uint8_t color;           // 0=main, 1=accent, 2=background -- which of the active color
                            // scheme's 3 colors this ring draws in. Independently selectable
                            // per ring (hour vs second), same "each gets its own popup
                            // control" pattern as translucent above. The 3 procedural presets
                            // leave this at its default (0/main), matching how they always
                            // drew before this field existed.
} MarkerRingConfig;

// Text-numeral overlay -- hour and second markers share ONE of these
// (mutually exclusive by design), since drawing both at once was
// explicitly ruled out.
typedef struct {
  uint8_t target;       // 0=off, 1=numerals on the hour ring, 2=numerals on the second ring
                          // (every 5s, drawn at the same 12 angular slots hour numerals use)
  uint8_t font_choice;   // unified font id (see font_lookup.h) -- same table and ids as
                          // clock_font/clock_font_small/corner_font, so e.g. picking "Bebas"
                          // anywhere in the settings page always means the same id here too
  int8_t offset_px;      // -50..50 -- radial nudge of the text away from (positive) or
                          // towards (negative) the line/dot/square marker it's paired with,
                          // so the two can be visually independent instead of overlapping.
  uint16_t hour_mask;    // bit h (0-11) set => draw a numeral at that hour position
  uint16_t second_mask;  // bit i (0-11) set => draw a numeral at second-slot i (= i*5 seconds)
  bool roman_numerals;   // render each label (1-12 for hour, 0/5/10.../55 for second) as a
                           // Roman numeral (I, II, III, ... ) instead of an Arabic digit string
                           // -- independent of font_choice, see int_to_roman() in background_layer.c.
                           // 0 has no traditional Roman numeral, so it's shown as "0" either way.
} MarkerTextConfig;

// How many separation samples we keep for interpolating the sun/moon
// gap smoothly between contact times. Must match SAMPLE_COUNT sent
// from PKJS (see src/pkjs/astro.js MAX_SAMPLES).
#define MAX_SEP_SAMPLES 12

// How many sun-altitude / cloud-cover samples we keep, spanning the
// whole day (roughly hourly). Must match SKY_SAMPLE_COUNT sent from
// PKJS (see src/pkjs/astro.js MAX_SKY_SAMPLES).
#define MAX_SKY_SAMPLES 26

// How many bright named stars space-view sky mode draws. Fixed (not
// sent over AppMessage) -- must match src/pkjs/astro.js's STAR_CATALOG
// length exactly, and STARS[]' own length in background_layer.c (which
// owns each star's display name, in the same order PKJS's catalog is
// in) has to match too.
#define STAR_COUNT 16

// ---------------------------------------------------------------------
// AppMessage chunking
//
// PKJS used to build one giant dictionary (every eclipse/weather/sky/
// settings/features key at once) and send it in a single
// Pebble.sendAppMessage() call. That forced app_message_open() to
// reserve buffers big enough for the *whole* payload, which on Emery
// was needlessly close to the platform max and ate into heap the rest
// of the watchapp could've used.
//
// Instead, PKJS now sends several smaller, purpose-built dictionaries
// per refresh cycle, one at a time (see sendChunk()/pumpSendQueue() in
// index.js), each tagged with MESSAGE_KEY_MESSAGE_TYPE so the C side
// (and anyone reading a packet capture) can tell which subset of keys
// to expect. inbox_received_handler() still just does a dict_find()
// per key it cares about -- that's already tolerant of a partial
// dictionary -- so MESSAGE_TYPE isn't required for correctness, only
// for logging/validation and so a chunk's *shape* is documented in one
// place. Keep this enum's values in sync with the MSG_TYPE object at
// the top of src/pkjs/index.js.
typedef enum {
  MSG_TYPE_STATUS = 0,      // DATA_VALID, ERROR_CODE, LOCATION_NAME
  MSG_TYPE_ECLIPSE = 1,     // contact times, magnitude, separation/mag sample arrays
  MSG_TYPE_WEATHER = 2,     // current conditions: temps, humidity, wind, AQI, ...
  MSG_TYPE_ASTRONOMY = 3,   // sun/moon/planet/star position samples, rise/set times
  MSG_TYPE_SKY_EFFECTS = 4, // aurora, meteor shower, ISS pass
  MSG_TYPE_FEATURES = 5,    // corner/edge content-slot selection + what feeds them
  MSG_TYPE_SETTINGS = 6,    // clock face cosmetics: hands, markers, colors, units, fonts
} MsgType;

// Largest of the chunks above is MSG_TYPE_ASTRONOMY, at roughly:
//   4B dict header + ~23 tuples (7B overhead each) + sample-array
//   payload bytes (5 planets x 26 samples x 2 arrays x 2B, plus
//   sun/moon/star/cloud arrays) -- works out to ~1060 bytes as of the
//   current MAX_SKY_SAMPLES/PLANET_COUNT/STAR_COUNT. APPMSG_INBOX_SIZE
//   below rounds that up with headroom for future fields; if you add
//   samples/planets/stars, re-check this against the actual size
//   (APP_LOG the return value of dict_write_end() from PKJS, or watch
//   for APP_MSG_BUFFER_OVERFLOW in inbox_dropped_handler) and bump it.
//
// Outbox only ever carries the watch's own tiny REQUEST_UPDATE ping
// back to the phone, so it stays small regardless of how big the
// inbound chunks get.
//
// See app_message_open()'s docs for why sizing to the biggest actual
// chunk (not app_message_inbox_size_maximum()) is the point of all
// this: https://developer.rebble.io/docs/c/Foundation/AppMessage/#app_message_open
#define APPMSG_INBOX_SIZE 1200
#define APPMSG_OUTBOX_SIZE 64

// Eclipse phase, derived on-watch from the current time vs the
// contact times we were sent.
typedef enum {
  PHASE_NO_ECLIPSE = 0,   // nothing happening today
  PHASE_BEFORE_C1,        // waiting for first contact
  PHASE_PARTIAL_IN,       // moon encroaching (C1 -> C2 or C1 -> max)
  PHASE_TOTAL,            // totality / annularity (C2 -> C3)
  PHASE_PARTIAL_OUT,      // moon receding (max/C3 -> C4)
  PHASE_DONE,             // C4 has passed, sun is (or should be) whole again
  PHASE_NIGHT              // sun already below horizon before/after eclipse
} EclipsePhase;

typedef enum {
  ECLIPSE_TYPE_NONE = 0,
  ECLIPSE_TYPE_PARTIAL = 1,
  ECLIPSE_TYPE_TOTAL = 2,
  ECLIPSE_TYPE_ANNULAR = 3
} EclipseType;

// Clock font codes handled by apply_clock_font() in
// pebble-eclipse-watch.c: 0=Leco (default), 1-16=custom .ttf/.otf
// resources (see that switch for which), 17=Roboto, 18=Bitham Light,
// 19=Bitham Bold. Kept as a plain uint8_t on EclipseData rather than
// an enum, since the meaningful range doesn't fit a small one.

// Which slot each planet occupies in the arrays below -- keep this in
// sync with PLANET_NAMES in eclipse_layer.c and the concatenation
// order PKJS uses when building PLANET_ALT_SAMPLES/RISE/SET.
typedef enum {
  PLANET_MERCURY = 0,
  PLANET_VENUS,
  PLANET_MARS,
  PLANET_JUPITER,
  PLANET_SATURN,
  PLANET_COUNT
} PlanetId;

typedef struct {
  bool valid;              // has the watch ever received a good payload?
  uint8_t error_code;       // 0=none, 1=no location, 2=calc error, 3=send failed
  bool has_eclipse;         // is there any eclipse today at this location?
  uint8_t clock_font;         // unified font id (see font_lookup.h) for the main clock digits, applies
                                // regardless of data validity
  uint8_t clock_font_small;   // unified font id for the smaller companion readout next to the clock
                                // (seconds digits in digital mode, sunrise/sunset time, the date line) --
                                // a separate id rather than something derived from clock_font, since
                                // PKJS is what decides which small font looks good paired with which
                                // big one (see CLOCK_FONTS_BIG in config-page.js)
  uint8_t temp_unit;          // user setting: 0=Celsius, 1=Fahrenheit, 2=Kelvin
  uint8_t wind_speed_unit;    // user setting: 0=km/h, 1=mph, 2=m/s, 3=knots
  bool show_seconds;          // user setting: show seconds (grayed out in settings for wide fonts)
  bool show_sun_time;          // user setting: replace the week number with the upcoming sunrise/sunset
                                 // time (plus an arrow + horizon-sun icon). Digital mode only -- small-
                                 // analog mode's own week/sun-time readout is now just one of its 4
                                 // user-picked feature rows (content 16 = sunrise/sunset, 19 = week
                                 // number), so this toggle no longer has anything to do there.
  int16_t temp_high_c;        // today's forecast high, whole degrees Celsius
  int16_t temp_low_c;         // today's forecast low, whole degrees Celsius
  uint8_t uv_index_x10;        // today's max UV index, x10 fixed point (e.g. 53 = UV 5.3)
  uint8_t rain_chance_pct;      // today's max precipitation probability, 0-100
  uint8_t humidity_pct;          // current relative humidity, 0-100
  int16_t wind_speed_kmh;        // current wind speed, km/h

  // The four corners overlay (see corners_layer_update_proc in
  // pebble-eclipse-watch.c) -- replaces the old fixed-position
  // temp-range/brief-weather/battery/moon-phase readouts with a fully
  // user-picked set of up to 4 small info items, one per screen
  // corner. Indexed 0=top-left, 1=top-right, 2=bottom-left,
  // 3=bottom-right.
  //
  // corner_content: 0=none, 1=heart rate, 2=steps today, 3=step goal
  // %, 4=high/low temp, 5=current conditions, 6=UV index, 7=rain
  // chance, 8=humidity, 9=wind, 10=battery, 11=Moon phase.
  //
  // corner_color_mode: 0=solid monochrome (main color), 1=solid
  // accent, 2=translucent accent (dithered), 3=dynamic (value-driven
  // gradient/rule, specific to each content type -- see
  // corner_color_for()).
  uint8_t corner_content[4];
  uint8_t corner_color_mode[4];
  uint16_t daily_step_goal;     // user-set target for the "step goal %" corner content --
                                 // Pebble's HealthService has no API to read a system-level
                                 // goal, so (like every other Pebble health app) this app
                                 // keeps its own

  // "Edge-middle" slots -- upper-middle, bottom-middle, middle-left,
  // middle-right -- around the big-analogue clock face. Which of the
  // 4 are actually shown depends on bottom_style/big_analog_marker_style
  // (see corners_layer_update_proc in pebble-eclipse-watch.c):
  // digital/analog modes use none of them; big-analogue procedural
  // marker styles (<3, no artwork constraints) use all 4 alongside
  // the corners; big-analogue bitmap styles (>=3) are limited to
  // whichever slots that specific mask graphic's design actually has
  // room for (upper-middle alone for swiss/bell, upper+left for
  // tally, upper+bottom for modern/brown), and suppress the 4 corners
  // entirely since the mask already fills most of the screen. Same
  // content/color-mode codes as the corners (content 12 = short date).
  // Upper-middle and bottom-middle each hold two independently-chosen
  // lines rather than one -- when line 2 is content 0 (none), only
  // line 1 draws, vertically re-centered within the pair's slot
  // rather than left sitting at the "line 1 of 2" position.
  uint8_t upper_middle_line1_content;
  uint8_t upper_middle_line1_color_mode;
  uint8_t upper_middle_line2_content;
  uint8_t upper_middle_line2_color_mode;
  uint8_t bottom_middle_line1_content;
  uint8_t bottom_middle_line1_color_mode;
  uint8_t bottom_middle_line2_content;
  uint8_t bottom_middle_line2_color_mode;
  uint8_t middle_left_line1_content;
  uint8_t middle_left_line1_color_mode;
  uint8_t middle_left_line2_content;
  uint8_t middle_left_line2_color_mode;
  uint8_t middle_right_line1_content;
  uint8_t middle_right_line1_color_mode;
  uint8_t middle_right_line2_content;
  uint8_t middle_right_line2_color_mode;

  // Colors: raw packed GColor argb bytes (one of the 64 real Pebble
  // display colors), reconstructed on-watch via a GColor union rather
  // than shipped as separate RGB components -- guarantees whatever the
  // user picked is one of the 64 real colors, not an arbitrary (and
  // possibly unsupported) RGB triple. The named presets shown in the
  // settings page are a phone-side-only convenience (see COLOR_SCHEMES
  // in config-page.js) -- picking one just fills these three fields in
  // with that preset's colors before sending, so the watch only ever
  // sees concrete colors and has no concept of "preset" at all.
  uint8_t custom_bg;
  uint8_t custom_text;
  uint8_t custom_accent;

  // Optional separate colors applied between sunset and sunrise, same
  // encoding as the day colors above.
  bool night_scheme_enabled;
  uint8_t night_custom_bg;
  uint8_t night_custom_text;
  uint8_t night_custom_accent;

  uint8_t bottom_style;       // 0=digital (big time+date), 1=analog clock + 4-line text panel,
                               // 2=big analogue (fullscreen hands over the sky, no bottom bar)
  uint8_t analog_style;        // 0=solid circle, 1=hour markers, 2=solid circle + markers,
                                 // 3=12/3/6/9 in a tiny procedural pixel font (bottom_style==1 only)

  uint8_t sun_moon_size_pct;   // 25/50/75/100, scales SUN_R_NORMAL/MOON_R_NORMAL. Ignored during
                                 // an active eclipse (and in big-analogue's fullscreen-sun mode) --
                                 // both of those already have their own dedicated sizing.
  uint8_t shake_label_seconds; // how long the shake-to-reveal name labels stay up, in seconds
  uint8_t label_style;         // user setting ("Astronomy" section, right below shake_label_seconds):
                                 // 0=Boxed (opaque rounded rect, white text -- the original look),
                                 // 1=Outlined (main-color text with a 4-direction-shifted contrasting
                                 // outline, same technique corner/edge feature text already uses),
                                 // 2=Soft (plain light-gray text, no background or outline). See
                                 // draw_label() in background_layer.c.
  uint8_t bottom_info_bar_mode; // user setting: the clouds%/visibility/location bar at the
                                  // bottom of the sky view -- 0=off, 1=on shake (with the
                                  // Sun/Moon/planet name labels), 2=permanent (sky view shifts
                                  // up 20px to make room, rather than the bar overlapping it)
  bool vibrate_on_phase_change; // user setting: brief double vibration when the eclipse crosses
                                  // into its next phase (C1/C2/C3/C4) -- not on the "there's an
                                  // eclipse today, waiting" transition, only real contact events
  bool startup_clock_animation_enabled; // user setting ("Style" section, default true): the clock
                                          // hands/digits animate in from a "cold start" position (00:00,
                                          // or hands at 12) up to the real current time on app launch,
                                          // rather than just appearing already showing it. See
                                          // s_startup_clock_anim_* in pebble-eclipse-watch.c and
                                          // hand_layer.c's HandConfig-level sweep-in support. Under 1.5s.
  uint8_t bg_anim_mode; // user setting ("Style" section, default 0=off): radio-style, exactly one
                         // of 0=off, 1=weather (clouds slide in from the sides), 2=planets (Sun/
                         // Moon/planets + the sky gradient sweep in from their position a couple
                         // hours ago), 3=markers (big-analog HOUR markers only -- second markers
                         // are excluded and always drawn normally -- animate in from off-screen,
                         // see draw_all_markers()'s own comment on why they're not genuinely
                         // cached rather than just skipped-from-animation). Only one kind of
                         // element animates at a time -- see canvas_update_proc's own gating at
                         // each of its 3 uses (the sky_now substitution, the draw_clouds() call,
                         // and the draw_all_markers() call).
  uint8_t shake_anim_mode; // user setting ("Style" section, default 0=off): radio-style, exactly
                             // one of 0=off, 1=gradient (outlines sweep through a rainbow -- a real
                             // gradient across the screen, not a single shared flashing color; see
                             // shake_outline_color()/shake_gradient_active() in
                             // pebble-eclipse-watch.c and subpixel.h's stroke_*_gradient_fp()
                             // functions), 2=smooth second hand (continuous sub-second motion
                             // instead of per-second jumps, for as long as shake_label_seconds),
                             // 3=both at once, 4="Planet seek" (points the sky view at whatever
                             // 90deg slice of the horizon the watch's compass is currently facing,
                             // for as long as shake_label_seconds -- weather is suppressed for the
                             // duration; the Sun/Moon/planets keep the same altitude they'd show in
                             // the normal, non-rotated view, just repositioned left-to-right across
                             // the screen by compass-relative azimuth, with off-screen bodies shown
                             // as an edge-pinned label + arrow instead. Unavailable whenever today
                             // has an eclipse -- see s_data.has_eclipse's own gating at the trigger
                             // site), 5="Paths" (each currently-visible Sun/Moon/planet grows a
                             // dotted 1px trail in its own color, tracing that body's altitude over
                             // roughly the surrounding 4 hours at its own fixed on-screen column --
                             // see draw_body_paths_overlay() in background_layer.c. Extends out from
                             // the body over the first 500ms, holds, then contracts back over the
                             // last 500ms, same shape as Planet seek's own 500ms-in/hold/500ms-out
                             // timing. Bodies currently below the horizon or otherwise not drawn get
                             // no path at all in this first version -- an off-screen body's own path
                             // + label + arrow, and rise/set labels at each path end, are both real
                             // ideas but neither is implemented here; see this project's own notes on
                             // both near draw_body_paths_overlay() for what a real attempt would need).
  bool outline_enabled; // user setting: 1px contrasting-color outline behind corner/edge text,
                          // the big-analog date, the eclipse phase text, and (procedurally, non-
                          // translucent mode only) corner/edge icons and the CUSTOM hand system
                          // (big_analog_hand_style == 4) -- the 4 built-in hand style presets
                          // (styles 0-3) use their own separate hand_preset_contrast_style
                          // setting below instead, not this one.
  uint8_t hand_preset_contrast_style; // user setting ("Style" section, right below the hand
                                        // style picker): applies ONLY to the 4 built-in hand
                                        // style presets (big_analog_hand_style 0-3) -- never to
                                        // the custom hand system, icons, or text features, which
                                        // all keep using outline_enabled above unchanged. Exactly
                                        // one of: 0=None (no outline at all), 1=Contrasting
                                        // outline (auto black/white by luma against the hand's
                                        // own fill color -- see HandConfig's own
                                        // outline_auto_contrast comment in hand_layer.h),
                                        // 2=Background color outline (the scheme's own background
                                        // color -- this was the fixed, only behavior before this
                                        // setting existed), 3=Shadow (a solid black copy of the
                                        // hand shifted 1px right+down, NOT a traced outline at
                                        // all -- see HandConfig's own hard_shadow comment).
  uint8_t corner_font; // unified font id (see font_lookup.h) for corner/edge feature text and the
                         // big-analog date text; default (1 = System Medium) matches the old
                         // corner_font_size default

  // bottom_style==2 (big analogue) settings -- hands/markings render
  // in their own always-on-top layer (see pebble-eclipse-watch.c),
  // separate from the sky canvas underneath.
  uint8_t big_analog_hand_style; // 0=pointy (triangular), 1=square (rectangular),
                                   // 2=modern (rounded, hollow hour/minute hands), 3=rounded/classic,
                                   // 4=custom -- user-built per-hand system, see hand_layer.h/.c and
                                   // the hand_hour/hand_minute/hand_second/center_circle_* fields below.
  bool big_analog_hands_transparent; // dither the hands instead of solid fill, so the
                                       // sky/eclipse drawing underneath still shows through --
                                       // for the custom hand style (4), this instead means "draw
                                       // hands (and their outline, if any) as a 1px stroke only"
  bool big_analog_hands_shadow; // user setting: turns on the drop-shadow HandConfig.shadow_enabled
                                  // field for all 3 procedural hand presets (styles 0-3) at once,
                                  // same "one global toggle applied uniformly" pattern
                                  // outline_enabled/big_analog_hands_transparent already use --
                                  // meaningless for the custom hand style (4), which has its own
                                  // per-hand hand_hour/hand_minute/hand_second.shadow_enabled
                                  // instead. Procedural presets always use a hardcoded 2px
                                  // shadow_distance_px when this is on (the angle is always
                                  // shadow_angle_deg below, shared with every other hand).
  bool shadow_translucent; // user setting ("Style" section): whether EVERY hand's shadow (both
                             // procedural presets and the custom hand system) draws solid black
                             // or a dithered translucent black -- ~50% normally, ~25% when that
                             // particular hand is itself translucent too. A single global style
                             // choice, unlike shadow_enabled/distance which are per-hand
                             // (or, for presets, one shared toggle) -- see hand_layer.h. Defaults
                             // to true (translucent).
  uint16_t shadow_angle_deg; // user setting ("Style" section, right below shadow_translucent):
                              // single shared light-source direction for every hand's shadow, 0-359,
                              // same "0 = 12 o'clock, clockwise" convention as every other angle in
                              // this project. All 3 hands share one physical light source, so a
                              // separately-adjustable angle per hand (as this briefly was) made no
                              // real sense -- only shadow_enabled/distance stayed per-hand. Defaults
                              // to 120, matching the procedural presets' own hardcoded angle.
  bool draw_features_beneath_hands; // user setting ("Style" section, big-analog only): when
                                      // true, apply_layout() adds the features overlay layer
                                      // BEFORE the hands layer instead of after, so hands draw
                                      // on top of corners/edges info instead of under it.
                                      // Meaningless (and hidden on the settings page) outside
                                      // bottom_style == 2.

  // Only meaningful when big_analog_hand_style == 4. See hand_layer.h for field docs.
  HandConfig hand_hour;
  HandConfig hand_minute;
  HandConfig hand_second;
  uint8_t center_circle_radius; // 0 = off, else px
  uint8_t center_circle_color;  // 0=main, 1=accent, 2=background

  // 0=minimal, 1=small, 2=big -- all three now drawn by background_layer.c's shared
  // marker rasterizer too, via a small hardcoded MarkerRingConfig preset per style
  // (see MARKER_STYLE_PRESETS in that file) rather than their own separate procedural
  // drawing code -- same code path as style 8 (custom), just with fixed presets instead
  // of the user's own custom_hour_marker/custom_second_marker.
  // 3=modern, 4=swiss, 5=tally, 6=bell, 7=brown -- each a user-supplied bitmap mask
  // (RESOURCE_ID_xxx_BACKGROUND) tinted with the main color, replacing the procedural
  // markers entirely. See corner_content/upper_middle_content below for how picking a
  // bitmap style also disables the 4 corners in favor of one upper-middle slot.
  // 8=custom -- user-built hour/second marker system, see background_layer.c and the
  // custom_hour_marker/custom_second_marker/marker_text fields below.
  // 9=none -- no marker ring at all (hour or second), same corner/edge-slot availability
  // as the procedural styles (all 4 corners + all 4 edge-middle slots, no bitmap mask
  // eating into that room). Shown first in the settings-page dropdown despite the high
  // numeric value, to keep 0-8 backward compatible with already-installed configs.
  uint8_t big_analog_marker_style;

  // Only meaningful for bitmap marker styles (3-7 above) -- dithers the
  // mask's tint to ~67% opacity instead of solid, same alpha-forcing
  // technique tint_marker_bitmap() already used, just driven by its own
  // setting now rather than reusing big_analog_hands_transparent (that
  // coupling made bitmap markers dim whenever hands transparency was
  // toggled, whether or not that's what the user actually wanted for
  // the markers). Procedural/custom marker rings use MarkerRingConfig's
  // own per-ring translucent field above instead, since those are two
  // independent rings (hour, second) rather than one single mask.
  bool bitmap_marker_transparent;

  // Only meaningful when big_analog_marker_style == 8. See background_layer.c (the
  // merged sky-canvas-and-markers layer) for how these get drawn -- as part of its own
  // once-a-minute cached full redraw, not a separate per-tick pass.
  MarkerRingConfig custom_hour_marker;
  MarkerRingConfig custom_second_marker;
  MarkerTextConfig marker_text;

  time_t c1;                // first contact (moon touches sun's edge)
  time_t c2;                // start of totality/annularity (0 if partial-only)
  time_t max_t;              // greatest eclipse (always set if has_eclipse)
  time_t c3;                // end of totality/annularity (0 if partial-only)
  time_t c4;                // last contact (moon fully clear of sun)
  time_t sunset;             // today's sunset, used to cap the animation

  uint8_t magnitude_pct;    // 0-100, fraction of the sun's disc covered at max
  EclipseType type;

  int16_t pos_angle_deg;    // direction (0-359) the moon approaches from,
                             // in on-screen "clock" degrees, 0 = straight up

  // Separation samples across [sample_start, sample_start + (count-1)*interval]
  // in hundredths of a degree, used to animate the gap between the two
  // discs without the watch needing to redo orbital mechanics.
  time_t sample_start;
  uint32_t sample_interval_s;
  uint8_t sample_count;
  uint16_t sep_samples_centideg[MAX_SEP_SAMPLES];
  uint8_t mag_pct_samples[MAX_SEP_SAMPLES]; // live "% of Sun covered", same grid as above
  uint8_t radius_ratio_pct;                  // moon radius / sun radius at greatest eclipse, x100;
                                               // <100 = annular (ring stays visible), >=100 = total

  uint8_t cloud_cover_pct;   // 0-100 averaged over the eclipse window
  uint8_t vis_score_pct;     // 0-100 "chance you'll actually see it" score
  uint8_t weather_sources;   // how many weather sources were averaged
  uint8_t weather_condition; // 0=clear/cloudy (handled by cloud_cover_pct alone),
                              // 1=fog, 2=rain, 3=snow, 4=thunderstorm
  uint8_t weather_icon_style; // 0=simple, 1=hollow, 2=full color -- which of the "Weather icon"/
                                // "Temp + weather icon" corner content styles to draw. 1=hollow
                                // and 2=full color are both implemented (see draw_weather_icon_hollow()/
                                // draw_weather_icon_filled() in pebble-eclipse-watch.c); 0=simple is
                                // still a placeholder stub. Full color is a genuinely different kind of
                                // icon from the other two -- see the "Full color weather icons" section
                                // in README.md -- so it ignores whatever corner_color_mode the slot is
                                // set to; the other two styles are single-color silhouettes tinted by it.
                                // A settings-page-only choice in spirit (there's exactly one value, not
                                // per-slot), sent like any other setting since the watch has no other way
                                // to know it.
  int16_t weather_temp_c;    // current temperature, whole degrees Celsius (converted to F on-watch if the user prefers)
  // Robust per-service error reporting -- see servicelog.js's
  // classifyError() on the PKJS side for what these values mean (an
  // HTTP status if the fetch got a response at all, one of a handful
  // of small ERR_* codes otherwise). 0 = this refresh's weather fetch
  // was fine. weather_error_streak counts consecutive refreshes that
  // arrived with a nonzero code (reset to 0 the moment one arrives
  // with 0), capped well below 255 so it can never wrap around.
  // weather_ever_valid latches true the first time a real weather
  // reading is ever received, and never goes back to false -- see
  // weather_should_show_error() in pebble-eclipse-watch.c for how the
  // three combine to decide whether a corner slot shows "ERR ###"
  // instead of the (possibly stale, but still real) last-known
  // reading.
  uint8_t weather_error_code;
  uint8_t weather_error_streak;
  bool weather_ever_valid;
  char location_name[32];    // reverse-geocoded place name, e.g. "Innsbruck, Austria"

  uint8_t timezone_id;       // index into the TIMEZONES[] table in pebble-eclipse-watch.c --
                               // which city's time the "Timezone" corner content shows. A
                               // settings-page-only choice in spirit (one value, not per-slot),
                               // same pattern as weather_icon_style above.

  // Weather-extra fields (pressure/wind direction/dew point/air
  // quality) -- all from the same Open-Meteo source as cloud_cover_pct
  // etc. above, added for the "Pressure"/"Wind direction"/"Air quality"/
  // "Dew point" corner content types.
  int16_t wind_dir_deg;      // 0-359, compass bearing the wind is blowing FROM (meteorological convention)
  int16_t dew_point_c;       // whole degrees Celsius (converted on-watch like weather_temp_c)
  int16_t pressure_hpa;      // sea-level-adjusted, hectopascals (~950-1050 in practice)
  uint8_t pressure_trend;    // 0=flat, 1=rising, 2=falling -- vs. ~3 hours ago
  uint16_t aqi_us;           // US EPA AQI scale (0-500+), 0 = not available
  uint16_t aqi_eu;           // European AQI scale (0-100+), 0 = not available
  uint8_t aqi_unit;          // user setting: 0=show aqi_us, 1=show aqi_eu

  // GPS altitude -- meters above the WGS84 ellipsoid, same source
  // already used for the horizon-dip correction (see astro.js), just
  // also exposed as a corner content type here. -32000 = sentinel for
  // "not available" (many phones don't report GPS altitude, and manual-
  // coordinates location mode never has it) -- a real altitude can
  // legitimately be negative or exactly 0, so those can't double as
  // the "missing" signal.
  int16_t altitude_m;
  uint8_t altitude_unit;     // user setting: 0=meters, 1=feet

  // Full-day sky background data: sun altitude (drives the sky
  // gradient colour) and cloud cover (drives the dithered cloud
  // puffs), both on the same time grid so one pair of start/interval
  // covers both arrays. Moon altitude rides the same grid too, for
  // its own rise/set animation.
  time_t sky_sample_start;
  uint32_t sky_sample_interval_s;
  uint8_t sky_sample_count;
  int16_t sun_alt_decideg[MAX_SKY_SAMPLES];   // altitude x10, e.g. 123 = 12.3 deg
  uint16_t sun_az_decideg[MAX_SKY_SAMPLES];   // true-north-relative azimuth x10, 0-3599 -- for
                                                // "Planet seek" (see shake_anim_mode's own comment
                                                // further down); same grid as sun_alt_decideg
  uint8_t cloud_pct_samples[MAX_SKY_SAMPLES]; // 0-100 straight percentage
  uint8_t cloud_altitude_pct;                  // 0=low cloud, 100=high cloud (from Open-Meteo's
                                                 // low/mid/high cloud-cover split), biases cloud
                                                 // cluster height within the lower half of the sky
  uint8_t cloud_render_style;                  // user setting: 0=Simple (battery-friendly circle
                                                 // puffs), 1=Realistic (metaball field, sun-relative
                                                 // warm/cool lighting) -- see draw_clouds()
  uint8_t sky_mode;                            // user setting ("Style" section): 0=Weather sky
                                                 // (default -- gradient + clouds/weather effects,
                                                 // everything above), 1=Clear sky (same day/night
                                                 // gradient, but never draws clouds/weather effects
                                                 // or the overcast gray haze), 2=Space view (no
                                                 // gradient at all -- a fixed dark background, Sun/
                                                 // Moon/planets still only shown above the horizon
                                                 // but with no atmospheric haze/color, and the
                                                 // STAR_COUNT bright stars below always visible,
                                                 // day or night, since there's no atmosphere left to
                                                 // scatter sunlight and wash them out).
  int16_t moon_alt_decideg[MAX_SKY_SAMPLES];  // altitude x10, same grid as sun
  uint16_t moon_az_decideg[MAX_SKY_SAMPLES];  // azimuth x10 -- same convention as sun_az_decideg

  // Same grid again, one row per PlanetId -- kept as a 2D array
  // (rather than 5 separately-named fields) so both the AppMessage
  // parsing and the drawing code can loop over PLANET_COUNT instead
  // of duplicating near-identical code per planet.
  int16_t planet_alt_decideg[PLANET_COUNT][MAX_SKY_SAMPLES];
  uint16_t planet_az_decideg[PLANET_COUNT][MAX_SKY_SAMPLES]; // azimuth x10 -- same convention as sun_az_decideg
  time_t planet_rise[PLANET_COUNT];
  time_t planet_set[PLANET_COUNT];

  // Space-view sky mode's bright-star field -- current alt/az only
  // (x10 degrees, like every other _decideg field here), NOT a full-
  // day grid like sun/moon/planet_alt_decideg above. Stars barely move
  // within a single refresh interval, so unlike the Sun (which
  // animates continuously along its precomputed day-arc), these are
  // just re-sent as a fresh snapshot each refresh and drawn as-is
  // until the next one -- see computeVisibleStars() in astro.js and
  // STARS[] in background_layer.c for the fixed name/order both sides
  // share.
  int16_t star_alt_decideg[STAR_COUNT];
  uint16_t star_az_decideg[STAR_COUNT];

  // Saturn's ring-opening angle as seen from Earth, 0-100 (0 = edge
  // on/invisible, 100 = maximally open) -- real rings, narrow near a
  // ring-plane crossing and widening over Saturn's ~29.5-year orbit.
  uint8_t saturn_ring_open_pct;

  // Shared vertical-scale reference (the higher of today's max sun
  // and max moon altitude) so both bodies' rise/set motion is drawn
  // on one consistent degrees-to-pixels scale rather than each
  // being independently stretched to fill the frame.
  int16_t sky_scale_max_alt_decideg;

  uint8_t moon_phase_pct;   // 0-100 illuminated fraction (0=new, 100=full)
  bool moon_waxing;         // true = growing toward full, false = shrinking toward new

  // Today's actual rise/set times (0 = none found -- e.g. the body
  // doesn't cross the horizon that day), used to drive the on-watch
  // rise/set animation by real time rather than by re-deriving it
  // from the (deliberately compressed) altitude-to-pixel scale.
  time_t sun_rise;
  time_t sun_set;
  time_t sun_rise_tomorrow; // used once `now` is past both sun_rise and sun_set today,
                             // so get_next_sun_event() has something to fall back to
                             // instead of reporting "no event" (which used to show as "--:--").
  time_t moon_rise;
  time_t moon_set;

  // 0-100, ramps up/down around whichever major annual meteor
  // shower's active window (if any) covers today -- see astro.js's
  // activeMeteorShower(). 0 when none are active.
  uint8_t meteor_intensity;
  char meteor_shower_name[16]; // e.g. "Perseids", "Geminids" -- empty when meteor_intensity is 0

  bool show_iss;              // user setting: depict the ISS when a fresh-enough position is available
  int16_t iss_alt_deg;         // whole degrees, snapshot at iss_computed_at (not continuously propagated)
  uint16_t iss_az_deg;          // 0-359, compass bearing
  time_t iss_computed_at;       // when this snapshot was computed phone-side; the watch treats it as
                                  // stale (and doesn't draw it) once too much time has passed, since
                                  // the ISS moves fast enough that an old snapshot would be visibly wrong
  time_t iss_next_pass;         // start time of the next visible pass (observer dark + ISS above ~10 deg
                                  // + ISS itself sunlit) found by astro.js's findNextIssPass(), searched
                                  // forward from "now" at fetch time -- 0 if none found in that window
                                  // (window and thresholds documented on findNextIssPass() itself). Used
                                  // by the "Next ISS pass" corner content; independent of iss_alt_deg/
                                  // iss_az_deg/show_iss above, which are the separate "draw it on the sky
                                  // view right now" snapshot and its own on/off setting.
  uint8_t iss_error_code;       // 0 = this refresh's ISS fetch was fine (or ISS wasn't in use at all).
                                  // See weather_error_code's own comment above for what a nonzero value
                                  // means -- ISS doesn't get the same 10-refresh grace/streak treatment,
                                  // just a code available for diagnostics/future use.

  bool aurora_enabled;          // user setting ("Astronomy" section): whether auroras are fetched/shown
                                  // at all -- gates both the "Aurora Kp index" corner content option
                                  // (removed from the settings-page dropdown entirely when off, not just
                                  // hidden) and the sky-view aurora glow itself.
  uint8_t aurora_kp_x10;         // current planetary Kp index x10 (e.g. 43 = Kp 4.3), from NOAA SWPC --
                                  // see fetchAuroraKp() in weather.js. 0 if aurora_enabled is off or the
                                  // fetch failed; doesn't by itself mean "no aurora", just "no reading".
  uint8_t aurora_visibility_pct; // 0-100 rough estimate of whether the current Kp index reaches the
                                  // user's own geomagnetic latitude -- see astro.js's
                                  // auroraVisibilityScore()/geomagneticLatitudeDeg(). Gates whether the
                                  // sky view's aurora glow actually draws (still also needs a dark sky);
                                  // the Kp index itself is shown/colored regardless, since a Kp reading
                                  // is informative on its own even when the estimate says "not from here".
  uint8_t aurora_error_code;    // 0 = this refresh's aurora fetch was fine (or aurora_enabled is off).
                                  // Same meaning/source as weather_error_code and iss_error_code above.
} EclipseData;

// Defined in pebble-eclipse-watch.c, declared here (rather than a new
// header) since both that file and background_layer.c need it -- resolves
// the active day/night color scheme into concrete GColors. Takes `d`
// explicitly rather than reading a global, so background_layer.c can use
// its own EclipseData pointer (the same one, via eclipse_canvas_set_data())
// to get marker colors without duplicating the palette tables.
void get_active_color_scheme(const EclipseData *d, time_t now, GColor *bg, GColor *text, GColor *accent);

// Also defined in pebble-eclipse-watch.c, declared here for the same reason:
// unpacks one of the 64 real display colors from the single raw byte the
// phone/settings page sends for a custom scheme/hand/marker/etc. color --
// shared with features_layer.c's full-color weather icon rendering and
// get_active_color_scheme() above, both of which need to do the same
// unpacking.
GColor gcolor_from_packed(uint8_t packed);

// Also defined in pebble-eclipse-watch.c, declared here so
// features_layer.c's weather-derived corner content cases can call it.
// True when a weather corner slot should show "ERR ###" (see
// weather_error_code above) instead of its normal reading: either
// there's been no good weather data at all yet (so there's nothing
// worth falling back to), or the last 10+ consecutive refreshes have
// all come back as errors (so this isn't just a blip -- see
// weather_error_streak's own comment for the reasoning and the
// request that led to it).
bool weather_should_show_error(const EclipseData *d);

// Also defined in pebble-eclipse-watch.c, declared here for the same reason:
// the "on shake" animation's outline gradient effect is driven by state
// that file owns (when the shake happened, shake_anim_mode,
// shake_label_seconds), but applies to outlines drawn from hand_layer.c
// (which samples it per-pixel for a true screen-space gradient there --
// see subpixel.h's own stroke_*_gradient_fp() functions) and
// features_layer.c (which, unlike hand_layer.c, only has one fill color
// to give a whole text/icon draw call, so it samples this once at that
// item's own screen position instead). screen_x is whichever pixel/item
// position is relevant to the caller. Returns normal_color unchanged
// whenever the animation isn't actually running (off in settings, no
// shake in progress, or in a mode that doesn't include the gradient) --
// always safe to call unconditionally wherever an outline color is
// being resolved.
GColor shake_outline_color(GColor normal_color, int16_t screen_x);

// hand_layer.c's own version -- see its definition in
// pebble-eclipse-watch.c for why it's different from the one above.
bool shake_gradient_active(uint8_t *out_shift);

// Also defined in pebble-eclipse-watch.c -- the watch's current compass
// heading (0-359, true-north-relative, clockwise -- 0=N, 90=E, 180=S,
// 270=W, matching every other bearing in this app), as of the most
// recent reading while planet seek's own compass subscription is
// active. Meaningless (and not kept fresh) outside that window -- see
// shake_anim_mode's own comment for what's actually implemented here
// so far.
int32_t planet_seek_heading_deg(void);

// Also defined in pebble-eclipse-watch.c -- true whenever the compass
// backing planet_seek_heading_deg() above isn't (yet) fully calibrated,
// i.e. CompassStatus is anything other than CompassStatusCalibrated.
// Meaningless outside planet seek's own compass-subscription window,
// same as planet_seek_heading_deg() itself.
bool planet_seek_compass_low_accuracy(void);

// Also defined in pebble-eclipse-watch.c -- the Compass corner/edge
// content's own compass state, independent of planet_seek_heading_deg
// above (see compass_feature_is_asleep()'s own comment there for why).
int32_t compass_feature_heading_deg(void);
bool compass_feature_is_asleep(void);

// Also defined in pebble-eclipse-watch.c, declared here for the same reason:
// features_layer.c's "current conditions" and sunrise/sunset corner content
// reuse the digital bottom panel's own sunrise/sunset row logic rather than
// duplicating it.
//
// Finds whichever of sun_rise/sun_set/sun_rise_tomorrow is the next one to
// occur after `now`, writing it to *event_time and whether it's a rise
// (true) or a set (false) to *is_sunrise. Returns false if none of the
// three are set yet (no data received).
bool get_next_sun_event(time_t now, time_t sun_rise, time_t sun_set, time_t sun_rise_tomorrow,
                         time_t *event_time, bool *is_sunrise);

// A compact "sunrise/sunset" glyph (arrow + horizon-sun), built from plain
// fill primitives. Returns the total width drawn, so the caller can place
// the time text right after it.
int16_t draw_sun_time_icon(GContext *ctx, GPoint top_left, bool is_sunrise, GColor color, GColor bg);
