#include "./weather_layer.h"
#include "../../graphics/subpixel.h"
#include "./weather_layout.h"
#include "../render_math.h"

// Treat a weather value as stale only after repeated failed refreshes.
// Keep the last valid weather state during short-lived fetch failures.
bool weather_layer_should_show_error(const EclipseData *data) {
  if (data->weather_error_code == 0) return false;
  if (!data->weather_ever_valid) return true;
  return data->weather_error_streak >= 10;
}
#define GROUND_H 18
#define SKY_TOP_MARGIN 20

typedef struct { uint8_t r, g, b; } RGB8;

static GColor dither_pixel(RGB8 c, uint8_t bayer_0_15) {
  return render_math_dither_rgb(c.r, c.g, c.b, bayer_0_15);
}


// Each cloud mass is a continuous procedural field (a "metaball" --
typedef struct {
  int8_t dx, dy, r;
} CloudSeed;

static const CloudSeed CLOUD_SEEDS[13] = {
  { -24,  8, 13 }, { -14, 14, 11 }, {  -2, 15, 12 }, {  11, 13, 10 }, {  23,  7, 11 },
  { -17, -2, 14 }, {  -3, -5, 16 }, {  14, -1, 13 },
  {  -9,-15,  9 }, {   5,-17, 10 }, {  18, -9,  8 },
  { -26, -6,  8 }, {  27, -3,  8 },
};
#define CLOUD_SEED_COUNT 13
#define CLOUD_FIELD_THRESHOLD 400

// A CLOUD_SEEDS[] entry pre-scaled by whichever scale_pct the current
typedef struct {
  int16_t sx, sy;
  int32_t r2;
} ScaledCloudSeed;

static void scale_cloud_seeds(int16_t scale_pct, ScaledCloudSeed out[CLOUD_SEED_COUNT]) {
  for (int i = 0; i < CLOUD_SEED_COUNT; i++) {
    int16_t sr = (CLOUD_SEEDS[i].r * scale_pct) / 100;
    if (sr < 2) sr = 2;
    out[i].sx = (CLOUD_SEEDS[i].dx * scale_pct) / 100;
    out[i].sy = (CLOUD_SEEDS[i].dy * scale_pct) / 100;
    out[i].r2 = (int32_t)sr * sr;
  }
}

// Sum of each seed's smooth falloff contribution at (px, py), offset
static int32_t cloud_field_value(int16_t px, int16_t py, const ScaledCloudSeed seeds[CLOUD_SEED_COUNT]) {
  int32_t field = 0;
  for (int i = 0; i < CLOUD_SEED_COUNT; i++) {
    int32_t dx = px - seeds[i].sx, dy = py - seeds[i].sy;
    int32_t d2 = dx * dx + dy * dy;
    int32_t r2 = seeds[i].r2;
    if (d2 >= r2) continue;
    int32_t frac = ((r2 - d2) * 1000) / r2; // 0..1000, (1 - t^2)*1000
    field += (frac * frac) / 1000;           // ~0..1000 contribution per seed.
  }
  return field;
}

// Up to 4 cloud masses across the band; how many are actually drawn
static int cloud_cluster_count(uint8_t cloud_pct, bool stormy) {
  if (stormy) return WEATHER_CLOUD_CLUSTER_SLOTS;
  int count = 1;
  if (cloud_pct > 15) count = 2;
  if (cloud_pct > 45) count = 3;
  if (cloud_pct > 75) count = 4;
  return count;
}

// Warm (sun-facing) / cool (shadow-facing) color pairs -- blended
static uint8_t cloud_night_factor(int16_t sun_alt_decideg) {
  if (sun_alt_decideg >= 0) return 0;
  if (sun_alt_decideg <= -100) return 100;
  return (uint8_t)(((int32_t)(-sun_alt_decideg) * 100) / 100);
}

static void cloud_shading_colors(uint8_t cloud_pct, bool stormy, RGB8 sun_rgb,
                                  int16_t sun_alt_decideg, RGB8 *warm, RGB8 *cool) {
  if (stormy) {
    warm->r = 130; warm->g = 122; warm->b = 128;
    cool->r =  42; cool->g =  44; cool->b =  54;
  } else if (cloud_pct > 70) {
    warm->r = 255; warm->g = 236; warm->b = 220;
    cool->r = 150; cool->g = 158; cool->b = 174;
  } else if (cloud_pct > 35) {
    warm->r = 255; warm->g = 242; warm->b = 230;
    cool->r = 190; cool->g = 196; cool->b = 206;
  } else {
    warm->r = 255; warm->g = 250; warm->b = 244;
    cool->r = 222; cool->g = 226; cool->b = 232;
  }

// The sunlit side of the cloud should actually look lit BY the Sun's
  if (sun_alt_decideg > 0 && sun_alt_decideg < 300) {
    int32_t tint_pct = 100 - ((int32_t)sun_alt_decideg * 100) / 300;
    warm->r = render_math_lerp8(warm->r, sun_rgb.r, tint_pct, 100);
    warm->g = render_math_lerp8(warm->g, sun_rgb.g, tint_pct, 100);
    warm->b = render_math_lerp8(warm->b, sun_rgb.b, tint_pct, 100);
  }

// These bright/pale colors were the same at any hour, so clouds at
  uint8_t night = cloud_night_factor(sun_alt_decideg);
  if (night > 0) {
    RGB8 night_dark = { 28, 29, 36 };
    warm->r = render_math_lerp8(warm->r, night_dark.r, night, 100);
    warm->g = render_math_lerp8(warm->g, night_dark.g, night, 100);
    warm->b = render_math_lerp8(warm->b, night_dark.b, night, 100);
    cool->r = render_math_lerp8(cool->r, night_dark.r, night, 100);
    cool->g = render_math_lerp8(cool->g, night_dark.g, night, 100);
    cool->b = render_math_lerp8(cool->b, night_dark.b, night, 100);
  }
}

// "Realistic" cloud style -- metaball field with sun-relative
void weather_layer_draw_clouds(GContext *ctx, GRect bounds, uint8_t cloud_pct, uint8_t cloud_altitude_pct,
                         uint8_t visibility_pct, bool stormy, GPoint sun_center, bool sun_up, bool flash_active,
                         int16_t sun_alt_decideg, uint8_t sun_r, uint8_t sun_g, uint8_t sun_b) {
  if (cloud_pct == 0 && !stormy) return;

  RGB8 warm_rgb, cool_rgb;
  RGB8 sun_rgb = { sun_r, sun_g, sun_b };
  cloud_shading_colors(cloud_pct, stormy, sun_rgb, sun_alt_decideg, &warm_rgb, &cool_rgb);

  int16_t band_y = render_math_cloud_band_y(bounds, cloud_altitude_pct);
  int cluster_count = cloud_cluster_count(cloud_pct, stormy);

  uint8_t density = cloud_pct < 30 ? 30 : cloud_pct; // even thin cloud reads as a real puff, not a ghost
  if (visibility_pct < 70) density += (70 - visibility_pct) / 3; // hazier air thickens the puffs a little further
  if (density > 96) density = 96; // never fully opaque outside real storm cover
  if (stormy && density < 92) density = 92;

  int16_t scale_pct = 70 + (cloud_pct * 60) / 100; // 70%..130% by cloud coverage.
  if (stormy && scale_pct < 130) scale_pct = 130;

// Scaled once here rather than inside cloud_field_value() itself --
  ScaledCloudSeed scaled_seeds[CLOUD_SEED_COUNT];
  scale_cloud_seeds(scale_pct, scaled_seeds);

  int16_t half_w = (45 * scale_pct) / 100 + 5;
  int16_t up_h = (35 * scale_pct) / 100 + 5;
  int16_t down_h = (30 * scale_pct) / 100 + 5;

  for (int c = 0; c < cluster_count; c++) {
    int16_t cx = bounds.origin.x + (bounds.size.w * WEATHER_CLOUD_CLUSTER_X_PCT[c]) / 100;
    int16_t cy = band_y + WEATHER_CLOUD_CLUSTER_Y_OFFSET[c];

// Light direction from this cluster toward the Sun, normalized to
    int32_t light_dx = 0, light_dy = -100;
    if (sun_up) {
      int32_t to_sun_x = sun_center.x - cx;
      int32_t to_sun_y = sun_center.y - cy;
      int32_t mag = (int32_t)render_math_isqrt32(to_sun_x * to_sun_x + to_sun_y * to_sun_y);
      if (mag > 0) {
        light_dx = (to_sun_x * 100) / mag;
        light_dy = (to_sun_y * 100) / mag;
      }
    }

    int16_t x0 = cx - half_w, x1 = cx + half_w;
    int16_t y0 = cy - up_h, y1 = cy + down_h;
    if (x0 < bounds.origin.x) x0 = bounds.origin.x;
    if (y0 < bounds.origin.y) y0 = bounds.origin.y;
    if (x1 >= bounds.origin.x + bounds.size.w) x1 = bounds.origin.x + bounds.size.w - 1;
    if (y1 >= bounds.origin.y + bounds.size.h) y1 = bounds.origin.y + bounds.size.h - 1;

    for (int16_t y = y0; y <= y1; y++) {
      int16_t py = y - cy;
      for (int16_t x = x0; x <= x1; x++) {
        int16_t px = x - cx;
        int32_t field = cloud_field_value(px, py, scaled_seeds);
        if (field < CLOUD_FIELD_THRESHOLD) continue;

// Soft edge: pixels just past the threshold get reduced
        int32_t edge_pct = ((field - CLOUD_FIELD_THRESHOLD) * 100) / CLOUD_FIELD_THRESHOLD;
        if (edge_pct > 100) edge_pct = 100;
        int32_t local_density = ((int32_t)density * (60 + (edge_pct * 40) / 100)) / 100;
        if (local_density > 100) local_density = 100;

        uint8_t bayer = BAYER4[y & 3][x & 3];
        uint8_t threshold16 = (uint8_t)((local_density * 16) / 100);
        if (bayer >= threshold16) continue; // translucent gap -- sky/Sun/Moon shows through here

        int32_t facing = (px * light_dx + py * light_dy) / 15; // roughly -1000..1000 across a cluster
        if (facing > 1000) facing = 1000;
        if (facing < -1000) facing = -1000;
        int32_t warm_frac = (facing + 1000) / 2; // 0..1000

        RGB8 blend;
        blend.r = render_math_lerp8(cool_rgb.r, warm_rgb.r, warm_frac, 1000);
        blend.g = render_math_lerp8(cool_rgb.g, warm_rgb.g, warm_frac, 1000);
        blend.b = render_math_lerp8(cool_rgb.b, warm_rgb.b, warm_frac, 1000);
        if (flash_active) {
// Lit from within: lean hard toward white rather than the
          RGB8 white = { 255, 255, 255 };
          blend.r = render_math_lerp8(blend.r, white.r, 70, 100);
          blend.g = render_math_lerp8(blend.g, white.g, 70, 100);
          blend.b = render_math_lerp8(blend.b, white.b, 70, 100);
        }
        GColor color = dither_pixel(blend, bayer);
        graphics_context_set_fill_color(ctx, color);
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }

  if (flash_active) {
// A single jagged bolt from the first (always-present) cluster's
    int16_t bx = bounds.origin.x + (bounds.size.w * WEATHER_CLOUD_CLUSTER_X_PCT[0]) / 100;
    int16_t by = band_y + 10;
    int16_t ground_y = bounds.origin.y + bounds.size.h - GROUND_H;
    int16_t span = ground_y - by;
    if (span > 8) {
      GPoint bolt[6];
      bolt[0] = GPoint(bx, by);
      bolt[1] = GPoint(bx - 6, by + span * 2 / 10);
      bolt[2] = GPoint(bx + 4, by + span * 4 / 10);
      bolt[3] = GPoint(bx - 8, by + span * 6 / 10);
      bolt[4] = GPoint(bx + 2, by + span * 8 / 10);
      bolt[5] = GPoint(bx - 4, ground_y);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 2);
      for (int i = 0; i < 5; i++) {
        graphics_draw_line(ctx, bolt[i], bolt[i + 1]);
      }
    }
  }
}

// Short weather-condition word used by the status/corners overlay.
const char *weather_layer_short_condition_text(uint8_t weather_condition, uint8_t cloud_pct) {
  switch (weather_condition) {
    case 1: return "Fog";
    case 2: return "Rain";
    case 3: return "Snow";
    case 4: return "Storm";
    default:
      if (cloud_pct < 20) return "Sunny";
      if (cloud_pct < 60) return "P.Cloudy";
      if (cloud_pct < 90) return "Cloudy";
      return "Overcast";
  }
}
