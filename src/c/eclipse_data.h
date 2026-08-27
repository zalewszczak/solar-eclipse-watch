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
} MarkerRingConfig;

// Text-numeral overlay -- hour and second markers share ONE of these
// (mutually exclusive by design), since drawing both at once was
// explicitly ruled out.
typedef struct {
  uint8_t target;       // 0=off, 1=numerals on the hour ring, 2=numerals on the second ring
                          // (every 5s, drawn at the same 12 angular slots hour numerals use)
  uint8_t font_choice;   // 0-2: system GOTHIC_14 / GOTHIC_14_BOLD / GOTHIC_18_BOLD (all <25px)
                          // 3-6: custom Digital/Minecraft/Pixelate/Miso (all <25px) --
                          // same encoding as corner_custom_font/corner_font_size combined,
                          // see marker_text_font_resource_id() in background_layer.c
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
} EclipseData;

// Defined in pebble-eclipse-watch.c, declared here (rather than a new
// header) since both that file and background_layer.c need it -- resolves
// the active day/night color scheme into concrete GColors. Takes `d`
// explicitly rather than reading a global, so background_layer.c can use
// its own EclipseData pointer (the same one, via eclipse_canvas_set_data())
// to get marker colors without duplicating the palette tables.
void get_active_color_scheme(const EclipseData *d, time_t now, GColor *bg, GColor *text, GColor *accent);
