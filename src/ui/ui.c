#include "ui.h"

#include <math.h>
#include <string.h>

#include "backend.h"
#include "board.h"
#include "config.h"
#include "model.h"
#include "screen.h"
#include "settings.h"

#define PRESSED_OPA LV_OPA_60

static screen_t *const screens[] = {
    [SCREEN_HOME] = &home_screen,
    [SCREEN_LIGHT] = &light_screen,
    [SCREEN_CLOCK] = &clock_screen,
    [SCREEN_SETTINGS] = &settings_screen,
};

static screen_id_t current = SCREEN_HOME;
static uint32_t seen_revision, seen_layout;
static int seen_link = -1;
static uint32_t link_changed_at;
static lv_obj_t *rim;

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(label, "");
  return label;
}

lv_obj_t *ui_circle(lv_obj_t *parent, int size, lv_color_t color) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_size(obj, size, size);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  ui_inert(obj);
  return obj;
}

lv_color_t ui_pastel(uint32_t rgb) {
  return lv_color_mix(lv_color_white(), lv_color_hex(rgb), 110);
}

lv_color_t ui_on_pastel(lv_color_t pastel) {
  return lv_color_mix(pastel, lv_color_black(), 64);
}

void ui_inert(lv_obj_t *obj) {
  lv_obj_set_clickable(obj, false);
  lv_obj_set_scrollable(obj, false);
}

void ui_set_text(lv_obj_t *label, const char *text) {
  if (strcmp(lv_label_get_text(label), text))
    lv_label_set_text(label, text);
}

// setting a style property redraws the object even when the value is the same

void ui_set_text_color(lv_obj_t *obj, lv_color_t color) {
  if (!lv_color_eq(lv_obj_get_style_text_color(obj, 0), color))
    lv_obj_set_style_text_color(obj, color, 0);
}

void ui_set_bg(lv_obj_t *obj, lv_color_t color, lv_opa_t opa) {
  if (!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), color))
    lv_obj_set_style_bg_color(obj, color, 0);
  if (lv_obj_get_style_bg_opa(obj, 0) != opa)
    lv_obj_set_style_bg_opa(obj, opa, 0);
}

void ui_set_outline(lv_obj_t *obj, int width) {
  if (lv_obj_get_style_outline_width(obj, 0) != width)
    lv_obj_set_style_outline_width(obj, width, 0);
}

void ui_place(lv_obj_t *obj, float degrees, float radius) {
  float rad = degrees * (float)M_PI / 180;
  int32_t x = lroundf(radius * cosf(rad)), y = lroundf(radius * sinf(rad));
  if (lv_obj_get_style_align(obj, 0) != LV_ALIGN_CENTER ||
      lv_obj_get_style_x(obj, 0) != x || lv_obj_get_style_y(obj, 0) != y)
    lv_obj_align(obj, LV_ALIGN_CENTER, x, y);
}

void ui_sound(bool knob) {
  if (settings.clicks == CLICKS_ALL ||
      (settings.clicks == CLICKS_TOUCH && !knob))
    board_click();
}

static void on_tap_sound(lv_event_t *e) { ui_sound(false); }

void ui_on_click(lv_obj_t *obj, lv_event_cb_t cb, void *user_data) {
  lv_obj_set_clickable(obj, true);
  lv_obj_set_style_opa(obj, PRESSED_OPA, LV_STATE_PRESSED);
  lv_obj_add_event_cb(obj, on_tap_sound, LV_EVENT_SHORT_CLICKED, NULL);
  lv_obj_add_event_cb(obj, cb, LV_EVENT_SHORT_CLICKED, user_data);
}

static lv_obj_t *make_screen(void) {
  lv_obj_t *root = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(root, false);
  return root;
}

static void update_rim(void) {
  link_t link = backend->link();
  bool show = link == LINK_AUTH_FAILED ||
              (link != LINK_READY && now_ms() - link_changed_at > 1500);
  if (show)
    lv_obj_set_style_border_color(
        rim, link == LINK_AUTH_FAILED ? COLOR_ERROR : COLOR_WARNING, 0);
  if (show == lv_obj_is_hidden(rim))
    lv_obj_set_hidden(rim, !show);
}

void ui_init(void) {
  for (int i = 0; i < sizeof screens / sizeof *screens; i++) {
    screens[i]->root = make_screen();
    screens[i]->build(screens[i]->root);
  }
  rim = lv_obj_create(lv_layer_top());
  lv_obj_set_size(rim, SCREEN_SIZE, SCREEN_SIZE);
  lv_obj_set_style_radius(rim, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(rim, 3, 0);
  ui_inert(rim);
  lv_obj_set_hidden(rim, true);

  seen_layout = model_layout();
  home_relayout();
  ui_show(SCREEN_HOME, NULL);
}

void ui_loop(void) {
  if (model_layout() != seen_layout) {
    seen_layout = model_layout();
    home_relayout();
    seen_revision = 0;
  }
  if ((int)backend->link() != seen_link) {
    seen_link = backend->link();
    link_changed_at = now_ms();
    seen_revision = 0;
  }
  if (model_revision() != seen_revision) {
    seen_revision = model_revision();
    if (screens[current]->refresh)
      screens[current]->refresh();
  }
  update_rim();
}

void ui_show(screen_id_t id, const char *target) {
  screen_t *from = screens[current], *to = screens[id];
  if (from->leave)
    from->leave();
  current = id;
  if (to->enter)
    to->enter(target);
  if (to->refresh)
    to->refresh();
  if (current != id)
    return;
  lv_screen_load(to->root);
  int level = settings.brightness;
  if (id == SCREEN_CLOCK)
    level = level / 3 > 10 ? level / 3 : 10;
  board_set_backlight(level);
}

screen_id_t ui_current(void) { return current; }

void ui_rotate(int steps) {
  if (!screens[current]->rotate)
    return;
  ui_sound(true);
  screens[current]->rotate(steps);
}

void ui_click(void) {
  if (screens[current]->click)
    screens[current]->click();
}

void ui_long_press(void) {
  if (screens[current]->long_press)
    screens[current]->long_press();
}
