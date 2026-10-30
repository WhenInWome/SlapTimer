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

// Background wave: its edge starts above the top and steps down one slice per second.
#define WAVE_AMP 6
#define WAVE_STRIP 4
#define WAVE_LEN 100
#define ANIM_MS 60
static AppTimer *s_anim;
static int s_edge, s_edge_target;
static int32_t s_phase;
static int s_height = 228;

static int edge_for(int shown) {
  return -WAVE_AMP + (s_height + 2 * WAVE_AMP) * (s_set_secs - shown) / s_set_secs;
}

static void format_time(int secs, char *buf, size_t n) {
  if (secs >= 60) snprintf(buf, n, "%d:%02d", secs / 60, secs % 60);
  else snprintf(buf, n, "%d", secs);
}

// 5x7 pixel font, drawn as blocks so it scales by whole pixels.
typedef struct { char c; uint8_t w; uint8_t rows[7]; } Glyph;
static const Glyph GLYPHS[] = {
  {'0', 5, {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
  {'1', 5, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
  {'2', 5, {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
  {'3', 5, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
  {'4', 5, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
  {'5', 5, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
  {'6', 5, {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}},
  {'7', 5, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
  {'8', 5, {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
  {'9', 5, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
  {':', 1, {0x00,0x01,0x01,0x00,0x01,0x01,0x00}},
  {'!', 1, {0x01,0x01,0x01,0x01,0x01,0x00,0x01}},
  {'A', 5, {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}},
  {'E', 5, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
  {'G', 5, {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}},
  {'I', 5, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
  {'L', 5, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
  {'M', 5, {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
  {'O', 5, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
  {'P', 5, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
  {'R', 5, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
  {'S', 5, {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
  {'T', 5, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
};

static const Glyph *find_glyph(char c) {
  for (size_t i = 0; i < ARRAY_LENGTH(GLYPHS); i++) if (GLYPHS[i].c == c) return &GLYPHS[i];
  return NULL;  // space and anything unknown
}

static int pixel_text_width(const char *s, int scale) {
  int w = 0;
  for (; *s; s++) {
    const Glyph *g = find_glyph(*s);
    w += ((g ? g->w : 3) + 1) * scale;
  }
  return w - scale;
}

static void draw_pixel_text(GContext *ctx, const char *s, int scale, int cx, int y) {
  int x = cx - pixel_text_width(s, scale) / 2;
  for (; *s; s++) {
    const Glyph *g = find_glyph(*s);
    if (g) {
      for (int r = 0; r < 7; r++)
        for (int c = 0; c < g->w; c++)
          if (g->rows[r] & (1 << (g->w - 1 - c)))
            graphics_fill_rect(ctx, GRect(x + c * scale, y + r * scale, scale, scale), 0, GCornerNone);
    }
    x += ((g ? g->w : 3) + 1) * scale;
  }
}

static int fit_scale(const char *s, int max_scale, int max_w) {
  int sc = max_scale;
  while (sc > 1 && pixel_text_width(s, sc) > max_w) sc--;
  return sc;
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = GColorBlack;
  if (s_state == StateDone) bg = PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  if (s_state == StateRunning) {
    // Time left is the coloured area below the wave edge; elapsed time is black above it.
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorWhite));
    for (int x = 0; x < b.size.w; x += WAVE_STRIP) {
      int32_t ang = (int32_t)x * TRIG_MAX_ANGLE / WAVE_LEN + s_phase;
      int y = s_edge + WAVE_AMP * sin_lookup(ang) / TRIG_MAX_RATIO;
      if (y < 0) y = 0;
      if (y < b.size.h)
        graphics_fill_rect(ctx, GRect(x, y, WAVE_STRIP, b.size.h - y), 0, GCornerNone);
    }
  }
  graphics_context_set_fill_color(ctx, GColorWhite);

  const char *top = s_state == StateSet ? "SLAP TIMER"
                  : s_state == StateRunning ? "GO" : "TIME!";
  const char *bottom = s_state == StateSet ? "TAP TO START"
                     : s_state == StateRunning ? "" : "TAP TO RESTART";
  int cx = b.size.w / 2, max_w = b.size.w - 8;

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  char clock[10];
  if (clock_is_24h_style()) {
    snprintf(clock, sizeof(clock), "%d:%02d", lt->tm_hour, lt->tm_min);
  } else {
    int h = lt->tm_hour % 12;
    snprintf(clock, sizeof(clock), "%d:%02d%s", h ? h : 12, lt->tm_min, lt->tm_hour < 12 ? "AM" : "PM");
  }
  draw_pixel_text(ctx, clock, 2, b.size.w - 4 - pixel_text_width(clock, 2) / 2, 4);

  int ts = fit_scale(top, 3, max_w);
  draw_pixel_text(ctx, top, ts, cx, 26);

  char num[12];
  format_time(s_shown_secs, num, sizeof(num));
  int ns = fit_scale(num, 10, max_w);
  draw_pixel_text(ctx, num, ns, cx, (b.size.h - 7 * ns) / 2);

  int bs = fit_scale(bottom, 3, max_w);
  draw_pixel_text(ctx, bottom, bs, cx, b.size.h - 10 - 7 * bs);
}

static void tick(void *data);

static void finish(void) {
  s_timer = NULL;
  if (s_anim) { app_timer_cancel(s_anim); s_anim = NULL; }
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
  s_edge_target = edge_for(s_shown_secs);
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(rem_ms % 1000 ? rem_ms % 1000 : 1000, tick, NULL);
}

static void anim(void *data) {
  s_phase += TRIG_MAX_ANGLE / 24;
  int diff = s_edge_target - s_edge;
  if (diff > 0) {
    s_edge += diff / 3 + 1;
    if (s_edge > s_edge_target) s_edge = s_edge_target;
  }
  layer_mark_dirty(s_layer);
  s_anim = app_timer_register(ANIM_MS, anim, NULL);
}

static void start(void) {
  if (s_state == StateRunning) return;
  time_t now; uint16_t now_ms;
  time_ms(&now, &now_ms);
  s_end_sec = now + s_set_secs;
  s_end_ms = now_ms;
  s_state = StateRunning;
  s_shown_secs = s_set_secs;
  s_edge = s_edge_target = edge_for(s_set_secs);
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(1000, tick, NULL);
  s_anim = app_timer_register(ANIM_MS, anim, NULL);
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

static void minute_tick(struct tm *t, TimeUnits units) {
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
  s_height = layer_get_bounds(root).size.h;
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
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
#ifdef PBL_TOUCH
  touch_service_subscribe(touch_handler, NULL);
#endif
}

static void deinit(void) {
#ifdef PBL_TOUCH
  touch_service_unsubscribe();
#endif
  tick_timer_service_unsubscribe();
  if (s_timer) app_timer_cancel(s_timer);
  if (s_anim) app_timer_cancel(s_anim);
  persist_write_int(1, s_set_secs);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
