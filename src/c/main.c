#include <pebble.h>

#define STEP 5
#define MIN_SECS 5
#define MAX_SECS 5995
#define REPEAT_MS 150

typedef enum { StateSet, StateRunning, StateDone } State;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;
static State s_state = StateSet;
static int s_set_secs = 30;     // value chosen with the side buttons
static int s_shown_secs = 30;   // value currently on screen
static time_t s_end_sec;
static uint16_t s_end_ms;

static void format_time(int secs, char *buf, size_t n) {
  if (secs >= 60) snprintf(buf, n, "%d:%02d", secs / 60, secs % 60);
  else snprintf(buf, n, "%d", secs);
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = GColorBlack;
  if (s_state == StateRunning) bg = PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorBlack);
  if (s_state == StateDone) bg = PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);

  const char *top = s_state == StateSet ? "SLAP TIMER"
                  : s_state == StateRunning ? "GO" : "TIME!";
  const char *bottom = s_state == StateSet ? "Tap to start"
                     : s_state == StateRunning ? "" : "Tap to restart";

  graphics_draw_text(ctx, top, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, 8, b.size.w, 30), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);

  char num[12];
  format_time(s_shown_secs, num, sizeof(num));
  graphics_draw_text(ctx, num, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                     GRect(0, b.size.h / 2 - 36, b.size.w, 50), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);

  graphics_draw_text(ctx, bottom, fonts_get_system_font(FONT_KEY_GOTHIC_24),
                     GRect(0, b.size.h - 40, b.size.w, 30), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

static void tick(void *data);

static void finish(void) {
  s_timer = NULL;
  s_state = StateDone;
  s_shown_secs = 0;
  vibes_long_pulse();
  layer_mark_dirty(s_layer);
}

// Whole seconds remaining, rounded up, from the wall clock so it can't drift.
static void tick(void *data) {
  time_t now; uint16_t now_ms;
  time_ms(&now, &now_ms);
  int rem_ms = (int)(s_end_sec - now) * 1000 + (int)s_end_ms - (int)now_ms;
  if (rem_ms <= 0) { finish(); return; }
  s_shown_secs = (rem_ms + 999) / 1000;
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(rem_ms % 1000 ? rem_ms % 1000 : 1000, tick, NULL);
}

static void start(void) {
  if (s_state == StateRunning) return;
  time_t now; uint16_t now_ms;
  time_ms(&now, &now_ms);
  s_end_sec = now + s_set_secs;
  s_end_ms = now_ms;
  s_state = StateRunning;
  s_shown_secs = s_set_secs;
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(1000, tick, NULL);
}

static void adjust(int delta) {
  if (s_state == StateRunning) return;
  s_set_secs += delta;
  if (s_set_secs < MIN_SECS) s_set_secs = MIN_SECS;
  if (s_set_secs > MAX_SECS) s_set_secs = MAX_SECS;
  s_state = StateSet;
  s_shown_secs = s_set_secs;
  layer_mark_dirty(s_layer);
}

static void up_handler(ClickRecognizerRef r, void *c) { adjust(STEP); }
static void down_handler(ClickRecognizerRef r, void *c) { adjust(-STEP); }

static void click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, REPEAT_MS, up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, REPEAT_MS, down_handler);
}

#ifdef PBL_TOUCH
static void touch_handler(const TouchEvent *e, void *ctx) {
  if (e->type == TouchEvent_Touchdown) start();
}
#endif

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, draw);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *w) {
  layer_destroy(s_layer);
}

static void init(void) {
  if (persist_exists(1)) s_set_secs = persist_read_int(1);
  if (s_set_secs < MIN_SECS || s_set_secs > MAX_SECS) s_set_secs = 30;
  s_shown_secs = s_set_secs;
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
#ifdef PBL_TOUCH
  touch_service_subscribe(touch_handler, NULL);
#endif
}

static void deinit(void) {
#ifdef PBL_TOUCH
  touch_service_unsubscribe();
#endif
  if (s_timer) app_timer_cancel(s_timer);
  persist_write_int(1, s_set_secs);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
