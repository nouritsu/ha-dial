#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define FACE_MAX_MODES 4

typedef lv_color_t (*face_color_fn)(float t, const void *ctx);

typedef struct {
  lv_obj_t *ring;
  lv_obj_t *knob;
  lv_obj_t *value;
  lv_obj_t *icon_button;
  lv_obj_t *icon;
  lv_obj_t *name;
  lv_obj_t *modes[FACE_MAX_MODES];
  lv_obj_t *mode_icons[FACE_MAX_MODES];
  int mode_count;
  bool dots;
  float t, goal;
  bool jump;
  bool gradient;
  bool dim;
  lv_color_t color;
  face_color_fn color_at;
  const void *ctx;
} face_t;

// without mode icons, the modes show as page dots
void face_build(face_t *f, lv_obj_t *parent, const char *const *mode_icons,
                int mode_count, lv_event_cb_t on_icon);
// the next value lands without animating, as when a screen opens
void face_reset(face_t *f);
void face_progress(face_t *f, float t, lv_color_t color, lv_color_t knob);
void face_gradient(face_t *f, float t, face_color_fn color_at, const void *ctx,
                   lv_color_t knob);
void face_set_dim(face_t *f, bool dim);
void face_set_value(face_t *f, const char *text, lv_color_t color);
void face_set_icon(face_t *f, const char *glyph, lv_color_t color);
void face_set_name(face_t *f, const char *text);
void face_set_modes(face_t *f, int active, uint32_t available);
