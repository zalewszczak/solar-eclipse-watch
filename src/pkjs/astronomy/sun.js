// ---- Sun (low precision, Meeus ch.25) --------------------------------
//
// Moved out of astro.js (astronomy split, by mathematical domain).

var angles = require('./angles');
var sind = angles.sind;
var cosd = angles.cosd;
var atan2d = angles.atan2d;
var asind = angles.asind;
var norm360 = angles.norm360;

function sunPosition(T) {
  var L0 = norm360(280.46646 + 36000.76983 * T + 0.0003032 * T * T);
  var M = norm360(357.52911 + 35999.05029 * T - 0.0001537 * T * T);
  var e = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;

  var C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sind(M) +
          (0.019993 - 0.000101 * T) * sind(2 * M) +
          0.000289 * sind(3 * M);

  var trueLong = L0 + C;
  var trueAnom = M + C;
  var R = (1.000001018 * (1 - e * e)) / (1 + e * cosd(trueAnom)); // AU

  var omega = 125.04 - 1934.136 * T;
  var apparentLong = trueLong - 0.00569 - 0.00478 * sind(omega);

  var eps0 = 23 + 26 / 60 + 21.448 / 3600 -
             (46.8150 * T + 0.00059 * T * T - 0.001813 * T * T * T) / 3600;
  var eps = eps0 + 0.00256 * cosd(omega);

  var ra = norm360(atan2d(cosd(eps) * sind(apparentLong), cosd(apparentLong)));
  var dec = asind(sind(eps) * sind(apparentLong));

  // Angular semidiameter in degrees (959.63" at 1 AU).
  var semidiameter = (959.63 / 3600) / R;

  return { ra: ra, dec: dec, distanceAU: R, semidiameterDeg: semidiameter, eclipticLon: norm360(apparentLong) };
}

// Sun's topocentric altitude (parallax on the Sun is ~8.8" and
// ignored here -- irrelevant at this precision).
function sunAltitude(sun, latDeg, lstDeg) {
  var H = norm360(lstDeg - sun.ra);
  return asind(sind(latDeg) * sind(sun.dec) + cosd(latDeg) * cosd(sun.dec) * cosd(H));
}

module.exports = {
  sunPosition: sunPosition,
  sunAltitude: sunAltitude
};
