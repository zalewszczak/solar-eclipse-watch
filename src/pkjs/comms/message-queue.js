// ---- AppMessage send queue -------------------------------------------
//
// Moved to comms/message-queue.js (communication subsystem extraction).
// Behavior is unchanged; only the module boundary is new. enqueueFlatDict()
// is the only entry point anything outside this file needs -- everything
// else here (the queue array, in-flight flag, startup delay, raw-message
// log) is this module's own private state.

var messageSchema = require('./message-schema');
var MSG_TYPE = messageSchema.MSG_TYPE;
var MSG_TYPE_SEND_ORDER = messageSchema.MSG_TYPE_SEND_ORDER;
var KEY_TYPE_MAP = messageSchema.KEY_TYPE_MAP;

// Only one AppMessage can be in flight at a time -- s_sendQueue holds
// the chunks still waiting to go out for the current send*() call(s),
// s_sendInFlight guards against overlapping Pebble.sendAppMessage()
// calls (which would otherwise error/clobber each other). Each queue
// entry is {dict, batchIndex, batchTotal} rather than the bare dict --
// batchIndex/batchTotal (1-based position / count within THIS
// enqueueFlatDict() call, e.g. "3/6") are never sent to the watch
// (only entry.dict is), just carried alongside for
// recordRawMessage()'s own log entries and the Testing section's
// per-chunk button labels in the settings page.
var s_sendQueue = [];
var s_sendInFlight = false;

// Nothing actually leaves the phone for the first few seconds after
// PKJS starts up, regardless of what queued it (index.js's 'ready'
// handler's own first refresh, a REQUEST_UPDATE reply, a settings-page
// Save) -- gated here, in the one place every send*() call already
// funnels through (see pumpSendQueue()), rather than in each
// individual caller, so nothing can slip through by some other path.
// Lets the watch's own screen finish laying out/settling first before
// a message starts changing what's on it -- the watch itself holds
// its own first outbound request back a few seconds for the same
// reason (see request_update()'s own comment in pebble-eclipse-watch.c),
// so this is the phone-side half of that same "let it settle first"
// intent, on its own longer timer since the phone doesn't know
// whether the watch's request has actually arrived yet.
var PHONE_STARTUP_SEND_DELAY_MS = 5000;
var s_phoneStartupSendDelayElapsed = false;
setTimeout(function () {
  s_phoneStartupSendDelayElapsed = true;
  pumpSendQueue(); // resume whatever queued up during the delay, if anything did
}, PHONE_STARTUP_SEND_DELAY_MS);

// Last RAW_MESSAGE_LOG_MAX individual AppMessage chunks actually sent
// (acked, not just attempted), for the Testing section's raw-message
// browser -- see recordRawMessage() below and buildConfigHtml()'s own
// rawMessageLog. Chunking (see message-schema.js's KEY_TYPE_MAP)
// means any one send*() call now produces several small messages
// instead of one big one, so logging only "the last thing sent" (the
// old LAST_COMPUTED_DICT/"Reload last sent data" button) stopped
// being useful -- it would just show whichever single chunk happened
// to go out last, not the full picture. Logging every chunk instead
// means a full refresh's whole 6-7-message batch is all inspectable
// afterwards, not just its tail end.
var RAW_MESSAGE_LOG_MAX = 10;
function recordRawMessage(batchIndex, batchTotal, dict) {
  try {
    var entries = [];
    var raw = localStorage.getItem('RAW_MESSAGE_LOG');
    if (raw) entries = JSON.parse(raw);
    if (!Array.isArray(entries)) entries = [];
    entries.push({ t: Date.now(), batchIndex: batchIndex, batchTotal: batchTotal, dict: dict });
    if (entries.length > RAW_MESSAGE_LOG_MAX) entries = entries.slice(entries.length - RAW_MESSAGE_LOG_MAX);
    localStorage.setItem('RAW_MESSAGE_LOG', JSON.stringify(entries));
  } catch (e) {
    // Not critical if this fails (storage full, etc.) -- just means the
    // settings page's Testing section won't have a fresh log this time.
  }
}

// Buckets a flat {KEY: value} dict (as every send*() function used to
// build a single one) into per-MESSAGE_TYPE chunks and queues them.
// A key with no entry in KEY_TYPE_MAP is a bug (a key was added
// without updating KEY_TYPE_MAP in message-schema.js) -- rather than silently dropping
// it, it's logged and placed in SETTINGS so it still reaches the
// watch instead of vanishing.
function enqueueFlatDict(flatDict) {
  var buckets = {};
  Object.keys(flatDict).forEach(function (k) {
    var type = KEY_TYPE_MAP[k];
    if (type === undefined) {
      console.log('eclipse-watch: no MSG_TYPE mapping for key ' + k + ', defaulting to SETTINGS -- update KEY_TYPE_MAP');
      type = MSG_TYPE.SETTINGS;
    }
    if (!buckets[type]) buckets[type] = {};
    buckets[type][k] = flatDict[k];
  });
  var batch = [];
  MSG_TYPE_SEND_ORDER.forEach(function (type) {
    if (buckets[type]) {
      buckets[type]['MESSAGE_TYPE'] = type;
      batch.push(buckets[type]);
    }
  });
  batch.forEach(function (dict, i) {
    s_sendQueue.push({ dict: dict, batchIndex: i + 1, batchTotal: batch.length });
  });
  pumpSendQueue();
}

function pumpSendQueue() {
  if (s_sendInFlight || s_sendQueue.length === 0) return;
  if (!s_phoneStartupSendDelayElapsed) return; // the setTimeout above re-pumps once the startup delay elapses
  s_sendInFlight = true;
  var entry = s_sendQueue.shift();
  var chunk = entry.dict;
  var msgType = chunk['MESSAGE_TYPE'];
  Pebble.sendAppMessage(chunk, function () {
    console.log('eclipse-watch: chunk sent (type ' + msgType + '), ' + s_sendQueue.length + ' queued');
    recordRawMessage(entry.batchIndex, entry.batchTotal, chunk);
    s_sendInFlight = false;
    pumpSendQueue();
  }, function (e) {
    // Drop rather than retry-forever: the next refresh/settings-save
    // cycle will enqueue a fresh chunk of the same type anyway, so a
    // stuck retry here would just delay everything behind it.
    console.log('eclipse-watch: chunk send failed (type ' + msgType + '), dropping: ' + JSON.stringify(e));
    s_sendInFlight = false;
    pumpSendQueue();
  });
}

module.exports = {
  enqueueFlatDict: enqueueFlatDict
};
