#include "weather_effects.h"
#include "weather_layer.h"
#include "subpixel.h"

typedef struct { uint8_t r, g, b; } RGB8;

static uint8_t lerp8(uint8_t a, uint8_t b, int32_t num, int32_t den) {
  if (den == 0) return a;
  return (uint8_t)(a + ((int32_t)(b - a) * num) / den);
}

static uint8_t dither_channel(uint8_t continuous_255, uint8_t bayer_0_15) {
  int32_t scaled = (int32_t)continuous_255 * 3;
  int32_t level = scaled / 255;
  int32_t rem = scaled - level * 255;
  int32_t threshold = (bayer_0_15 * 255) / 16;
  if (rem > threshold && level < 3) level++;
  return (uint8_t)level;
}

static GColor dither_pixel(RGB8 c, uint8_t bayer_0_15) {
  return GColorFromRGB(dither_channel(c.r, bayer_0_15) * 85,
                       dither_channel(c.g, bayer_0_15) * 85,
                       dither_channel(c.b, bayer_0_15) * 85);
}

#define GROUND_H 18
#define SKY_TOP_MARGIN 20

// Rain/snow columns are anchored to the same cluster x-positions and
// band_y the clouds themselves use (see CLUSTER_X_PCT / cloud_cluster_count
// above), so precipitation visibly falls from the cloud masses rather
// than scattering across the whole sky regardless of where the clouds
// are. Fixed, deliberately-not-random offsets within each column --
// redraws happen at most once a minute, so per-frame randomness
// wouldn't read as motion anyway, and fixed positions are cheap and
// reproducible. Weather-condition codes match what index.js sends:
// 1=fog, 2=rain, 3=snow, 4=thunderstorm.
static const GPoint RAIN_OFFSETS[5] = {
  { -16,  8 }, { -5, 15 }, { 6, 10 }, { 17, 17 }, { 0, 24 },
};
#define RAIN_OFFSET_COUNT 5

static const GPoint SNOW_OFFSETS[6] = {
  { -18, 10 }, { -7, 19 }, { 4, 13 }, { 15, 23 }, { -2, 29 }, { 20, 16 },
};
#define SNOW_OFFSET_COUNT 6

void weather_effects_draw_effect(GContext *ctx, GRect bounds, uint8_t condition,
                                 uint8_t cloud_pct, uint8_t cloud_altitude_pct) {
  int16_t sky_h = bounds.size.h - GROUND_H; // don't draw effects over the ground strip

  if (condition == 2 || condition == 4) { // rain, or a storm's heavier rain
    int16_t band_y = weather_layer_cloud_band_y(bounds, cloud_altitude_pct);
    int cluster_count = weather_layer_cloud_cluster_count(cloud_pct < 60 ? 60 : cloud_pct, condition == 4);
    graphics_context_set_stroke_color(ctx, GColorFromRGB(40, 100, 210));
    graphics_context_set_stroke_width(ctx, condition == 4 ? 2 : 1);
    for (int c = 0; c < cluster_count; c++) {
      int16_t cx = bounds.origin.x + (bounds.size.w * weather_layer_cluster_x_pct(c)) / 100;
      int16_t base_y = band_y + weather_layer_cluster_y_offset(c) + 15; // just below the puff cluster's underside
      for (int i = 0; i < RAIN_OFFSET_COUNT; i++) {
        int16_t x = cx + RAIN_OFFSETS[i].x;
        int16_t y = base_y + RAIN_OFFSETS[i].y;
        if (y + 8 >= sky_h) continue;
        graphics_draw_line(ctx, GPoint(x, y), GPoint(x - 3, y + 8));
      }
    }
    if (condition == 4) { // plus a static lightning-bolt accent
      graphics_context_set_stroke_color(ctx, GColorYellow);
      graphics_context_set_stroke_width(ctx, 2);
      static const GPoint BOLT[4] = { { 95, 45 }, { 88, 65 }, { 100, 65 }, { 90, 90 } };
      for (int i = 0; i < 3; i++) {
        graphics_draw_line(ctx,
          GPoint(bounds.origin.x + BOLT[i].x, bounds.origin.y + BOLT[i].y),
          GPoint(bounds.origin.x + BOLT[i + 1].x, bounds.origin.y + BOLT[i + 1].y));
      }
    }
  } else if (condition == 3) { // snow
    int16_t band_y = weather_layer_cloud_band_y(bounds, cloud_altitude_pct);
    int cluster_count = weather_layer_cloud_cluster_count(cloud_pct < 60 ? 60 : cloud_pct, false);
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int c = 0; c < cluster_count; c++) {
      int16_t cx = bounds.origin.x + (bounds.size.w * weather_layer_cluster_x_pct(c)) / 100;
      int16_t base_y = band_y + weather_layer_cluster_y_offset(c) + 15;
      for (int i = 0; i < SNOW_OFFSET_COUNT; i++) {
        int16_t x = cx + SNOW_OFFSETS[i].x;
        int16_t y = base_y + SNOW_OFFSETS[i].y;
        if (y >= sky_h) continue;
        graphics_fill_rect(ctx, GRect(x, y, 2, 2), 0, GCornerNone);
      }
    }
  } else if (condition == 1) { // fog
    // Ground-hugging haze: density ramps up toward the horizon and
    // fades going up, the way real fog actually behaves (thickest
    // near the ground, thinning with altitude), rather than a
    // uniform sparkle spread evenly over the whole sky.
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (int16_t y = 0; y < sky_h; y++) {
      int32_t density_pct = ((int32_t)y * 55) / (sky_h > 0 ? sky_h : 1) + 10; // 10%..65%
      uint8_t threshold = (uint8_t)((density_pct * 16) / 100);
      for (int16_t x = 0; x < bounds.size.w; x++) {
        if (BAYER4[y & 3][x & 3] < threshold) {
          graphics_fill_rect(ctx, GRect(bounds.origin.x + x, bounds.origin.y + y, 1, 1), 0, GCornerNone);
        }
      }
    }
  }
}

// Fixed streak positions/angles for meteor showers -- how many are
// actually drawn scales with meteor_intensity (0-100, ramped by
// astro.js's activeMeteorShower() around whichever shower's active
// window covers today). Only meaningful against a genuinely dark
// sky, so the caller gates this on sun altitude.
static const GPoint METEOR_STARTS[6] = {
  { 30, 15 }, { 80, 10 }, { 130, 20 }, { 165, 40 }, { 50, 45 }, { 110, 55 },
};
static const GPoint METEOR_ENDS[6] = {
  { 45, 35 }, { 100, 28 }, { 148, 40 }, { 180, 58 }, { 68, 65 }, { 128, 75 },
};

void weather_effects_draw_meteors(GContext *ctx, GRect bounds, uint8_t intensity) {
  int count = (intensity * 6) / 100;
  if (count > 6) count = 6;
  if (count <= 0) return;
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < count; i++) {
    graphics_draw_line(ctx,
      GPoint(bounds.origin.x + METEOR_STARTS[i].x, bounds.origin.y + METEOR_STARTS[i].y),
      GPoint(bounds.origin.x + METEOR_ENDS[i].x, bounds.origin.y + METEOR_ENDS[i].y));
  }
}

// ---- aurora -------------------------------------------------------------
// Kp-index-driven upper-sky glow. Only ever considered when it's dark
// (aurora_visible below, computed in canvas_update_proc) and
// aurora_enabled is on; the caller further gates on
// aurora_visibility_pct (see eclipse_data.h) before calling this --
// it assumes it's only being asked to draw because that check already
// passed.
#define AURORA_STREAK_COUNT 7
static const int16_t AURORA_STREAK_X_PCT[AURORA_STREAK_COUNT] = { 8, 22, 38, 50, 64, 80, 94 };
// Uneven heights (percent of the band's own max) so the streaks read
// as a rippling curtain skyline rather than a uniform wall.
static const int16_t AURORA_STREAK_HEIGHT_PCT[AURORA_STREAK_COUNT] = { 70, 100, 55, 85, 65, 95, 60 };
// Native-angle (0-65535) ripple phase offsets, so neighboring streaks
// don't wave in lockstep.
static const int32_t AURORA_STREAK_PHASE[AURORA_STREAK_COUNT] = { 0, 9362, 18725, 28087, 37449, 46811, 56174 };

// Several vertical "curtain" streaks, each with a genuine sine-wave
// horizontal ripple (via sin_lookup, same fixed-point trig every hand/
// marker in this app already uses) and a top-to-bottom color blend --
// green at the base fading toward violet/magenta higher up (redder/
// more magenta overall as Kp climbs), the classic look of a real
// display's lower green arc topped by faint red/purple structure.
void weather_effects_draw_aurora(GContext *ctx, GRect bounds, uint8_t visibility_pct, uint8_t kp_x10) {
  int16_t top_y = bounds.origin.y + SKY_TOP_MARGIN;
  int16_t horizon_y = bounds.origin.y + bounds.size.h - GROUND_H;
  int16_t max_band_h = 60 + (kp_x10 * 3) / 9;
  int16_t streak_w = (bounds.size.w / AURORA_STREAK_COUNT) + 4; // slight overlap merges streaks into one curtain

  uint8_t base_density = 35 + (uint8_t)(((int32_t)visibility_pct * 35) / 100);
  RGB8 base_color = { 20, 210, 110 };   // low arc: green
  RGB8 top_color = (kp_x10 > 60) ? (RGB8){ 170, 40, 200 } : (RGB8){ 90, 40, 180 }; // upper structure: violet, more magenta once storming

  for (int s = 0; s < AURORA_STREAK_COUNT; s++) {
    int16_t cx = bounds.origin.x + (bounds.size.w * AURORA_STREAK_X_PCT[s]) / 100;
    int16_t streak_h = (max_band_h * AURORA_STREAK_HEIGHT_PCT[s]) / 100;
    int16_t bottom_y = top_y + streak_h;
    if (bottom_y > horizon_y) bottom_y = horizon_y;
    if (bottom_y <= top_y) continue;

    for (int16_t y = top_y; y < bottom_y; y++) {
      int16_t rel = y - top_y;
      int32_t wave_angle = (AURORA_STREAK_PHASE[s] + (int32_t)rel * 900) & 0xFFFF;
      int16_t wobble = (int16_t)((6 * sin_lookup(wave_angle)) / TRIG_MAX_RATIO);
      int16_t row_cx = cx + wobble;

      int32_t height_frac1000 = ((int32_t)rel * 1000) / streak_h;
      RGB8 blend;
      blend.r = lerp8(top_color.r, base_color.r, height_frac1000, 1000);
      blend.g = lerp8(top_color.g, base_color.g, height_frac1000, 1000);
      blend.b = lerp8(top_color.b, base_color.b, height_frac1000, 1000);

      // Fades toward both edges (faint upper reach, dissolving into
      // the sky at the bottom) -- richest through the middle third.
      int32_t edge_fade = height_frac1000 < 500 ? height_frac1000 : (1000 - height_frac1000);
      uint8_t density = (uint8_t)((base_density * edge_fade) / 500);
      uint8_t threshold = (uint8_t)((density * 16) / 100);

      int16_t x0 = row_cx - streak_w / 2, x1 = row_cx + streak_w / 2;
      if (x0 < bounds.origin.x) x0 = bounds.origin.x;
      if (x1 >= bounds.origin.x + bounds.size.w) x1 = bounds.origin.x + bounds.size.w - 1;
      for (int16_t x = x0; x <= x1; x++) {
        uint8_t bayer = BAYER4[y & 3][x & 3];
        if (bayer >= threshold) continue;
        graphics_context_set_fill_color(ctx, dither_pixel(blend, bayer));
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }
}


