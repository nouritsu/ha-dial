#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "model.h"
#include "net.h"
#include "secrets.h"

#define get cJSON_GetObjectItemCaseSensitive

typedef enum { MSG_CONNECTED, MSG_DISCONNECTED, MSG_TEXT } msg_type_t;

typedef struct {
  msg_type_t type;
  char *text;
} msg_t;

static const char *TAG = "ha";

static esp_websocket_client_handle_t client;
static QueueHandle_t inbox;
static link_t state = LINK_OFFLINE;
static bool started;
static int next_id, search_id, subscribe_id, answered;
static char *partial;
static char token[256];

static void post(msg_type_t type, char *text) {
  msg_t msg = {type, text};
  if (xQueueSend(inbox, &msg, 0) != pdTRUE) {
    ESP_LOGW(TAG, "inbox full, dropping message");
    free(text);
  }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id,
                     void *event_data) {
  esp_websocket_event_data_t *e = event_data;
  switch (id) {
  case WEBSOCKET_EVENT_CONNECTED:
    post(MSG_CONNECTED, NULL);
    break;
  case WEBSOCKET_EVENT_DISCONNECTED:
  case WEBSOCKET_EVENT_CLOSED:
    free(partial);
    partial = NULL;
    post(MSG_DISCONNECTED, NULL);
    break;
  case WEBSOCKET_EVENT_DATA:
    if (e->op_code != 0x01 && e->op_code != 0x00)
      break;
    if (e->payload_offset == 0) {
      free(partial);
      partial = malloc(e->payload_len + 1);
    }
    if (!partial || e->payload_offset + e->data_len > e->payload_len)
      break;
    memcpy(partial + e->payload_offset, e->data_ptr, e->data_len);
    if (e->payload_offset + e->data_len == e->payload_len) {
      partial[e->payload_len] = 0;
      post(MSG_TEXT, partial);
      partial = NULL;
    }
    break;
  }
}

static bool send_json(cJSON *root) {
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!text)
    return false;
  bool sent = esp_websocket_client_is_connected(client) &&
              esp_websocket_client_send_text(client, text, strlen(text),
                                             pdMS_TO_TICKS(1000)) >= 0;
  free(text);
  return sent;
}

static cJSON *command(const char *type, int *id) {
  cJSON *root = cJSON_CreateObject();
  int n = ++next_id;
  if (id)
    *id = n;
  cJSON_AddNumberToObject(root, "id", n);
  cJSON_AddStringToObject(root, "type", type);
  return root;
}

static void copy_string(char *dst, size_t size, const cJSON *v) {
  if (cJSON_IsString(v))
    strlcpy(dst, v->valuestring, size);
}

static bool has_prefix(const char *s, const char *prefix) {
  return !strncmp(s, prefix, strlen(prefix));
}

static void set_entities(const cJSON *ids) {
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
      if (old)
        *l = *old;
      else
        model_new_light(l, id, id + 6);
    } else if (has_prefix(id, "scene.") && scene_count < MODEL_MAX_SCENES) {
      scene_t *old = model_scene(id), *s = &scenes[scene_count++];
      if (old) {
        *s = *old;
      } else {
        *s = (scene_t){0};
        strlcpy(s->id, id, sizeof s->id);
        strlcpy(s->name, id + 6, sizeof s->name);
      }
    }
  }
  model_replace(lights, light_count, scenes, scene_count);
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
  else if ((s = model_scene(id)))
    apply_scene(s, attributes);
  else if (!strcmp(id, DIAL_WEATHER))
    apply_weather(state, attributes);
}

static void apply_event(const cJSON *event) {
  const cJSON *added = get(event, "a"), *changes = get(event, "c"), *item;
  cJSON_ArrayForEach(item, added) apply(item->string, item);
  cJSON_ArrayForEach(item, changes) apply(item->string, get(item, "+"));
  if (cJSON_GetArraySize(added) > 0)
    model_sort();
  model_changed();
}

static void subscribe(void) {
  const char *ids[MODEL_MAX_LIGHTS + MODEL_MAX_SCENES + 1];
  int count = 0;
  for (int i = 0; i < model_light_count; i++)
    ids[count++] = model_lights[i].id;
  for (int i = 0; i < model_scene_count; i++)
    ids[count++] = model_scenes[i].id;
  ids[count++] = DIAL_WEATHER;
  cJSON *root = command("subscribe_entities", &subscribe_id);
  cJSON_AddItemToObject(root, "entity_ids",
                        cJSON_CreateStringArray(ids, count));
  send_json(root);
}

static void handle_result(const cJSON *msg) {
  int id = cJSON_GetNumberValue(get(msg, "id"));
  if (id > answered)
    answered = id;
  if (!cJSON_IsTrue(get(msg, "success"))) {
    const char *error = cJSON_GetStringValue(get(get(msg, "error"), "message"));
    ESP_LOGW(TAG, "request %d failed: %s", id, error ? error : "?");
  } else if (id == search_id) {
    set_entities(get(get(msg, "result"), "entity"));
    ESP_LOGI(TAG, "%d lights, %d scenes labelled '%s'", model_light_count,
             model_scene_count, DIAL_LABEL);
    subscribe();
  } else if (id == subscribe_id) {
    state = LINK_READY;
  }
}

static void stop_client(void) {
  msg_t msg;
  if (started)
    esp_websocket_client_stop(client);
  started = false;
  answered = next_id;
  while (xQueueReceive(inbox, &msg, 0))
    free(msg.text);
}

static void handle(const char *text) {
  cJSON *msg = cJSON_Parse(text);
  const char *type = cJSON_GetStringValue(get(msg, "type"));
  if (!type)
    type = "";
  if (!strcmp(type, "auth_required")) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "auth");
    cJSON_AddStringToObject(root, "access_token", token);
    send_json(root);
  } else if (!strcmp(type, "auth_ok")) {
    cJSON *root = command("search/related", &search_id);
    cJSON_AddStringToObject(root, "item_type", "label");
    cJSON_AddStringToObject(root, "item_id", DIAL_LABEL);
    send_json(root);
  } else if (!strcmp(type, "auth_invalid")) {
    ESP_LOGE(TAG, "token rejected");
    state = LINK_AUTH_FAILED;
  } else if (!strcmp(type, "result")) {
    handle_result(msg);
  } else if (!strcmp(type, "event") &&
             cJSON_GetNumberValue(get(msg, "id")) == subscribe_id) {
    apply_event(get(msg, "event"));
  }
  cJSON_Delete(msg);
}

static void ha_init(void) {
  static char uri[96];
  char host[64], port[6];
  secrets_get("ha_host", host, sizeof host);
  secrets_get("ha_port", port, sizeof port);
  secrets_get("ha_token", token, sizeof token);
  snprintf(uri, sizeof uri, "ws://%s:%s/api/websocket", host,
           port[0] ? port : "8123");
  inbox = xQueueCreate(16, sizeof(msg_t));
  esp_websocket_client_config_t config = {
      .uri = uri,
      .buffer_size = 2048,
      .task_stack = 6144,
      .reconnect_timeout_ms = 3000,
      .network_timeout_ms = 5000,
      .ping_interval_sec = 20,
  };
  client = esp_websocket_client_init(&config);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_event, NULL);
  esp_log_level_set("websocket_client", ESP_LOG_WARN);
  esp_log_level_set("transport_ws", ESP_LOG_WARN);
  if (!token[0])
    state = LINK_AUTH_FAILED;
}

static void ha_loop(void) {
  if (state == LINK_AUTH_FAILED) {
    if (started)
      stop_client();
    return;
  }
  if (!net_up()) {
    if (started)
      stop_client();
    state = LINK_OFFLINE;
    return;
  }
  if (!started) {
    started = true;
    state = LINK_CONNECTING;
    esp_websocket_client_start(client);
  }
  msg_t msg;
  while (state != LINK_AUTH_FAILED && xQueueReceive(inbox, &msg, 0)) {
    if (msg.type == MSG_DISCONNECTED) {
      state = LINK_CONNECTING;
      answered = next_id;
    } else if (msg.type == MSG_TEXT) {
      handle(msg.text);
      free(msg.text);
    }
  }
}

static void ha_stop(void) {
  stop_client();
  if (state != LINK_AUTH_FAILED)
    state = LINK_OFFLINE;
}

static link_t ha_link(void) { return state; }

static int call(const char *domain, const char *service, const char *const *ids,
                int count, const char *data) {
  int id;
  cJSON *root = command("call_service", &id);
  cJSON_AddStringToObject(root, "domain", domain);
  cJSON_AddStringToObject(root, "service", service);
  cJSON *target = cJSON_AddObjectToObject(root, "target");
  cJSON_AddItemToObject(target, "entity_id",
                        cJSON_CreateStringArray(ids, count));
  cJSON *service_data = data ? cJSON_Parse(data) : NULL;
  if (service_data)
    cJSON_AddItemToObject(root, "service_data", service_data);
  return send_json(root) ? id : 0;
}

static int ha_set(const char *const *ids, int count, prop_t prop, int value) {
  float transition = TRANSITION_MS / 1000.0f;
  char data[64];
  switch (prop) {
  case PROP_POWER:
    return call("light", value ? "turn_on" : "turn_off", ids, count, NULL);
  case PROP_BRIGHTNESS:
    if (!value) {
      snprintf(data, sizeof data, "{\"transition\":%.2f}", transition);
      return call("light", "turn_off", ids, count, data);
    }
    snprintf(data, sizeof data, "{\"brightness_pct\":%d,\"transition\":%.2f}",
             value, transition);
    break;
  case PROP_COLOR:
    snprintf(data, sizeof data, "{\"hs_color\":[%d,%d],\"transition\":%.2f}",
             color_hue(value), color_saturation(value), transition);
    break;
  case PROP_KELVIN:
    snprintf(data, sizeof data,
             "{\"color_temp_kelvin\":%d,\"transition\":%.2f}", value,
             transition);
    break;
  }
  return call("light", "turn_on", ids, count, data);
}

static int ha_scene(const char *id) {
  return call("scene", "turn_on", &id, 1, NULL);
}

static bool ha_waiting(int id) { return id > answered; }

const backend_t ha_backend = {
    .name = "Home Assistant",
    .rejected = "Token rejected",
    .empty = "Label lights '" DIAL_LABEL "'",
    .init = ha_init,
    .loop = ha_loop,
    .stop = ha_stop,
    .link = ha_link,
    .set = ha_set,
    .scene = ha_scene,
    .waiting = ha_waiting,
};
