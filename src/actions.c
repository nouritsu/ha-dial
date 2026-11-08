#include "actions.h"

#include <stdbool.h>
#include <string.h>

#include "backend.h"
#include "config.h"
#include "model.h"

#define QUEUE_SIZE 8

typedef struct {
  bool scene;
  prop_t prop;
  int value;
  char target[MODEL_ID_LEN];
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

static int in_flight;
static uint32_t sent_at;

static void enqueue(const call_t *call) {
  call_t *last = queued ? &queue[queued - 1] : NULL;
  if (!(call->merge && last && last->merge == call->merge &&
        !strcmp(last->target, call->target))) {
    if (queued == QUEUE_SIZE)
      memmove(queue, queue + 1, sizeof(call_t) * --queued);
    last = &queue[queued++];
  }
  *last = *call;
}

static call_t make_call(bool scene, prop_t prop, int value, const char *target,
                        int merge) {
  call_t call = {.scene = scene, .prop = prop, .value = value, .merge = merge};
  strlcpy(call.target, target, sizeof call.target);
  return call;
}

static call_t pending_call(void) {
  return make_call(false, pending.prop, pending.value, pending.target,
                   pending.prop + 1);
}

static void queue_pending(void) {
  if (!pending.active)
    return;
  pending.active = false;
  call_t call = pending_call();
  enqueue(&call);
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
    queue_pending();
  pending.active = true;
  strlcpy(pending.target, target, sizeof pending.target);
  pending.prop = prop;
  pending.value = value;
  actions_loop();
}

void actions_toggle(const char *target) {
  target_t t;
  queue_pending();
  if (!model_target(target, &t))
    return;
  bool on = !t.state.on;
  for (int i = 0; i < t.count; i++)
    t.lights[i]->state.on = on;
  hold(&t);
  call_t call = make_call(false, PROP_POWER, on, target, 0);
  enqueue(&call);
  actions_loop();
}

void actions_scene(const char *id) {
  queue_pending();
  call_t call = make_call(true, PROP_POWER, 1, id, 0);
  enqueue(&call);
  actions_loop();
}

static int send(const call_t *call) {
  if (call->scene)
    return backend->scene(call->target);
  const char *ids[MODEL_MAX_LIGHTS];
  target_t t;
  if (!model_target(call->target, &t))
    return 0;
  for (int i = 0; i < t.count; i++)
    ids[i] = t.lights[i]->id;
  return backend->set(ids, t.count, call->prop, call->value);
}

static bool busy(void) {
  uint32_t since = now_ms() - sent_at;
  return since < SEND_INTERVAL_MS ||
         (backend->waiting(in_flight) && since < CALL_TIMEOUT_MS);
}

void actions_loop(void) {
  if (backend->link() != LINK_READY)
    return;
  for (int i = 0; i < queued; i++)
    send(&queue[i]);
  queued = 0;
  if (!pending.active || busy())
    return;
  pending.active = false;
  call_t call = pending_call();
  in_flight = send(&call);
  sent_at = now_ms();
}
