#!/usr/bin/env python3
"""
Draws small silhouette icons of each HandConfig.style shape (see
compute_hand_geometry_fp() in src/c/hand_layer.c) for the hand-style
picker popup -- see scripts/generate-hand-style-icons.js for how these
get baked into the settings page.

Requires Python 3 and Pillow ('pip install pillow' or 'pip install
pillow --break-system-packages' if your system's pip refuses to
install outside a virtualenv). Deliberately separate from this
project's other scripts/generate-*.js scripts, which only ever bake
PRE-EXISTING PNGs into base64 -- actually rendering shapes needs a
real rasterizer, which plain Node.js doesn't have without a native
canvas dependency; Python + Pillow is a common, simple choice for
that specific need and nothing else here depends on it.

Usage:
  python3 scripts/generate_hand_style_icons.py
  node scripts/generate-hand-style-icons.js

(the first renders resources/hand-style-icons/<style>.png from the
geometry below; the second bakes those PNGs into
src/pkjs/hand-style-icon-images.js the same way every other
generate-*.js script here bakes its own resources/ folder -- see
that script's own comment. Run both, in that order, whenever you
change a shape's parameters below.)

This is a faithful (float, not fixed-point -- no need for that outside
the watch's own limited CPU) port of that C function's geometry
construction, just oriented so the hand always points right (axial =
+x, perpendicular half-width = +y) instead of whatever angle a real
clock hand would be at, and rendered directly instead of returned as
polygon data for a Pebble GContext to fill.

Representative parameter values (width/length/back_offset/etc, all in
px) were chosen per style to resemble this project's own reference
diagrams (resources/infographics/<style>.png) -- not pulled from any
particular real preset. Tweak CONFIGS below and re-run to adjust any
shape's proportions.
"""
import math
from PIL import Image, ImageDraw

CANVAS_W, CANVAS_H = 440, 150
PIVOT = (70, 75)
SUPERSAMPLE = 4  # draw at 4x then downsample for anti-aliased edges
INK = (0, 0, 0, 255)
PIVOT_DOT_R = 6

def px(v):
    return v * SUPERSAMPLE

def axial(x):
    """Axial position -> canvas point (x = cx + axial, y = cy)."""
    return (px(PIVOT[0] + x), px(PIVOT[1]))

def perp(half_w):
    """Perpendicular half-width offset (purely vertical at this orientation)."""
    return (0, px(half_w))

def new_canvas():
    return Image.new('RGBA', (px(CANVAS_W), px(CANVAS_H)), (0, 0, 0, 0))

def draw_capsule(draw, back, length, half_w, round_caps):
    inner = axial(back)
    outer = axial(length)
    _, dy = perp(half_w)
    draw.polygon([
        (inner[0], inner[1] - dy), (inner[0], inner[1] + dy),
        (outer[0], outer[1] + dy), (outer[0], outer[1] - dy),
    ], fill=INK)
    if round_caps:
        for cx, cy in (inner, outer):
            draw.ellipse([cx - dy, cy - dy, cx + dy, cy + dy], fill=INK)

def draw_taper(draw, base, half_w, tip_point):
    base_pt = axial(base)
    _, dy = perp(half_w)
    draw.polygon([
        (base_pt[0], base_pt[1] - dy), (base_pt[0], base_pt[1] + dy), tip_point,
    ], fill=INK)

def style_baton(draw):  # 0
    draw_capsule(draw, 20, 340, 13, True)

def style_galba(draw):  # 1 - triangle
    tip = axial(390)
    draw_taper(draw, 15, 16, tip)

def style_pencil(draw):  # 2 - flat capsule
    draw_capsule(draw, 15, 360, 8, False)

def style_dauphine(draw):  # 3 - 4-point kite
    back_ax, len_ax, mid_ax, half_w = 15, 390, 50, 17
    back_tip = axial(back_ax)
    top_tip = axial(len_ax)
    mid = axial(mid_ax)
    _, dy = perp(half_w)
    draw.polygon([back_tip, (mid[0], mid[1] - dy), top_tip, (mid[0], mid[1] + dy)], fill=INK)

def style_sword(draw):  # 4 - 5-point pentagon
    back_ax, len_ax, mid_ax, half_w, half_sw = 15, 390, 230, 12, 16
    top = axial(len_ax)
    base = axial(back_ax)
    mid = axial(mid_ax)
    _, dyw = perp(half_w)
    _, dysw = perp(half_sw)
    draw.polygon([
        (base[0], base[1] - dyw), (mid[0], mid[1] - dysw), top,
        (mid[0], mid[1] + dysw), (base[0], base[1] + dyw),
    ], fill=INK)

def style_pomme(draw):  # 5
    back_ax, mid_ax, len_ax = 15, 110, 380
    half_w, half_sw = 15, 7
    draw_capsule(draw, mid_ax, len_ax, half_w, True)
    draw_capsule(draw, back_ax, mid_ax, half_sw, False)

def style_spade(draw):  # 6
    back_ax, len_ax = 15, 330
    half_w, half_sw = 11, 20
    mid_offset = 210
    draw_capsule(draw, back_ax, len_ax, half_w, True)
    tip = axial(len_ax)
    _, r = perp(half_sw)
    draw.ellipse([tip[0] - r, tip[1] - r, tip[0] + r, tip[1] + r], fill=INK)
    center_ax = (len_ax + back_ax) / 2
    apex_ax = center_ax + mid_offset
    if apex_ax > len_ax:
        apex = axial(apex_ax)
        _, dysw = perp(half_sw)
        draw.polygon([(tip[0], tip[1] - dysw), (tip[0], tip[1] + dysw), apex], fill=INK)

def style_arrow(draw):  # 7
    back_ax, len_ax = 15, 300
    half_w, half_sw = 15, 45
    mid_offset = 60
    tip = axial(len_ax)
    draw_taper(draw, back_ax, half_w, tip)
    apex = axial(len_ax + mid_offset)
    _, dysw = perp(half_sw)
    draw.polygon([(tip[0], tip[1] - dysw), (tip[0], tip[1] + dysw), apex], fill=INK)

def style_leaf(draw):  # 8
    back_ax, len_ax, half_w = 15, 390, 30
    mid_offset = 0
    center_ax = (back_ax + len_ax) / 2
    peak_ax = center_ax + mid_offset
    peak_ax = max(back_ax, min(len_ax, peak_ax))
    N = 8  # interior samples per half, smoother than the watch's own 2 (fine at icon scale)
    plus, minus = [], []
    have_back = peak_ax > back_ax
    have_tip = peak_ax < len_ax
    if have_back:
        for k in range(1, N + 1):
            u = k / (N + 1)
            ax = back_ax + (peak_ax - back_ax) * u
            w = half_w * math.sin(math.pi / 2 * u)
            p = axial(ax)
            _, dy = perp(w)
            plus.append((p[0], p[1] - dy))
            minus.append((p[0], p[1] + dy))
    if have_tip:
        plus2, minus2 = [], []
        for k in range(1, N + 1):
            v = k / (N + 1)
            ax = peak_ax + (len_ax - peak_ax) * v
            w = half_w * math.cos(math.pi / 2 * v)
            p = axial(ax)
            _, dy = perp(w)
            plus2.append((p[0], p[1] - dy))
            minus2.append((p[0], p[1] + dy))
    peak = axial(peak_ax)
    _, dy = perp(half_w)
    peak_plus = (peak[0], peak[1] - dy)
    peak_minus = (peak[0], peak[1] + dy)

    pts = []
    if have_back:
        pts.append(axial(back_ax))
        pts += plus
    pts.append(peak_plus)
    if have_tip:
        pts += plus2
        pts.append(axial(len_ax))
        pts += list(reversed(minus2))
    pts.append(peak_minus)
    if have_back:
        pts += list(reversed(minus))
    draw.polygon(pts, fill=INK)

def style_syringe(draw):  # 9
    back_ax, len_ax = 15, 300
    half_w, half_sw = 15, 4
    mid_offset = 0
    draw_capsule(draw, back_ax, len_ax, half_w, False)
    corner_ax = len_ax + mid_offset
    taper_len = max(half_w - half_sw, 4)
    tip_ax = corner_ax + taper_len * 4.2  # stretched a bit for a visibly long needle at icon scale
    corner = axial(corner_ax)
    tip = axial(tip_ax)
    _, dyw = perp(half_w)
    _, dysw = perp(half_sw)
    draw.polygon([
        (corner[0], corner[1] - dyw), (corner[0], corner[1] + dyw),
        (tip[0], tip[1] + dysw), (tip[0], tip[1] - dysw),
    ], fill=INK)

def style_serpentine(draw):  # 10
    # Drawn as one thick rounded-joint polyline instead of separate
    # quads (what the watch itself does, per compute_hand_geometry_fp's
    # own comment on why) -- that approach leaves visible facets at
    # icon scale/thin-line-count that read as jagged rather than a
    # smooth squiggle; a rounded stroke is both simpler and closer to
    # how this actually looks rendered at real watch-face resolution.
    back_ax, len_ax = 15, 390
    half_w, half_sw = 6, 26
    diameter = 46
    SEGMENTS = 48
    amp = max(half_sw - half_w, 0)
    period = diameter * 2
    span = len_ax - back_ax
    verts = []
    for i in range(SEGMENTS + 1):
        s = span * i / SEGMENTS
        ax = back_ax + s
        angle = (s / period) * 2 * math.pi
        dev = amp * math.sin(angle)
        p = axial(ax)
        _, dy = perp(dev)
        verts.append((p[0], p[1] + dy))
    _, halfw_px = perp(half_w)
    draw.line(verts, fill=INK, width=int(halfw_px * 2), joint='curve')
    for pt in (verts[0], verts[-1]):
        draw.ellipse([pt[0] - halfw_px, pt[1] - halfw_px, pt[0] + halfw_px, pt[1] + halfw_px], fill=INK)

# Where to place the white pivot-marker dot for each style -- must
# land somewhere already solidly inside that style's own black shape
# (matching the reference diagrams, where the dot sits inside the
# shape near its back edge) rather than at literal axial=0, which is
# behind (outside) most styles' own back_offset and would draw the
# dot on empty transparent canvas instead. A plain number is an axial
# position (perpendicular offset 0, fine for every style whose
# centerline actually runs along the pivot line there); serpentine's
# own wave curve doesn't, so it gets an explicit (x, y) function of
# its own instead -- see SERPENTINE_DOT_POINT below.
def serpentine_dot_point():
    back_ax = 15
    diameter, half_w, half_sw = 46, 6, 26
    amp = max(half_sw - half_w, 0)
    period = diameter * 2
    s = 20  # a little past the start, where the curve has swung enough off the pivot line to have solid black on multiple sides of the dot
    angle = (s / period) * 2 * math.pi
    dev = amp * math.sin(angle)
    p = axial(back_ax + s)
    _, dy = perp(dev)
    return (p[0], p[1] + dy)

STYLES = [
    ('baton', style_baton, 35), ('galba', style_galba, 30), ('pencil', style_pencil, 30),
    ('dauphine', style_dauphine, 30), ('sword', style_sword, 30), ('pomme', style_pomme, 30),
    ('spade', style_spade, 30), ('arrow', style_arrow, 30), ('leaf', style_leaf, 58),
    ('syringe', style_syringe, 30), ('serpentine', style_serpentine, serpentine_dot_point),
]

import os
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(SCRIPT_DIR, '..', 'resources', 'hand-style-icons')
os.makedirs(OUT_DIR, exist_ok=True)

for name, fn, dot_spec in STYLES:
    img = new_canvas()
    draw = ImageDraw.Draw(img)
    fn(draw)
    # pivot dot -- a real feature of the reference diagrams (marks
    # where the hand actually attaches), not a measurement annotation,
    # so kept here; drawn last so it's never covered by the shape.
    cx, cy = dot_spec() if callable(dot_spec) else axial(dot_spec)
    r = px(PIVOT_DOT_R)
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, 255))
    img = img.resize((CANVAS_W, CANVAS_H), Image.LANCZOS)
    img.save(os.path.join(OUT_DIR, name + '.png'))
    print('wrote', name + '.png')
