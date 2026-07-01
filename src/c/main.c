#include <pebble.h>

#define NAME_LEN 32
#define SEND_RETRY_MS 2500
#define MAX_AUTO_RETRIES 3
#define PERSIST_KEY_AUTO_CLOSE 1
#define PERSIST_KEY_IDLE_EXIT  2   // idle auto-exit timeout, seconds (0 = off)

#define HEADER_H     50   // big "REC m:ss" / "Ready" header
#define STATUS_ROW_H 44   // connecting/error/no-device row
#define ACTION_ROW_H 44   // Start / Stop row

static Window *s_window;
static MenuLayer *s_menu;

typedef enum { CONN_LOADING, CONN_READY, CONN_ERROR } ConnState;
static ConnState s_conn = CONN_LOADING;

static bool s_recording = false;
static char s_device_name[NAME_LEN + 1] = "";
static int32_t s_rec_start = 0; // epoch seconds
static int s_conn_count = 0;    // connected BLE device count

static uint8_t s_last_cmd = 1;
static int s_retries = 0;
static AppTimer *s_retry_timer = NULL;

static bool s_auto_close = true;        // CFG_AUTO_CLOSE; persisted
static bool s_close_after_send = false; // set on a Start/Stop tap; exits in outbox_sent

// CMD values: 1=request, 2=start, 3=stop
static void send_cmd(uint8_t cmd);

// ---- idle auto-exit: return to the watchface after s_idle_timeout_sec of no
// button press. Armed in the window's .appear, cancelled in .disappear, reset by
// menu_select. 0 = off; default 15s (persist + config). Recording continues on
// the phone after exit (the watch is only a remote). ----
static int       s_idle_timeout_sec = 15;
static AppTimer *s_idle_timer = NULL;
static bool      s_config_open = false;   // true while the phone config page is open (pauses idle)

static void idle_cancel(void) {
  if (s_idle_timer) { app_timer_cancel(s_idle_timer); s_idle_timer = NULL; }
}
static void idle_fire(void *ctx) {
  s_idle_timer = NULL;
  exit_reason_set(APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY);
  window_stack_pop_all(true);   // single-window app -> exits to the watchface
}
static void idle_reset(void) {
  if (s_config_open) return;               // never (re)arm while the phone config page is open
  if (s_idle_timeout_sec <= 0) { idle_cancel(); return; }
  if (s_idle_timer) { app_timer_reschedule(s_idle_timer, s_idle_timeout_sec * 1000); }
  else { s_idle_timer = app_timer_register(s_idle_timeout_sec * 1000, idle_fire, NULL); }
}
// Tolerant read: default Clay auto-send delivers a `select` as a CString. -1 =
// key absent. NB: hand-rolled digit parse — atoi/strtol are NOT exported by the
// Core firmware (hard fault).
static int idle_read_seconds(Tuple *t) {
  if (!t) { return -1; }
  if (t->type == TUPLE_CSTRING) {
    int v = 0; const char *p = t->value->cstring;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); }
    return v;
  }
  return (int)t->value->int32;
}
static void idle_appear(Window *w) { idle_reset(); }
static void idle_disappear(Window *w) { idle_cancel(); }

// Whether we have a "real" action row (Start needs a connected device; Stop always shown while recording).
static bool has_action_row() { return s_recording || s_conn_count > 0; }
static bool showing_status_row() { return !has_action_row(); }

static void request(uint8_t cmd) { s_retries = 0; send_cmd(cmd); }

static void send_cmd(uint8_t cmd) {
  s_last_cmd = cmd;
  s_conn = CONN_LOADING;
  if (s_menu) menu_layer_reload_data(s_menu);

  DictionaryIterator *iter;
  AppMessageResult r = app_message_outbox_begin(&iter);
  if (r != APP_MSG_OK) { APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_begin failed: %d", (int)r); return; }
  dict_write_int32(iter, MESSAGE_KEY_CMD, cmd);
  app_message_outbox_send();
}

static void retry_timer_cb(void *data) { s_retry_timer = NULL; send_cmd(s_last_cmd); }

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_ST_RECORDING))) s_recording = (t->value->uint8 != 0);
  if ((t = dict_find(iter, MESSAGE_KEY_ST_DEVICE_NAME))) {
    strncpy(s_device_name, t->value->cstring, NAME_LEN); s_device_name[NAME_LEN] = '\0';
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ST_REC_START))) s_rec_start = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_ST_CONN))) s_conn_count = (int)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_AUTO_CLOSE))) {
    s_auto_close = (t->value->int32 != 0);
    persist_write_bool(PERSIST_KEY_AUTO_CLOSE, s_auto_close);
    APP_LOG(APP_LOG_LEVEL_INFO, "CFG_AUTO_CLOSE = %d", (int)s_auto_close);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_IDLE_EXIT_SEC))) {
    int isec = idle_read_seconds(t);
    if (isec >= 0) {
      s_idle_timeout_sec = isec;
      persist_write_int(PERSIST_KEY_IDLE_EXIT, isec);
      idle_reset();
    }
  }
  // Pause the idle auto-exit while the phone config page is open (no watch buttons
  // are pressed during config, so the idle timer would otherwise fire and kill the
  // app -- and PKJS with it -- closing the config page and losing unsaved changes).
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_OPEN))) {
    s_config_open = (t->value->int32 != 0);
    if (s_config_open) { idle_cancel(); APP_LOG(APP_LOG_LEVEL_INFO, "config open: idle paused"); }
    else               { idle_reset();  APP_LOG(APP_LOG_LEVEL_INFO, "config closed: idle resumed"); }
  }

  s_conn = CONN_READY;
  s_retries = 0;
  if (s_retry_timer) { app_timer_cancel(s_retry_timer); s_retry_timer = NULL; }
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "inbox_dropped reason=%d", (int)reason);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  if (s_retries < MAX_AUTO_RETRIES) {
    s_retries++;
    if (s_retry_timer) app_timer_cancel(s_retry_timer);
    s_retry_timer = app_timer_register(SEND_RETRY_MS, retry_timer_cb, NULL);
  } else {
    s_close_after_send = false;
    s_conn = CONN_ERROR;
    if (s_menu) menu_layer_reload_data(s_menu);
  }
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  if (s_close_after_send) {
    s_close_after_send = false;
    // Action done: exit to the watchface, not back to the launcher.
    exit_reason_set(APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY);
    window_stack_pop_all(true); // single-window app -> this exits the app
  }
}

static uint16_t menu_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  return 1; // always one row: either the action row or the status row
}
static int16_t menu_header_height(MenuLayer *m, uint16_t section, void *ctx) { return HEADER_H; }
static int16_t menu_get_cell_height(MenuLayer *m, MenuIndex *idx, void *ctx) {
  return showing_status_row() ? STATUS_ROW_H : ACTION_ROW_H;
}

static void menu_draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *c) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_text_color(ctx, GColorBlack);
  if (s_conn != CONN_READY) {
    graphics_draw_text(ctx, "MIDI Recorder", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        GRect(4, (b.size.h - 28) / 2, b.size.w - 8, 28),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  if (s_recording) {
    int32_t elapsed = s_rec_start > 0 ? ((int32_t)time(NULL) - s_rec_start) : 0;
    if (elapsed < 0) elapsed = 0;
    static char buf[16];
    int h = elapsed / 3600, m = (elapsed % 3600) / 60, s = elapsed % 60;
    if (h > 0) snprintf(buf, sizeof(buf), "REC %d:%02d:%02d", h, m, s);
    else       snprintf(buf, sizeof(buf), "REC %d:%02d", m, s);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        GRect(4, (b.size.h - 30) / 2, b.size.w - 8, 30),
        GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  } else {
    graphics_draw_text(ctx, "Ready", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        GRect(4, (b.size.h - 30) / 2, b.size.w - 8, 30),
        GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}

static void menu_draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  if (showing_status_row()) {
    const char *title, *subtitle;
    switch (s_conn) {
      case CONN_ERROR: title = "No phone connection"; subtitle = "Tap to retry"; break;
      case CONN_READY: title = "No device connected"; subtitle = "Tap to refresh"; break;
      default:         title = "Connecting..."; subtitle = "Waiting for phone"; break;
    }
    menu_cell_basic_draw(ctx, cell, title, subtitle, NULL);
    return;
  }
  GRect b = layer_get_bounds(cell);
  GColor fg = menu_cell_layer_is_highlighted(cell) ? GColorWhite : GColorBlack;
  graphics_context_set_text_color(ctx, fg);

  if (s_recording) {
    const int sq = 16;
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(8, (b.size.h - sq) / 2, sq, sq), 2, GCornersAll);
    graphics_draw_text(ctx, "Stop", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        GRect(8 + sq + 8, (b.size.h - 26) / 2, b.size.w - (8 + sq + 8) - 4, 26),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  } else {
    // Start: title + connected device name as subtitle
    menu_cell_basic_draw(ctx, cell, "Start recording",
        s_device_name[0] ? s_device_name : "Tap to record", NULL);
  }
}

static void menu_select(MenuLayer *m, MenuIndex *idx, void *c) {
  idle_reset();
  if (showing_status_row()) { s_close_after_send = false; request(1); return; } // refresh / retry
  s_close_after_send = s_auto_close;                    // exit once the Start/Stop is delivered
  if (s_recording) { request(3); } else { request(2); } // stop / start
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_num_rows,
    .get_header_height = menu_header_height,
    .get_cell_height = menu_get_cell_height,
    .draw_header = menu_draw_header,
    .draw_row = menu_draw_row,
    .select_click = menu_select,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}
static void window_unload(Window *w) { menu_layer_destroy(s_menu); s_menu = NULL; }

static void tick_handler(struct tm *t, TimeUnits u) {
  if (s_menu && s_recording) menu_layer_reload_data(s_menu); // live REC timer
}

static void init(void) {
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);
  app_message_open(256, 64);

  if (persist_exists(PERSIST_KEY_AUTO_CLOSE)) {
    s_auto_close = persist_read_bool(PERSIST_KEY_AUTO_CLOSE);
  }
  if (persist_exists(PERSIST_KEY_IDLE_EXIT)) {
    s_idle_timeout_sec = persist_read_int(PERSIST_KEY_IDLE_EXIT);
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) { .load = window_load, .unload = window_unload,
    .appear = idle_appear, .disappear = idle_disappear });
#ifdef SCREENSHOT_FIXTURES
  // Demo state for appstore screenshots (no phone): recording, one device connected.
  s_conn = CONN_READY;
  s_recording = true;
  strncpy(s_device_name, "Roland FP-30", NAME_LEN); s_device_name[NAME_LEN] = '\0';
  s_rec_start = (int32_t)time(NULL) - 154; // 2:34 elapsed
  s_conn_count = 1;
#endif
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
#ifndef SCREENSHOT_FIXTURES
  request(1);
#endif
}
static void deinit(void) {
  if (s_retry_timer) { app_timer_cancel(s_retry_timer); s_retry_timer = NULL; }
  idle_cancel();
  window_destroy(s_window);
}
int main(void) { init(); app_event_loop(); deinit(); }
