// Preview "(token)" -> icon file name (resources/images/<style>/<name>.png).
// The tokens are the placeholders PRIMITIVE_PREVIEW_TEXT / the content
// previews use for icons; where one maps here, the settings preview draws the
// real icon art instead of the text (config-preview.js). Names mirror the kinds
// the watch resolves in primitive_resolver.c (see STYLED_ICONS in
// feature_icons.c). Tokens absent from this table (battery, moon, compass,
// pressure, altitude, forecast offset) are drawn natively/vectorially on the
// watch and keep their text placeholder in the preview.
// "(weather)" is special-cased in the preview: it picks the day/night
// variant of the weather icon for the sample condition.
var PREVIEW_ICON_TOKENS = {
  hr: 'heart',
  steps: 'foot',
  bt: 'bluetooth',
  quiet: 'quiet_time',
  spkr: 'quiet_time_muted',
  rain: 'umbrella',
  humid: 'droplet',
  wind: 'wind',
  vis: 'eye',
  cloud: 'cloud',
  refresh: 'refresh',
  sleep: 'bed_clock',
  rest: 'bed_check_clock',
  qual: 'bed_check',
  bed: 'bed_arrow_in',
  wake: 'bed_arrow_out',
  pin: 'gps_pin',
  planets: 'planets',
  saturn: 'saturn_ring',
  iss: 'iss',
  aurora: 'aurora',
  sun: 'sun_time_rise',
  weather: 'weather_partly_cloudy'
};

module.exports = { PREVIEW_ICON_TOKENS: PREVIEW_ICON_TOKENS };
