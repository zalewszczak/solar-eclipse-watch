#pragma once

#include <pebble.h>
#include "./hand_types.h"
#include "./marker_types.h"

// Eclipse separation samples; must match PKJS SAMPLE_COUNT.
#define MAX_SEP_SAMPLES 12

// Full-day sky samples; must match PKJS MAX_SKY_SAMPLES.
#define MAX_SKY_SAMPLES 26

// Fixed number of bright stars in Space view; must match PKJS catalog.
#define STAR_COUNT 16

// Shake-triggered "overhead objects" list (ISS + nearby flights), sent as
// one packed blob -- see comms_decoder.c's own _Static_assert on its size.
#define MAX_OVERHEAD_OBJECTS 6

typedef struct {
  uint16_t az_deg;  // 0-359, plain degrees (matches iss_az_deg's convention)
  int8_t alt_deg;   // elevation above horizon; not drawn when <= 0
  uint8_t is_iss;   // 0 = flight, 1 = ISS -- only used to pick the label text
} OverheadObject; // 4 bytes

// AppMessage chunk types. Values must match PKJS MSG_TYPE.
typedef enum {
  MSG_TYPE_STATUS = 0,      // status/error/location
  MSG_TYPE_ECLIPSE = 1,     // eclipse data
  MSG_TYPE_WEATHER = 2,     // current weather
  MSG_TYPE_ASTRONOMY = 3,   // Sun/Moon/planet/star data
  MSG_TYPE_SKY_EFFECTS = 4, // aurora/meteors/ISS
  MSG_TYPE_FEATURES = 5,    // feature layout/content
  MSG_TYPE_SETTINGS = 6,    // display settings
} MsgType;

// Sized for the largest inbound AppMessage chunk.
#define APPMSG_INBOX_SIZE 1200
#define APPMSG_OUTBOX_SIZE 64

// Eclipse phase derived from current time and contact times.
typedef enum {
  PHASE_NO_ECLIPSE = 0, // no eclipse or no status to show
  PHASE_BEFORE_C1,      // before first contact
  PHASE_PARTIAL_IN,     // coverage increasing
  PHASE_TOTAL,          // totality or annularity
  PHASE_PARTIAL_OUT,    // coverage decreasing
  PHASE_DONE,           // after last contact
  PHASE_NIGHT           // Sun below the horizon
} EclipsePhase;

typedef enum {
  ECLIPSE_TYPE_NONE = 0,
  ECLIPSE_TYPE_PARTIAL = 1,
  ECLIPSE_TYPE_TOTAL = 2,
  ECLIPSE_TYPE_ANNULAR = 3
} EclipseType;

// Planet array order; must match PKJS and celestial_bodies.c.
typedef enum {
  PLANET_MERCURY = 0,
  PLANET_VENUS,
  PLANET_MARS,
  PLANET_JUPITER,
  PLANET_SATURN,
  PLANET_COUNT
} PlanetId;

typedef struct {
  // Payload/status state.
  bool valid;              // true after a valid payload has been received.
  uint8_t error_code;      // 0=none, 1=no location, 2=calculation, 3=send failure.
  bool has_eclipse;        // true when an eclipse occurs today.
  uint8_t clock_font;      // main clock font ID.

  uint8_t temp_unit;       // 0=C, 1=F, 2=K.
  uint8_t wind_speed_unit; // 0=km/h, 1=mph, 2=m/s, 3=knots.
  bool show_seconds;       // show seconds in the main clock.
  bool show_sun_time;      // digital mode: show next sunrise/sunset instead of week number.

  int16_t temp_high_c;
  int16_t temp_low_c;
  uint8_t uv_index_x10;    // daily maximum UV index ×10.
  uint8_t uv_index_current_x10; // current UV index ×10.

  uint8_t rain_chance_pct; // daily maximum precipitation probability.
  uint8_t humidity_pct;     // current relative humidity.
  int16_t wind_speed_kmh;  // current wind speed.

  // Corner feature content and color modes.
  uint8_t corner_content[4];
  uint8_t corner_color_mode[4];
  uint16_t daily_step_goal; // app-local step goal.

  // Shared analog edge slots; digital layouts reuse these fields.
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

  // Day color scheme, stored as packed Pebble color values.
  uint8_t custom_bg;
  uint8_t custom_text;
  uint8_t custom_accent;

  // Optional night color scheme.
  bool night_scheme_enabled;
  uint8_t night_custom_bg;
  uint8_t night_custom_text;
  uint8_t night_custom_accent;

  // 1 = analog; other values select digital layouts.
  uint8_t bottom_style;

  uint8_t sun_moon_size_pct; // 25, 50, 75 or 100 percent.

  uint8_t shake_label_seconds; // label visibility duration.
  uint8_t label_style;     // 0=boxed, 1=outlined, 2=soft.

  bool vibrate_on_phase_change; // vibrate at C1/C2/C3/C4.

  uint8_t startup_clock_anim_mode; // 0=off, 1=clock sweep, 2=planet-time sweep.

  uint8_t bg_anim_mode;    // 0=off, 1=planets, 2=hour markers.

  uint8_t shake_anim_mode;  // 0=off, 1=smooth seconds, 2=planet seek.

  uint8_t outline_style;   // 0=none, 1=thin, 2=thick.

  bool battery_saver_enabled; // reduce updates to save power.

  uint8_t corner_font;     // feature text font ID.

  bool shadow_translucent; // draw hand shadows dithered.

  bool draw_features_beneath_hands; // analog mode layering.

  uint16_t shadow_angle_deg; // shadow direction in degrees.

  // Analog hand and marker configuration.
  HandConfig hand_hour;
  HandConfig hand_minute;
  HandConfig hand_second;
  uint8_t center_circle_radius; // 0 disables the center circle.
  uint8_t center_circle_color; // 0=main, 1=accent, 2=background.

  uint8_t big_analog_marker_style; // bitmap/procedural analog marker style.

  bool bitmap_marker_transparent; // use transparent bitmap marker background.

  MarkerRingConfig custom_hour_marker;
  MarkerRingConfig custom_second_marker;
  MarkerTextConfig marker_text;

  uint8_t custom_hour_marker_inner_thickness;
  uint8_t custom_second_marker_inner_thickness;

  // Eclipse contacts and animation samples.
  time_t c1;
  time_t c2;
  time_t max_t;
  time_t c3;
  time_t c4;
  time_t sunset;

  uint8_t magnitude_pct;    // maximum Sun coverage, 0-100.
  uint8_t type;             // EclipseType value.

  int16_t pos_angle_deg;    // Moon approach direction; 0 = up.

  time_t sample_start;       // first separation sample time.
  uint32_t sample_interval_s; // sample spacing in seconds.
  uint8_t sample_count;      // number of valid samples.
  uint8_t radius_ratio_pct; // Moon/sun radius ×100 at greatest eclipse.

  uint16_t sep_samples_centideg[MAX_SEP_SAMPLES]; // separation ×100 degrees.
  uint8_t mag_pct_samples[MAX_SEP_SAMPLES]; // Sun coverage percentage.

  // Weather conditions for the eclipse window.
  uint8_t cloud_cover_pct; // average cloud cover during eclipse.
  uint8_t vis_score_pct;   // estimated visibility percentage.
  uint8_t weather_sources; // number of weather sources used.
  uint8_t weather_condition; // 0=clear/cloudy, 1=fog, 2=rain, 3=snow, 4=storm.

  uint8_t icon_style; // 0=simple, 1=hollow, 2=full color. Applies to weather AND plain feature icons alike (was weather-only; wire key is still MK_WEATHER_ICON_STYLE, see comms_decoder.c).

  int16_t weather_temp_c;  // current temperature in Celsius.

  // Weather fetch status and freshness.
  uint8_t weather_error_code;
  uint8_t weather_error_streak;
  bool weather_ever_valid;
  time_t weather_last_update;

  // Forecast for the next 1-6 hours.
  int16_t forecast_temp_c[6];
  uint8_t forecast_condition[6];
  char location_name[32];  // reverse-geocoded place name.

  uint8_t timezone_id;     // index into the timezone table.

  // Additional weather data.
  int16_t wind_dir_deg;    // meteorological wind direction, degrees.
  int16_t dew_point_c;     // dew point in Celsius.
  int16_t pressure_hpa;    // sea-level pressure in hPa.
  uint8_t pressure_trend;  // 0=flat, 1=rising, 2=falling.
  uint8_t aqi_unit;        // 0=US AQI, 1=EU AQI.

  uint16_t aqi_us;         // 0 means unavailable.
  uint16_t aqi_eu;         // 0 means unavailable.

  int16_t altitude_m;      // WGS84 ellipsoid height; -32000 = unavailable.
  uint8_t altitude_unit;   // 0=meters, 1=feet.

  // Full-day Sun, Moon, cloud and planet position samples.
  time_t sky_sample_start;
  uint32_t sky_sample_interval_s; // sample spacing in seconds.
  uint8_t sky_sample_count;       // number of valid samples.
  int16_t sun_alt_decideg[MAX_SKY_SAMPLES]; // altitude ×10 degrees.
  uint16_t sun_az_decideg[MAX_SKY_SAMPLES]; // true-north azimuth ×10 degrees.

  uint8_t cloud_pct_samples[MAX_SKY_SAMPLES]; // 0-100 cloud cover.
  uint8_t cloud_altitude_pct; // 0=low, 100=high cloud.

  uint8_t sky_mode;         // 0=weather, 1=clear, 2=Space.

  bool show_major_stars;    // Space view star field.

  int16_t moon_alt_decideg[MAX_SKY_SAMPLES]; // altitude ×10 degrees.
  uint16_t moon_az_decideg[MAX_SKY_SAMPLES]; // azimuth ×10 degrees.

  int16_t planet_alt_decideg[PLANET_COUNT][MAX_SKY_SAMPLES];
  uint16_t planet_az_decideg[PLANET_COUNT][MAX_SKY_SAMPLES]; // azimuth ×10 degrees.
  time_t planet_rise[PLANET_COUNT];
  time_t planet_set[PLANET_COUNT];

  int16_t star_alt_decideg[STAR_COUNT];
  uint16_t star_az_decideg[STAR_COUNT];

  uint8_t saturn_ring_open_pct; // 0-100 ring opening.

  int16_t sky_scale_max_alt_decideg; // shared altitude scale ×10 degrees.

  uint8_t moon_phase_pct;  // illuminated fraction, 0-100.
  bool moon_waxing;        // true when phase is increasing.

  time_t sun_rise;         // today’s sunrise; 0 if unavailable.
  time_t sun_set;          // today’s sunset; 0 if unavailable.
  time_t sun_rise_tomorrow; // fallback next sunrise.

  time_t moon_rise;
  time_t moon_set;

  // Active meteor shower, if any.
  uint8_t meteor_intensity; // active-shower intensity, 0-100.
  char meteor_shower_name[16]; // empty when no shower is active.

  // ISS visibility and current-pass data.
  bool show_iss;            // draw current ISS position when fresh.
  int16_t iss_alt_deg;      // snapshot altitude in degrees.
  uint16_t iss_az_deg;      // snapshot azimuth in degrees.
  time_t iss_computed_at;   // time of the ISS position snapshot.

  time_t iss_next_pass;     // next visible pass start; 0 if none.

  uint8_t iss_error_code;   // current refresh error code, 0=ok.

  // Shake ("Planet Seek") overhead-objects list -- ISS plus nearby flights,
  // fetched on demand; see hourly_vibration.h-style comment analogue in
  // input.c's maybe_start_shake_animation() for what triggers a refetch.
  bool show_flights;                          // "Flights" toggle in Other settings.
  OverheadObject overhead_objects[MAX_OVERHEAD_OBJECTS];
  uint8_t overhead_object_count;
  time_t overhead_objects_computed_at;        // 0 = never fetched.
  bool overhead_objects_loading;              // watch-local only; never sent over the wire.

  // Aurora data and display settings.
  bool aurora_enabled;      // fetch and display aurora data.

  uint8_t aurora_kp_x10;    // Kp index ×10.

  uint8_t aurora_visibility_pct; // estimated local visibility, 0-100.

  uint8_t aurora_error_code; // current refresh error code, 0=ok.

  // Hourly vibration schedule.
  uint8_t hourly_vibe_mode; // 0=off, 1=hourly, 2=interval.
  uint8_t hourly_vibe_interval_min; // interval for mode 2.
  uint8_t hourly_vibe_pattern; // 0=short, 1=double, 2=long.
  uint16_t hourly_vibe_start_min; // start minute of day.
  uint16_t hourly_vibe_end_min; // end minute; equal means all day.

  uint8_t hourly_vibe_days_mask; // bit 0=Sun through bit 6=Sat.

  bool hourly_vibe_override_quiet; // vibrate during Quiet Time when true.

  bool draw_debug;
} EclipseData;
