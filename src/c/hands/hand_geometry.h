#pragma once

#include <pebble.h>
#include "../graphics/subpixel.h"
#include "../data/hand_types.h"


#define HAND_MAX_POLY_PTS 12  // Leaf uses the largest polygon.
#define HAND_MAX_POLYS 6      // Serpentine uses one quad per segment.
#define HAND_MAX_CIRCLES 3    // Spade uses three circles.

typedef struct {
  FGPoint pts[HAND_MAX_POLY_PTS];
  int n;
  bool thin; // Use the thin rasterizer for widths below 3 px.
              
              
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


// Build the fixed-point primitives for one configured hand.
void hand_geometry_compute_fp(FGPoint center, int32_t angle, const HandConfig *cfg, HandGeometry *geo);
