#include "model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "settings.h"

#define get cJSON_GetObjectItemCaseSensitive

light_t model_lights[MODEL_MAX_LIGHTS];
int model_light_count;
scene_t model_scenes[MODEL_MAX_SCENES];
int model_scene_count;
weather_t model_weather;

static uint32_t revision = 1;
static uint32_t layout = 1;

uint32_t model_revision(void) { return revision; }
uint32_t model_layout(void) { return layout; }
void model_changed(void) { revision++; }

static void copy_string(char *dst, size_t size, const cJSON *v) {
  if (cJSON_IsString(v))
    strlcpy(dst, v->valuestring, size);
}

static bool has_prefix(const char *s, const char *prefix) {
  return !strncmp(s, prefix, strlen(prefix));
}

light_t *model_light(const char *id) {
  for (int i = 0; i < model_light_count; i++)
    if (!strcmp(model_lights[i].id, id))
      return &model_lights[i];
  return NULL;
}

static scene_t *find_scene(const char *id) {
  for (int i = 0; i < model_scene_count; i++)
    if (!strcmp(model_scenes[i].id, id))
      return &model_scenes[i];
  return NULL;
}

void model_set_entities(const cJSON *ids) {
  static light_t lights[MODEL_MAX_LIGHTS];
  static scene_t scenes[MODEL_MAX_SCENES];
  int light_count = 0, scene_count = 0;
  const cJSON *item;
  cJSON_ArrayForEach(item, ids) {
    if (!cJSON_IsString(item))
      continue;
    const char *id = item->valuestring;
    if (has_prefix(id, "light.") && light_count < MODEL_MAX_LIGHTS) {
      light_t *old = model_light(id), *l = &lights[light_count++];
      if (old) {
        *l = *old;
      } else {
        *l = (light_t){.min_kelvin = 2000, .max_kelvin = 6500};
        strlcpy(l->id, id, sizeof l->id);
        strlcpy(l->name, id + 6, sizeof l->name);
      }
    } else if (has_prefix(id, "scene.") && scene_count < MODEL_MAX_SCENES) {
      scene_t *old = find_scene(id), *s = &scenes[scene_count++];
      if (old) {
        *s = *old;
      } else {
        *s = (scene_t){0};
        strlcpy(s->id, id, sizeof s->id);
        strlcpy(s->name, id + 6, sizeof s->name);
      }
    }
  }
  memcpy(model_lights, lights, sizeof(light_t) * light_count);
  memcpy(model_scenes, scenes, sizeof(scene_t) * scene_count);
  model_light_count = light_count;
  model_scene_count = scene_count;
  layout++;
  revision++;
}

light_t *model_add_light(const char *id) {
  if (model_light_count == MODEL_MAX_LIGHTS)
    return NULL;
  light_t *l = &model_lights[model_light_count++];
  *l = (light_t){.min_kelvin = 2000, .max_kelvin = 6500};
  strlcpy(l->id, id, sizeof l->id);
  strlcpy(l->name, id, sizeof l->name);
  layout++;
  revision++;
  return l;
}

void model_apply_remote(light_t *l) {
  if (now_ms() >= l->hold_until) {
    l->state = l->remote;
    l->hold_until = 0;
  }
  revision++;
}

static void apply_light(light_t *l, const cJSON *state, const cJSON *a) {
  light_state_t *r = &l->remote;
  if (cJSON_IsString(state)) {
    r->on = !strcmp(state->valuestring, "on");
    r->available = strcmp(state->valuestring, "unavailable") != 0;
  }
  if (cJSON_IsObject(a)) {
    const cJSON *v;
    copy_string(l->name, sizeof l->name, get(a, "friendly_name"));
    copy_string(l->icon, sizeof l->icon, get(a, "icon"));
    if (cJSON_IsNumber(v = get(a, "min_color_temp_kelvin")))
      l->min_kelvin = v->valueint;
    if (cJSON_IsNumber(v = get(a, "max_color_temp_kelvin")))
      l->max_kelvin = v->valueint;
    if (cJSON_IsArray(v = get(a, "supported_color_modes"))) {
      l->has_color = l->has_temp = false;
      const cJSON *mode;
      cJSON_ArrayForEach(mode, v) {
        if (!cJSON_IsString(mode))
          continue;
        const char *m = mode->valuestring;
        if (!strcmp(m, "color_temp"))
          l->has_temp = true;
        else if (strcmp(m, "onoff") && strcmp(m, "brightness") &&
                 strcmp(m, "white"))
          l->has_color = true;
      }
    }
    if (cJSON_IsNumber(v = get(a, "brightness")))
      r->brightness = v->valueint;
    if (cJSON_IsArray(v = get(a, "hs_color")) && cJSON_GetArraySize(v) == 2) {
      r->hue =
          (uint16_t)lroundf((float)cJSON_GetArrayItem(v, 0)->valuedouble) % 360;
      r->saturation =
          (uint8_t)lroundf((float)cJSON_GetArrayItem(v, 1)->valuedouble);
    }
    if (cJSON_IsNumber(v = get(a, "color_temp_kelvin")))
      r->kelvin = v->valueint;
    if (cJSON_IsString(v = get(a, "color_mode")))
      r->color_temp = !strcmp(v->valuestring, "color_temp");
  }
  model_apply_remote(l);
}

static void apply_scene(scene_t *s, const cJSON *a) {
  if (!cJSON_IsObject(a))
    return;
  copy_string(s->name, sizeof s->name, get(a, "friendly_name"));
  copy_string(s->icon, sizeof s->icon, get(a, "icon"));
}

static void apply_weather(const cJSON *state, const cJSON *a) {
  if (cJSON_IsString(state)) {
    const char *s = state->valuestring;
    model_weather.valid = strcmp(s, "unavailable") && strcmp(s, "unknown");
    strlcpy(model_weather.condition, s, sizeof model_weather.condition);
  }
  const cJSON *t = cJSON_IsObject(a) ? get(a, "temperature") : NULL;
  if (cJSON_IsNumber(t))
    model_weather.temperature = (float)t->valuedouble;
}

static void apply(const char *id, const cJSON *diff) {
  if (!id || !cJSON_IsObject(diff))
    return;
  const cJSON *state = get(diff, "s"), *attributes = get(diff, "a");
  light_t *l;
  scene_t *s;
  if ((l = model_light(id)))
    apply_light(l, state, attributes);
  else if ((s = find_scene(id)))
    apply_scene(s, attributes);
  else if (!strcmp(id, DIAL_WEATHER))
    apply_weather(state, attributes);
}

static int by_light_name(const void *a, const void *b) {
  return strcasecmp(((const light_t *)a)->name, ((const light_t *)b)->name);
}

static int by_scene_name(const void *a, const void *b) {
  return strcasecmp(((const scene_t *)a)->name, ((const scene_t *)b)->name);
}

void model_sort(void) {
  qsort(model_lights, model_light_count, sizeof(light_t), by_light_name);
  qsort(model_scenes, model_scene_count, sizeof(scene_t), by_scene_name);
  layout++;
}

void model_apply_event(const cJSON *event) {
  const cJSON *added = get(event, "a"), *changes = get(event, "c"), *item;
  cJSON_ArrayForEach(item, added) apply(item->string, item);
  cJSON_ArrayForEach(item, changes) apply(item->string, get(item, "+"));
  if (cJSON_GetArraySize(added) > 0)
    model_sort();
  revision++;
}

bool model_shown(const light_t *l) {
  return l->state.available || !settings.hide_unavailable;
}

void model_loop(void) {
  static uint32_t shown;
  uint32_t now = now_ms(), now_shown = 0;
  for (int i = 0; i < model_light_count; i++) {
    light_t *l = &model_lights[i];
    if (l->hold_until && now >= l->hold_until) {
      l->state = l->remote;
      l->hold_until = 0;
      revision++;
    }
    now_shown |= (uint32_t)model_shown(l) << i;
  }
  if (now_shown != shown) {
    shown = now_shown;
    layout++;
  }
}

bool model_target(const char *id, target_t *out) {
  *out = (target_t){0};
  if (id && *id) {
    light_t *l = model_light(id);
    if (!l || !model_shown(l))
      return false;
    out->lights[0] = l;
    out->count = 1;
    out->name = l->name;
    out->icon = l->icon[0] ? l->icon : NULL;
    out->state = l->state;
    out->min_kelvin = l->min_kelvin;
    out->max_kelvin = l->max_kelvin;
    out->has_color = l->has_color;
    out->has_temp = l->has_temp;
    return true;
  }
  light_t *lead = NULL;
  int min_kelvin = 0, max_kelvin = 65535, brightness = 0, on_brightness = 0;
  for (int i = 0; i < model_light_count; i++) {
    light_t *l = &model_lights[i];
    if (!model_shown(l))
      continue;
    out->lights[out->count++] = l;
    if (l->state.on && !lead)
      lead = l;
    out->state.on |= l->state.on;
    out->state.available |= l->state.available;
    out->has_color |= l->has_color;
    out->has_temp |= l->has_temp;
    if (l->state.brightness > brightness)
      brightness = l->state.brightness;
    if (l->state.on && l->state.brightness > on_brightness)
      on_brightness = l->state.brightness;
    if (l->min_kelvin > min_kelvin)
      min_kelvin = l->min_kelvin;
    if (l->max_kelvin < max_kelvin)
      max_kelvin = l->max_kelvin;
  }
  if (!out->count)
    return false;
  if (!lead)
    lead = out->lights[0];
  out->name = "All lights";
  out->state.brightness = out->state.on ? on_brightness : brightness;
  out->state.hue = lead->state.hue;
  out->state.saturation = lead->state.saturation;
  out->state.kelvin = lead->state.kelvin;
  out->state.color_temp = lead->state.color_temp;
  if (min_kelvin >= max_kelvin) {
    min_kelvin = lead->min_kelvin;
    max_kelvin = lead->max_kelvin;
  }
  out->min_kelvin = min_kelvin;
  out->max_kelvin = max_kelvin;
  return true;
}

int model_percent(const light_state_t *s) {
  if (!s->on)
    return 0;
  int p = (s->brightness * 100 + 127) / 255;
  return p < 1 ? 1 : p;
}

static uint8_t clamp_byte(float v) {
  return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

static uint32_t rgb(float r, float g, float b) {
  return (uint32_t)clamp_byte(r) << 16 | (uint32_t)clamp_byte(g) << 8 |
         clamp_byte(b);
}

uint32_t model_kelvin_color(int kelvin) {
  float t = kelvin / 100.0f, r, g, b;
  if (t <= 66) {
    r = 255;
    g = 99.4708025861f * logf(t) - 161.1195681661f;
  } else {
    r = 329.698727446f * powf(t - 60, -0.1332047592f);
    g = 288.1221695283f * powf(t - 60, -0.0755148492f);
  }
  if (t >= 66)
    b = 255;
  else if (t <= 19)
    b = 0;
  else
    b = 138.5177312231f * logf(t - 10) - 305.0447927307f;
  return rgb(r, g, b);
}

uint32_t model_hsv(int hue, int saturation, int value) {
  float h = (hue % 360) / 60.0f, s = saturation / 100.0f, v = value * 2.55f;
  float c = v * s, x = c * (1 - fabsf(fmodf(h, 2) - 1)), m = v - c;
  switch ((int)h) {
  case 0:
    return rgb(c + m, x + m, m);
  case 1:
    return rgb(x + m, c + m, m);
  case 2:
    return rgb(m, c + m, x + m);
  case 3:
    return rgb(m, x + m, c + m);
  case 4:
    return rgb(x + m, m, c + m);
  default:
    return rgb(c + m, m, x + m);
  }
}

uint32_t model_tint(const light_state_t *s) {
  if (s->color_temp)
    return model_kelvin_color(s->kelvin);
  return model_hsv(s->hue, s->saturation < 15 ? 15 : s->saturation, 100);
}
