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
  "1": {title: "Pebble sky", description: "Simple hands, weather sky and retro features in corners, not too much, not too little, just pebble enough", preset: {
    "bottomStyleValue": "analog",
    "showSeconds": true,
    "clockFont": "37",
    "handHourStyle": "0",
    "handHourWidth": "10",
    "handHourLength": "51",
    "handHourBackOffset": "0",
    "handHourMiddleOffset": "40",
    "handHourSecondaryWidth": "14",
    "handHourColor": "1",
    "handHourOutlineEnabled": "true",
    "handHourOutlineColor": "2",
    "handHourTranslucent": "true",
    "handHourShadowEnabled": "false",
    "handHourShadowDistance": "4",
    "handHourHollow": "false",
    "handHourHollowThickness": "1",
    "handMinStyle": "0",
    "handMinWidth": "10",
    "handMinLength": "78",
    "handMinBackOffset": "0",
    "handMinMiddleOffset": "40",
    "handMinSecondaryWidth": "8",
    "handMinColor": "0",
    "handMinOutlineEnabled": "true",
    "handMinOutlineColor": "2",
    "handMinTranslucent": "true",
    "handMinShadowEnabled": "false",
    "handMinShadowDistance": "4",
    "handMinHollow": "false",
    "handMinHollowThickness": "1",
    "handSecStyle": "0",
    "handSecWidth": "2",
    "handSecLength": "85",
    "handSecBackOffset": "0",
    "handSecMiddleOffset": "8",
    "handSecSecondaryWidth": "17",
    "handSecColor": "0",
    "handSecOutlineEnabled": "true",
    "handSecOutlineColor": "2",
    "handSecTranslucent": "false",
    "handSecShadowEnabled": "false",
    "handSecShadowDistance": "3",
    "handSecHollow": "false",
    "handSecHollowThickness": "1",
    "bigAnalogMarkerStyle": "9",
    "bitmapMarkerTransparent": true,
    "bitmapCornerOverride": true,
    "customHourStyle": "2",
    "customHourThickness": "7",
    "customHourInnerEcc": "70",
    "customHourOuterEcc": "100",
    "customHourInnerBorder": "75",
    "customHourOuterBorder": "100",
    "customHourTranslucent": "true",
    "customHourColor": "0",
    "customSecStyle": "2",
    "customSecThickness": "1",
    "customSecInnerEcc": "70",
    "customSecOuterEcc": "75",
    "customSecInnerBorder": "75",
    "customSecOuterBorder": "80",
    "customSecTranslucent": "true",
    "customSecColor": "0",
    "markerTextHourMask": "1365",
    "markerTextSecMask": "4095",
    "skyMode": "0",
    "digitalSides": "none",
    "outlineEnabled": true,
    "customBgValue": "192",
    "customTextValue": "255",
    "customAccentValue": "240",
    "nightEnabled": true,
    "nightCustomBgValue": "192",
    "nightCustomTextValue": "255",
    "nightCustomAccentValue": "240",
    "cornerFont": "22",
    "weatherIconStyle": "1",
    "cornerTL": "84",
    "cornerTLColor": "3",
    "cornerTR": "100",
    "cornerTRColor": "0",
    "cornerBL": "4",
    "cornerBLColor": "3",
    "cornerBR": "11",
    "cornerBRColor": "3",
    "upperMiddleLine1Content": "0",
    "upperMiddleLine1Color": "0",
    "upperMiddleLine2Content": "0",
    "upperMiddleLine2Color": "3",
    "bottomMiddleLine1Content": "87",
    "bottomMiddleLine1Color": "3",
    "bottomMiddleLine2Content": "0",
    "bottomMiddleLine2Color": "3",
    "middleLeftLine1Content": "0",
    "middleLeftLine1Color": "3",
    "middleLeftLine2Content": "0",
    "middleLeftLine2Color": "3",
    "middleRightLine1Content": "0",
    "middleRightLine1Color": "3",
    "middleRightLine2Content": "0",
    "middleRightLine2Color": "3",
    "stepGoal": "10000",
    "drawFeaturesBeneathHands": true,
    "startupClockAnimationEnabled": true,
    "bgAnimMode": "2",
    "shakeAnimMode": "1"
  }},
  "2": null,
  "3": null,
  "4": null,
  "5": null,
  "6": null,
  "7": null,
  "8": null,
  "9": null
};
