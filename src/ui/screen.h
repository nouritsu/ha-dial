#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(text_14)
LV_FONT_DECLARE(text_18)
LV_FONT_DECLARE(text_28)
LV_FONT_DECLARE(clock_56)
LV_FONT_DECLARE(icons_20)
LV_FONT_DECLARE(icons_32)
LV_FONT_DECLARE(icons_72)

#define COLOR_TEXT lv_color_hex(0xF5F5F7)
#define COLOR_MUTED lv_color_hex(0x8E8E93)
#define COLOR_FAINT lv_color_hex(0x48484A)
#define COLOR_SURFACE lv_color_hex(0x1C1C1E)
#define COLOR_TRACK lv_color_hex(0x2C2C2E)
#define COLOR_ACCENT lv_color_hex(0x64D2FF)
#define COLOR_WARNING lv_color_hex(0xFFB340)
#define COLOR_ERROR lv_color_hex(0xFF453A)

#define SCREEN_SIZE 240
#define SCREEN_CENTER 120

typedef struct {
  lv_obj_t *root;
  void (*build)(lv_obj_t *root);
  void (*enter)(const char *target);
  void (*leave)(void);
  void (*refresh)(void);
  void (*rotate)(int steps);
  void (*click)(void);
  void (*long_press)(void);
} screen_t;

extern screen_t home_screen, light_screen, clock_screen, settings_screen;

void home_relayout(void);

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);
lv_obj_t *ui_circle(lv_obj_t *parent, int size, lv_color_t color);
void ui_inert(lv_obj_t *obj);
void ui_set_text(lv_obj_t *label, const char *text);
void ui_place(lv_obj_t *obj, float degrees, float radius);
void ui_on_click(lv_obj_t *obj, lv_event_cb_t cb, void *user_data);
