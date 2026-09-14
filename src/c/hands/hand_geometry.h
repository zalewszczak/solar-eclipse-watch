#pragma once

#include <pebble.h>
#include "../graphics/subpixel.h"
#include "../data/hand_types.h"

// Maximum fixed storage required by the supported hand styles.
#define HAND_MAX_POLY_PTS 12
#define HAND_MAX_POLYS 6
#define HAND_MAX_CIRCLES 3

typedef struct {
  FGPoint pts[HAND_MAX_POLY_PTS];
  int n;     // 0 = unused
  bool thin; // true when this polygon's own half-width is < 1.5px -- see
              // subpixel.h's subpixel_fill_polygon_thin_fp() comment for why that
              // needs a different fill routine than a normal-width shape.
} HandPoly;

typedef struct {
  FGPoint center;
  int32_t radius_fp;
  bool thin;
} HandCircle;

typedef struct {
  HandPoly polys[HAND_MAX_POLYS];
  int n_polys;
  HandCircle circles[HAND_MAX_CIRCLES];
  int n_circles;
} HandGeometry;


// Build the sub-pixel geometric representation for one configured hand.
void hand_geometry_compute_fp(FGPoint center, int32_t angle, const HandConfig *cfg, HandGeometry *geo);
