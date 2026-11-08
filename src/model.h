#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

#define MODEL_MAX_LIGHTS 16
#define MODEL_MAX_SCENES 12
#define MODEL_ID_LEN 64
#define MODEL_NAME_LEN 48
#define MODEL_ICON_LEN 40

typedef struct {
  bool available;
  bool on;
  uint8_t brightness;
  uint16_t hue;
  uint8_t saturation;
  uint16_t kelvin;
  bool color_temp;
} light_state_t;

typedef struct {
  char id[MODEL_ID_LEN];
  char name[MODEL_NAME_LEN];
  char icon[MODEL_ICON_LEN];
  uint16_t min_kelvin;
  uint16_t max_kelvin;
  bool has_color;
  bool has_temp;
  light_state_t state;
  light_state_t remote;
  uint32_t hold_until;
} light_t;

typedef struct {
  char id[MODEL_ID_LEN];
  char name[MODEL_NAME_LEN];
  char icon[MODEL_ICON_LEN];
} scene_t;

typedef struct {
  bool valid;
  char condition[24];
  float temperature;
} weather_t;

typedef struct {
  light_t *lights[MODEL_MAX_LIGHTS];
  int count;
  const char *name;
  const char *icon;
  light_state_t state;
  uint16_t min_kelvin;
  uint16_t max_kelvin;
  bool has_color;
  bool has_temp;
} target_t;

extern light_t model_lights[MODEL_MAX_LIGHTS];
extern int model_light_count;
extern scene_t model_scenes[MODEL_MAX_SCENES];
extern int model_scene_count;
extern weather_t model_weather;

uint32_t model_revision(void);
uint32_t model_layout(void);
void model_changed(void);
void model_loop(void);

void model_set_entities(const cJSON *ids);
void model_apply_event(const cJSON *event);

// for backends that build the model themselves; the pointer is only good
// until the next model_sort
light_t *model_add_light(const char *id);
// takes on l->remote unless the user is still adjusting the light
void model_apply_remote(light_t *l);
void model_sort(void);

light_t *model_light(const char *id);
bool model_target(const char *id, target_t *out);

int model_percent(const light_state_t *s);
uint32_t model_tint(const light_state_t *s);
uint32_t model_kelvin_color(int kelvin);
uint32_t model_hsv(int hue, int saturation, int value);
