#include "face.h"

#include "screen.h"

#define RING_RADIUS 116
#define RING_WIDTH 12
#define RING_START 135
#define RING_SWEEP 270
#define RING_SEGMENTS 54
#define KNOB_SIZE 22
#define KNOB_RADIUS 110
#define MODE_RADIUS 98
#define MODE_SPACING 24

static lv_color_t shade(const face_t *f, lv_color_t c) {
  return f->dim ? lv_color_mix(c, lv_color_black(), 90) : c;
}

static void draw_ring(lv_event_t *e) {
  face_t *f = lv_event_get_user_data(e);
  lv_area_t area;
  lv_obj_get_coords(lv_event_get_target_obj(e), &area);
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = area.x1 + SCREEN_CENTER;
  arc.center.y = area.y1 + SCREEN_CENTER;
  arc.radius = RING_RADIUS;
  arc.width = RING_WIDTH;
  lv_layer_t *layer = lv_event_get_layer(e);
  if (!f->gradient) {
    arc.rounded = 1;
    arc.color = shade(f, COLOR_TRACK);
    arc.start_angle = RING_START;
    arc.end_angle = (RING_START + RING_SWEEP) % 360;
    lv_draw_arc(layer, &arc);
    int end = RING_START + (int)(RING_SWEEP * f->t);
    if (end > RING_START) {
      arc.color = shade(f, f->color);
      arc.end_angle = end % 360;
      lv_draw_arc(layer, &arc);
    }
    return;
  }
  for (int i = 0; i < RING_SEGMENTS; i++) {
    arc.color = shade(f, f->color_at((i + 0.5f) / RING_SEGMENTS, f->ctx));
    arc.start_angle = (RING_START + RING_SWEEP * i / RING_SEGMENTS) % 360;
    arc.end_angle = (RING_START + RING_SWEEP * (i + 1) / RING_SEGMENTS +
                     (i < RING_SEGMENTS - 1)) %
                    360;
    arc.rounded = i == 0 || i == RING_SEGMENTS - 1;
    lv_draw_arc(layer, &arc);
  }
}

void face_build(face_t *f, lv_obj_t *parent, const char *const *mode_icons,
                int mode_count, lv_event_cb_t on_icon) {
  f->ring = lv_obj_create(parent);
  lv_obj_set_size(f->ring, SCREEN_SIZE, SCREEN_SIZE);
  ui_inert(f->ring);
  lv_obj_add_event_cb(f->ring, draw_ring, LV_EVENT_DRAW_MAIN, f);

  f->knob = ui_circle(parent, KNOB_SIZE, COLOR_TEXT);
  lv_obj_set_style_border_width(f->knob, 3, 0);

  f->value = ui_label(parent, &text_28, COLOR_TEXT);
  lv_obj_align(f->value, LV_ALIGN_TOP_MID, 0, 30);

  f->icon_button = lv_obj_create(parent);
  lv_obj_set_size(f->icon_button, 100, 88);
  lv_obj_align(f->icon_button, LV_ALIGN_TOP_MID, 0, 62);
  lv_obj_set_scrollable(f->icon_button, false);
  ui_on_click(f->icon_button, on_icon, NULL);
  f->icon = ui_label(f->icon_button, &icons_72, COLOR_TEXT);
  lv_obj_center(f->icon);

  f->name = ui_label(parent, &text_18, COLOR_TEXT);
  lv_obj_set_width(f->name, 168);
  lv_label_set_long_mode(f->name, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_align(f->name, LV_ALIGN_TOP_MID, 0, 150);

  f->mode_count = mode_count;
  for (int i = 0; i < mode_count; i++) {
    f->modes[i] = ui_label(parent, &icons_20, COLOR_FAINT);
    lv_label_set_text(f->modes[i], mode_icons[i]);
  }
}

static void set_ring(face_t *f, float t, bool gradient, lv_color_t color,
                     face_color_fn color_at, const void *ctx, lv_color_t knob) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  if (t != f->t || gradient != f->gradient || !lv_color_eq(color, f->color) ||
      color_at != f->color_at || ctx != f->ctx) {
    f->t = t;
    f->gradient = gradient;
    f->color = color;
    f->color_at = color_at;
    f->ctx = ctx;
    lv_obj_invalidate(f->ring);
  }
  lv_obj_set_style_bg_color(f->knob, knob, 0);
  ui_place(f->knob, RING_START + RING_SWEEP * t, KNOB_RADIUS);
}

void face_progress(face_t *f, float t, lv_color_t color, lv_color_t knob) {
  set_ring(f, t, false, color, NULL, NULL, knob);
}

void face_gradient(face_t *f, float t, face_color_fn color_at, const void *ctx,
                   lv_color_t knob) {
  set_ring(f, t, true, f->color, color_at, ctx, knob);
}

void face_set_dim(face_t *f, bool dim) {
  if (dim == f->dim)
    return;
  f->dim = dim;
  lv_obj_invalidate(f->ring);
}

void face_set_value(face_t *f, const char *text) {
  ui_set_text(f->value, text);
}

void face_set_icon(face_t *f, const char *glyph, lv_color_t color) {
  ui_set_text(f->icon, glyph);
  lv_obj_set_style_text_color(f->icon, color, 0);
}

void face_set_name(face_t *f, const char *text) { ui_set_text(f->name, text); }

void face_set_modes(face_t *f, int active, uint32_t available) {
  int visible = 0, n = 0;
  for (int i = 0; i < f->mode_count; i++)
    visible += (available >> i) & 1;
  for (int i = 0; i < f->mode_count; i++) {
    bool shown = (available >> i) & 1;
    lv_obj_set_hidden(f->modes[i], !shown || visible < 2);
    if (!shown)
      continue;
    lv_obj_set_style_text_color(f->modes[i],
                                i == active ? COLOR_TEXT : COLOR_FAINT, 0);
    ui_place(f->modes[i], 90 + MODE_SPACING * (n++ - (visible - 1) / 2.0f),
             MODE_RADIUS);
  }
}
