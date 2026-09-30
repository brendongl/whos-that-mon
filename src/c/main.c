#include <pebble.h>
#include "names.h"

#define PK_DAY 1
#define PK_RESULT 2

enum { RES_NONE = 0, RES_CORRECT = 1, RES_WRONG = 2 };

static Window *s_main, *s_menu;
static Layer *s_canvas;
static MenuLayer *s_list;
static GBitmap *s_bmp;
static int s_key, s_idx, s_result;
static int s_opt[3];

static int day_key(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  return (t->tm_year - 100) * 400 + t->tm_yday;
}

static void setup_day(void) {
  s_key = day_key();
  s_idx = (s_key * 37 + 11) % 151;
  int a = 1 + (s_key * 7) % 150;
  int b = 1 + (s_key * 11 + 5) % 150;
  if (b == a) b = 1 + b % 150;
  int slot = s_key % 3;
  int others[2] = {(s_idx + a) % 151, (s_idx + b) % 151};
  int o = 0;
  for (int i = 0; i < 3; i++) s_opt[i] = (i == slot) ? s_idx : others[o++];
  s_result = (persist_exists(PK_DAY) && persist_read_int(PK_DAY) == s_key) ? persist_read_int(PK_RESULT) : RES_NONE;
}

static void load_sprite(void) {
  if (s_bmp) gbitmap_destroy(s_bmp);
  s_bmp = gbitmap_create_with_resource(SPRITES[s_idx]);
  if (s_result != RES_NONE || !s_bmp) return;
  GBitmapFormat fmt = gbitmap_get_format(s_bmp);
  GColor *pal = gbitmap_get_palette(s_bmp);
  int n = fmt == GBitmapFormat1BitPalette ? 2 : fmt == GBitmapFormat2BitPalette ? 4 : fmt == GBitmapFormat4BitPalette ? 16 : 0;
  if (pal && n) {
    for (int i = 0; i < n; i++) if (pal[i].a != 0) pal[i] = GColorBlack;
  } else if (fmt == GBitmapFormat8Bit) {
    uint8_t *d = gbitmap_get_data(s_bmp);
    GRect r = gbitmap_get_bounds(s_bmp);
    int stride = gbitmap_get_bytes_per_row(s_bmp);
    for (int y = 0; y < r.size.h; y++)
      for (int x = 0; x < r.size.w; x++) if (d[y * stride + x] & 0xC0) d[y * stride + x] = GColorBlack.argb;
  }
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
  graphics_fill_circle(ctx, GPoint(b.size.w / 2, 98), 84);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  if (s_bmp) graphics_draw_bitmap_in_rect(ctx, s_bmp, GRect((b.size.w - 104) / 2, 46, 104, 104));

  GFont f24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (!done) {
    draw_center(ctx, "Who's that Pokemon?", f18, GRect(0, 2, b.size.w, 24), GColorWhite);
    draw_center(ctx, "Press SELECT to guess", f18, GRect(0, 190, b.size.w, 24), GColorWhite);
  } else {
    draw_center(ctx, NAMES[s_idx], f24, GRect(0, 168, b.size.w, 30), GColorBlack);
    draw_center(ctx, s_result == RES_CORRECT ? "Correct!" : "Wrong!", f18, GRect(0, 198, b.size.w, 24),
                s_result == RES_CORRECT ? GColorDarkGreen : GColorRed);
    draw_center(ctx, "SELECT: play again", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 4, b.size.w, 20), GColorDarkGray);
  }
}

static uint16_t menu_rows(MenuLayer *m, uint16_t s, void *d) { return 3; }
static void menu_draw(GContext *ctx, const Layer *cl, MenuIndex *i, void *d) {
  menu_cell_basic_draw(ctx, cl, NAMES[s_opt[i->row]], NULL, NULL);
}
static void menu_select(MenuLayer *m, MenuIndex *i, void *d) {
  s_result = (s_opt[i->row] == s_idx) ? RES_CORRECT : RES_WRONG;
  if (s_key == day_key() && s_idx == (s_key * 37 + 11) % 151) {
    persist_write_int(PK_DAY, s_key);
    persist_write_int(PK_RESULT, s_result);
  }
  load_sprite();
  window_stack_pop(true);
  layer_mark_dirty(s_canvas);
  vibes_short_pulse();
}

static void menu_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_list = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_list, NULL, (MenuLayerCallbacks){
    .get_num_rows = menu_rows, .draw_row = menu_draw, .select_click = menu_select});
  menu_layer_set_click_config_onto_window(s_list, w);
  layer_add_child(root, menu_layer_get_layer(s_list));
}
static void menu_unload(Window *w) { menu_layer_destroy(s_list); }

static void new_round(void) {
  srand(time(NULL));
  s_idx = rand() % 151;
  int a = 1 + rand() % 150, b = 1 + rand() % 150;
  while (b == a) b = 1 + rand() % 150;
  int slot = rand() % 3, others[2] = {(s_idx + a) % 151, (s_idx + b) % 151}, o = 0;
  for (int i = 0; i < 3; i++) s_opt[i] = (i == slot) ? s_idx : others[o++];
  s_result = RES_NONE;
  load_sprite();
  layer_mark_dirty(s_canvas);
}

static void select_click(ClickRecognizerRef r, void *c) {
  if (s_result != RES_NONE) { new_round(); return; }
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
static void main_unload(Window *w) { layer_destroy(s_canvas); }

int main(void) {
  setup_day();
  s_main = window_create();
  window_set_window_handlers(s_main, (WindowHandlers){.load = main_load, .unload = main_unload});
  s_menu = window_create();
  window_set_window_handlers(s_menu, (WindowHandlers){.load = menu_load, .unload = menu_unload});
  load_sprite();
  window_stack_push(s_main, true);
  app_event_loop();
  if (s_bmp) gbitmap_destroy(s_bmp);
  window_destroy(s_menu);
  window_destroy(s_main);
}
