#include "face.h"

#include <math.h>

#include "screen.h"

#define RING_RADIUS 116
#define RING_WIDTH 12
#define RING_START 135
#define RING_SWEEP 270
#define RING_SEGMENTS 54
#define KNOB_SIZE 22
#define KNOB_BORDER 3
#define KNOB_RADIUS 110
#define KNOB_ANIM_MS 120
// four modes fit between the ends of the ring, clear of the knob at either end
#define MODE_SIZE 30
#define MODE_RADIUS 100
#define MODE_SPACING 20
#define DOT_SIZE 8
#define DOT_ACTIVE_WIDTH 20
#define DOT_GAP 6
#define DOT_BOTTOM 22

static lv_color_t shade(const face_t *f, lv_color_t c) {
  return f->dim ? lv_color_mix(c, lv_color_black(), 90) : c;
}

static float ring_angle(float t) { return RING_START + RING_SWEEP * t; }

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
    arc.color = shade(f, COLOR_SURFACE_CONTAINER_HIGHEST);
    arc.start_angle = RING_START;
    arc.end_angle = (RING_START + RING_SWEEP) % 360;
    lv_draw_arc(layer, &arc);
    int end = (int)ring_angle(f->t);
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

static void include(lv_area_t *area, float degrees, int radius) {
  float rad = degrees * (float)M_PI / 180;
  int32_t x = SCREEN_CENTER + lroundf(radius * cosf(rad));
  int32_t y = SCREEN_CENTER + lroundf(radius * sinf(rad));
  area->x1 = LV_MIN(area->x1, x);
  area->y1 = LV_MIN(area->y1, y);
  area->x2 = LV_MAX(area->x2, x);
  area->y2 = LV_MAX(area->y2, y);
}

// redraws just the stretch of ring between two positions, so the panel
// only receives the few rows that changed
static void invalidate_span(face_t *f, float from, float to) {
  float end = ring_angle(fmaxf(from, to));
  lv_area_t area = {SCREEN_SIZE, SCREEN_SIZE, 0, 0};
  for (float a = ring_angle(fminf(from, to));; a += 10) {
    a = fminf(a, end);
    include(&area, a, RING_RADIUS);
    include(&area, a, RING_RADIUS - RING_WIDTH);
    if (a == end)
      break;
  }
  int pad = RING_WIDTH / 2 + 2;
  area.x1 -= pad;
  area.y1 -= pad;
  area.x2 += pad;
  area.y2 += pad;
  lv_obj_invalidate_area(f->ring, &area);
}

static void show(face_t *f, float t) {
  if (!f->gradient)
    invalidate_span(f, f->t, t);
  f->t = t;
  ui_place(f->knob, ring_angle(t), KNOB_RADIUS);
}

static void animate(void *f, int32_t permille) { show(f, permille / 1000.0f); }

void face_build(face_t *f, lv_obj_t *parent, const char *const *mode_icons,
                int mode_count, lv_event_cb_t on_icon) {
  f->ring = lv_obj_create(parent);
  lv_obj_set_size(f->ring, SCREEN_SIZE, SCREEN_SIZE);
  ui_inert(f->ring);
  lv_obj_add_event_cb(f->ring, draw_ring, LV_EVENT_DRAW_MAIN, f);

  f->knob = ui_circle(parent, KNOB_SIZE, COLOR_PRIMARY);
  lv_obj_set_style_border_width(f->knob, KNOB_BORDER, 0);
  lv_obj_set_style_border_color(f->knob, lv_color_black(), 0);

  f->value = ui_label(parent, &text_36, COLOR_ON_SURFACE);
  lv_obj_align(f->value, LV_ALIGN_TOP_MID, 0, 30);

  f->icon_button = lv_obj_create(parent);
  lv_obj_set_size(f->icon_button, 100, 76);
  lv_obj_align(f->icon_button, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_set_scrollable(f->icon_button, false);
  ui_on_click(f->icon_button, on_icon, NULL);
  f->icon = ui_label(f->icon_button, &icons_72, COLOR_ON_SURFACE);
  lv_obj_center(f->icon);

  f->name = ui_label(parent, &text_20, COLOR_ON_SURFACE);
  lv_obj_set_width(f->name, 168);
  lv_obj_set_style_text_line_space(f->name, -2, 0);
  lv_label_set_long_mode(f->name, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_align(f->name, LV_ALIGN_TOP_MID, 0, 148);

  f->mode_count = mode_count;
  f->dots = !mode_icons;
  lv_obj_t *row = NULL;
  if (f->dots) {
    row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, DOT_GAP, 0);
    ui_inert(row);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -DOT_BOTTOM);
  }
  for (int i = 0; i < mode_count; i++) {
    if (f->dots) {
      f->modes[i] = ui_circle(row, DOT_SIZE, COLOR_OUTLINE);
      continue;
    }
    f->modes[i] = ui_circle(parent, MODE_SIZE, COLOR_SECONDARY_CONTAINER);
    f->mode_icons[i] =
        ui_label(f->modes[i], &icons_24, COLOR_ON_SURFACE_VARIANT);
    lv_label_set_text(f->mode_icons[i], mode_icons[i]);
    lv_obj_center(f->mode_icons[i]);
  }
  f->jump = true;
}

void face_reset(face_t *f) { f->jump = true; }

static void set_ring(face_t *f, float t, bool gradient, lv_color_t color,
                     face_color_fn color_at, const void *ctx, lv_color_t knob) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  bool restyle = gradient != f->gradient || !lv_color_eq(color, f->color) ||
                 color_at != f->color_at || ctx != f->ctx;
  f->gradient = gradient;
  f->color = color;
  f->color_at = color_at;
  f->ctx = ctx;
  ui_set_bg(f->knob, knob, LV_OPA_COVER);

  // a wrap from one end to the other (hue) jumps rather than sweeping back
  if (restyle || f->jump || fabsf(t - f->goal) > 0.5f) {
    lv_anim_delete(f, animate);
    f->jump = false;
    f->t = f->goal = t;
    lv_obj_invalidate(f->ring);
    ui_place(f->knob, ring_angle(t), KNOB_RADIUS);
    return;
  }
  if (t == f->goal)
    return;
  f->goal = t;
  lv_anim_delete(f, animate);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, f);
  lv_anim_set_exec_cb(&a, animate);
  lv_anim_set_values(&a, lroundf(f->t * 1000), lroundf(t * 1000));
  lv_anim_set_duration(&a, KNOB_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
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

void face_set_value(face_t *f, const char *text, lv_color_t color) {
  ui_set_text(f->value, text);
  ui_set_text_color(f->value, color);
}

void face_set_icon(face_t *f, const char *glyph, lv_color_t color) {
  ui_set_text(f->icon, glyph);
  ui_set_text_color(f->icon, color);
}

void face_set_name(face_t *f, const char *text) { ui_set_text(f->name, text); }

void face_set_modes(face_t *f, int active, uint32_t available) {
  if (f->dots) {
    for (int i = 0; i < f->mode_count; i++) {
      int width = i == active ? DOT_ACTIVE_WIDTH : DOT_SIZE;
      if (lv_obj_get_style_width(f->modes[i], 0) != width)
        lv_obj_set_width(f->modes[i], width);
      ui_set_bg(f->modes[i], i == active ? COLOR_PRIMARY : COLOR_OUTLINE,
                LV_OPA_COVER);
    }
    return;
  }
  int visible = 0, n = 0;
  for (int i = 0; i < f->mode_count; i++)
    visible += (available >> i) & 1;
  for (int i = 0; i < f->mode_count; i++) {
    bool shown = (available >> i) & 1 && visible > 1;
    if (shown == lv_obj_is_hidden(f->modes[i]))
      lv_obj_set_hidden(f->modes[i], !shown);
    if (!shown)
      continue;
    ui_set_bg(f->modes[i], COLOR_SECONDARY_CONTAINER,
              i == active ? LV_OPA_COVER : LV_OPA_TRANSP);
    ui_set_text_color(f->mode_icons[i], i == active
                                            ? COLOR_ON_SECONDARY_CONTAINER
                                            : COLOR_ON_SURFACE_VARIANT);
    // angles grow clockwise, so counting down puts the first mode on the left
    ui_place(f->modes[i], 90 - MODE_SPACING * (n++ - (visible - 1) / 2.0f),
             MODE_RADIUS);
  }
}
