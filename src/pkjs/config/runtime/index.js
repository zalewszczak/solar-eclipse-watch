'use strict';

var fragments = [
  require('./core'),
  require('./settings'),
  require('./hands'),
  require('./markers'),
  require('./colors'),
  require('./features'),
  require('./fonts'),
  require('./presets'),
  require('./debug')
];

module.exports = function buildRuntime(context) {
  return fragments.map(function (build) {
    return build(context || {});
  }).join('');
};
