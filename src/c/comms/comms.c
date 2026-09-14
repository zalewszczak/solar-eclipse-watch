#include <pebble.h>
#include "./comms.h"
#include "./comms_decoder.h"

#define STARTUP_REQUEST_DELAY_MS 3000

static EclipseData *s_data;
static CommsDataAppliedHandler s_data_applied;
static void *s_data_context;
static AppTimer *s_retry_timer = NULL;
static AppTimer *s_startup_timer = NULL;
static uint16_t s_retry_delay_s = 8;

bool comms_send_battery_saver_phase(uint8_t phase) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return false;
  dict_write_uint8(iter, MESSAGE_KEY_BATTERY_SAVER_PHASE, phase);
  return app_message_outbox_send() == APP_MSG_OK;
}

static void request_update(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
  app_message_outbox_send();
}

static void request_retry_callback(void *context) {
  (void)context;
  s_retry_timer = NULL;
  if (!s_data || s_data->valid) return;
  request_update();
  s_retry_delay_s = (s_retry_delay_s < 60) ? s_retry_delay_s * 2 : 60;
  s_retry_timer = app_timer_register((uint32_t)s_retry_delay_s * 1000, request_retry_callback, NULL);
}

static void startup_request_delay_callback(void *context) {
  (void)context;
  s_startup_timer = NULL;
  request_update();
  s_retry_timer = app_timer_register((uint32_t)s_retry_delay_s * 1000, request_retry_callback, NULL);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  (void)context;
  EclipseData *d = s_data;
  CommsChangeFlags changes = comms_decoder_apply(iter, d);

  if ((dict_find(iter, MESSAGE_KEY_DATA_VALID)) && d->valid && s_retry_timer) {
    // Real data made it through -- no need to keep pinging PKJS
    // for it anymore.
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }

  if (!d->valid) {
    if (s_data_applied) s_data_applied(changes, s_data_context);
    return;
  }

  if (s_data_applied) s_data_applied(changes, s_data_context);
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

void comms_init(EclipseData *data, CommsDataAppliedHandler handler, void *context) {
  s_data = data;
  s_data_applied = handler;
  s_data_context = context;
  s_retry_delay_s = 8;
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(APPMSG_INBOX_SIZE, APPMSG_OUTBOX_SIZE);
  s_startup_timer = app_timer_register(STARTUP_REQUEST_DELAY_MS, startup_request_delay_callback, NULL);
}

void comms_deinit(void) {
  if (s_startup_timer) {
    app_timer_cancel(s_startup_timer);
    s_startup_timer = NULL;
  }
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  s_data = NULL;
  s_data_applied = NULL;
  s_data_context = NULL;
}
