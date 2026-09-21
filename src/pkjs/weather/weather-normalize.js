// ---- weather condition normalization -------------------------------------
//
// Moved out of weather.js (weather providers extraction).

// Maps an Open-Meteo/WMO weather code to our simplified on-watch
// condition enum. Plain clear/cloudy states need no special effect
// beyond the cloud-cover puffs already driven by CLOUD_SAMPLES, so
// they (and anything unrecognized) fall through to 0.
function conditionFromWmoCode(code) {
  if (code === 45 || code === 48) return 1; // fog
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 2; // drizzle/rain/showers
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 3; // snow
  if (code === 95 || code === 96 || code === 99) return 4; // thunderstorm
  return 0;
}

// ---- display-value formatting (Feature Primitive Refactor, Phase 7) ------
//
// Moves the current-temperature unit conversion out of the watch
// (feature_rules_convert_temp() in feature_rules.c, called from
// feature_value_weather_compute()'s content-32 case) and into PKJS, per
// FEATURE_PRIMITIVE_REFACTOR_REPORT_REVISED.md Section 6. `tempUnitCode`
// must be the same 0=C/1=F/2=K encoding feature_rules_convert_temp() used
// (see settingsCodecs.tempUnitCode()) -- this is deliberately the exact
// same integer-truncating arithmetic as the C it replaces (Math.trunc,
// not Math.round, for the C-to-F step) so switching which side computes
// this doesn't change what the number on the watch face actually reads.
// Only content 32 (current temperature + weather icon) consumes this for
// now; every other temperature-shaped content (5, 73, 76, 87-95, ...)
// still converts on the watch until a later pass migrates them too.
function formatTempDisplay(tempCRaw, tempUnitCode) {
  if (typeof tempCRaw !== 'number' || isNaN(tempCRaw)) return '';
  var c = Math.round(tempCRaw); // WEATHER_TEMP_C itself is sent Math.round()ed -- match it
  var shown;
  if (tempUnitCode === 1) { // Fahrenheit
    shown = Math.trunc((c * 9) / 5) + 32;
  } else if (tempUnitCode === 2) { // Kelvin
    shown = c + 273;
  } else { // Celsius
    shown = c;
  }
  return String(shown);
}

// Moved from feature_rules_apparent_temp_c() (feature_rules.c) as part of
// the same Phase 7 migration -- a simple windchill/heat-index approximation,
// not real apparent-temperature science, so it's ported verbatim rather
// than improved, to keep today's on-watch number unchanged. Takes and
// returns whole-degree Celsius (matching the C version's int16_t math);
// pass the result straight into formatTempDisplay() for the final string,
// same as the current/high/low temps above.
function apparentTempC(tempCRaw, windKmhRaw, humidityPctRaw) {
  if (typeof tempCRaw !== 'number' || isNaN(tempCRaw)) return tempCRaw;
  var tempC = Math.round(tempCRaw);
  var windKmh = (typeof windKmhRaw === 'number') ? Math.round(windKmhRaw) : 0;
  var humidityPct = (typeof humidityPctRaw === 'number') ? Math.round(humidityPctRaw) : 0;
  if (tempC <= 10 && windKmh > 4) {
    var chill = Math.trunc((windKmh - 4) / 5);
    if (chill > 12) chill = 12;
    return tempC - chill;
  }
  if (tempC >= 27 && humidityPct > 40) {
    var bump = Math.trunc(((humidityPct - 40) * 3) / 20);
    if (bump > 8) bump = 8;
    return tempC + bump;
  }
  return tempC;
}

// ---- gradient-color computation (continuing Phase 7 into Section 31) -----
//
// The report's ownership split (Section 31) puts "colors" on the watch's
// side of the line in general -- but that assumes the *value* driving the
// color is watch-local. Once a value's display text is PKJS-sourced (like
// the temperatures above), computing its gradient color on the watch means
// keeping feature_colors_seven_stop_gradient()'s color-ramp table AND the
// raw Celsius value around on-watch for no reason but that one lookup. So
// for every value already migrated to a *_DISPLAY string above, its
// gradient color moves to PKJS too, as a single packed-GColor8 byte sent
// alongside the string (same wire format the SETTINGS custom-color fields
// already use -- see settings-codecs.js's customColorByte() and
// eclipse_ui.c's eclipse_ui_color_from_packed()). This is a verbatim port
// of feature_colors_seven_stop_gradient() (feature_colors.c): same 7 color
// stops, same integer-truncating interpolation math, so the exact same
// color comes out either side of the wire -- only which side computes it
// changes. Not ported here: GColorFromRGB()'s own 8-bit-to-2-bit-per-channel
// packing is not officially documented as truncation vs. rounding, but is
// well-established SDK behavior (channel >> 6) -- see comms/gcolor.js's
// packGColorByte(), shared with astronomy/sky-display.js below.
var packGColorByte = require('../comms/gcolor').packGColorByte;

var SEVEN_STOP_GRADIENT_STOPS = [
  [64, 224, 208],  // turquoise
  [173, 216, 230], // light blue
  [0, 200, 0],     // green
  [255, 220, 0],   // yellow
  [255, 140, 0],   // orange
  [220, 20, 20],   // red
  [148, 0, 211]    // violet
];

function sevenStopGradientRGB(value, minV, maxV) {
  var stops = SEVEN_STOP_GRADIENT_STOPS;
  if (maxV <= minV || value <= minV) return stops[0].slice();
  if (value >= maxV) return stops[6].slice();
  var posX6000 = Math.trunc(((value - minV) * 6000) / (maxV - minV)); // 0..6000 across 6 segments
  var seg = Math.trunc(posX6000 / 1000);
  if (seg > 5) seg = 5;
  var segFrac = posX6000 - seg * 1000; // 0..1000 within the segment
  var from = stops[seg], to = stops[seg + 1];
  var r = from[0] + Math.trunc(((to[0] - from[0]) * segFrac) / 1000);
  var g = from[1] + Math.trunc(((to[1] - from[1]) * segFrac) / 1000);
  var b = from[2] + Math.trunc(((to[2] - from[2]) * segFrac) / 1000);
  return [r, g, b];
}

// Packs an 8-bit RGB triple (always fully opaque here) into the one-byte
// GColor8 wire format -- moved to comms/gcolor.js so astronomy/sky-display.js
// can share it without requiring this weather-specific module. Imported
// above as packGColorByte.

// The specific gradient every temperature display above uses: 7-stop
// rainbow across -10C..40C (matches feature_colors_seven_stop_gradient()'s
// callers in feature_value_weather.c's content 4/73/74/75/77 cases). Takes
// whole-degree Celsius, same as formatTempDisplay()/apparentTempC() above.
function tempGradientColorByte(tempCRaw) {
  var c = (typeof tempCRaw === 'number' && !isNaN(tempCRaw)) ? Math.round(tempCRaw) : 0;
  var rgb = sevenStopGradientRGB(c, -10, 40);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// ---- remaining Section 5 weather values (UV, humidity, wind, rain,
//      visibility, cloud cover, pressure, AQI, dew point, altitude) --------
//
// Same shape as the temperature cluster above: a display-text function and,
// where the watch used a *dynamic* (color_mode 3) gradient rather than a
// flat color, a matching *_COLOR byte function. Dew point has no gradient
// (feature_value_weather.c's case 37 always used a flat color, never
// feature_colors_*), so it gets a display function only.

// White (low) -> turquoise (high); verbatim port of
// feature_colors_white_to_turquoise_gradient() (feature_colors.c). Used by
// rain chance, humidity, wind speed, and altitude's gradients below.
function whiteToTurquoiseGradientRGB(value, minV, maxV) {
  if (maxV <= minV) return [255, 255, 255];
  var clamped = value < minV ? minV : (value > maxV ? maxV : value);
  var frac1000 = Math.trunc(((clamped - minV) * 1000) / (maxV - minV));
  var r = 255 - Math.trunc(((255 - 64) * frac1000) / 1000);
  var g = 255 - Math.trunc(((255 - 224) * frac1000) / 1000);
  var b = 255 - Math.trunc(((255 - 208) * frac1000) / 1000);
  return [r, g, b];
}

// Verbatim port of feature_colors_overcast_gray_gradient() (feature_colors.c),
// including its own internal 40% clamp (OVERCAST_CLOUD_THRESHOLD).
function overcastGrayGradientRGB(cloudPct) {
  var THRESHOLD = 40;
  var clamped = cloudPct < THRESHOLD ? THRESHOLD : cloudPct;
  var frac1000 = Math.trunc(((clamped - THRESHOLD) * 1000) / (100 - THRESHOLD));
  var v = 200 - Math.trunc((115 * frac1000) / 1000);
  return [v, v, v];
}

// UV index: PKJS already sends UV_INDEX_X10/UV_INDEX_CURRENT_X10 rounded
// the same way (see sendEclipseData/sendNoEclipseToday); mirror that exact
// rounding here so the truncated whole-number UV this derives matches
// bit-for-bit rather than re-rounding the raw value on a different path.
function formatUvDisplay(uvIndexRaw) {
  if (typeof uvIndexRaw !== 'number' || isNaN(uvIndexRaw)) return '';
  var uvX10 = Math.round(Math.max(0, Math.min(25.5, uvIndexRaw)) * 10);
  return 'UV' + Math.trunc(uvX10 / 10);
}
function uvGradientColorByte(uvIndexRaw) {
  var uvX10 = (typeof uvIndexRaw === 'number' && !isNaN(uvIndexRaw))
    ? Math.round(Math.max(0, Math.min(25.5, uvIndexRaw)) * 10) : 0;
  var rgb = sevenStopGradientRGB(Math.trunc(uvX10 / 10), 1, 13);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// Rain chance / humidity: plain "N%" text, white-to-turquoise 0-100 color.
function formatPercentDisplay(pctRaw) {
  if (typeof pctRaw !== 'number' || isNaN(pctRaw)) return '';
  return Math.round(pctRaw) + '%';
}
function percentGradientColorByte(pctRaw) {
  var pct = (typeof pctRaw === 'number' && !isNaN(pctRaw)) ? Math.round(pctRaw) : 0;
  var rgb = whiteToTurquoiseGradientRGB(pct, 0, 100);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// Visibility / cloud cover: "N%" text, overcast-gray color.
function overcastGrayColorByte(cloudPctRaw) {
  var pct = (typeof cloudPctRaw === 'number' && !isNaN(cloudPctRaw)) ? Math.round(cloudPctRaw) : 0;
  var rgb = overcastGrayGradientRGB(pct);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// Wind speed: verbatim port of feature_rules_convert_wind() (feature_rules.c)
// for the display text; gradient color uses the raw km/h value (0-60),
// matching feature_value_weather_compute()'s case 9, which grades on the
// unconverted speed regardless of which unit is displayed.
function formatWindSpeedDisplay(kmhRaw, windSpeedUnitCode) {
  if (typeof kmhRaw !== 'number' || isNaN(kmhRaw)) return '';
  var kmh = Math.round(kmhRaw);
  var shown;
  if (windSpeedUnitCode === 1) shown = Math.trunc((kmh * 621) / 1000);       // mph
  else if (windSpeedUnitCode === 2) shown = Math.trunc((kmh * 1000) / 3600); // m/s
  else if (windSpeedUnitCode === 3) shown = Math.trunc((kmh * 540) / 1000);  // knots
  else shown = kmh;                                                          // km/h
  return String(shown);
}
function windSpeedGradientColorByte(kmhRaw) {
  var kmh = (typeof kmhRaw === 'number' && !isNaN(kmhRaw)) ? Math.round(kmhRaw) : 0;
  var rgb = whiteToTurquoiseGradientRGB(kmh, 0, 60);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// Pressure: "N hPa" text, 7-stop gradient 970-1050 hPa. (The rising/
// falling/flat trend arrow drawn alongside this is a separate icon
// primitive, already PKJS-sourced via the existing PRESSURE_TREND field --
// nothing to migrate there.)
function formatPressureDisplay(pressureHpaRaw) {
  if (typeof pressureHpaRaw !== 'number' || isNaN(pressureHpaRaw)) return '';
  return Math.round(pressureHpaRaw) + ' hPa';
}
function pressureGradientColorByte(pressureHpaRaw) {
  var hpa = (typeof pressureHpaRaw === 'number' && !isNaN(pressureHpaRaw)) ? Math.round(pressureHpaRaw) : 0;
  var rgb = sevenStopGradientRGB(hpa, 970, 1050);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// AQI: "AQI N" text using whichever scale the user selected (aqiUnitCode:
// 0=US/0-300, 1=EU/0-100, matching feature_value_weather_compute()'s case
// 36), 7-stop gradient over that same scale's range.
function formatAqiDisplay(aqiValueRaw) {
  if (typeof aqiValueRaw !== 'number' || isNaN(aqiValueRaw)) return '';
  return 'AQI ' + Math.round(aqiValueRaw);
}
function aqiGradientColorByte(aqiValueRaw, aqiUnitCode) {
  var aqi = (typeof aqiValueRaw === 'number' && !isNaN(aqiValueRaw)) ? Math.round(aqiValueRaw) : 0;
  var maxV = (aqiUnitCode === 1) ? 100 : 300;
  var rgb = sevenStopGradientRGB(aqi, 0, maxV);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// Dew point: same unit conversion as temperature, so it reuses
// formatTempDisplay() directly -- no separate function needed. No color:
// case 37 never used a dynamic gradient (always the flat color_mode color).

// Altitude: "Nm"/"Nft" text (feature_rules_convert_temp-style integer
// conversion, verbatim port), white-to-turquoise 0-4000m color. Deliberately
// has NO "N/A" handling here -- unlike every other value on this page,
// ALTITUDE_M's -32000 "unavailable" sentinel is a stable fact rather than a
// transient fetch failure (see extraWeatherFieldsDict()'s own comment), and
// the watch's existing GColorLightGray/"N/A" special case for it is kept
// as-is on the watch rather than guessed at here -- see
// feature_value_weather.c's case 38 and IMPLEMENTATION_NOTES.md.
function formatAltitudeDisplay(altitudeMRaw, altitudeUnitCode) {
  if (typeof altitudeMRaw !== 'number' || isNaN(altitudeMRaw)) return '';
  var m = Math.round(altitudeMRaw);
  if (altitudeUnitCode === 1) {
    return Math.trunc((m * 328) / 100) + 'ft';
  }
  return m + 'm';
}
function altitudeGradientColorByte(altitudeMRaw) {
  var m = (typeof altitudeMRaw === 'number' && !isNaN(altitudeMRaw)) ? Math.round(altitudeMRaw) : 0;
  var rgb = whiteToTurquoiseGradientRGB(m, 0, 4000);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

// Icon-category selection (Section 4's "static icon" concept): verbatim
// port of feature_icons_weather_category() (feature_icons.c). Used both
// for the current-conditions weather icon and, with a neutral cloud%
// guess, for each forecast hour/day's icon -- same as the watch always
// did. Unlike every other function in this file, this doesn't produce
// display text or a color -- it produces the icon ID itself, so the watch
// no longer needs to interpret weather_condition/cloud_cover_pct into an
// icon category at all once this is present (see its fallback in
// feature_value_weather.c's cases 31/32/76/87-95).
function weatherIconCategory(conditionCode, cloudPct) {
  switch (conditionCode) {
    case 1: return 3; // fog
    case 2: return 4; // rain
    case 3: return 5; // snow
    case 4: return 6; // storm
    default:
      if (cloudPct < 20) return 0; // sunny
      if (cloudPct < 60) return 1; // partly cloudy
      return 2; // cloudy/overcast
  }
}

// ---- weather-condition color (completes weatherIconCategory's "PKJS
// decides the color too, since PKJS already decided the value" pairing --
// see the color-ownership principle in IMPLEMENTATION_NOTES.md's Phase 3
// update) -----------------------------------------------------------------
//
// Verbatim port of feature_colors_weather_condition_color()
// (feature_colors.c) and the two gradients private to it.
function sunnyYellowWhiteGradientRGB(cloudPct) {
  var OVERCAST_THRESHOLD = 40;
  var clamped = cloudPct > OVERCAST_THRESHOLD ? OVERCAST_THRESHOLD : cloudPct;
  var frac1000 = Math.trunc(((OVERCAST_THRESHOLD - clamped) * 1000) / OVERCAST_THRESHOLD);
  var b = 255 - Math.trunc((85 * frac1000) / 1000);
  return [255, 255, b];
}
function snowWhiteGradientRGB(cloudPct) {
  var frac1000 = Math.trunc((cloudPct * 1000) / 100);
  var rg = 255 - Math.trunc((85 * frac1000) / 1000);
  return [rg, rg, 255];
}
function weatherConditionColorByte(conditionCode, cloudPct, rainChancePct) {
  var rgb;
  var rain = (typeof rainChancePct === 'number' && !isNaN(rainChancePct)) ? rainChancePct : 0;
  if (conditionCode === 4) rgb = [255, 0, 0];
  else if (conditionCode === 3) rgb = snowWhiteGradientRGB(cloudPct);
  else if (conditionCode === 2) rgb = whiteToTurquoiseGradientRGB(rain, 0, 100);
  else if (conditionCode === 1) rgb = overcastGrayGradientRGB(cloudPct);
  else rgb = (cloudPct < 40) ? sunnyYellowWhiteGradientRGB(cloudPct) : overcastGrayGradientRGB(cloudPct);
  return packGColorByte(rgb[0], rgb[1], rgb[2]);
}

module.exports = {
  conditionFromWmoCode: conditionFromWmoCode,
  formatTempDisplay: formatTempDisplay,
  apparentTempC: apparentTempC,
  tempGradientColorByte: tempGradientColorByte,
  formatUvDisplay: formatUvDisplay,
  uvGradientColorByte: uvGradientColorByte,
  formatPercentDisplay: formatPercentDisplay,
  percentGradientColorByte: percentGradientColorByte,
  overcastGrayColorByte: overcastGrayColorByte,
  formatWindSpeedDisplay: formatWindSpeedDisplay,
  windSpeedGradientColorByte: windSpeedGradientColorByte,
  formatPressureDisplay: formatPressureDisplay,
  pressureGradientColorByte: pressureGradientColorByte,
  formatAqiDisplay: formatAqiDisplay,
  aqiGradientColorByte: aqiGradientColorByte,
  formatAltitudeDisplay: formatAltitudeDisplay,
  altitudeGradientColorByte: altitudeGradientColorByte,
  weatherIconCategory: weatherIconCategory,
  weatherConditionColorByte: weatherConditionColorByte
};