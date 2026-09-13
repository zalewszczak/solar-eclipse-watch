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

module.exports = {
  conditionFromWmoCode: conditionFromWmoCode
};
