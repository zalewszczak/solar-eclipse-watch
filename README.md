# Eclipz

A Pebble Time 2 (emery platform) solar eclipse watchface, written in C
(watch app) and JavaScript (PebbleKit JS + a self-contained settings
page).

## Project layout

```
src/c/          Watch app (C)
src/pkjs/        Phone-side JS: astronomy/weather fetching, and the
                 settings page itself
resources/       Fonts and images declared in package.json
scripts/         Build-time helper scripts (see below)
```

## Building

```
pebble build
pebble install --emulator emery
```

## Full color weather icons

The "Weather icon"/"Temp + weather icon" corner content has 3 icon
styles: Simple (placeholder stub), Hollow (a single-color outline,
tinted by whatever color mode the corner/edge slot itself is set to
-- same as every other icon in the app), and **Full color**. Full
color is a genuinely different kind of icon: each one is a real
multi-color image with its own baked-in colors and per-pixel
transparency, e.g. the storm icon is a gray cloud with a yellow bolt
and blue rain, all in one glyph. Because the colors are baked into the
icon itself, Full color **ignores the slot's color mode entirely** --
picking Mono/Accent/Semi/Color for a slot showing a Full color icon
has no effect on the icon (any text next to it, e.g. the temperature
in "Temp + weather icon", is unaffected by this and still follows the
color mode normally). It also never draws the contrasting outline
`outline_enabled` adds to other icons -- see the comment on that
`case 14` block in `corners_layer_update_proc`'s content switch in
`pebble-eclipse-watch.c` for why.

### Format

A Full color icon is a flat array of bytes, one byte per pixel,
row-major, at the same 16x12 resolution every other corner/edge icon
uses (`ICON_WIDTH` x `ICON_ROWS` in `pebble-eclipse-watch.c`) -- so
`16 * 12 = 192` bytes per icon. This is different from every other
icon in the app (`HEART_ICON`, `HOLLOW_SUN_ICON`, etc.), which are
1-bit-per-pixel silhouette masks (2 bytes per row, `width x rows`
bits) drawn by `draw_tiny_icon()` in one externally-supplied color.
Full color icons are drawn by `draw_full_color_icon()` instead.

Each byte is a packed `GColor` -- exactly the same 1-byte-per-color
representation the rest of the app already uses for user-picked
colors (see `gcolor_from_packed()` and `custom_bg`/`custom_text`/
`custom_accent` in `eclipse_data.h`): 2 bits of alpha, then 2 bits
each of red/green/blue, packed as `AARRGGBB`:

```
byte = (alpha << 6) | (red << 4) | (green << 2) | blue
```

Each 2-bit channel is one of 4 levels (0, 1, 2, 3), corresponding to
brightness 0x00/0x55/0xAA/0xFF. With alpha fixed at 3 (fully opaque),
the 4x4x4 red/green/blue combinations give exactly the same 64 "real"
display colors any color picker elsewhere in this app already lets
you choose from. Alpha 0 is the other commonly-used value -- that's
`GColorClear`, and `draw_full_color_icon()` skips any pixel byte equal
to `0x00` entirely rather than drawing black, so the background
underneath (sky, watch face background, whatever's already there)
shows through everywhere the icon doesn't cover. Alpha 1 and 2 are
also valid (two intermediate "dithered" transparency levels, drawn
as-is and left to Pebble's own dithering on color hardware) but
aren't needed for any of the icons below -- every pixel in this set is
either fully opaque or fully clear.

A few worked examples:

| Color | alpha,r,g,b | byte |
|---|---|---|
| Transparent | 0,0,0,0 | `0x00` |
| Opaque white | 3,3,3,3 | `0xFF` |
| Opaque black | 3,0,0,0 | `0xC0` |
| Opaque yellow | 3,3,3,0 | `0xFC` |
| Opaque blue | 3,0,1,3 | `0xC7` |
| Opaque light gray | 3,2,2,2 | `0xEA` |
| Opaque dark gray | 3,1,1,1 | `0xD5` |

### The icon set

The 7 weather categories (`weather_icon_category()` in
`pebble-eclipse-watch.c`: sunny, partly cloudy, cloudy/overcast, fog,
rain, snow, storm) each have their own `FULLCOLOR_<NAME>_ICON[192]`
array, right above `draw_weather_icon_filled()`. The storm icon is
the one built to the example above: a gray cloud (light gray
highlight over dark gray shadow, same two-tone shape the Hollow/Simple
cloud icons use), a yellow lightning bolt, and blue rain streaks,
each region simply assigned its own color while every pixel outside
the icon's silhouette stays `0x00` (transparent).

### Building or editing one

Hand-writing 192 hex bytes per icon is error-prone, so these were
generated from a small Python script rather than typed out directly:
lay the icon out as a 12-row x 16-column grid of single characters (one
character per pixel, `.` for transparent), map each character to a
packed byte via the `pack(alpha, r, g, b)` helper above, then emit the
byte array. For example, a minimal script for one icon:

```python
def pack(a, r, g, b):
    return (a << 6) | (r << 4) | (g << 2) | b

PALETTE = {
    '.': None,                # transparent, omitted
    'Y': pack(3, 3, 3, 0),    # yellow
    'B': pack(3, 0, 1, 3),    # blue
    'G': pack(3, 2, 2, 2),    # light gray
    'D': pack(3, 1, 1, 1),    # dark gray
}

rows = [
    "................",
    "......GGGG......",
    # ... 10 more 16-character rows ...
]

bytes_out = [(PALETTE[ch] or 0x00) for row in rows for ch in row]
print(', '.join('0x%02X' % b for b in bytes_out))
```

Paste the result into the matching `FULLCOLOR_<NAME>_ICON[192]` array
in `pebble-eclipse-watch.c`. There's no build-time generation step for
these (unlike the marker preview images below) -- they're committed
as plain C source, since there are only 7 of them and they change
rarely.



The big-analog watchface mode has several "bitmap" marker styles
(Modern, Swiss, Tally, Bell, Brown), each using its own PNG resource
under `resources/images/<name>_background.png`. The settings page can
show a live preview of each style's artwork so you see it before
applying -- but since that settings page is a single self-contained
`data:` URI with no server behind it, it has no way to load an
external image file when it's actually open on the phone. The preview
images have to already be embedded in the page's HTML by the time the
app is built.

That embedding is handled by:

```
node scripts/generate-marker-previews.js
```

Run this **before** `pebble build`, any time you add or change one of
the `*_background.png` files in `resources/images/`. It reads
whatever's there, base64-encodes it, and writes
`src/pkjs/marker-preview-images.js` -- a generated file the settings
page requires and embeds directly. No separate preview-only files are
needed: whatever art a marker style already uses on the watch is
exactly what shows as its preview in settings.

You don't need every style's PNG to exist for this to work -- any
style missing a `*_background.png` simply shows no preview image in
settings (not an error, and it doesn't affect that style working on
the watch itself). The committed placeholder
(`src/pkjs/marker-preview-images.js` with no entries) means the app
also builds fine before you've added any of these images at all.

### Running it automatically

`pebble build` has no plugin/hook system of its own, so to avoid
remembering this step by hand, either:

- Add an npm script and use `npm run build` instead of `pebble build`
  directly:

  ```json
  "scripts": {
    "build": "node scripts/generate-marker-previews.js && pebble build"
  }
  ```

- Or add `node scripts/generate-marker-previews.js` to whatever shell
  alias/CI step you already use to invoke `pebble build`.

### Keep the source PNGs reasonably small

Everything the script embeds ends up base64-encoded (about 33% larger
than the raw file) inside the settings page's own HTML, which is
itself one big `data:` URI passed to the phone's webview. The simple
white-on-transparent mask art these marker styles already use is
fine. Full-resolution photos or anything visually complex will bloat
the page and risk hitting inconsistent limits across different
phones/OS versions.
