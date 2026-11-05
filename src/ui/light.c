#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actions.h"
#include "config.h"
#include "face.h"
#include "icons.h"
#include "model.h"
#include "screen.h"
#include "ui.h"

enum { MODE_BRIGHTNESS, MODE_HUE, MODE_KELVIN, MODE_COUNT };

static const char *const mode_icons[MODE_COUNT] = {ICON_BRIGHTNESS, ICON_HUE,
                                                   ICON_KELVIN};

static face_t face;
static char target[MODEL_ID_LEN];
static int mode;
static struct {
  int min, max;
} range;

static uint32_t available_modes(const target_t *t) {
  return 1 << MODE_BRIGHTNESS | t->has_color << MODE_HUE |
         t->has_temp << MODE_KELVIN;
}

static int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static lv_color_t hue_at(float t, const void *ctx) {
  return ui_pastel(model_hsv((int)(t * 359), 100, 100));
}

static lv_color_t kelvin_at(float t, const void *ctx) {
  return ui_pastel(
      model_kelvin_color(range.min + (int)(t * (range.max - range.min))));
}

static void on_icon(lv_event_t *e) { actions_toggle(target); }

static void build(lv_obj_t *root) {
  face_build(&face, root, mode_icons, MODE_COUNT, on_icon);
}

static void enter(const char *id) {
  strlcpy(target, id ? id : "", sizeof target);
  mode = MODE_BRIGHTNESS;
  face_reset(&face);
}

static void refresh(void) {
  target_t t;
  if (!model_target(target, &t)) {
    ui_show(SCREEN_HOME, NULL);
    return;
  }
  uint32_t modes = available_modes(&t);
  if (!(modes >> mode & 1))
    mode = MODE_BRIGHTNESS;
  const light_state_t *s = &t.state;
  lv_color_t tint = ui_pastel(model_tint(s));
  range.min = t.min_kelvin;
  range.max = t.max_kelvin > t.min_kelvin ? t.max_kelvin : t.min_kelvin + 1;
  char text[16];

  switch (mode) {
  case MODE_BRIGHTNESS: {
    int percent = model_percent(s);
    if (s->on)
      snprintf(text, sizeof text, "%d%%", percent);
    else
      snprintf(text, sizeof text, "Off");
    face_progress(&face, percent / 100.0f, s->on ? tint : COLOR_OUTLINE,
                  s->on ? tint : COLOR_ON_SURFACE_VARIANT);
    break;
  }
  case MODE_HUE:
    snprintf(text, sizeof text, "%d\u00B0", s->hue);
    face_gradient(&face, s->hue / 359.0f, hue_at, NULL,
                  ui_pastel(model_hsv(s->hue, 100, 100)));
    break;
  case MODE_KELVIN: {
    int k = clamp(s->kelvin, range.min, range.max);
    snprintf(text, sizeof text, "%d K", k);
    face_gradient(&face, (float)(k - range.min) / (range.max - range.min),
                  kelvin_at, &range, ui_pastel(model_kelvin_color(k)));
    break;
  }
  }
  face_set_value(&face, text,
                 s->on ? COLOR_ON_SURFACE : COLOR_ON_SURFACE_VARIANT);
  face_set_dim(&face, !s->on);
  const char *glyph =
      *target ? icon_named(t.icon, s->on ? ICON_BULB : ICON_BULB_OFF)
              : (s->on ? ICON_GROUP : ICON_GROUP_OFF);
  face_set_icon(&face, glyph,
                !s->available ? COLOR_OUTLINE_VARIANT
                : s->on       ? tint
                              : COLOR_ON_SURFACE_VARIANT);
  face_set_name(&face, t.name);
  face_set_modes(&face, mode, modes);
}

static int step_brightness(int v, int steps) {
  for (; steps > 0; steps--)
    v = v == 0                ? 1
        : v < BRIGHTNESS_STEP ? BRIGHTNESS_STEP
                              : (v / BRIGHTNESS_STEP + 1) * BRIGHTNESS_STEP;
  for (; steps < 0; steps++)
    v = v > BRIGHTNESS_STEP ? (v - 1) / BRIGHTNESS_STEP * BRIGHTNESS_STEP
        : v > 1             ? 1
                            : 0;
  return clamp(v, 0, 100);
}

static int accelerate(int steps) {
  static uint32_t last, pace = ACCEL_IDLE_MS;
  static int direction;
  uint32_t now = now_ms(), gap = (now - last) / abs(steps);
  int sign = steps > 0 ? 1 : -1;
  pace = gap > ACCEL_IDLE_MS || sign != direction ? ACCEL_IDLE_MS
                                                  : (pace + gap) / 2;
  last = now;
  direction = sign;
  return steps * (pace < ACCEL_QUADRUPLE_MS ? 4
                  : pace < ACCEL_DOUBLE_MS  ? 2
                                            : 1);
}

static void rotate(int steps) {
  target_t t;
  if (!model_target(target, &t))
    return;
  steps = accelerate(steps);
  const light_state_t *s = &t.state;
  switch (mode) {
  case MODE_BRIGHTNESS:
    actions_set(target, PROP_BRIGHTNESS,
                step_brightness(model_percent(s), steps));
    break;
  case MODE_HUE:
    actions_set(target, PROP_HUE,
                ((s->hue + steps * HUE_STEP) % 360 + 360) % 360);
    break;
  case MODE_KELVIN: {
    int k = clamp(s->kelvin, t.min_kelvin, t.max_kelvin);
    int snapped = t.min_kelvin + (k - t.min_kelvin + KELVIN_STEP / 2) /
                                     KELVIN_STEP * KELVIN_STEP;
    actions_set(
        target, PROP_KELVIN,
        clamp(snapped + steps * KELVIN_STEP, t.min_kelvin, t.max_kelvin));
    break;
  }
  }
}

static void click(void) {
  target_t t;
  if (!model_target(target, &t))
    return;
  uint32_t modes = available_modes(&t);
  do
    mode = (mode + 1) % MODE_COUNT;
  while (!(modes >> mode & 1));
  refresh();
}

static void long_press(void) { ui_show(SCREEN_HOME, NULL); }

screen_t light_screen = {
    .build = build,
    .enter = enter,
    .refresh = refresh,
    .rotate = rotate,
    .click = click,
    .long_press = long_press,
};
