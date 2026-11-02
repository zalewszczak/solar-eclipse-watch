// ---- bundled settings page <-> PKJS bridge ---------------------------------------
//
// Newer Pebble apps open a settings page bundled in the pbw ("configPage":
// "config.html" in package.json) and let it talk to PKJS while it is open:
// the page calls Pebble.sendMessage('pkjs', msg) and PKJS answers through the
// 'configmessage' event. config.html is only a small bootstrap: it asks for
// the real page (the same HTML the legacy flow builds, see config-page.js) in
// chunks, writes it into the document, and from then on sends every change to
// PKJS as it happens -- so the watch follows the settings live.
//
// Older apps never fire 'configmessage' and ignore "configPage": they open the
// legacy data: URI page via 'showConfiguration' and get the settings back when
// it closes, exactly as before.
//
// Messages (page -> PKJS), all answered with e.respond():
//   { type: 'page', index }      -> { total, chunk }     page HTML, chunk by chunk
//   { type: 'settings', settings } -> { ok }             apply (and push to the watch)
//   { type: 'action', payload }  -> { ok }               debug sends (full keyset / fake weather)
//   { type: 'plugins' }          -> { catalogue }        re-read the plugin catalogue
//   { type: 'preferences' }      -> { preferences }      re-read the Pebble app's user preferences
//                                                        (units, complications)

var CHUNK_CHARS = 150000;

// buildPage(): () => html string; applySettings(obj): the same function the
// legacy 'webviewclosed' flow uses.
function install(buildPage, applySettings, pluginManager, preferences) {
  if (typeof Pebble === 'undefined' || typeof Pebble.addEventListener !== 'function') return;
  var pageChunks = [];

  Pebble.addEventListener('configmessage', function (e) {
    var msg = (e && e.data) || {};
    function reply(obj) { try { e.respond(obj); } catch (err) { console.log('eclipse-watch: config reply failed - ' + err.message); } }
    try {
      switch (msg.type) {
        case 'page': {
          var index = parseInt(msg.index, 10) || 0;
          if (index === 0 || !pageChunks.length) {
            var html = buildPage();
            pageChunks = [];
            for (var i = 0; i < html.length; i += CHUNK_CHARS) pageChunks.push(html.substr(i, CHUNK_CHARS));
          }
          reply({ total: pageChunks.length, chunk: pageChunks[index] || '' });
          if (index >= pageChunks.length - 1) {
            // Everything delivered: keep the chunks around briefly for a retry, then free the memory.
            setTimeout(function () { pageChunks = []; }, 60000);
          }
          break;
        }
        case 'settings':
          applySettings(msg.settings || {});
          reply({ ok: true });
          break;
        case 'action':
          applySettings(msg.payload || {});
          reply({ ok: true });
          break;
        case 'plugins':
          reply({ catalogue: pluginManager.readCatalogue() });
          break;
        case 'preferences':
          // Answers once the Pebble app has replied (or after a short timeout, with the cache).
          preferences.capture(3000, function (prefs) { reply({ preferences: prefs }); });
          break;
        default:
          reply({ ok: false, error: 'unknown message' });
      }
    } catch (err) {
      console.log('eclipse-watch: config message failed - ' + err.message);
      reply({ ok: false, error: err.message });
    }
  });
}

module.exports = { install: install };
