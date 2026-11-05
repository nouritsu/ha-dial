#include "actions.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "ha.h"
#include "model.h"

#define QUEUE_SIZE 8

typedef struct {
  const char *domain;
  const char *service;
  char target[MODEL_ID_LEN];
  char data[48];
  int merge;
} call_t;

static call_t queue[QUEUE_SIZE];
static int queued;

static struct {
  bool active;
  char target[MODEL_ID_LEN];
  prop_t prop;
  int value;
} pending;

static uint32_t last_flush;

static void enqueue(const char *domain, const char *service, const char *target,
                    const char *data, int merge) {
  call_t *last = queued ? &queue[queued - 1] : NULL;
  if (!(merge && last && last->merge == merge &&
        !strcmp(last->target, target))) {
    if (queued == QUEUE_SIZE)
      memmove(queue, queue + 1, sizeof(call_t) * --queued);
    last = &queue[queued++];
  }
  last->domain = domain;
  last->service = service;
  last->merge = merge;
  strlcpy(last->target, target, sizeof last->target);
  strlcpy(last->data, data, sizeof last->data);
}

static void flush(void) {
  if (!pending.active)
    return;
  pending.active = false;
  last_flush = now_ms();
  const char *service = "turn_on";
  char data[48] = "";
  switch (pending.prop) {
  case PROP_BRIGHTNESS:
    if (pending.value)
      snprintf(data, sizeof data, "{\"brightness_pct\":%d}", pending.value);
    else
      service = "turn_off";
    break;
  case PROP_HUE:
    snprintf(data, sizeof data, "{\"hs_color\":[%d,100]}", pending.value);
    break;
  case PROP_KELVIN:
    snprintf(data, sizeof data, "{\"color_temp_kelvin\":%d}", pending.value);
    break;
  }
  enqueue("light", service, pending.target, data, pending.prop + 1);
}

static void hold(target_t *t) {
  uint32_t until = now_ms() + ECHO_HOLD_MS;
  for (int i = 0; i < t->count; i++)
    t->lights[i]->hold_until = until;
  model_changed();
}

void actions_set(const char *target, prop_t prop, int value) {
  target_t t;
  if (!model_target(target, &t))
    return;
  for (int i = 0; i < t.count; i++) {
    light_t *l = t.lights[i];
    light_state_t *s = &l->state;
    s->on = prop != PROP_BRIGHTNESS || value > 0;
    if (prop == PROP_BRIGHTNESS && value)
      s->brightness = (value * 255 + 50) / 100;
    if (prop == PROP_HUE) {
      s->hue = value;
      s->saturation = 100;
      s->color_temp = false;
    }
    if (prop == PROP_KELVIN) {
      s->kelvin = value < l->min_kelvin   ? l->min_kelvin
                  : value > l->max_kelvin ? l->max_kelvin
                                          : value;
      s->color_temp = true;
    }
  }
  hold(&t);
  if (pending.active &&
      (strcmp(pending.target, target) || pending.prop != prop))
    flush();
  pending.active = true;
  strlcpy(pending.target, target, sizeof pending.target);
  pending.prop = prop;
  pending.value = value;
  if (now_ms() - last_flush >= SEND_INTERVAL_MS)
    flush();
}

void actions_toggle(const char *target) {
  target_t t;
  flush();
  if (!model_target(target, &t))
    return;
  bool on = !t.state.on;
  for (int i = 0; i < t.count; i++)
    t.lights[i]->state.on = on;
  hold(&t);
  enqueue("light", on ? "turn_on" : "turn_off", target, "", 0);
}

void actions_scene(const char *id) { enqueue("scene", "turn_on", id, "", 0); }

static void send(const call_t *call) {
  const char *ids[MODEL_MAX_LIGHTS];
  int count = 0;
  target_t t;
  if (strcmp(call->domain, "light")) {
    ids[count++] = call->target;
  } else if (model_target(call->target, &t)) {
    for (; count < t.count; count++)
      ids[count] = t.lights[count]->id;
  }
  if (count)
    ha_call(call->domain, call->service, ids, count,
            call->data[0] ? call->data : NULL);
}

void actions_loop(void) {
  if (pending.active && now_ms() - last_flush >= SEND_INTERVAL_MS)
    flush();
  if (ha_link() != LINK_READY)
    return;
  for (int i = 0; i < queued; i++)
    send(&queue[i]);
  queued = 0;
}
