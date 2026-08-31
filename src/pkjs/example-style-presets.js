// Hand-authored, NOT generated -- fill in each numbered slot yourself
// as you design it, then add its matching screenshot as
// resources/example-styles/<n>.png (see
// scripts/generate-example-style-previews.js).
//
// Each non-null slot is an object: { title, description, preset }.
//   title       -- short, bold text shown at the top of the tap-to-
//                  preview popup (e.g. "Midnight Analog").
//   description -- a sentence or two shown below the title in that
//                  same popup, explaining what's distinctive about
//                  the look.
//   preset      -- an object in EXACTLY the shape the settings page's
//                  own "Style Presets" section exports/imports (Style
//                  + Colors + Features section field id -> value
//                  pairs) -- easiest way to build one: open the
//                  watchface's settings, design the look, open "Style
//                  Presets", tap "Generate JSON", and paste the box's
//                  contents in here as this field's value.
//
// A slot left as `null` just shows an empty/disabled tile in the
// "Example styles" grid -- not an error, and doesn't stop the app
// from building or running.
//
// How many slots exist at all (currently 9) is controlled by a single
// place: EXAMPLE_STYLE_COUNT in src/pkjs/config-page.js. Adding a
// 10th example later means bumping that number, adding
// resources/example-styles/10.png, and adding a "10" entry here --
// nothing else in the code needs to change.
module.exports = {
  "1": null,
  "2": null,
  "3": null,
  "4": null,
  "5": null,
  "6": null,
  "7": null,
  "8": null,
  "9": null
};
