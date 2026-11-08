#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "face.h"
#include "icons.h"
#include "screen.h"
#include "settings.h"
#include "ui.h"

typedef enum { UNIT_PERCENT, UNIT_SECONDS, UNIT_MINUTES, UNIT_DEGREES } unit_t;

typedef struct {
  const char *title;
  const char *icon;
  uint16_t *value;
  const uint16_t *choices;
  int count;
  unit_t unit;
  // shown instead of the value, one per choice
  const char *const *labels;
  setting_t optional;
} option_t;

static const uint16_t brightness_choices[] = {10, 20, 30, 40, 50,
                                              60, 70, 80, 90, 100};
static const uint16_t clock_choices[] = {5, 10, 15, 30, 60, 0};
static const uint16_t sleep_choices[] = {15, 30, 60, 120, 300, 0};
static const uint16_t power_off_choices[] = {10, 30, 60, 120, 360, 0};
static const uint16_t rotation_choices[] = {0, 90, 180, 270};
static const uint16_t touch_choices[] = {0, 1};
static const char *const touch_labels[] = {"Off", "On"};
static const uint16_t click_choices[] = {CLICKS_OFF, CLICKS_TOUCH, CLICKS_ALL};
static const char *const click_labels[] = {"Off", "Touch", "All"};
static const uint16_t hide_choices[] = {0, 1};
static const char *const hide_labels[] = {"Show", "Hide"};
static const uint16_t refresh_choices[] = {1, 2, 5, 10, 30};

#define CHOICES(array) .choices = array, .count = sizeof array / sizeof *array

static const option_t options[] = {
    {"Screen brightness", ICON_BRIGHTNESS, &settings.brightness,
     CHOICES(brightness_choices), .unit = UNIT_PERCENT},
    {"Clock after", ICON_CLOCK, &settings.clock_after, CHOICES(clock_choices),
     .unit = UNIT_SECONDS},
    {"Sleep after", ICON_SLEEP, &settings.sleep_after, CHOICES(sleep_choices),
     .unit = UNIT_SECONDS},
    {"Power off after", ICON_POWER, &settings.power_off_after,
     CHOICES(power_off_choices), .unit = UNIT_MINUTES},
    {"Rotation", ICON_ROTATION, &settings.rotation, CHOICES(rotation_choices),
     .unit = UNIT_DEGREES},
    {"Touchscreen", ICON_TOUCH, &settings.touch, CHOICES(touch_choices),
     .labels = touch_labels},
    {"Click sounds", ICON_CLICKS, &settings.clicks, CHOICES(click_choices),
     .labels = click_labels},
    {"Unavailable bulbs", ICON_EYE_OFF, &settings.hide_unavailable,
     CHOICES(hide_choices), .labels = hide_labels,
     .optional = SETTING_HIDE_UNAVAILABLE},
    {"Check bulbs every", ICON_REFRESH, &settings.bulb_refresh,
     CHOICES(refresh_choices), .unit = UNIT_SECONDS,
     .optional = SETTING_BULB_REFRESH},
};

#define OPTION_COUNT (int)(sizeof options / sizeof *options)
_Static_assert(OPTION_COUNT <= FACE_MAX_MODES, "a dot for every option");

static face_t face;
// the options this backend offers, in table order
static const option_t *shown[OPTION_COUNT];
static int shown_count, selected;

static int choice_index(const option_t *o) {
  for (int i = 0; i < o->count; i++)
    if (o->choices[i] == *o->value)
      return i;
  return 0;
}

static void format(const option_t *o, char *text, size_t size) {
  int v = *o->value;
  if (o->labels)
    snprintf(text, size, "%s", o->labels[choice_index(o)]);
  else if (o->unit == UNIT_DEGREES)
    snprintf(text, size, "%d\u00B0", v);
  else if (!v)
    snprintf(text, size, "Never");
  else if (o->unit == UNIT_PERCENT)
    snprintf(text, size, "%d%%", v);
  else if (o->unit == UNIT_SECONDS && v < 60)
    snprintf(text, size, "%d s", v);
  else if (o->unit == UNIT_SECONDS || v < 60)
    snprintf(text, size, "%d min", o->unit == UNIT_SECONDS ? v / 60 : v);
  else
    snprintf(text, size, "%d h", v / 60);
}

static void enter(const char *target) {
  selected = 0;
  face_reset(&face);
}

static void leave(void) { settings_save(); }

static void refresh(void) {
  const option_t *o = shown[selected];
  char text[16];
  format(o, text, sizeof text);
  face_set_value(&face, text, COLOR_ON_SURFACE);
  face_progress(&face, (float)choice_index(o) / (o->count - 1), COLOR_PRIMARY,
                COLOR_PRIMARY);
  face_set_icon(&face, o->icon, COLOR_PRIMARY);
  face_set_name(&face, o->title);
  face_set_modes(&face, selected, (1 << shown_count) - 1);
}

static bool set_choice(const option_t *o, int i) {
  uint16_t value = o->choices[i < 0 ? 0 : i >= o->count ? o->count - 1 : i];
  if (value == *o->value)
    return false;
  *o->value = value;
  if (o->value == &settings.brightness)
    board_set_backlight(settings.brightness);
  if (o->value == &settings.rotation)
    board_set_rotation(settings.rotation);
  if (o->value == &settings.touch)
    board_set_touch(settings.touch);
  refresh();
  return true;
}

static void rotate(int steps) {
  const option_t *o = shown[selected];
  set_choice(o, choice_index(o) + steps);
}

static void click(void) {
  selected = (selected + 1) % shown_count;
  refresh();
}

static void long_press(void) { ui_show(SCREEN_HOME, NULL); }

static void on_icon(lv_event_t *e) { ui_show(SCREEN_HOME, NULL); }

static void pick(int option) {
  selected = option;
  refresh();
}

// the options run as pages, with the home screen before the first
static void swipe(int direction) {
  int next = selected + direction;
  if (next < 0)
    ui_show(SCREEN_HOME, NULL);
  else if (next < shown_count)
    pick(next);
}

static void drag(float t) {
  const option_t *o = shown[selected];
  if (!set_choice(o, lroundf(t * (o->count - 1))))
    return;
  ui_sound(false);
  // the screen turning under the finger would move it onto another choice
  if (o->value == &settings.rotation)
    lv_indev_wait_release(lv_indev_active());
}

static const face_input_t input = {
    .icon = on_icon, .pick = pick, .swipe = swipe, .drag = drag};

static void build(lv_obj_t *root) {
  for (int i = 0; i < OPTION_COUNT; i++)
    if (settings_offered(options[i].optional))
      shown[shown_count++] = &options[i];
  face_build(&face, root, NULL, shown_count, &input);
}

screen_t settings_screen = {
    .build = build,
    .enter = enter,
    .leave = leave,
    .refresh = refresh,
    .rotate = rotate,
    .click = click,
    .long_press = long_press,
};
