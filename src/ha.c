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
    model_set_entities(get(get(msg, "result"), "entity"));
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
    cJSON_AddStringToObject(root, "access_token", HA_TOKEN);
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
    model_apply_event(get(msg, "event"));
  }
  cJSON_Delete(msg);
}

static void ha_init(void) {
  static char uri[96];
  snprintf(uri, sizeof uri, "ws://%s:%d/api/websocket", HA_HOST, HA_PORT);
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
  if (!HA_TOKEN[0])
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
  case PROP_HUE:
    snprintf(data, sizeof data, "{\"hs_color\":[%d,100],\"transition\":%.2f}",
             value, transition);
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
