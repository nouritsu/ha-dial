#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actions.h"
#include "backend.h"
#include "config.h"
#include "icons.h"
#include "model.h"
#include "screen.h"
#include "ui.h"

#define MAX_ITEMS (MODEL_MAX_LIGHTS + MODEL_MAX_SCENES + 2)
#define BUTTON_SIZE 46
#define BUTTON_RADIUS 90
#define FOCUS_WIDTH 3
#define FOCUS_GAP 2
#define CENTER_WIDTH 124
#define BUTTON_SPACING 38
#define MIN_SPACING 37.5f
#define ACTIVATED_MS 1500
// presses nearer the middle than this belong to the centre, not the wheel
#define WHEEL_INNER (CENTER_WIDTH / 2)
// how far a finger moves before a press becomes a turn of the wheel
#define WHEEL_SLOP 8

typedef enum { ITEM_ALL, ITEM_LIGHT, ITEM_SCENE, ITEM_SETTINGS } item_kind_t;

typedef struct {
  item_kind_t kind;
  char id[MODEL_ID_LEN];
  lv_obj_t *button;
  lv_obj_t *glyph;
  float angle;
} item_t;

static lv_obj_t *root, *center, *name, *status;
static item_t items[MAX_ITEMS];
static int item_count, focus;
static char activated[MODEL_ID_LEN];
static uint32_t activated_at;
static struct {
  bool armed, turning;
  lv_point_t from;
} wheel;

static void activate(void);
static void toggle(void);
static void refresh(void);

static void on_item_click(lv_event_t *e) {
  focus = (intptr_t)lv_event_get_user_data(e);
  activate();
}

static void on_item_long_press(lv_event_t *e) {
  focus = (intptr_t)lv_event_get_user_data(e);
  ui_sound(false);
  toggle();
}

static void on_center_click(lv_event_t *e) { activate(); }

static float degrees_apart(float a, float b) {
  float d = fmodf(fabsf(a - b), 360);
  return d > 180 ? 360 - d : d;
}

static void turn_wheel(const lv_point_t *p) {
  float at =
      atan2f(p->y - SCREEN_CENTER, p->x - SCREEN_CENTER) * 180 / (float)M_PI;
  int nearest = focus;
  for (int i = 0; i < item_count; i++)
    if (degrees_apart(items[i].angle, at) <
        degrees_apart(items[nearest].angle, at))
      nearest = i;
  if (nearest == focus)
    return;
  focus = nearest;
  ui_sound(false);
  refresh();
}

// a finger dragged round the ring moves the focus to the button nearest it,
// and lifting it opens nothing
static void on_wheel(lv_event_t *e) {
  lv_point_t p;
  switch (lv_event_get_code(e)) {
  case LV_EVENT_PRESSED: {
    lv_indev_get_point(lv_indev_active(), &p);
    int dx = p.x - SCREEN_CENTER, dy = p.y - SCREEN_CENTER;
    wheel.armed = dx * dx + dy * dy >= WHEEL_INNER * WHEEL_INNER;
    wheel.turning = false;
    wheel.from = p;
    break;
  }
  case LV_EVENT_PRESSING:
    lv_indev_get_point(lv_indev_active(), &p);
    if (!wheel.armed ||
        (!wheel.turning &&
         abs(p.x - wheel.from.x) + abs(p.y - wheel.from.y) < WHEEL_SLOP))
      break;
    if (!wheel.turning)
      lv_obj_remove_state(lv_event_get_current_target_obj(e), LV_STATE_PRESSED);
    wheel.turning = true;
    turn_wheel(&p);
    break;
  case LV_EVENT_SHORT_CLICKED:
  case LV_EVENT_LONG_PRESSED:
    if (wheel.turning)
      lv_event_stop_processing(e);
    break;
  default:
    break;
  }
}

static void build(lv_obj_t *parent) {
  root = parent;
  lv_obj_add_event_cb(root, on_wheel, LV_EVENT_ALL, NULL);
  center = lv_obj_create(root);
  lv_obj_set_size(center, CENTER_WIDTH, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(center, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(center, 4, 0);
  lv_obj_set_scrollable(center, false);
  lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_ext_click_area(center, 10);
  ui_on_click(center, on_center_click, NULL);

  name = ui_label(center, &text_20, COLOR_ON_SURFACE);
  lv_obj_set_width(name, lv_pct(100));
  lv_obj_set_style_text_line_space(name, -2, 0);

  status = ui_label(center, &text_16, COLOR_ON_SURFACE_VARIANT);
  lv_obj_set_width(status, lv_pct(100));
}

static void add_item(item_kind_t kind, const char *id) {
  if (item_count == MAX_ITEMS)
    return;
  item_t *item = &items[item_count];
  item->kind = kind;
  strlcpy(item->id, id, sizeof item->id);
  item->button = ui_circle(root, BUTTON_SIZE, COLOR_SURFACE_CONTAINER_HIGHEST);
  lv_obj_set_style_outline_color(item->button, COLOR_ON_SURFACE, 0);
  lv_obj_set_style_outline_pad(item->button, FOCUS_GAP, 0);
  item->glyph = ui_label(item->button, &icons_28, COLOR_ON_SURFACE_VARIANT);
  lv_obj_center(item->glyph);
  // ahead of the others, so that it can hold back a tap that turned the wheel
  lv_obj_add_event_cb(item->button, on_wheel, LV_EVENT_ALL, NULL);
  ui_on_click(item->button, on_item_click, (void *)(intptr_t)item_count);
  lv_obj_add_event_cb(item->button, on_item_long_press, LV_EVENT_LONG_PRESSED,
                      (void *)(intptr_t)item_count);
  item_count++;
}

static void place_items(void) {
  int lights = 0, scenes = 0, scene = 0;
  for (int i = 0; i < item_count; i++) {
    lights += items[i].kind == ITEM_ALL || items[i].kind == ITEM_LIGHT;
    scenes += items[i].kind == ITEM_SCENE;
  }
  // settings sits across from the middle of the lights, and the scenes halfway
  // round from the last light to it, so the ring runs in the knob's order
  float middle = lights ? -90 + BUTTON_SPACING * (lights - 1) / 2.0f : 0;
  float last = lights ? -90 + BUTTON_SPACING * (lights - 1) : 0;
  float across = middle + 180, between = (last + across) / 2;
  for (int i = 0; i < item_count; i++) {
    switch (items[i].kind) {
    case ITEM_ALL:
    case ITEM_LIGHT:
      items[i].angle = -90 + BUTTON_SPACING * i;
      break;
    case ITEM_SCENE:
      items[i].angle =
          between + BUTTON_SPACING * (scene++ - (scenes - 1) / 2.0f);
      break;
    case ITEM_SETTINGS:
      items[i].angle = across;
      break;
    }
  }
  bool crowded = false;
  for (int i = 0; i < item_count; i++) {
    float next = i + 1 < item_count ? items[i + 1].angle : items[0].angle + 360;
    if (item_count > 1 && next - items[i].angle < MIN_SPACING)
      crowded = true;
  }
  for (int i = 0; i < item_count; i++) {
    if (crowded)
      items[i].angle = -90 + 360.0f * i / item_count;
    ui_place(items[i].button, items[i].angle, BUTTON_RADIUS);
  }
}

void home_relayout(void) {
  item_kind_t kind = item_count ? items[focus].kind : ITEM_ALL;
  char id[MODEL_ID_LEN] = "";
  if (item_count)
    strlcpy(id, items[focus].id, sizeof id);

  for (int i = 0; i < item_count; i++)
    lv_obj_delete(items[i].button);
  item_count = 0;
  target_t all;
  if (model_target("", &all))
    add_item(ITEM_ALL, "");
  for (int i = 0; i < model_light_count; i++)
    if (model_shown(&model_lights[i]))
      add_item(ITEM_LIGHT, model_lights[i].id);
  for (int i = 0; i < model_scene_count; i++)
    add_item(ITEM_SCENE, model_scenes[i].id);
  add_item(ITEM_SETTINGS, "");
  place_items();

  focus = 0;
  for (int i = 0; i < item_count; i++)
    if (items[i].kind == kind && !strcmp(items[i].id, id))
      focus = i;
}

static void describe_light(const char *id, const char **glyph,
                           lv_color_t *color, char *text, size_t size) {
  target_t t;
  if (!model_target(id, &t))
    return;
  bool group = !*id;
  const light_state_t *s = &t.state;
  *glyph = group ? (s->on ? ICON_GROUP : ICON_GROUP_OFF)
                 : icon_named(t.icon, s->on ? ICON_BULB : ICON_BULB_OFF);
  *color = !s->available ? COLOR_OUTLINE_VARIANT
           : s->on       ? ui_pastel(model_tint(s))
                         : COLOR_ON_SURFACE_VARIANT;
  if (!s->available) {
    snprintf(text, size, "Unavailable");
  } else if (group) {
    int on = 0;
    for (int i = 0; i < t.count; i++)
      on += t.lights[i]->state.on;
    if (on)
      snprintf(text, size, "%d of %d on", on, t.count);
    else
      snprintf(text, size, "All off");
  } else if (s->on) {
    snprintf(text, size, "On \u00B7 %d%%", model_percent(s));
  } else {
    snprintf(text, size, "Off");
  }
}

static const char *link_status(void) {
  switch (backend->link()) {
  case LINK_OFFLINE:
    return "Joining Wi-Fi\u2026";
  case LINK_CONNECTING:
    return "Connecting\u2026";
  case LINK_AUTH_FAILED:
    return backend->rejected;
  default:
    return backend->empty;
  }
}

static void refresh(void) {
  for (int i = 0; i < item_count; i++) {
    item_t *item = &items[i];
    const char *glyph = ICON_COG;
    lv_color_t color = COLOR_ON_SURFACE_VARIANT,
               bg = COLOR_SURFACE_CONTAINER_HIGHEST;
    char unused[32];
    if (item->kind == ITEM_ALL || item->kind == ITEM_LIGHT) {
      describe_light(item->id, &glyph, &color, unused, sizeof unused);
      target_t t;
      bool available = model_target(item->id, &t) && t.state.available;
      if (!available) {
        bg = COLOR_SURFACE_CONTAINER;
      } else if (t.state.on) {
        bg = color;
        color = ui_on_pastel(color);
      }
    } else if (item->kind == ITEM_SCENE) {
      const scene_t *s = model_scene(item->id);
      glyph = icon_named(s ? s->icon : NULL, ICON_HUE);
      color = COLOR_TERTIARY;
    }
    ui_set_text(item->glyph, glyph);
    ui_set_text_color(item->glyph, color);
    ui_set_bg(item->button, bg, LV_OPA_COVER);
    ui_set_outline(item->button, i == focus ? FOCUS_WIDTH : 0);
  }

  item_t *item = &items[focus];
  const char *title = "Settings";
  lv_color_t color = COLOR_ON_SURFACE_VARIANT;
  char text[48] = "";
  switch (item->kind) {
  case ITEM_ALL:
  case ITEM_LIGHT: {
    target_t t;
    const char *glyph = NULL;
    lv_color_t tint = color;
    describe_light(item->id, &glyph, &tint, text, sizeof text);
    if (model_target(item->id, &t)) {
      title = t.name;
      if (t.state.available && t.state.on)
        color = tint;
    }
    break;
  }
  case ITEM_SCENE: {
    const scene_t *s = model_scene(item->id);
    color = COLOR_TERTIARY;
    title = s ? s->name : item->id;
    bool recent =
        !strcmp(activated, item->id) && now_ms() - activated_at < ACTIVATED_MS;
    snprintf(text, sizeof text, recent ? "Activated" : "Scene");
    break;
  }
  case ITEM_SETTINGS:
    if (item_count == 1 || backend->link() != LINK_READY) {
      title = backend->name;
      if (backend->link() == LINK_AUTH_FAILED)
        color = COLOR_ERROR;
      snprintf(text, sizeof text, "%s", link_status());
    }
    break;
  }
  ui_set_text(name, title);
  ui_set_text(status, text);
  ui_set_text_color(status, color);
  if (lv_obj_is_hidden(status) != !*text)
    lv_obj_set_hidden(status, !*text);
}

static void expire_activated(lv_timer_t *timer) { model_changed(); }

static void activate(void) {
  item_t *item = &items[focus];
  switch (item->kind) {
  case ITEM_ALL:
  case ITEM_LIGHT:
    ui_show(SCREEN_LIGHT, item->id);
    break;
  case ITEM_SCENE:
    actions_scene(item->id);
    strlcpy(activated, item->id, sizeof activated);
    activated_at = now_ms();
    lv_timer_set_repeat_count(
        lv_timer_create(expire_activated, ACTIVATED_MS + 10, NULL), 1);
    model_changed();
    break;
  case ITEM_SETTINGS:
    ui_show(SCREEN_SETTINGS, NULL);
    break;
  }
}

static void toggle(void) {
  item_t *item = &items[focus];
  if (item->kind == ITEM_ALL || item->kind == ITEM_LIGHT)
    actions_toggle(item->id);
  model_changed();
}

static void rotate(int steps) {
  focus = ((focus + steps) % item_count + item_count) % item_count;
  refresh();
}

screen_t home_screen = {
    .build = build,
    .refresh = refresh,
    .rotate = rotate,
    .click = activate,
    .long_press = toggle,
};
