#include <pebble.h>
#include "names.h"

#define PK_DAILY 1
#define PK_MASK 3
#define PK_TMASK 4
#define PK_FILTER 5
#define PK_CHOICES 6
#define PK_NONE 7
#define PK_SIL 8
#define SPR 56
#define ZOOM 2
#define DISP (SPR * ZOOM)
#define NUM_GENS 9
#define NUM_TYPES 18
#define MAX_CHOICES 4
#define CHUNK 900
#define RETRY_MS 1500
#define LOAD_TRIES 10

enum { RES_NONE = 0, RES_CORRECT = 1, RES_WRONG = 2 };
enum { W_SETTINGS, W_PREFS, W_POOL };

typedef struct { const char *name; int first, last; } Gen;
static const Gen GENS[NUM_GENS] = {
  {"Kanto", 1, 151}, {"Johto", 152, 251}, {"Hoenn", 252, 386}, {"Sinnoh", 387, 493}, {"Unova", 494, 649},
  {"Kalos", 650, 721}, {"Alola", 722, 809}, {"Galar", 810, 905}, {"Paldea", 906, 1025}};
static const char *const TYPE_NAMES[NUM_TYPES] = {
  "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel",
  "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark", "Fairy"};

static Window *s_main, *s_menu;
static Layer *s_canvas;
static MenuLayer *s_list;
static GBitmap *s_bmp;
static int s_key, s_idx, s_result;
static int s_mask = 1, s_tmask = (1 << NUM_TYPES) - 1, s_filter, s_choices = 3;
static bool s_none_opt = true, s_sil = true;
static int s_opt[MAX_CHOICES];
static bool s_absent, s_daily, s_changed, s_loading, s_err;
static uint8_t *s_png;
static int s_png_len, s_png_got, s_tries;
static AppTimer *s_load_timer;

static int day_key(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  return (t->tm_year - 100) * 400 + t->tm_yday;
}

static int gen_of(int idx) {
  for (int g = 0; g < NUM_GENS; g++) if (idx + 1 <= GENS[g].last) return g;
  return NUM_GENS - 1;
}

static bool in_pool(int idx) {
  if (s_filter == 0) return s_mask & (1 << gen_of(idx));
  for (int k = 0; k < 2; k++) {
    int t = TYPES[idx][k];
    if (t && (s_tmask & (1 << (t - 1)))) return true;
  }
  return false;
}

static int pool_count(void) {
  int n = 0;
  for (int i = 0; i < NUM_MON; i++) if (in_pool(i)) n++;
  return n;
}

static int pool_nth(int n) {
  for (int i = 0; i < NUM_MON; i++) if (in_pool(i) && n-- == 0) return i;
  return 0;
}

static GColor pixel_color(const GColor *pal, GBitmapFormat fmt, const uint8_t *data, int bpr, int x, int y) {
  const uint8_t *row = data + y * bpr;
  switch (fmt) {
    case GBitmapFormat8Bit: return (GColor){.argb = row[x]};
    case GBitmapFormat4BitPalette: return pal[(row[x >> 1] >> (4 * (1 - (x & 1)))) & 0xF];
    case GBitmapFormat2BitPalette: return pal[(row[x >> 2] >> (2 * (3 - (x & 3)))) & 0x3];
    case GBitmapFormat1BitPalette: return pal[(row[x >> 3] >> (7 - (x & 7))) & 0x1];
    default: return GColorClear;
  }
}

static void load_sprite(void) {
  if (s_bmp) { gbitmap_destroy(s_bmp); s_bmp = NULL; }
  if (!s_png || s_png_len <= 0) return;
  GBitmap *src = gbitmap_create_from_png_data(s_png, s_png_len);
  if (!src) return;
  s_bmp = gbitmap_create_blank(GSize(DISP, DISP), GBitmapFormat8Bit);
  if (s_bmp) {
    bool sil = s_result == RES_NONE && s_sil;
    GBitmapFormat fmt = gbitmap_get_format(src);
    const GColor *pal = gbitmap_get_palette(src);
    const uint8_t *sd = gbitmap_get_data(src);
    int sbpr = gbitmap_get_bytes_per_row(src);
    GRect sb = gbitmap_get_bounds(src);
    uint8_t *dd = gbitmap_get_data(s_bmp);
    int dbpr = gbitmap_get_bytes_per_row(s_bmp);
    for (int y = 0; y < sb.size.h && y < SPR; y++) {
      for (int x = 0; x < sb.size.w && x < SPR; x++) {
        GColor c = pixel_color(pal, fmt, sd, sbpr, x, y);
        uint8_t v = c.a ? (sil ? GColorBlack.argb : c.argb) : 0;
        for (int dy = 0; dy < ZOOM; dy++)
          for (int dx = 0; dx < ZOOM; dx++) dd[(y * ZOOM + dy) * dbpr + x * ZOOM + dx] = v;
      }
    }
  }
  gbitmap_destroy(src);
}

static void request_sprite(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_int32(it, MESSAGE_KEY_SprReq, s_idx + 1);
  app_message_outbox_send();
}

static void load_timeout(void *d) {
  s_load_timer = NULL;
  if (!s_loading) return;
  if (++s_tries > LOAD_TRIES) {
    s_loading = false;
    s_err = true;
    if (s_canvas) layer_mark_dirty(s_canvas);
    return;
  }
  if (s_png_got == 0) request_sprite();
  s_load_timer = app_timer_register(RETRY_MS, load_timeout, NULL);
}

static void begin_load(void) {
  if (s_bmp) { gbitmap_destroy(s_bmp); s_bmp = NULL; }
  free(s_png);
  s_png = NULL;
  s_png_len = s_png_got = s_tries = 0;
  s_loading = true;
  s_err = false;
  if (s_load_timer) app_timer_cancel(s_load_timer);
  request_sprite();
  s_load_timer = app_timer_register(RETRY_MS, load_timeout, NULL);
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  Tuple *id = dict_find(it, MESSAGE_KEY_SprId), *seq = dict_find(it, MESSAGE_KEY_SprSeq);
  Tuple *tot = dict_find(it, MESSAGE_KEY_SprTotal), *data = dict_find(it, MESSAGE_KEY_SprData);
  if (!id || !seq || !tot || !data || !s_loading) return;
  if (id->value->int32 != s_idx + 1) return;
  int sq = seq->value->int32, total = tot->value->int32;
  if (sq == 0) {
    free(s_png);
    s_png = malloc(total * CHUNK);
    s_png_len = 0;
    s_png_got = 0;
  }
  if (!s_png || sq != s_png_got || sq * CHUNK + (int)data->length > total * CHUNK) return;
  memcpy(s_png + sq * CHUNK, data->value->data, data->length);
  s_png_len = sq * CHUNK + data->length;
  s_png_got++;
  if (s_png_got == total) {
    s_loading = false;
    if (s_load_timer) { app_timer_cancel(s_load_timer); s_load_timer = NULL; }
    load_sprite();
    if (s_canvas) layer_mark_dirty(s_canvas);
  }
}

static void pick_options(void) {
  int pc = pool_count();
  bool from_pool = pc > s_choices;
  int taken[MAX_CHOICES + 1], nt = 0, slot = rand() % s_choices;
  taken[nt++] = s_idx;
  for (int i = 0; i < s_choices; i++) {
    if (i == slot && !s_absent) { s_opt[i] = s_idx; continue; }
    int m;
    bool dup;
    do {
      m = from_pool ? pool_nth(rand() % pc) : rand() % NUM_MON;
      dup = false;
      for (int j = 0; j < nt; j++) if (taken[j] == m) dup = true;
    } while (dup);
    taken[nt++] = m;
    s_opt[i] = m;
  }
}

static void start_round(bool daily) {
  int pc = pool_count();
  if (pc == 0) pc = 1;
  s_daily = daily;
  if (daily) {
    srand(s_key * 2654435761u);
    s_idx = pool_nth((s_key * 37 + 11) % pc);
    s_absent = s_none_opt && ((s_key * 13 + 3) % 6) == 0;
  } else {
    srand(time(NULL) ^ (uint32_t)(s_key * 7919));
    s_idx = pool_nth(rand() % pc);
    s_absent = s_none_opt && (rand() % 6) == 0;
  }
  pick_options();
  s_result = RES_NONE;
  begin_load();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void draw_center(GContext *ctx, const char *s, GFont f, GRect r, GColor c) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  bool done = s_result != RES_NONE;
  GFont f24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  graphics_context_set_fill_color(ctx, done ? GColorWhite : GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, done ? GColorPastelYellow : GColorPictonBlue);
  graphics_fill_circle(ctx, GPoint(b.size.w / 2, 98), 90);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  if (s_bmp) graphics_draw_bitmap_in_rect(ctx, s_bmp, GRect((b.size.w - DISP) / 2, 42, DISP, DISP));
  if (s_loading) draw_center(ctx, "Loading...", f24, GRect(0, 84, b.size.w, 30), GColorWhite);
  if (s_err) draw_center(ctx, "Can't reach phone", f24, GRect(0, 70, b.size.w, 60), GColorWhite);
  if (!done) {
    draw_center(ctx, "Who's that PebbleMon?", f18, GRect(0, 2, b.size.w, 24), GColorWhite);
    draw_center(ctx, s_err ? "SELECT: retry" : s_loading ? "" : "Press SELECT to guess", f18, GRect(0, 190, b.size.w, 24), GColorWhite);
  } else {
    draw_center(ctx, "SELECT: play again", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 4, b.size.w, 20), GColorDarkGray);
    draw_center(ctx, NAMES[s_idx], f24, GRect(0, 168, b.size.w, 30), GColorBlack);
    draw_center(ctx, s_result == RES_CORRECT ? "Correct!" : "Wrong!", f18, GRect(0, 198, b.size.w, 24),
                s_result == RES_CORRECT ? GColorDarkGreen : GColorRed);
  }
}

/* ---- Settings: one window type, three screens ---- */

static void draw_check(GContext *ctx, const Layer *cl, bool on) {
  GRect b = layer_get_bounds(cl);
  GRect box = GRect(b.size.w - 34, (b.size.h - 20) / 2, 20, 20);
  GColor fg = menu_cell_layer_is_highlighted(cl) ? GColorWhite : GColorBlack;
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_rect(ctx, box);
  if (on) {
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(box.origin.x + 5, box.origin.y + 5, 10, 10), 0, GCornerNone);
  }
}

typedef struct { int mode; MenuLayer *menu; } ListWin;

static uint16_t set_rows(MenuLayer *m, uint16_t s, void *d) {
  switch ((int)d) {
    case W_SETTINGS: return 2;
    case W_PREFS: return 3;
    default: return 1 + (s_filter == 0 ? NUM_GENS : NUM_TYPES);
  }
}
static int16_t set_height(MenuLayer *m, MenuIndex *i, void *d) { return 52; }

static void set_draw(GContext *ctx, const Layer *cl, MenuIndex *i, void *d) {
  char sub[24];
  int r = i->row;
  switch ((int)d) {
    case W_SETTINGS:
      if (r == 0) menu_cell_basic_draw(ctx, cl, "Preferences", "Choices, silhouette", NULL);
      else menu_cell_basic_draw(ctx, cl, "Pokemon", s_filter == 0 ? "By generation" : "By type", NULL);
      break;
    case W_PREFS:
      if (r == 0) {
        snprintf(sub, sizeof sub, "%d names", s_choices);
        menu_cell_basic_draw(ctx, cl, "Choices", sub, NULL);
      } else if (r == 1) {
        menu_cell_basic_draw(ctx, cl, "None of above", s_none_opt ? "On" : "Off", NULL);
        draw_check(ctx, cl, s_none_opt);
      } else {
        menu_cell_basic_draw(ctx, cl, "Silhouette", s_sil ? "On" : "Off", NULL);
        draw_check(ctx, cl, s_sil);
      }
      break;
    default:
      if (r == 0) {
        menu_cell_basic_draw(ctx, cl, "Sort by", s_filter == 0 ? "Generation" : "Type", NULL);
      } else if (s_filter == 0) {
        const Gen *g = &GENS[r - 1];
        snprintf(sub, sizeof sub, "%d-%d", g->first, g->last);
        menu_cell_basic_draw(ctx, cl, g->name, sub, NULL);
        draw_check(ctx, cl, s_mask & (1 << (r - 1)));
      } else {
        menu_cell_basic_draw(ctx, cl, TYPE_NAMES[r - 1], NULL, NULL);
        draw_check(ctx, cl, s_tmask & (1 << (r - 1)));
      }
  }
}

static Window *make_list_window(int mode);

static void set_select(MenuLayer *m, MenuIndex *i, void *d) {
  int r = i->row;
  switch ((int)d) {
    case W_SETTINGS:
      window_stack_push(make_list_window(r == 0 ? W_PREFS : W_POOL), true);
      return;
    case W_PREFS:
      if (r == 0) { s_choices = s_choices >= MAX_CHOICES ? 2 : s_choices + 1; persist_write_int(PK_CHOICES, s_choices); }
      else if (r == 1) { s_none_opt = !s_none_opt; persist_write_bool(PK_NONE, s_none_opt); }
      else { s_sil = !s_sil; persist_write_bool(PK_SIL, s_sil); }
      break;
    default:
      if (r == 0) {
        s_filter = !s_filter;
        persist_write_int(PK_FILTER, s_filter);
        menu_layer_reload_data(m);
      } else if (s_filter == 0) {
        int nm = s_mask ^ (1 << (r - 1));
        if (nm == 0) return;
        s_mask = nm;
        persist_write_int(PK_MASK, s_mask);
      } else {
        int nm = s_tmask ^ (1 << (r - 1));
        if (nm == 0) return;
        s_tmask = nm;
        persist_write_int(PK_TMASK, s_tmask);
      }
  }
  s_changed = true;
  layer_mark_dirty(menu_layer_get_layer(m));
}

static void settings_close_cb(void *d) {
  window_stack_remove(s_menu, false);
  start_round(false);
}

static void list_load(Window *w) {
  ListWin *lw = window_get_user_data(w);
  Layer *root = window_get_root_layer(w);
  lw->menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(lw->menu, (void *)lw->mode, (MenuLayerCallbacks){
    .get_num_rows = set_rows, .get_cell_height = set_height, .draw_row = set_draw, .select_click = set_select});
  menu_layer_set_click_config_onto_window(lw->menu, w);
  layer_add_child(root, menu_layer_get_layer(lw->menu));
}

static void list_unload(Window *w) {
  ListWin *lw = window_get_user_data(w);
  if (lw->mode == W_SETTINGS && s_changed) {
    s_changed = false;
    app_timer_register(50, settings_close_cb, NULL);
  }
  menu_layer_destroy(lw->menu);
  free(lw);
  window_destroy(w);
}

static Window *make_list_window(int mode) {
  ListWin *lw = malloc(sizeof(ListWin));
  lw->mode = mode;
  Window *w = window_create();
  window_set_user_data(w, lw);
  window_set_window_handlers(w, (WindowHandlers){.load = list_load, .unload = list_unload});
  return w;
}

/* ---- Guess menu: names, [none of the above], settings ---- */

static int guess_rows(void) { return s_choices + (s_none_opt ? 1 : 0) + 1; }
static uint16_t menu_rows(MenuLayer *m, uint16_t s, void *d) { return guess_rows(); }
static int16_t menu_height(MenuLayer *m, MenuIndex *i, void *d) { return 52; }
static void menu_draw(GContext *ctx, const Layer *cl, MenuIndex *i, void *d) {
  int r = i->row;
  if (r < s_choices) menu_cell_basic_draw(ctx, cl, NAMES[s_opt[r]], NULL, NULL);
  else if (r == guess_rows() - 1) menu_cell_basic_draw(ctx, cl, "Settings", NULL, NULL);
  else menu_cell_basic_draw(ctx, cl, "None of the above", NULL, NULL);
}

static void menu_select(MenuLayer *m, MenuIndex *i, void *d) {
  int r = i->row;
  if (r == guess_rows() - 1) { window_stack_push(make_list_window(W_SETTINGS), true); return; }
  bool ok = (r >= s_choices) ? s_absent : (!s_absent && s_opt[r] == s_idx);
  s_result = ok ? RES_CORRECT : RES_WRONG;
  if (s_daily) persist_write_int(PK_DAILY, s_key);
  s_daily = false;
  load_sprite();
  window_stack_pop(true);
  layer_mark_dirty(s_canvas);
  vibes_short_pulse();
}

static void menu_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_list = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_list, NULL, (MenuLayerCallbacks){
    .get_num_rows = menu_rows, .get_cell_height = menu_height, .draw_row = menu_draw, .select_click = menu_select});
  menu_layer_set_click_config_onto_window(s_list, w);
  layer_add_child(root, menu_layer_get_layer(s_list));
}
static void menu_unload(Window *w) { menu_layer_destroy(s_list); }

static void select_click(ClickRecognizerRef r, void *c) {
  if (s_loading) return;
  if (s_result != RES_NONE || s_err) { start_round(false); return; }
  window_stack_push(s_menu, true);
}
static void click_config(void *c) { window_single_click_subscribe(BUTTON_ID_SELECT, select_click); }

static void main_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
  window_set_click_config_provider(w, click_config);
}
static void main_unload(Window *w) { layer_destroy(s_canvas); s_canvas = NULL; }

int main(void) {
  s_key = day_key();
  if (persist_exists(PK_MASK)) s_mask = persist_read_int(PK_MASK) & ((1 << NUM_GENS) - 1);
  if (s_mask == 0) s_mask = 1;
  if (persist_exists(PK_TMASK)) s_tmask = persist_read_int(PK_TMASK) & ((1 << NUM_TYPES) - 1);
  if (s_tmask == 0) s_tmask = (1 << NUM_TYPES) - 1;
  if (persist_exists(PK_FILTER)) s_filter = persist_read_int(PK_FILTER) ? 1 : 0;
  if (persist_exists(PK_CHOICES)) s_choices = persist_read_int(PK_CHOICES);
  if (s_choices < 2 || s_choices > MAX_CHOICES) s_choices = 3;
  if (persist_exists(PK_NONE)) s_none_opt = persist_read_bool(PK_NONE);
  if (persist_exists(PK_SIL)) s_sil = persist_read_bool(PK_SIL);
  app_message_register_inbox_received(inbox_received);
  app_message_open(1200, 64);
  s_main = window_create();
  window_set_window_handlers(s_main, (WindowHandlers){.load = main_load, .unload = main_unload});
  s_menu = window_create();
  window_set_window_handlers(s_menu, (WindowHandlers){.load = menu_load, .unload = menu_unload});
  bool daily_done = persist_exists(PK_DAILY) && persist_read_int(PK_DAILY) == s_key;
  start_round(!daily_done);
  window_stack_push(s_main, true);
  app_event_loop();
  if (s_bmp) gbitmap_destroy(s_bmp);
  free(s_png);
  window_destroy(s_menu);
  window_destroy(s_main);
}
