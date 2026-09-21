// ---- live webfont manager for the embedded settings webview ------------
//
// This module exports JavaScript source text, just like config-preview.js.
// The resulting code runs inside the phone's settings WebView, not inside
// PKJS, so it deliberately uses browser APIs only and does not require().
//
// Goals:
//   * make the live canvas preview use the actual webfont when available;
//   * load remote font binaries lazily instead of downloading every font;
//   * accept a user-supplied TTF/OTF/WOFF/WOFF2 file as a preview override;
//   * persist imported font binaries best-effort between settings sessions;
//   * fall back cleanly to the existing CSS font / generated PNG previews.
//
// The watch-side font resource is NOT changed by an imported file. A custom
// file is a settings-preview override only; the selected watch font remains
// the normal FONT_LOOKUP/resource selection unless the app itself is rebuilt
// with that font as a resource.

module.exports =
'var FontManager = (function () {' +
'  var entries = [];' +
'  var byId = {};' +
'  var states = {};' +
'  var callbacks = {};' +
'  var customRecords = {};' +
'  var DB_NAME = "EclipzFontCache";' +
'  var DB_VERSION = 1;' +
'  var STORE_NAME = "fonts";' +
'  var STORAGE_PREFIX = "ECLIPZ_FONT_CACHE_V1_";' +
'  var initialized = false;' +
'  var storageProbeDone = false;' +
'  var idb = null;' +
'' +
'  function safeCall(fn) {' +
'    try { if (typeof fn === "function") fn(); } catch (e) {}' +
'  }' +
'' +
'  function notify(id) {' +
'    var list = callbacks[id] || [];' +
'    delete callbacks[id];' +
'    list.forEach(function (fn) { safeCall(fn); });' +
'  }' +
'' +
'  function addCallback(id, fn) {' +
'    if (typeof fn !== "function") return;' +
'    if (!callbacks[id]) callbacks[id] = [];' +
'    callbacks[id].push(fn);' +
'  }' +
'' +
'  function entry(id) {' +
'    id = parseInt(id, 10);' +
'    return byId[id] || null;' +
'  }' +
'' +
'  function bool(v) {' +
'    return v === true || v === 1 || v === "1" || v === "true";' +
'  }' +
'' +
'  function familyFromPreview(preview) {' +
'    var m = /font-family\\s*:\\s*([^;]+);?/i.exec(preview || "");' +
'    if (!m) return "sans-serif";' +
'    return m[1].trim();' +
'  }' +
'' +
'  function weightFromEntry(f) {' +
'    var n = parseInt(f && f.weight, 10);' +
'    return isNaN(n) ? 400 : n;' +
'  }' +
'' +
'  function italicFromEntry(f) {' +
'    return bool(f && f.italic);' +
'  }' +
'' +
'  function customFamily(id) {' +
'    return "EclipzImportedFont_" + parseInt(id, 10);' +
'  }' +
'' +
'  function cssFontFor(entryObj, px, familyOverride) {' +
'    var weight = weightFromEntry(entryObj);' +
'    var italic = italicFromEntry(entryObj) ? "italic " : "";' +
'    var bold = weight >= 600 ? "bold " : "";' +
'    var family = familyOverride || familyFromPreview(entryObj && entryObj.preview);' +
'    return italic + bold + px + "px " + family;' +
'  }' +
'' +
'  function customFamilyFor(id) {' +
'    var s = states[parseInt(id, 10)];' +
'    return s && s.customLoaded ? customFamily(id) : null;' +
'  }' +
'' +
'  function xhr(url, responseType, done) {' +
'    try {' +
'      var x = new XMLHttpRequest();' +
'      x.open("GET", url, true);' +
'      if (responseType) x.responseType = responseType;' +
'      x.onload = function () {' +
'        if (x.status >= 200 && x.status < 300 || x.status === 0) done(null, x);' +
'        else done(new Error("HTTP " + x.status));' +
'      };' +
'      x.onerror = function () { done(new Error("Network error")); };' +
'      x.ontimeout = function () { done(new Error("Network timeout")); };' +
'      x.timeout = 12000;' +
'      x.send();' +
'    } catch (e) {' +
'      done(e);' +
'    }' +
'  }' +
'' +
'  function arrayBufferToBase64(buffer) {' +
'    var bytes = new Uint8Array(buffer);' +
'    var chunk = 0x8000;' +
'    var out = "";' +
'    for (var i = 0; i < bytes.length; i += chunk) {' +
'      var end = Math.min(i + chunk, bytes.length);' +
'      var s = "";' +
'      for (var j = i; j < end; j++) s += String.fromCharCode(bytes[j]);' +
'      out += btoa(s);' +
'    }' +
'    return out;' +
'  }' +
'' +
'  function base64ToArrayBuffer(base64) {' +
'    var raw = atob(base64);' +
'    var bytes = new Uint8Array(raw.length);' +
'    for (var i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);' +
'    return bytes.buffer;' +
'  }' +
'' +
'  function storageKey(id) {' +
'    return STORAGE_PREFIX + parseInt(id, 10);' +
'  }' +
'' +
'  function localStorageGet(id, done) {' +
'    try {' +
'      var raw = localStorage.getItem(storageKey(id));' +
'      if (!raw) return done(null);' +
'      var rec = JSON.parse(raw);' +
'      if (!rec || !rec.data) return done(null);' +
'      done({ id: parseInt(id, 10), name: rec.name || "Imported font", data: base64ToArrayBuffer(rec.data) });' +
'    } catch (e) { done(null); }' +
'  }' +
'' +
'  function localStoragePut(id, record) {' +
'    try {' +
'      var b64 = arrayBufferToBase64(record.data);' +
'      localStorage.setItem(storageKey(id), JSON.stringify({ name: record.name || "Imported font", data: b64 }));' +
'      return true;' +
'    } catch (e) {' +
'      return false;' +
'    }' +
'  }' +
'' +
'  function openDb(done) {' +
'    if (idb) return done(idb);' +
'    if (storageProbeDone && !window.indexedDB) return done(null);' +
'    storageProbeDone = true;' +
'    try {' +
'      if (!window.indexedDB) return done(null);' +
'      var req = indexedDB.open(DB_NAME, DB_VERSION);' +
'      req.onupgradeneeded = function (ev) {' +
'        try {' +
'          if (!ev.target.result.objectStoreNames.contains(STORE_NAME)) ev.target.result.createObjectStore(STORE_NAME, { keyPath: "id" });' +
'        } catch (e) {}' +
'      };' +
'      req.onsuccess = function () { idb = req.result; done(idb); };' +
'      req.onerror = function () { done(null); };' +
'    } catch (e) {' +
'      done(null);' +
'    }' +
'  }' +
'' +
'  function cacheGet(id, done) {' +
'    openDb(function (db) {' +
'      if (!db) return localStorageGet(id, done);' +
'      try {' +
'        var req = db.transaction([STORE_NAME], "readonly").objectStore(STORE_NAME).get(parseInt(id, 10));' +
'        req.onsuccess = function () {' +
'          var r = req.result;' +
'          if (r && r.data) return done({ id: r.id, name: r.name || "Imported font", data: r.data });' +
'          localStorageGet(id, done);' +
'        };' +
'        req.onerror = function () { localStorageGet(id, done); };' +
'      } catch (e) { localStorageGet(id, done); }' +
'    });' +
'  }' +
'' +
'  function cachePut(id, record) {' +
'    var saved = false;' +
'    openDb(function (db) {' +
'      if (db) {' +
'        try {' +
'          var req = db.transaction([STORE_NAME], "readwrite").objectStore(STORE_NAME).put({ id: parseInt(id, 10), name: record.name || "Imported font", data: record.data });' +
'          req.onsuccess = function () { saved = true; };' +
'          req.onerror = function () { localStoragePut(id, record); };' +
'          return;' +
'        } catch (e) {}' +
'      }' +
'      localStoragePut(id, record);' +
'    });' +
'    return saved;' +
'  }' +
'' +
'  function registerBinary(id, buffer, name, done) {' +
'    var f = entry(id);' +
'    if (!f || !window.FontFace || !document.fonts) return safeCall(done);' +
'    var family = customFamily(id);' +
'    var descriptors = { weight: String(weightFromEntry(f)), style: italicFromEntry(f) ? "italic" : "normal" };' +
'    try {' +
'      var face = new FontFace(family, buffer, descriptors);' +
'      face.load().then(function (loaded) {' +
'        try { document.fonts.add(loaded); } catch (e) {}' +
'        states[id] = states[id] || {};' +
'        states[id].customLoaded = true;' +
'        states[id].customName = name || "Imported font";' +
'        states[id].status = "loaded";' +
'        customRecords[id] = { id: parseInt(id, 10), name: name || "Imported font", data: buffer };' +
'        cachePut(id, customRecords[id]);' +
'        safeCall(done);' +
'      }, function () {' +
'        states[id] = states[id] || {};' +
'        states[id].status = "failed";' +
'        safeCall(done);' +
'      });' +
'    } catch (e) {' +
'      states[id] = states[id] || {};' +
'      states[id].status = "failed";' +
'      safeCall(done);' +
'    }' +
'  }' +
'' +
'  function installCached(id, done) {' +
'    cacheGet(id, function (record) {' +
'      if (!record) return safeCall(done);' +
'      registerBinary(id, record.data, record.name, done);' +
'    });' +
'  }' +
'' +
'  function googleCssUrl(f) {' +
'    if (!f || !f.google) return null;' +
'    var ital = italicFromEntry(f) ? "1" : "0";' +
'    return "https://fonts.googleapis.com/css2?family=" + encodeURIComponent(f.google).replace(/%20/g, "+") + ":ital,wght@" + ital + "," + weightFromEntry(f) + "&display=swap";' +
'  }' +
'' +
'  function loadRemote(id, done) {' +
'    var f = entry(id);' +
'    if (!f) return done(false);' +
'    /* Direct webfont URLs can be loaded as binaries; Google/CDN providers use stylesheet-backed faces because their CSS can split one family into unicode-range faces. */' +
'    if (f.webfontUrl) {' +
'      return xhr(f.webfontUrl, "arraybuffer", function (err, x) {' +
'        if (err || !x || !x.response || !window.FontFace || !document.fonts) {' +
'          if (document.fonts && document.fonts.load) {' +
'            try { document.fonts.load(cssFontFor(f, 16, familyFromPreview(f.preview))).then(function () { states[id].cssReady = true; states[id].status = "loaded"; done(true); }, function () { done(false); }); return; } catch (e) {}' +
'          }' +
'          return done(false);' +
'        }' +
'        var family = "EclipzRemoteFont_" + parseInt(id, 10);' +
'        try {' +
'          var face = new FontFace(family, x.response, { weight: String(weightFromEntry(f)), style: italicFromEntry(f) ? "italic" : "normal" });' +
'          face.load().then(function (loaded) {' +
'            try { document.fonts.add(loaded); } catch (e) {}' +
'            states[id] = states[id] || {};' +
'            states[id].remoteLoaded = true;' +
'            states[id].remoteFamily = family;' +
'            states[id].status = "loaded";' +
'            done(true);' +
'          }, function () { done(false); });' +
'        } catch (e) { done(false); }' +
'      });' +
'    }' +
'    if (document.fonts && document.fonts.load) {' +
'      try {' +
'        var family = familyFromPreview(f.preview);' +
'        var spec = cssFontFor(f, 16, family);' +
'        document.fonts.load(spec).then(function () {' +
'          states[id] = states[id] || {};' +
'          states[id].cssReady = true;' +
'          states[id].status = "loaded";' +
'          done(true);' +
'        }, function () { done(false); });' +
'        return;' +
'      } catch (e) {}' +
'    }' +
'    done(false);' +
'  }' +
'' +
'  function ensure(id, done) {' +
'    id = parseInt(id, 10);' +
'    if (!entry(id)) return safeCall(done);' +
'    var s = states[id] || (states[id] = {});' +
'    if (s.customLoaded || s.remoteLoaded || s.cssReady) return true;' +
'    if (s.status === "failed" && s.lastAttempt && Date.now() - s.lastAttempt < 30000) return false;' +
'    addCallback(id, done);' +
'    if (s.loading) return;' +
'    s.loading = true;' +
'    s.lastAttempt = Date.now();' +
'    installCached(id, function () {' +
'      var afterCache = states[id] && states[id].customLoaded;' +
'      if (afterCache) {' +
'        states[id].loading = false;' +
'        notify(id);' +
'        return;' +
'      }' +
'      loadRemote(id, function (ok) {' +
'        s.loading = false;' +
'        if (ok) {' +
'          s.cssReady = true;' +
'          notify(id);' +
'        } else {' +
'          s.status = "failed";' +
'          notify(id);' +
'        }' +
'      });' +
'    });' +
'    return false;' +
'  }' +
'' +
'  function canvasFont(id, px, fallbackCss) {' +
'    id = parseInt(id, 10);' +
'    var f = entry(id);' +
'    var s = states[id];' +
'    if (!f) return fallbackCss || (px + "px sans-serif");' +
'    if (s && s.customLoaded) return cssFontFor(f, px, customFamily(id));' +
'    if (s && s.remoteLoaded) return cssFontFor(f, px, s.remoteFamily);' +
'    return fallbackCss || cssFontFor(f, px);' +
'  }' +
'' +
'  function previewStyle(id, baseStyle) {' +
'    var s = states[parseInt(id, 10)];' +
'    if (s && s.customLoaded) return baseStyle + " font-family:\\"" + customFamily(id) + "\\";";' +
'    if (s && s.remoteLoaded) return baseStyle + " font-family:\\"" + s.remoteFamily + "\\";";' +
'    return baseStyle;' +
'  }' +
'' +
'  function importFile(id, file, done) {' +
'    id = parseInt(id, 10);' +
'    if (!entry(id) || !file) return safeCall(done);' +
'    try {' +
'      var reader = new FileReader();' +
'      reader.onload = function () {' +
'        var buffer = reader.result;' +
'        if (!buffer) return safeCall(done);' +
'        states[id] = states[id] || {};' +
'        states[id].loading = true;' +
'        registerBinary(id, buffer, file.name || "Imported font", function () {' +
'          states[id].loading = false;' +
'          notify(id);' +
'          safeCall(done);' +
'        });' +
'      };' +
'      reader.onerror = function () { safeCall(done); };' +
'      reader.readAsArrayBuffer(file);' +
'    } catch (e) { safeCall(done); }' +
'  }' +
'' +
'  function importSelected(id, input, done) {' +
'    var file = input && input.files && input.files[0];' +
'    if (!file) return;' +
'    importFile(id, file, done);' +
'    try { input.value = ""; } catch (e) {}' +
'  }' +
'' +
'  function clearCustom(id, done) {' +
'    id = parseInt(id, 10);' +
'    delete customRecords[id];' +
'    if (states[id]) {' +
'      states[id].customLoaded = false;' +
'      states[id].customName = null;' +
'      if (states[id].remoteLoaded) states[id].status = "loaded";' +
'      else states[id].status = null;' +
'    }' +
'    try { localStorage.removeItem(storageKey(id)); } catch (e) {}' +
'    openDb(function (db) {' +
'      if (!db) return safeCall(done);' +
'      try {' +
'        var req = db.transaction([STORE_NAME], "readwrite").objectStore(STORE_NAME).delete(id);' +
'        req.onsuccess = function () { safeCall(done); };' +
'        req.onerror = function () { safeCall(done); };' +
'      } catch (e) { safeCall(done); }' +
'    });' +
'  }' +
'' +
'  function customName(id) {' +
'    var s = states[parseInt(id, 10)];' +
'    return s && s.customLoaded ? s.customName : null;' +
'  }' +
'' +
'  function init(list) {' +
'    if (initialized) return;' +
'    initialized = true;' +
'    entries = list || [];' +
'    entries.forEach(function (f) { byId[parseInt(f.id, 10)] = f; states[parseInt(f.id, 10)] = {}; });' +
'  }' +
'' +
'  return {' +
'    init: init,' +
'    ensure: ensure,' +
'    canvasFont: canvasFont,' +
'    previewStyle: previewStyle,' +
'    importFile: importFile,' +
'    importSelected: importSelected,' +
'    customName: customName,' +
'    clearCustom: clearCustom,' +
'    customFamilyFor: customFamilyFor' +
'  };' +
'})();' +
'';
