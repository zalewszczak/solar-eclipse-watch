/**
 * astro.js -- phone-side (PKJS) solar eclipse geometry.
 *
 * This uses Jean Meeus's *low-precision* series for the Sun
 * (Astronomical Algorithms ch.25) and a reduced set of the largest
 * terms of the ELP2000-based lunar series (ch.47), plus a standard
 * parallax-in-right-ascension correction so the Moon's position is
 * *topocentric* (i.e. correct for the observer's actual location --
 * this matters a lot for eclipses, since the Moon's parallax is
 * roughly 1 degree, about twice its own apparent diameter).
 *
 * Accuracy: roughly 1-3 arcminutes on the Moon's position, which
 * translates to contact-time errors on the order of a minute or two.
 * That's not good enough for a professional ephemeris, but it's
 * plenty to drive a live "is the Moon about to bite into the Sun"
 * indicator on your wrist. Treat all times as approximate and
 * cross-check against a proper eclipse calculator (e.g. NASA's) for
 * anything mission-critical (i.e. flying somewhere to see totality).
 *
 * Split into domain modules under astronomy/ (astronomy split, by
 * mathematical domain): angles.js (foundational trig/JD/coordinate
 * math), sun.js, moon.js, planets.js, eclipse.js (geometry + event
 * search), stars.js, meteors.js, aurora.js, iss-orbit.js. This file is
 * a thin facade: it requires each domain module and re-exports the
 * exact same public API this file always had, so every existing
 * caller (refresh-manager.js, iss.js) keeps requiring just './astro'
 * with no call-site changes.
 */

var eclipse = require('./astronomy/eclipse');
var moon = require('./astronomy/moon');
var stars = require('./astronomy/stars');
var meteors = require('./astronomy/meteors');
var aurora = require('./astronomy/aurora');
var issOrbit = require('./astronomy/iss-orbit');

module.exports = {
  findEclipse: eclipse.findEclipse,
  computeGeometry: eclipse.computeGeometry,
  computeDaySkySamples: eclipse.computeDaySkySamples,
  computeMoonPhase: moon.computeMoonPhase,
  findRiseSet: eclipse.findRiseSet,
  activeMeteorShower: meteors.activeMeteorShower,
  issLookAngle: issOrbit.issLookAngle,
  findNextIssPass: issOrbit.findNextIssPass,
  computeVisibleStars: stars.computeVisibleStars,
  STAR_CATALOG: stars.STAR_CATALOG,
  geomagneticLatitudeDeg: aurora.geomagneticLatitudeDeg,
  auroraVisibilityScore: aurora.auroraVisibilityScore
};
