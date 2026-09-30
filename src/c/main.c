#include <pebble.h>
#include "names.h"

#define PK_DAILY 1
#define PK_MASK 3
#define SPR 56
#define ZOOM 2
#define DISP (SPR * ZOOM)
#define NUM_GENS 2

enum { RES_NONE = 0, RES_CORRECT = 1, RES_WRONG = 2 };

typedef struct { const char *name; int first, last; } Gen;
static const Gen GENS[NUM_GENS] = {{"Kanto", 1, 151}, {"Johto", 152, 251}};

static Window *s_main, *s_menu, *s_settings;
static Layer *s_canvas;
static MenuLayer *s_list, *s_set_list;
static GBitmap *s_bmp;
static int s_key, s_idx, s_result, s_mask = 1;
static int s_opt[3];
static bool s_absent, s_daily, s_mask_changed;

static int day_key(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  return (t->tm_year - 100) * 400 + t->tm_yday;
}

static int pool_count(void) {
  int n = 0;
  for (int g = 0; g < NUM_GENS; g++) if (s_mask & (1 << g)) n += GENS[g].last - GENS[g].first + 1;
  return n;
}

static int pool_nth(int n) {
  for (int g = 0; g < NUM_GENS; g++) {
    if (!(s_mask & (1 << g))) continue;
    int c = GENS[g].last - GENS[g].first + 1;
    if (n < c) return GENS[g].first - 1 + n;
    n -= c;
  }
  return 0;
}

static GColor pixel_color(const GBitmap *src, const GColor *pal, GBitmapFormat fmt, const uint8_t *data, int bpr, int x, int y) {
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
  GBitmap *src = gbitmap_create_with_resource(SPRITES[s_idx]);
  if (!src) return;
  s_bmp = gbitmap_create_blank(GSize(DISP, DISP), GBitmapFormat8Bit);
  if (s_bmp) {
    bool sil = s_result == RES_NONE;
    GBitmapFormat fmt = gbitmap_get_format(src);
    const GColor *pal = gbitmap_get_palette(src);
    const uint8_t *sd = gbitmap_get_data(src);
    int sbpr = gbitmap_get_bytes_per_row(src);
    GRect sb = gbitmap_get_bounds(src);
    uint8_t *dd = gbitmap_get_data(s_bmp);
    int dbpr = gbitmap_get_bytes_per_row(s_bmp);
    for (int y = 0; y < sb.size.h && y < SPR; y++) {
      for (int x = 0; x < sb.size.w && x < SPR; x++) {
        GColor c = pixel_color(src, pal, fmt, sd, sbpr, x, y);
        uint8_t v = c.a ? (sil ? GColorBlack.argb : c.argb) : 0;
        for (int dy = 0; dy < ZOOM; dy++)
          for (int dx = 0; dx < ZOOM; dx++) dd[(y * ZOOM + dy) * dbpr + x * ZOOM + dx] = v;
      }
    }
  }
  gbitmap_destroy(src);
}

static void pick_options(void) {
  int pc = pool_count();
  int taken[4], nt = 0, slot = rand() % 3;
  taken[nt++] = s_idx;
  for (int i = 0; i < 3; i++) {
    if (i == slot && !s_absent) { s_opt[i] = s_idx; continue; }
    int m;
    bool dup;
    do {
      m = pool_nth(rand() % pc);
      dup = false;
      for (int j = 0; j < nt; j++) if (taken[j] == m) dup = true;
    } while (dup);
    taken[nt++] = m;
    s_opt[i] = m;
  }
}

static void start_round(bool daily) {
  int pc = pool_count();
  s_daily = daily;
  if (daily) {
    srand(s_key * 2654435761u);
    s_idx = pool_nth((s_key * 37 + 11) % pc);
    s_absent = ((s_key * 13 + 3) % 6) == 0;
  } else {
    srand(time(NULL) ^ (uint32_t)(s_key * 7919));
    s_idx = pool_nth(rand() % pc);
    s_absent = (rand() % 6) == 0;
  }
  pick_options();
  s_result = RES_NONE;
  load_sprite();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void draw_center(GContext *ctx, const char *s, GFont f, GRect r, GColor c) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  bool done = s_result != RES_NONE;
  graphics_context_set_fill_color(ctx, done ? GColorWhite : GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, done ? GColorPastelYellow : GColorPictonBlue);
  graphics_fill_circle(ctx, GPoint(b.size.w / 2, 98), 90);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  if (s_bmp) graphics_draw_bitmap_in_rect(ctx, s_bmp, GRect((b.size.w - DISP) / 2, 42, DISP, DISP));

  GFont f24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (!done) {
    draw_center(ctx, "Who's that PebbleMon?", f18, GRect(0, 2, b.size.w, 24), GColorWhite);
    draw_center(ctx, "Press SELECT to guess", f18, GRect(0, 190, b.size.w, 24), GColorWhite);
  } else {
    draw_center(ctx, "SELECT: play again", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 4, b.size.w, 20), GColorDarkGray);
    draw_center(ctx, NAMES[s_idx], f24, GRect(0, 168, b.size.w, 30), GColorBlack);
    draw_center(ctx, s_result == RES_CORRECT ? "Correct!" : "Wrong!", f18, GRect(0, 198, b.size.w, 24),
                s_result == RES_CORRECT ? GColorDarkGreen : GColorRed);
  }
}

/* Guess menu: rows 0-2 names, 3 none of the above, 4 settings */
static uint16_t menu_rows(MenuLayer *m, uint16_t s, void *d) { return 5; }
static int16_t menu_height(MenuLayer *m, MenuIndex *i, void *d) { return 52; }
static void menu_draw(GContext *ctx, const Layer *cl, MenuIndex *i, void *d) {
  if (i->row < 3) menu_cell_basic_draw(ctx, cl, NAMES[s_opt[i->row]], NULL, NULL);
  else if (i->row == 3) menu_cell_basic_draw(ctx, cl, "None of the above", NULL, NULL);
  else menu_cell_basic_draw(ctx, cl, "Settings", "Pick generations", NULL);
}

static void settings_close_cb(void *d) {
  window_stack_remove(s_menu, false);
  start_round(false);
}
static void settings_unload(Window *w) {
  menu_layer_destroy(s_set_list);
  if (s_mask_changed) { s_mask_changed = false; app_timer_register(50, settings_close_cb, NULL); }
}

static uint16_t set_rows(MenuLayer *m, uint16_t s, void *d) { return NUM_GENS; }
static int16_t set_height(MenuLayer *m, MenuIndex *i, void *d) { return 52; }
static void set_draw(GContext *ctx, const Layer *cl, MenuIndex *i, void *d) {
  char sub[24];
  const Gen *g = &GENS[i->row];
  snprintf(sub, sizeof sub, "%d-%d", g->first, g->last);
  menu_cell_basic_draw(ctx, cl, g->name, sub, NULL);
  GRect b = layer_get_bounds(cl);
  GRect box = GRect(b.size.w - 34, (b.size.h - 20) / 2, 20, 20);
  GColor fg = menu_cell_layer_is_highlighted(cl) ? GColorWhite : GColorBlack;
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_rect(ctx, box);
  if (s_mask & (1 << i->row)) {
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(box.origin.x + 5, box.origin.y + 5, 10, 10), 0, GCornerNone);
  }
}
static void set_select(MenuLayer *m, MenuIndex *i, void *d) {
  int nm = s_mask ^ (1 << i->row);
  if (nm == 0) return;
  s_mask = nm;
  s_mask_changed = true;
  persist_write_int(PK_MASK, s_mask);
  layer_mark_dirty(menu_layer_get_layer(m));
}
static void settings_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_set_list = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_set_list, NULL, (MenuLayerCallbacks){
    .get_num_rows = set_rows, .get_cell_height = set_height, .draw_row = set_draw, .select_click = set_select});
  menu_layer_set_click_config_onto_window(s_set_list, w);
  layer_add_child(root, menu_layer_get_layer(s_set_list));
}

static void menu_select(MenuLayer *m, MenuIndex *i, void *d) {
  if (i->row == 4) { window_stack_push(s_settings, true); return; }
  bool ok = (i->row == 3) ? s_absent : (!s_absent && s_opt[i->row] == s_idx);
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
  if (s_result != RES_NONE) { start_round(false); return; }
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
  s_main = window_create();
  window_set_window_handlers(s_main, (WindowHandlers){.load = main_load, .unload = main_unload});
  s_menu = window_create();
  window_set_window_handlers(s_menu, (WindowHandlers){.load = menu_load, .unload = menu_unload});
  s_settings = window_create();
  window_set_window_handlers(s_settings, (WindowHandlers){.load = settings_load, .unload = settings_unload});
  bool daily_done = persist_exists(PK_DAILY) && persist_read_int(PK_DAILY) == s_key;
  start_round(!daily_done);
  window_stack_push(s_main, true);
  app_event_loop();
  if (s_bmp) gbitmap_destroy(s_bmp);
  window_destroy(s_settings);
  window_destroy(s_menu);
  window_destroy(s_main);
}
