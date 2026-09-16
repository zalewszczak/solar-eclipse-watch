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

## Build-time scripts

The settings page is one self-contained `data:` URI with no server
behind it, so it can't load external image files when it's actually
open on the phone -- anything it previews (fonts, hand shapes, marker
art, weather icons, ...) has to already be embedded as base64 by the
time the app is built. Each of the scripts below handles one such
preview set; every one of them is safe to run even before you've
added its own source PNGs (a missing source image just means that one
preview shows no picture in settings -- not an error, and it never
affects that feature working on the watch itself).

Run them all at once, in the one order that actually matters
(`generate_hand_style_icons.py` has to run before
`generate-hand-style-icons.js`, since the latter bakes the former's
own output):

```
node scripts/generate-all.js
```

This keeps going even if one script fails (e.g. a missing optional
dependency like `pngjs` or Pillow -- see each script's own section
below) and prints a pass/fail summary at the end, so one broken
generator never blocks the others from still updating. See "Running
it automatically" further down for wiring this into your own build
step instead of remembering to run it by hand.

Quick reference (details for each are in their own section below):

| Script | Runtime | What it embeds |
| --- | --- | --- |
| `generate-all.js` | Node | Runs every script below, in order |
| `generate-marker-previews.js` | Node | Bitmap marker style artwork (Modern/Swiss/Tally/Bell/Brown) |
| `generate-infographics.js` | Node | Hand-style picker pictures, marker-preset pictures, hand-editor explainer diagrams |
| `generate-example-style-previews.js` | Node | Example style gallery preview images |
| `generate-font-previews.js` | Node | Real on-watch renderings for the font picker (Gothic/Bitham/LECO) |
| `generate_hand_style_icons.py` | Python 3 + Pillow | Renders the 11 hand-style shape icons |
| `generate-hand-style-icons.js` | Node | Bakes those 11 icons for the hand-style picker popup |
| `generate-weather-icon-previews.js` | Node + `pngjs` | Weather icon style picker previews |

## Style Presets: Example styles and custom presets

The settings page has two related but separate ways to save/share a
whole look (every setting in the Style, Colors, and Features
sections) as a single blob:

- **Style Presets** (its own section, between Features and Weather) --
  3 user-editable quick-recall slots, plus a free-form export/import
  box, all live only on the phone (never sent to the watch itself,
  and never touch `package.json`'s `messageKeys`).
- **Example styles** (the very first section on the page) -- a fixed,
  developer-authored gallery of ready-made looks, meant to ship with
  the app rather than be edited by the end user.

Both build the underlying design snapshot the same way, produced by
walking every `input`/`select`/`textarea` with an `id` inside the
page's `#section-style`, `#section-colors`, and `#section-corners`
containers (see `collectStyleCornersJson()` in
`src/pkjs/config-page.js`). Style Presets stores that object directly;
Example styles wraps it as one field (`preset`) alongside a `title`
and `description` for its preview popup -- either way, a design
exported from Style Presets pastes straight into an Example style's
`preset` field, and vice versa.

### Making one

The easiest way to author a preset -- for either an Example style or
a Style Presets slot -- is to just use the settings page itself:

1. Open the watchface's settings and design the look you want (fonts,
   colors, hand style, corner/edge features, everything in Style,
   Colors, and Features).
2. Open the **Style Presets** section, tap **Generate JSON** under
   "Export current design", and copy the box's contents.
3. That's a complete preset. Paste it into a Style Presets slot's
   save flow, the "Import a design" box, or (for a permanent, shipped
   example) the `preset` field of an entry in
   `src/pkjs/example-style-presets.js`.

You don't need to hand-write or understand the JSON's keys -- it's
just whatever the settings page's own form fields happened to be set
to, keyed by their HTML element `id` (`bottomStyle`, `customBg`,
`cornerTL`, and so on). Applying a preset just writes each value back
into the matching field and re-runs the page's normal change
handlers, the same as if you'd clicked through every control by hand.

### Adding a new Example style

Example styles are meant to be authored by you (the developer), not
end users, and shipped with the app. Tapping one in the settings page
opens a preview popup (screenshot, title, description, an Apply
button) rather than applying it immediately:

1. Design the look and export its JSON as above.
2. Open `src/pkjs/example-style-presets.js` and fill in the next
   empty numbered slot with `{ title, description, preset }` --
   `title` is the short bold heading the popup shows, `description`
   is a sentence or two under it, and `preset` is the JSON you just
   copied (`"1": null` becomes `"1": { title: "...", description:
   "...", preset: { ...the JSON you copied... } }`).
3. Take a screenshot of that look (emulator or a real watch) and save
   it as `resources/example-styles/<n>.png`, matching the slot number.
4. Run `node scripts/generate-example-style-previews.js` to bake that
   PNG into `src/pkjs/data/generated/example-style-images.js` as the button's preview
   image -- the settings page can't load an external image at
   runtime, so this has to happen before `pebble build`.
5. A slot left as `null`, or with no matching PNG generated yet, just
   shows an empty/disabled placeholder tile -- not an error, and
   doesn't stop the app from building.

How many Example style slots exist at all is controlled by a single
constant, `EXAMPLE_STYLE_COUNT` near the top of
`src/pkjs/config-page.js` (9 by default) -- bump it, add the matching
PNG and preset entry, and nothing else needs to change.

## Feature icons

Every small, fixed-shape icon the corner/edge content slots can show
(weather condition, heart rate, steps, umbrella/rain chance, wind,
GPS/manual location, visibility, cloud cover, Bluetooth, ISS pass,
Saturn ring angle, planets-up count, aurora, the 5 sleep icons, Quiet
Time, Hourly Vibrations, sunrise/sunset, ...) is a PNG image resource,
in **3 style variants** -- Simple, Hollow, Full color -- picked by the
single "Icons style" setting in settings (`icon_style` in
`eclipse_data.h`; still travels over the wire as `WEATHER_ICON_STYLE`
for backwards compatibility, even though it now covers every icon, not
just weather). Icons that draw a genuinely dynamic/continuous shape
instead -- the battery bar, the compass rose, the wind direction arrow,
moon phase, pressure trend chevron, the altitude mountain glyph, and
the small Pebble logo used by one corner content option -- are NOT
part of this system and stay hand-drawn vector/bitmap-array code, same
as before.

Each style lives in its own resources subfolder:

```
resources/images/simple/<name>.png
resources/images/hollow/<name>.png
resources/images/fullcolor/<name>.png
```

declared in `package.json`'s `resources.media` list as
`ICON_SIMPLE_<NAME>` / `ICON_HOLLOW_<NAME>` / `ICON_FULLCOLOR_<NAME>`
for plain feature icons, and `ICON_WEATHER_SIMPLE_<NAME>` /
`ICON_WEATHER_HOLLOW_<NAME>` / `ICON_WEATHER_FULLCOLOR_<NAME>` for
weather condition icons (so `RESOURCE_ID_ICON_..._<NAME>` in C). The 2
weather conditions that actually look different after dark -- clear
sky and partly cloudy -- also have a `_NIGHT`/`MOON` variant of each
style (`weather_moon.png`, `weather_partly_cloudy_night.png`); every
other icon is used as-is at night too.

All the drawing logic lives behind one small generic API in
`feature_icon_assets.c`, built around an `IconResourceSet { simple,
hollow, fullcolor }` struct:

- `feature_icon_assets_draw_styled(ctx, top_left, set, style, color, w,
  h)` -- resolves `set` against `style`, tints for Simple/Hollow (these
  are authored as 2-color, fully-transparent-plus-one-opaque-color
  PNGs with `"memoryFormat": "1BitPalette"`, remapped to any `GColor`
  at draw time via `gbitmap_set_palette()`, exactly the same trick as
  before), and draws Full color art natively with no tint (it's a
  genuinely multi-color image, e.g. the storm icon bakes in a gray
  cloud, yellow bolt, and blue rain).

- `feature_icon_assets_draw_styled_with_outline(...)` -- same, plus (if
  `outline_style != 0`) an outline pass first, 4 or 12 extra draws at a
  1px offset in a contrasting color, same `OUTLINE_OFFSETS` technique
  `feature_render_draw_text_outlined()` uses for text. Full color's
  outline pass borrows the Hollow set's silhouette (full color art has
  no alpha channel to tint).

Every call site -- the weather condition table in
`feature_weather_icons.c` and the `STYLED_ICONS` table plus the
Bluetooth/Quiet Time/Hourly Vibrations/sunrise-sunset cases in
`feature_icons.c` -- is just a small `IconResourceSet` table (or a
literal one for a single icon) plus one call into the API above. This
is deliberate: adding an icon or a style is a data row, not new drawing
code, which is what keeps the compiled binary small as the icon set
grows, instead of a duplicated switch/case per style per icon.

Every icon resource is now drawn at a uniform **16x16px**
(`ICON_DRAW_W`/`ICON_DRAW_H` in `feature_icons.c`, `ICON_W`/`ICON_H` in
`feature_weather_icons.c`) -- up from the old 16x12 (and 20x10 for the
sunrise/sunset glyph). The top-left point each icon is drawn from was
deliberately left unchanged (the position/centering math still uses the
old row-height constants), so icons simply draw taller/wider from where
they always started rather than re-centering.

Nothing is cached or preloaded -- `feature_icon_assets_draw_styled[_with_outline]`
calls `gbitmap_create_with_resource()` right before drawing and
`gbitmap_destroy()` right after, so at any redraw only the icon(s)
belonging to feature slots actually visible on screen are ever decoded
into memory.

### Editing or adding an icon

Simple/Hollow icons need to stay a **2-color** PNG -- fully transparent
plus exactly one opaque color (any color works; only the alpha pattern
matters, since the opaque color gets replaced at draw time). Full color
icons can use any colors/alpha you want, same as a normal image. Keep
every icon at 16x16px. A minimal Python/Pillow snippet for either case:

```python
from PIL import Image

# 1-bit silhouette icon (any single opaque color; it's re-tinted at
# runtime, so plain black is the simplest choice to author against)
img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
img.putpixel((7, 5), (0, 0, 0, 255))  # opaque = part of the icon
# ... draw the rest of the silhouette ...
img.save("resources/images/simple/myicon.png")
```

Then add a matching `ICON_SIMPLE_MYICON` / `ICON_HOLLOW_MYICON` /
`ICON_FULLCOLOR_MYICON` trio to `package.json`'s `resources.media`
list (`"type": "bitmap"`), and reference the new `IconResourceSet` from
a `feature_icon_assets_draw_styled_with_outline()` call site --
usually just a new row in `STYLED_ICONS` in `feature_icons.c`, or the
weather tables in `feature_weather_icons.c` for a new weather
condition.


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
`src/pkjs/data/generated/marker-preview-images.js` -- a generated file the settings
page requires and embeds directly. No separate preview-only files are
needed: whatever art a marker style already uses on the watch is
exactly what shows as its preview in settings.

You don't need every style's PNG to exist for this to work -- any
style missing a `*_background.png` simply shows no preview image in
settings (not an error, and it doesn't affect that style working on
the watch itself). The committed placeholder
(`src/pkjs/data/generated/marker-preview-images.js` with no entries) means the app
also builds fine before you've added any of these images at all.

### Running it automatically

`pebble build` has no plugin/hook system of its own, so to avoid
remembering this step by hand, either:

- Add an npm script and use `npm run build` instead of `pebble build`
  directly:

  ```json
  "scripts": {
    "build": "node scripts/generate-all.js && pebble build"
  }
  ```

- Or add `node scripts/generate-all.js` to whatever shell alias/CI
  step you already use to invoke `pebble build`.

(`generate-all.js` runs every script in this file's own "Build-time
scripts" section above, including this one -- there's no need to also
list `generate-marker-previews.js` separately once you're using it.)

### Keep the source PNGs reasonably small

Everything the script embeds ends up base64-encoded (about 33% larger
than the raw file) inside the settings page's own HTML, which is
itself one big `data:` URI passed to the phone's webview. The simple
white-on-transparent mask art these marker styles already use is
fine. Full-resolution photos or anything visually complex will bloat
the page and risk hitting inconsistent limits across different
phones/OS versions.

## Font picker previews

The font picker popup (Clock font / Small companion font / corner-
feature font / marker numerals font) shows each option rendered in
its own real typeface, loaded live from Google Fonts -- see
`FONT_LOOKUP` in `config-page.js` for the full table and which of
these are the exact same family as the on-watch font versus a close
visual substitute (`approx: true`, with a comment explaining why for
each one).

Three of the watch's built-in system fonts have no substitute at all
in that table: Gothic ("System"), Bitham, and LECO. These are
commercially licensed typefaces (Bitham is a renamed/relicensed
Gotham from Hoefler&Co/Typography.com; Gothic is Mark Simonson's
Raster Gothic; LECO 1976 is licensed from MyFonts), so embedding them
as a live webfont for the settings page to render arbitrary text with
isn't something their licenses cover, even though the app itself is
licensed to use them on the watch. `scripts/generate_hand_style_icons.py`'s
sibling for this, `scripts/generate-font-previews.js`, sidesteps that
by baking in a small PNG of one real on-watch RENDERING of the
picker's own sample text instead -- you're using a font you're
already licensed to use to flatten your own fixed string to an image,
not redistributing the font file itself for a browser to render
arbitrary text with, the same distinction most font EULAs draw between
ordinary desktop use and a separate webfont license.

### Producing the source PNGs

There's no automated renderer for these three (they only exist inside
Pebble's firmware) -- you make them yourself, once, using the actual
`FONT_KEY_*` constant each font id maps to (see `font_lookup.c`):

```c
GFont font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
graphics_context_set_text_color(ctx, GColorWhite);
graphics_draw_text(ctx, "12:34", font, bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
```

White text (not black) on a transparent/black background -- see
"Keep it themed" below for why. Run this in a throwaway watchapp in
the emulator, screenshot it, and crop tight to the text.

Save each as `resources/font-previews/<fontId>_<role>.png`, where
`fontId` is that font's own numeric id in `FONT_LOOKUP` and `role` is
one of `clock`, `clockFontSmall`, `cornerFont`, `markerTextFont`
(matching `FONT_PICKER_ROLES` in `config-page.js`) -- whichever
picker(s) that font can actually appear in, each with its own sample
text ("12:34", "Tue 12", "-10°C", "12"/"XII"). You don't need every
combination -- a font with no PNG for a given role just falls back to
its existing CSS-approximation text preview for that one button.
`fontLookupEntry(id).mainClock` tells you whether a font is eligible
for the Clock font role at all.

Then bake it in:

```
node scripts/generate-font-previews.js
```

which reads `resources/font-previews/*.png` and writes
`src/pkjs/data/generated/font-preview-images.js`.

### Keep it themed

The picker previews render as white-on-transparent and get CSS-
inverted for light mode (`filter: invert(1)`, undone via
`prefers-color-scheme: dark`) -- the same trick `bitmap-marker-img`
already uses. Author your screenshot with light/white text so it
already looks right in dark mode without any extra work.

## Hand style picker icons

The hand style picker popup (inside each "Edit hour/minute/second
hand" editor) shows a small silhouette icon per `HandConfig.style`
value (Baton/Galba/Pencil/Dauphine/Sword/Pomme/Spade/Arrow/Leaf/
Syringe/Serpentine) next to its name. Unlike the full-width labeled
explainer diagram already covered above (`resources/infographics/
<style name>.png`, via `generate-infographics.js`), these are plain
unlabeled shapes meant to read at a glance in a compact button list --
a separate image set in its own `resources/hand-style-icons/`
subfolder, so the two never collide on a filename.

This is a two-step pipeline, since actually rendering a shape (rather
than just packaging an existing file, which is all the other
generate-*.js scripts here do) needs a real rasterizer, and plain
Node.js has none built in without a native `canvas` dependency:

```
python3 scripts/generate_hand_style_icons.py
node scripts/generate-hand-style-icons.js
```

The first is a from-scratch Python + Pillow port of this project's
own `compute_hand_geometry_fp()` (`hand_layer.c`) -- the exact same
capsule/taper/kite/pentagon/etc. construction the watch itself uses,
just oriented flat (pointing right) and rendered directly instead of
returned as polygon data for a Pebble `GContext` to fill, using
representative width/length/offset parameters chosen to resemble this
project's own reference diagrams rather than any particular preset.
Requires Pillow (`pip install pillow`, or `pip install pillow
--break-system-packages` if your system's `pip` refuses to install
outside a virtualenv). It writes PNGs straight into
`resources/hand-style-icons/`.

The second bakes those PNGs into `src/pkjs/data/generated/hand-style-icon-images.js`,
the same shape every other generate-*.js script here uses.

### Tweaking a shape

Every style's own proportions (width, length, back offset, middle
offset, secondary width, all in the same "px" units `HandConfig`
itself uses) are plain local variables at the top of that style's own
`style_<name>(draw)` function in `generate_hand_style_icons.py` --
change them and re-run both scripts above to see the result. The
white pivot-marker dot's own position (`STYLES`'s third entry per
style) has to land somewhere already solidly inside that style's own
black shape, or it'll just draw on empty transparent canvas -- most
styles just need an axial (x) position, but serpentine's own wavy
centerline needs its actual curve position at that point too (see
`serpentine_dot_point()`).

## Weather icon style previews

The Weather icon style picker (Simple/Hollow/Full color) shows one
representative icon -- the "partly cloudy" one -- from each style's
own resource set (`resources/images/icon_<style>_partly_cloudy.png`;
see `draw_weather_icon_simple()`/`_hollow()`/`_filled()` in
`features_layer.c`), scaled up so it's actually legible next to the
option's name in the picker's own button row.

```
npm install pngjs --save-dev   # once
node scripts/generate-weather-icon-previews.js
```

Requires `pngjs` (a pure-JS PNG decoder/encoder, no native bindings)
since, like the hand-style icon script above, this does real pixel
work rather than just packaging an existing file -- specifically,
nearest-neighbor upscaling (never smooth/bilinear interpolation,
which would blur these into a soft smudge) so the enlarged preview
stays crisp and true to the icon's actual on-watch blocky look, the
same reasoning the picker's own CSS (`image-rendering: pixelated`)
backs up on the display side. The scale factor is always a whole
number, chosen per source image so the result lands close to
`TARGET_HEIGHT` (a constant near the top of the script) -- tweak that
constant, not the source PNGs, if the previews come out too small or
large next to the option names.
