#pragma once

typedef enum {
  SCREEN_HOME,
  SCREEN_LIGHT,
  SCREEN_CLOCK,
  SCREEN_SETTINGS,
} screen_id_t;

void ui_init(void);
void ui_loop(void);
void ui_show(screen_id_t id, const char *target);
screen_id_t ui_current(void);

void ui_rotate(int steps);
void ui_click(void);
void ui_long_press(void);
