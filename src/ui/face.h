#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define FACE_MAX_MODES 12

typedef lv_color_t (*face_color_fn)(float t, const void *ctx);

// what touch on the face asks of the screen
typedef struct {
  // the big icon was tapped
  lv_event_cb_t icon;
  // a mode icon or page dot was tapped
  void (*pick)(int mode);
  // a swipe left (1) or right (-1)
  void (*swipe)(int direction);
  // the ring was touched at t, from 0 at its start to 1 at its end
  void (*drag)(float t);
} face_input_t;

typedef struct {
  const face_input_t *input;
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
                int mode_count, const face_input_t *input);
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
