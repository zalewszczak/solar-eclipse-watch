#include "weather_layer.h"

// Treat a weather value as stale only after repeated failed refreshes.
// Previously valid data remains useful through short-lived fetch failures.
bool weather_layer_should_show_error(const EclipseData *data) {
  if (data->weather_error_code == 0) return false;
  if (!data->weather_ever_valid) return true;
  return data->weather_error_streak >= 10;
}
#include "subpixel.h"

#define GROUND_H 18
#define SKY_TOP_MARGIN 20

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

static uint16_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  uint32_t x = (uint32_t)v;
  uint32_t res = 0;
  uint32_t bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= res + bit) {
      x -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return (uint16_t)res;
}

static int16_t compute_cloud_band_y(GRect bounds, uint8_t cloud_altitude_pct) {
  int16_t half_h = bounds.size.h / 2;
  int16_t lower_top = bounds.origin.y + half_h;
  int16_t lower_bottom = bounds.origin.y + bounds.size.h - GROUND_H;
  return lower_bottom - (((int32_t)(lower_bottom - lower_top) * cloud_altitude_pct) / 100);
}

// Each cloud mass is a continuous procedural field (a "metaball" --
// each seed point contributes a soft, bounded falloff blob, and
// overlapping seeds' contributions add together) rather than a
// handful of discrete circles, so the silhouette merges into one
// organic, irregular shape with soft edges instead of visibly
// separate blobs. Seed positions/radii are deliberately irregular
// (not a grid or ring) so the summed field reads as an actual cloud
// mass. `scale_pct` scales every seed uniformly to grow/shrink the
// whole mass with coverage.
typedef struct {
  int16_t dx, dy;
  int16_t r;
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
// draw_clouds_realistic() call is using, so cloud_field_value() (called
// once per candidate pixel -- up to tens of thousands of times per full
// redraw) doesn't have to redo these 3 divisions on every single call.
// scale_pct is constant for an entire draw_clouds_realistic() call (it
// depends only on cloud_pct/stormy, not on which cluster or pixel is
// being evaluated), so all the per-pixel loop actually needs is the
// already-scaled (sx, sy, r2) -- see scale_cloud_seeds() below, which
// computes this array exactly once per call instead of once per pixel.
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
// from the cluster's own center. Each seed contributes
// max(0, 1-(d/r)^2)^2 (scaled to a ~0-1000 range) -- bounded and
// well-behaved close to the seed's own center (unlike a raw inverse-
// square metaball kernel, which blows up there), while still merging
// smoothly with its neighbors: two adjacent seeds' overlap region
// sums well past the threshold even though neither alone would clear
// it there, which is what makes the union read as one continuous
// mass instead of a cluster of separate circles.
static int32_t cloud_field_value(int16_t px, int16_t py, const ScaledCloudSeed seeds[CLOUD_SEED_COUNT]) {
  int32_t field = 0;
  for (int i = 0; i < CLOUD_SEED_COUNT; i++) {
    int32_t dx = px - seeds[i].sx, dy = py - seeds[i].sy;
    int32_t d2 = dx * dx + dy * dy;
    int32_t r2 = seeds[i].r2;
    if (d2 >= r2) continue;
    int32_t frac = ((r2 - d2) * 1000) / r2; // 0..1000, (1 - t^2)*1000
    field += (frac * frac) / 1000;           // ~(1-t^2)^2, still ~0..1000 per seed
  }
  return field;
}

// Up to 4 cloud masses across the band; how many are actually drawn
// scales with coverage (see cloud_cluster_count), each one nudged up
// or down slightly so a multi-cluster sky doesn't look like the same
// shape copy-pasted in a row. Shared with draw_weather_effect so
// rain/snow fall from the same positions the clouds actually occupy.
#define CLOUD_CLUSTER_SLOTS 4
static const int16_t CLUSTER_X_PCT[CLOUD_CLUSTER_SLOTS] = { 18, 45, 68, 88 };
static const int16_t CLUSTER_Y_OFFSET[CLOUD_CLUSTER_SLOTS] = { 0, -6, 4, -3 };

static int cloud_cluster_count(uint8_t cloud_pct, bool stormy) {
  if (stormy) return CLOUD_CLUSTER_SLOTS;
  int count = 1;
  if (cloud_pct > 15) count = 2;
  if (cloud_pct > 45) count = 3;
  if (cloud_pct > 75) count = 4;
  return count;
}

int16_t weather_layer_cloud_band_y(GRect bounds, uint8_t cloud_altitude_pct) {
  return compute_cloud_band_y(bounds, cloud_altitude_pct);
}

int weather_layer_cloud_cluster_count(uint8_t cloud_pct, bool stormy) {
  return cloud_cluster_count(cloud_pct, stormy);
}

int16_t weather_layer_cluster_x_pct(uint8_t index) {
  return index < CLOUD_CLUSTER_SLOTS ? CLUSTER_X_PCT[index] : 0;
}

int16_t weather_layer_cluster_y_offset(uint8_t index) {
  return index < CLOUD_CLUSTER_SLOTS ? CLUSTER_Y_OFFSET[index] : 0;
}

// Warm (sun-facing) / cool (shadow-facing) color pairs -- blended
// per pixel by how directly that point faces the Sun's actual
// on-screen position (see draw_clouds), the way real clouds pick up
// warm light on their sunward side and read cool/blue-gray in their
// own shadow. Thin cloud stays close to white either way; heavier
// cover and storms push both ends darker and more saturated toward
// gray, per the same coverage/storminess logic as before.
// 0 (full daylight brightness) .. 100 (fully night-darkened) -- ramps
// linearly as the Sun sinks from the horizon (alt 0) to -10deg, well
// past the -6deg (-60 decideg) civil-twilight threshold used
// elsewhere for "sky_is_dark", so clouds visibly dim through sunset/
// sunrise rather than popping instantly dark/bright at a threshold.
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
  // own current color (see sun_color_for_altitude() above) -- washed-
  // out white-yellow at midday, deepening through orange to red right
  // at the horizon, the same real sunset/sunrise glow that colors the
  // undersides of real clouds. Blended in rather than replacing warm
  // outright, and tapered to nothing by 30deg up (past that the Sun's
  // own color is close enough to white that blending toward it
  // wouldn't visibly change anything anyway) so this only actually
  // does anything through the low-sun/golden-hour range. The cool
  // (shadowed) side is deliberately left alone -- a cloud's shadowed
  // face doesn't take on the Sun's direct color the way its lit face
  // does.
  if (sun_alt_decideg > 0 && sun_alt_decideg < 300) {
    int32_t tint_pct = 100 - ((int32_t)sun_alt_decideg * 100) / 300;
    warm->r = lerp8(warm->r, sun_rgb.r, tint_pct, 100);
    warm->g = lerp8(warm->g, sun_rgb.g, tint_pct, 100);
    warm->b = lerp8(warm->b, sun_rgb.b, tint_pct, 100);
  }

  // These bright/pale colors were the same at any hour, so clouds at
  // night looked identical to a bright overcast afternoon -- clearly
  // wrong against a near-black night sky. Darken both warm and cool
  // toward a dark near-black gray as the Sun sinks, same idea (and
  // same lerp8-toward-a-target-color trick) as the overcast horizon
  // darkening above.
  uint8_t night = cloud_night_factor(sun_alt_decideg);
  if (night > 0) {
    RGB8 night_dark = { 28, 29, 36 };
    warm->r = lerp8(warm->r, night_dark.r, night, 100);
    warm->g = lerp8(warm->g, night_dark.g, night, 100);
    warm->b = lerp8(warm->b, night_dark.b, night, 100);
    cool->r = lerp8(cool->r, night_dark.r, night, 100);
    cool->g = lerp8(cool->g, night_dark.g, night, 100);
    cool->b = lerp8(cool->b, night_dark.b, night, 100);
  }
}

// "Realistic" cloud style -- metaball field with sun-relative
// warm/cool lighting (see cloud_field_value above). More CPU-hungry
// per redraw than the "Simple" style below (per-pixel field
// evaluation across 13 seeds vs. plain circle fills), traded for a
// painterly, organically-shaped result.
//
// `cloud_altitude_pct` biases where in the lower half of the canvas
// the deck sits (see compute_cloud_band_y). Cluster count and puff
// scale both grow with coverage -- a mostly-clear sky shows one
// modest wisp, an overcast one fills the band with several
// overlapping masses -- and low visibility thickens the haze density
// a little further on top of that, since poor visibility in real
// weather usually means denser moisture in the air generally, not
// just more cloud. `sun_center`/`sun_up` drive the per-pixel warm/cool
// lighting: each cluster gets its own light direction toward the
// Sun's actual current position (clusters on either side of it
// naturally end up lit from opposite sides), falling back to a
// flat overhead light when the Sun's below the horizon.
//
// `flash_active` (see canvas_update_proc's storm-flash comment for
// the timing) briefly lights the cloud mass from within -- every
// pixel's blend leans toward white rather than its normal warm/cool
// shading -- and drops a jagged bolt down from the cloud base,
// exactly like a real strike briefly overexposing the clouds around
// it while a thin bright channel reaches the ground.
void weather_layer_draw_clouds(GContext *ctx, GRect bounds, uint8_t cloud_pct, uint8_t cloud_altitude_pct,
                         uint8_t visibility_pct, bool stormy, GPoint sun_center, bool sun_up, bool flash_active,
                         int16_t sun_alt_decideg, uint8_t sun_r, uint8_t sun_g, uint8_t sun_b) {
  if (cloud_pct == 0 && !stormy) return;

  RGB8 warm_rgb, cool_rgb;
  RGB8 sun_rgb = { sun_r, sun_g, sun_b };
  cloud_shading_colors(cloud_pct, stormy, sun_rgb, sun_alt_decideg, &warm_rgb, &cool_rgb);

  int16_t band_y = compute_cloud_band_y(bounds, cloud_altitude_pct);
  int cluster_count = cloud_cluster_count(cloud_pct, stormy);

  uint8_t density = cloud_pct < 30 ? 30 : cloud_pct; // even thin cloud reads as a real puff, not a ghost
  if (visibility_pct < 70) density += (70 - visibility_pct) / 3; // hazier air thickens the puffs a little further
  if (density > 96) density = 96; // never fully opaque outside real storm cover
  if (stormy && density < 92) density = 92;

  int16_t scale_pct = 70 + (cloud_pct * 60) / 100; // 70%..130% across the coverage range
  if (stormy && scale_pct < 130) scale_pct = 130;

  // Scaled once here rather than inside cloud_field_value() itself --
  // scale_pct is the same for every cluster and every pixel this whole
  // call draws, so redoing these 13 seeds' divisions per-pixel (as the
  // code used to) was pure waste across what can be tens of thousands
  // of candidate pixels in a single redraw. See ScaledCloudSeed's own
  // comment above.
  ScaledCloudSeed scaled_seeds[CLOUD_SEED_COUNT];
  scale_cloud_seeds(scale_pct, scaled_seeds);

  int16_t half_w = (45 * scale_pct) / 100 + 5;
  int16_t up_h = (35 * scale_pct) / 100 + 5;
  int16_t down_h = (30 * scale_pct) / 100 + 5;

  for (int c = 0; c < cluster_count; c++) {
    int16_t cx = bounds.origin.x + (bounds.size.w * CLUSTER_X_PCT[c]) / 100;
    int16_t cy = band_y + CLUSTER_Y_OFFSET[c];

    // Light direction from this cluster toward the Sun, normalized to
    // ~100 magnitude -- falls back to straight up (Sun below horizon,
    // or degenerate zero-distance case) so lighting stays well-defined
    // at night rather than undefined/erratic.
    int32_t light_dx = 0, light_dy = -100;
    if (sun_up) {
      int32_t to_sun_x = sun_center.x - cx;
      int32_t to_sun_y = sun_center.y - cy;
      int32_t mag = (int32_t)isqrt32(to_sun_x * to_sun_x + to_sun_y * to_sun_y);
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
        // density, full density once well inside -- reads as a soft
        // painted edge rather than a hard silhouette cutoff.
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
        blend.r = lerp8(cool_rgb.r, warm_rgb.r, warm_frac, 1000);
        blend.g = lerp8(cool_rgb.g, warm_rgb.g, warm_frac, 1000);
        blend.b = lerp8(cool_rgb.b, warm_rgb.b, warm_frac, 1000);
        if (flash_active) {
          // Lit from within: lean hard toward white rather than the
          // normal warm/cool shading, same "brief overexposure" look
          // a real strike gives the clouds around it.
          RGB8 white = { 255, 255, 255 };
          blend.r = lerp8(blend.r, white.r, 70, 100);
          blend.g = lerp8(blend.g, white.g, 70, 100);
          blend.b = lerp8(blend.b, white.b, 70, 100);
        }
        GColor color = dither_pixel(blend, bayer);
        graphics_context_set_fill_color(ctx, color);
        graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
      }
    }
  }

  if (flash_active) {
    // A single jagged bolt from the first (always-present) cluster's
    // base down toward the ground -- fixed zigzag shape, not
    // randomized per strike, which keeps this cheap (no RNG state to
    // carry) and is barely noticeable given how brief each flash is.
    int16_t bx = bounds.origin.x + (bounds.size.w * CLUSTER_X_PCT[0]) / 100;
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
