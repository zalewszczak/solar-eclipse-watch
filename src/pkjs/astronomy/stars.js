// Moved out of astro.js (astronomy split, by mathematical domain).

var angles = require('./angles');
var norm360 = angles.norm360;
var julianDay = angles.julianDay;
var julianCenturies = angles.julianCenturies;
var greenwichSiderealDeg = angles.greenwichSiderealDeg;
var altAz = angles.altAz;

// ---- bright named stars (space-view sky mode) -----------------------
// A curated list of the sky's brightest stars, fixed J2000 RA(hours)/
// Dec(degrees)/apparent magnitude -- proper motion and precession are
// both negligible at this app's timescale and pixel precision, so
// (unlike the Sun/Moon/planets) these coordinates never need
// recomputing, just re-projecting to alt/az for "now". Order here is
// the order STAR_ALT_SAMPLES/STAR_AZ_SAMPLES are packed in, and MUST
// match the STARS[] name/magnitude table in background_layer.c exactly
// -- that side owns the display name, this side owns the position.
var STAR_CATALOG = [
  { raH: 6.7525,  decDeg: -16.7161 }, // Sirius
  { raH: 6.3992,  decDeg: -52.6957 }, // Canopus
  { raH: 14.2610, decDeg: 19.1825 },  // Arcturus
  { raH: 18.6156, decDeg: 38.7837 },  // Vega
  { raH: 5.2782,  decDeg: 45.9980 },  // Capella
  { raH: 5.2423,  decDeg: -8.2016 },  // Rigel
  { raH: 7.6550,  decDeg: 5.2250 },   // Procyon
  { raH: 5.9195,  decDeg: 7.4071 },   // Betelgeuse
  { raH: 19.8464, decDeg: 8.8683 },   // Altair
  { raH: 4.5987,  decDeg: 16.5093 },  // Aldebaran
  { raH: 16.4901, decDeg: -26.4320 }, // Antares
  { raH: 13.4199, decDeg: -11.1613 }, // Spica
  { raH: 7.7553,  decDeg: 28.0262 },  // Pollux
  { raH: 22.9608, decDeg: -29.6222 }, // Fomalhaut
  { raH: 20.6905, decDeg: 45.2803 },  // Deneb
  { raH: 10.1395, decDeg: 11.9672 }   // Regulus
];

// alt/az (degrees, az north-based via altAz()) for every cataloged
// star at `date`/`latDeg`/`lonDeg` -- one pass of the same sidereal-
// time machinery computeGeometry() uses for the Moon/planets, just
// against fixed RA/Dec instead of an ephemeris. No visibility
// filtering here (space-view mode wants stars below the horizon
// omitted on-watch, not here) -- see background_layer.c.
function computeVisibleStars(date, latDeg, lonDeg) {
  var jd = julianDay(date);
  var T = julianCenturies(jd);
  var lst = norm360(greenwichSiderealDeg(jd, T) + lonDeg);
  return STAR_CATALOG.map(function (star) {
    var aa = altAz(star.raH * 15, star.decDeg, latDeg, lst);
    return { alt: aa.alt, az: aa.az };
  });
}

module.exports = {
  STAR_CATALOG: STAR_CATALOG,
  computeVisibleStars: computeVisibleStars
};
