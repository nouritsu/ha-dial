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
} option_t;

static const uint16_t brightness_choices[] = {10, 20, 30, 40, 50,
                                              60, 70, 80, 90, 100};
static const uint16_t clock_choices[] = {5, 10, 15, 30, 60, 0};
static const uint16_t sleep_choices[] = {15, 30, 60, 120, 300, 0};
static const uint16_t power_off_choices[] = {10, 30, 60, 120, 360, 0};
static const uint16_t rotation_choices[] = {0, 90, 180, 270};
#ifdef BACKEND_TAPO
static const uint16_t hide_choices[] = {0, 1};
static const char *const hide_labels[] = {"Show", "Hide"};
static const uint16_t refresh_choices[] = {1, 2, 5, 10, 30};
#endif

#define OPTION(title, icon, field, choices, unit)                              \
  {title, icon, &settings.field, choices, sizeof choices / sizeof *choices,    \
   unit}
#define LABELLED(title, icon, field, choices, names)                           \
  {title,                                                                      \
   icon,                                                                       \
   &settings.field,                                                            \
   choices,                                                                    \
   sizeof choices / sizeof *choices,                                           \
   .labels = names}

static const option_t options[] = {
    OPTION("Screen brightness", ICON_BRIGHTNESS, brightness, brightness_choices,
           UNIT_PERCENT),
    OPTION("Clock after", ICON_CLOCK, clock_after, clock_choices, UNIT_SECONDS),
    OPTION("Sleep after", ICON_SLEEP, sleep_after, sleep_choices, UNIT_SECONDS),
    OPTION("Power off after", ICON_POWER, power_off_after, power_off_choices,
           UNIT_MINUTES),
    OPTION("Rotation", ICON_ROTATION, rotation, rotation_choices, UNIT_DEGREES),
#ifdef BACKEND_TAPO
    LABELLED("Unavailable bulbs", ICON_EYE_OFF, hide_unavailable, hide_choices,
             hide_labels),
    OPTION("Check bulbs every", ICON_REFRESH, bulb_refresh, refresh_choices,
           UNIT_SECONDS),
#endif
};

#define OPTION_COUNT (int)(sizeof options / sizeof *options)

static face_t face;
static int selected;

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

static void on_icon(lv_event_t *e) { ui_show(SCREEN_HOME, NULL); }

static void build(lv_obj_t *root) {
  face_build(&face, root, NULL, OPTION_COUNT, on_icon);
}

static void enter(const char *target) {
  selected = 0;
  face_reset(&face);
}

static void leave(void) { settings_save(); }

static void refresh(void) {
  const option_t *o = &options[selected];
  char text[16];
  format(o, text, sizeof text);
  face_set_value(&face, text, COLOR_ON_SURFACE);
  face_progress(&face, (float)choice_index(o) / (o->count - 1), COLOR_PRIMARY,
                COLOR_PRIMARY);
  face_set_icon(&face, o->icon, COLOR_PRIMARY);
  face_set_name(&face, o->title);
  face_set_modes(&face, selected, (1 << OPTION_COUNT) - 1);
}

static void rotate(int steps) {
  const option_t *o = &options[selected];
  int i = choice_index(o) + steps;
  *o->value = o->choices[i < 0 ? 0 : i >= o->count ? o->count - 1 : i];
  if (o->value == &settings.brightness)
    board_set_backlight(settings.brightness);
  if (o->value == &settings.rotation)
    board_set_rotation(settings.rotation);
  refresh();
}

static void click(void) {
  selected = (selected + 1) % OPTION_COUNT;
  refresh();
}

static void long_press(void) { ui_show(SCREEN_HOME, NULL); }

screen_t settings_screen = {
    .build = build,
    .enter = enter,
    .leave = leave,
    .refresh = refresh,
    .rotate = rotate,
    .click = click,
    .long_press = long_press,
};
