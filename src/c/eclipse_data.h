#pragma once

#include <pebble.h>
#include "marker_layer.h"
#include "hand_layer.h"

// How many separation samples we keep for interpolating the sun/moon
// gap smoothly between contact times. Must match SAMPLE_COUNT sent
// from PKJS (see src/pkjs/astro.js MAX_SAMPLES).
#define MAX_SEP_SAMPLES 12

// How many sun-altitude / cloud-cover samples we keep, spanning the
// whole day (roughly hourly). Must match SKY_SAMPLE_COUNT sent from
// PKJS (see src/pkjs/astro.js MAX_SKY_SAMPLES).
#define MAX_SKY_SAMPLES 26

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
  uint8_t clock_font;         // user's chosen clock typeface code, applies regardless of data validity
  uint8_t temp_unit;          // user setting: 0=Celsius, 1=Fahrenheit, 2=Kelvin
  uint8_t wind_speed_unit;    // user setting: 0=km/h, 1=mph, 2=m/s, 3=knots
  bool show_seconds;          // user setting: show seconds (grayed out in settings for wide fonts)
  bool show_sun_time;          // user setting: replace the week number with the upcoming sunrise/sunset
                                 // time (plus an arrow + horizon-sun icon), in both digital and analog modes
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
  uint8_t middle_left_content;
  uint8_t middle_left_color_mode;
  uint8_t middle_right_content;
  uint8_t middle_right_color_mode;

  // Color scheme: 0-9 are the built-in presets (see get_color_scheme()
  // in pebble-eclipse-watch.c), 10 means "use the custom_* colors
  // below instead". Each custom_* value is a raw packed GColor argb
  // byte (one of the 64 real Pebble display colors), reconstructed
  // on-watch via a GColor union rather than shipped as separate RGB
  // components -- guarantees whatever the user picked is one of the
  // 64 real colors, not an arbitrary (and possibly unsupported) RGB
  // triple.
  uint8_t color_scheme;       // 0-9 preset, 10 = custom
  uint8_t custom_bg;
  uint8_t custom_text;
  uint8_t custom_accent;

  // Optional separate scheme applied between sunset and sunrise,
  // same encoding as the day scheme above.
  bool night_scheme_enabled;
  uint8_t night_color_scheme;
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
  uint8_t bottom_info_bar_mode; // user setting: the clouds%/visibility/location bar at the
                                  // bottom of the sky view -- 0=off, 1=on shake (with the
                                  // Sun/Moon/planet name labels), 2=permanent (sky view shifts
                                  // up 20px to make room, rather than the bar overlapping it)
  bool vibrate_on_phase_change; // user setting: brief double vibration when the eclipse crosses
                                  // into its next phase (C1/C2/C3/C4) -- not on the "there's an
                                  // eclipse today, waiting" transition, only real contact events
  bool outline_enabled; // user setting: 1px contrasting-color outline behind corner/edge text,
                          // the big-analog date, the eclipse phase text, and (procedurally, non-
                          // translucent mode only) corner/edge icons and the analog hands
  uint8_t corner_font_size; // user setting: 0=small (GOTHIC_14), 1=medium (GOTHIC_14_BOLD),
                              // 2=large (GOTHIC_18_BOLD) -- ignored when corner_custom_font != 0,
                              // since a custom font has its own fixed baked-in size
  uint8_t corner_custom_font; // user setting: 0=default (system font, corner_font_size applies),
                                // 1=Digital, 2=Minecraft, 3=Pixelate, 4=Miso -- applies to
                                // corner/edge feature text AND the big-analog date text

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

  // Only meaningful when big_analog_hand_style == 4. See hand_layer.h for field docs.
  HandConfig hand_hour;
  HandConfig hand_minute;
  HandConfig hand_second;
  uint8_t center_circle_radius; // 0 = off, else px
  uint8_t center_circle_color;  // 0=main, 1=accent, 2=background

  // 0=minimal (thin hour markers only), 1=small (longer thin hour, shorter thin second),
  // 2=big (thick hour, thin short second) -- all three procedurally drawn, same as before.
  // 3=modern, 4=swiss, 5=tally, 6=bell, 7=brown -- each a user-supplied bitmap mask
  // (RESOURCE_ID_xxx_BACKGROUND) tinted with the main color, replacing the procedural
  // markers entirely. See corner_content/upper_middle_content below for how picking a
  // bitmap style also disables the 4 corners in favor of one upper-middle slot.
  // 8=custom -- user-built hour/second marker system, see marker_layer.h/.c and the
  // custom_hour_marker/custom_second_marker/marker_text fields below.
  uint8_t big_analog_marker_style;

  // Only meaningful when big_analog_marker_style == 8. See marker_layer.h for field
  // docs -- these three structs are handed straight to marker_layer_ensure_ring_cache()
  // / marker_layer_draw_text() each tick.
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
  int16_t weather_temp_c;    // current temperature, whole degrees Celsius (converted to F on-watch if the user prefers)
  char location_name[32];    // reverse-geocoded place name, e.g. "Innsbruck, Austria"

  // Full-day sky background data: sun altitude (drives the sky
  // gradient colour) and cloud cover (drives the dithered cloud
  // puffs), both on the same time grid so one pair of start/interval
  // covers both arrays. Moon altitude rides the same grid too, for
  // its own rise/set animation.
  time_t sky_sample_start;
  uint32_t sky_sample_interval_s;
  uint8_t sky_sample_count;
  int16_t sun_alt_decideg[MAX_SKY_SAMPLES];   // altitude x10, e.g. 123 = 12.3 deg
  uint8_t cloud_pct_samples[MAX_SKY_SAMPLES]; // 0-100 straight percentage
  uint8_t cloud_altitude_pct;                  // 0=low cloud, 100=high cloud (from Open-Meteo's
                                                 // low/mid/high cloud-cover split), biases cloud
                                                 // cluster height within the lower half of the sky
  uint8_t cloud_render_style;                  // user setting: 0=Simple (battery-friendly circle
                                                 // puffs), 1=Realistic (metaball field, sun-relative
                                                 // warm/cool lighting) -- see draw_clouds()
  int16_t moon_alt_decideg[MAX_SKY_SAMPLES];  // altitude x10, same grid as sun

  // Same grid again, one row per PlanetId -- kept as a 2D array
  // (rather than 5 separately-named fields) so both the AppMessage
  // parsing and the drawing code can loop over PLANET_COUNT instead
  // of duplicating near-identical code per planet.
  int16_t planet_alt_decideg[PLANET_COUNT][MAX_SKY_SAMPLES];
  time_t planet_rise[PLANET_COUNT];
  time_t planet_set[PLANET_COUNT];

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
} EclipseData;
