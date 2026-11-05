#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(text_16)
LV_FONT_DECLARE(text_20)
LV_FONT_DECLARE(text_36)
LV_FONT_DECLARE(clock_64)
LV_FONT_DECLARE(icons_24)
LV_FONT_DECLARE(icons_28)
LV_FONT_DECLARE(icons_72)

// Material 3 dark roles over a black background
#define COLOR_ON_SURFACE lv_color_hex(0xE6E0E9)
#define COLOR_ON_SURFACE_VARIANT lv_color_hex(0xCAC4D0)
#define COLOR_OUTLINE lv_color_hex(0x938F99)
#define COLOR_OUTLINE_VARIANT lv_color_hex(0x49454F)
#define COLOR_SURFACE_CONTAINER lv_color_hex(0x211F26)
#define COLOR_SURFACE_CONTAINER_HIGHEST lv_color_hex(0x36343B)
#define COLOR_PRIMARY lv_color_hex(0xD0BCFF)
#define COLOR_SECONDARY_CONTAINER lv_color_hex(0x4A4458)
#define COLOR_ON_SECONDARY_CONTAINER lv_color_hex(0xE8DEF8)
#define COLOR_TERTIARY lv_color_hex(0xEFB8C8)
#define COLOR_WARNING lv_color_hex(0xFFCC80)
#define COLOR_ERROR lv_color_hex(0xF2B8B5)

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
void ui_set_text_color(lv_obj_t *obj, lv_color_t color);
void ui_set_bg(lv_obj_t *obj, lv_color_t color, lv_opa_t opa);
void ui_set_outline(lv_obj_t *obj, int width);
void ui_place(lv_obj_t *obj, float degrees, float radius);
void ui_on_click(lv_obj_t *obj, lv_event_cb_t cb, void *user_data);
lv_color_t ui_pastel(uint32_t rgb);
lv_color_t ui_on_pastel(lv_color_t pastel);
