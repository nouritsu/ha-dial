#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "backend.h"
#include "cJSON.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "model.h"
#include "net.h"

#define get cJSON_GetObjectItemCaseSensitive

#define WORKERS 4
#define WORKER_STACK 6144
#define TIMEOUT_MS 2000
#define RESPONSE_SIZE 4096
#define SIGNATURE_LEN 32
#define FAILURES_UNTIL_UNAVAILABLE 2
#define UNAVAILABLE_POLL_MS (TAPO_POLL_MS * 5)
// bulbs drop a session after a day, by a clock that is not very accurate
#define SESSION_MS ((86400u - 20 * 60) * 1000)
#define SESSION_TIMEOUT_ERROR 9999
#define SESSION_EXPIRED_ERROR -40401

typedef enum {
  RESULT_OK,
  RESULT_FAILED,
  RESULT_REJECTED,
  RESULT_EXPIRED,
} result_t;

// parameters of set_device_info; -1 and 0 leave a value as it is
typedef struct {
  int8_t on;
  uint8_t brightness;
  int16_t hue;
  uint16_t kelvin;
} patch_t;

static const patch_t no_patch = {.on = -1, .hue = -1};

typedef struct {
  char name[MODEL_NAME_LEN];
  const char *icon;
  light_state_t state;
  uint16_t min_kelvin, max_kelvin;
  bool has_color, has_temp;
} info_t;

typedef struct {
  int bulb;
  bool poll;
  patch_t patch;
} job_t;

typedef struct {
  int bulb;
  result_t result;
  bool polled;
  info_t info;
  patch_t applied;
} report_t;

typedef struct {
  char host[MODEL_ID_LEN];
  // only touched by the worker running the bulb's job
  bool session;
  uint32_t session_at;
  char cookie[96];
  uint8_t key[16], iv[12], sig[28];
  uint32_t seq;
  // only touched by the main task
  bool busy, answered, rejected;
  int failures;
  uint32_t poll_at;
  patch_t pending;
} bulb_t;

static const char *TAG = "tapo";

static bulb_t bulbs[MODEL_MAX_LIGHTS];
static int bulb_count;
static uint8_t auth_hash[32];
static bool have_login;
static char terminal_uuid[32];
static QueueHandle_t jobs, reports;
static link_t state = LINK_OFFLINE;
static bool started;

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static void derive(uint8_t out[32], const char *label, const uint8_t *first,
                   const uint8_t *second) {
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  mbedtls_sha256_update(&sha, (const uint8_t *)label, strlen(label));
  mbedtls_sha256_update(&sha, first, 16);
  mbedtls_sha256_update(&sha, second, 16);
  mbedtls_sha256_update(&sha, auth_hash, sizeof auth_hash);
  mbedtls_sha256_finish(&sha, out);
  mbedtls_sha256_free(&sha);
}

static void cipher(const bulb_t *b, int mode, const uint8_t *in, uint8_t *out,
                   size_t length) {
  uint8_t iv[16];
  memcpy(iv, b->iv, sizeof b->iv);
  put_u32(iv + sizeof b->iv, b->seq);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mode == MBEDTLS_AES_ENCRYPT)
    mbedtls_aes_setkey_enc(&aes, b->key, 128);
  else
    mbedtls_aes_setkey_dec(&aes, b->key, 128);
  mbedtls_aes_crypt_cbc(&aes, mode, length, iv, in, out);
  mbedtls_aes_free(&aes);
}

static void sign(const bulb_t *b, const uint8_t *data, size_t length,
                 uint8_t out[SIGNATURE_LEN]) {
  uint8_t seq[4];
  put_u32(seq, b->seq);
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  mbedtls_sha256_update(&sha, b->sig, sizeof b->sig);
  mbedtls_sha256_update(&sha, seq, sizeof seq);
  mbedtls_sha256_update(&sha, data, length);
  mbedtls_sha256_finish(&sha, out);
  mbedtls_sha256_free(&sha);
}

static esp_err_t on_http_event(esp_http_client_event_t *e) {
  bulb_t *b = e->user_data;
  if (e->event_id == HTTP_EVENT_ON_HEADER &&
      !strcasecmp(e->header_key, "Set-Cookie") &&
      !strncmp(e->header_value, "TP_SESSIONID=", 13)) {
    strlcpy(b->cookie, e->header_value, sizeof b->cookie);
    b->cookie[strcspn(b->cookie, ";")] = 0;
  }
  return ESP_OK;
}

// returns the HTTP status, or 0 when the bulb could not be reached
static int post(bulb_t *b, const char *path, const void *body, int length,
                void *response, int size, int *received) {
  char url[MODEL_ID_LEN + 48];
  snprintf(url, sizeof url, "http://%s/app/%s", b->host, path);
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_POST,
      .timeout_ms = TIMEOUT_MS,
      .disable_auto_redirect = true,
      .event_handler = on_http_event,
      .user_data = b,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  *received = 0;
  if (!client)
    return 0;
  esp_http_client_set_header(client, "Content-Type",
                             "application/octet-stream");
  if (b->cookie[0])
    esp_http_client_set_header(client, "Cookie", b->cookie);
  int status = 0;
  if (esp_http_client_open(client, length) == ESP_OK &&
      esp_http_client_write(client, body, length) == length &&
      esp_http_client_fetch_headers(client) >= 0) {
    status = esp_http_client_get_status_code(client);
    int n = size ? esp_http_client_read_response(client, response, size) : 0;
    *received = n > 0 ? n : 0;
  }
  esp_http_client_cleanup(client);
  return status;
}

static result_t handshake(bulb_t *b) {
  uint8_t local[16], reply[48], hash[32];
  int received;
  esp_fill_random(local, sizeof local);
  b->session = false;
  b->cookie[0] = 0;
  int status = post(b, "handshake1", local, sizeof local, reply, sizeof reply,
                    &received);
  if (status != 200 || received != sizeof reply) {
    if (status)
      ESP_LOGW(TAG, "%s answered the handshake with %d", b->host, status);
    return RESULT_FAILED;
  }
  const uint8_t *remote = reply;
  derive(hash, "", local, remote);
  if (memcmp(hash, reply + 16, sizeof hash))
    return RESULT_REJECTED;
  derive(hash, "", remote, local);
  if (post(b, "handshake2", hash, sizeof hash, NULL, 0, &received) != 200)
    return RESULT_FAILED;

  derive(hash, "lsk", local, remote);
  memcpy(b->key, hash, sizeof b->key);
  derive(hash, "iv", local, remote);
  memcpy(b->iv, hash, sizeof b->iv);
  b->seq = (uint32_t)hash[28] << 24 | hash[29] << 16 | hash[30] << 8 | hash[31];
  derive(hash, "ldk", local, remote);
  memcpy(b->sig, hash, sizeof b->sig);
  b->session = true;
  b->session_at = now_ms();
  return RESULT_OK;
}

// encrypts and signs a request under the next sequence number
static uint8_t *seal(bulb_t *b, const char *request, int *length) {
  size_t size = strlen(request), padded = (size / 16 + 1) * 16;
  uint8_t *plain = malloc(padded), *body = malloc(SIGNATURE_LEN + padded);
  if (!plain || !body) {
    free(plain);
    free(body);
    return NULL;
  }
  memcpy(plain, request, size);
  memset(plain + size, padded - size, padded - size);
  b->seq++;
  cipher(b, MBEDTLS_AES_ENCRYPT, plain, body + SIGNATURE_LEN, padded);
  free(plain);
  sign(b, body + SIGNATURE_LEN, padded, body);
  *length = SIGNATURE_LEN + padded;
  return body;
}

static char *unseal(const bulb_t *b, const uint8_t *data, int length) {
  int sealed = length - SIGNATURE_LEN;
  uint8_t signature[SIGNATURE_LEN];
  if (sealed <= 0 || sealed % 16)
    return NULL;
  sign(b, data + SIGNATURE_LEN, sealed, signature);
  if (memcmp(signature, data, SIGNATURE_LEN))
    return NULL;
  uint8_t *text = malloc(sealed + 1);
  if (!text)
    return NULL;
  cipher(b, MBEDTLS_AES_DECRYPT, data + SIGNATURE_LEN, text, sealed);
  int pad = text[sealed - 1];
  if (pad < 1 || pad > 16) {
    free(text);
    return NULL;
  }
  text[sealed - pad] = 0;
  return (char *)text;
}

static result_t exchange(bulb_t *b, const char *request, cJSON **reply) {
  int length, received = 0, status = 0;
  uint8_t *body = seal(b, request, &length), *response = malloc(RESPONSE_SIZE);
  char path[32];
  snprintf(path, sizeof path, "request?seq=%ld", (long)(int32_t)b->seq);
  if (body && response)
    status = post(b, path, body, length, response, RESPONSE_SIZE, &received);
  char *text = status == 200 && received < RESPONSE_SIZE
                   ? unseal(b, response, received)
                   : NULL;
  free(body);
  free(response);
  if (status == 403)
    return RESULT_EXPIRED;
  *reply = cJSON_Parse(text);
  free(text);
  double code = cJSON_GetNumberValue(get(*reply, "error_code"));
  if (code == SESSION_TIMEOUT_ERROR || code == SESSION_EXPIRED_ERROR) {
    cJSON_Delete(*reply);
    *reply = NULL;
    return RESULT_EXPIRED;
  }
  return *reply ? RESULT_OK : RESULT_FAILED;
}

static result_t send_request(bulb_t *b, const char *request, cJSON **reply) {
  if (b->session && now_ms() - b->session_at > SESSION_MS)
    b->session = false;
  for (int attempt = 0; attempt < 2; attempt++) {
    result_t result = b->session ? RESULT_OK : handshake(b);
    if (result == RESULT_OK)
      result = exchange(b, request, reply);
    if (result != RESULT_OK)
      b->session = false;
    if (result != RESULT_EXPIRED)
      return result;
  }
  return RESULT_FAILED;
}

static char *compose(const job_t *job) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "method",
                          job->poll ? "get_device_info" : "set_device_info");
  if (!job->poll) {
    const patch_t *p = &job->patch;
    cJSON *params = cJSON_AddObjectToObject(root, "params");
    if (p->on >= 0)
      cJSON_AddBoolToObject(params, "device_on", p->on);
    if (p->brightness)
      cJSON_AddNumberToObject(params, "brightness", p->brightness);
    if (p->hue >= 0) {
      cJSON_AddNumberToObject(params, "hue", p->hue);
      cJSON_AddNumberToObject(params, "saturation", 100);
      cJSON_AddNumberToObject(params, "color_temp", 0);
    }
    if (p->kelvin)
      cJSON_AddNumberToObject(params, "color_temp", p->kelvin);
  }
  cJSON_AddNumberToObject(root, "request_time_milis",
                          (double)time(NULL) * 1000);
  cJSON_AddStringToObject(root, "terminal_uuid", terminal_uuid);
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return text;
}

static const char *avatar_icon(const char *avatar) {
  static const struct {
    const char *word, *icon;
  } icons[] = {
      {"ceiling", "mdi:ceiling-light"},
      {"hang", "mdi:ceiling-light"},
      {"table", "mdi:desk-lamp"},
      {"desk", "mdi:desk-lamp"},
      {"floor", "mdi:floor-lamp"},
      {"strip", "mdi:led-strip-variant"},
      {"lamp", "mdi:lamp"},
  };
  for (int i = 0; avatar && i < sizeof icons / sizeof *icons; i++)
    if (strstr(avatar, icons[i].word))
      return icons[i].icon;
  return NULL;
}

static void parse_info(const cJSON *result, info_t *info) {
  *info = (info_t){.min_kelvin = 2500, .max_kelvin = 6500};
  light_state_t *s = &info->state;
  const cJSON *v;
  s->available = true;
  s->on = cJSON_IsTrue(get(result, "device_on"));
  if (cJSON_IsNumber(v = get(result, "brightness")))
    s->brightness = (v->valueint * 255 + 50) / 100;
  info->has_color = cJSON_IsNumber(v = get(result, "hue"));
  if (info->has_color)
    s->hue = v->valueint % 360;
  if (cJSON_IsNumber(v = get(result, "saturation")))
    s->saturation = v->valueint;
  if (cJSON_GetArraySize(v = get(result, "color_temp_range")) == 2) {
    info->min_kelvin = cJSON_GetArrayItem(v, 0)->valueint;
    info->max_kelvin = cJSON_GetArrayItem(v, 1)->valueint;
  }
  v = get(result, "color_temp");
  info->has_temp = cJSON_IsNumber(v) && info->max_kelvin > info->min_kelvin;
  if (info->has_temp && v->valueint > 0) {
    s->color_temp = true;
    s->kelvin = v->valueint;
  }

  const char *nickname = cJSON_GetStringValue(get(result, "nickname"));
  char name[96];
  size_t n;
  if (nickname &&
      !mbedtls_base64_decode((uint8_t *)name, sizeof name - 1, &n,
                             (const uint8_t *)nickname, strlen(nickname))) {
    name[n] = 0;
    strlcpy(info->name, name, sizeof info->name);
  }
  info->icon = avatar_icon(cJSON_GetStringValue(get(result, "avatar")));
}

static void process(bulb_t *b, const job_t *job, report_t *r) {
  char *request = compose(job);
  cJSON *reply = NULL;
  r->result = request ? send_request(b, request, &reply) : RESULT_FAILED;
  free(request);
  if (r->result != RESULT_OK)
    return;
  double code = cJSON_GetNumberValue(get(reply, "error_code"));
  if (code != 0) {
    ESP_LOGW(TAG, "%s refused %s with %g", b->host,
             job->poll ? "get_device_info" : "set_device_info", code);
  } else if (job->poll) {
    parse_info(get(reply, "result"), &r->info);
    r->polled = true;
  } else {
    r->applied = job->patch;
  }
  cJSON_Delete(reply);
}

static void worker(void *arg) {
  job_t job;
  for (;;) {
    xQueueReceive(jobs, &job, portMAX_DELAY);
    report_t r = {.bulb = job.bulb, .applied = no_patch};
    process(&bulbs[job.bulb], &job, &r);
    xQueueSend(reports, &r, portMAX_DELAY);
  }
}

static bool is_empty(const patch_t *p) {
  return p->on < 0 && !p->brightness && p->hue < 0 && !p->kelvin;
}

static void apply_patch(light_state_t *s, const patch_t *p) {
  if (p->on >= 0)
    s->on = p->on;
  if (p->brightness)
    s->brightness = (p->brightness * 255 + 50) / 100;
  if (p->hue >= 0) {
    s->hue = p->hue;
    s->saturation = 100;
    s->color_temp = false;
  }
  if (p->kelvin) {
    s->kelvin = p->kelvin;
    s->color_temp = true;
  }
}

static void handle(const report_t *r) {
  bulb_t *b = &bulbs[r->bulb];
  b->busy = false;
  light_t *l = model_light(b->host);
  // a request that sleep cut off says nothing about the bulb
  if (!started || !l)
    return;
  b->poll_at = now_ms() + TAPO_POLL_MS;
  if (r->result == RESULT_REJECTED) {
    ESP_LOGE(TAG, "%s rejected the login", b->host);
    b->rejected = true;
    b->answered = false;
    b->pending = no_patch;
    l->remote.available = false;
    model_apply_remote(l);
    return;
  }
  if (r->result != RESULT_OK) {
    if (++b->failures == FAILURES_UNTIL_UNAVAILABLE) {
      ESP_LOGW(TAG, "%s is not answering", b->host);
      l->remote.available = false;
      model_apply_remote(l);
    }
    if (b->failures >= FAILURES_UNTIL_UNAVAILABLE)
      b->poll_at = now_ms() + UNAVAILABLE_POLL_MS;
    return;
  }

  b->failures = 0;
  b->answered = true;
  light_state_t *s = &l->remote;
  bool renamed = false;
  if (r->polled) {
    const info_t *info = &r->info;
    uint16_t kelvin = s->kelvin;
    *s = info->state;
    if (!s->color_temp)
      s->kelvin = kelvin;
    l->has_color = info->has_color;
    l->has_temp = info->has_temp;
    l->min_kelvin = info->min_kelvin;
    l->max_kelvin = info->max_kelvin;
    strlcpy(l->icon, info->icon ? info->icon : "", sizeof l->icon);
    renamed = info->name[0] && strcmp(l->name, info->name);
    if (renamed)
      strlcpy(l->name, info->name, sizeof l->name);
  }
  s->available = true;
  apply_patch(s, &r->applied);
  model_apply_remote(l);
  if (renamed)
    model_sort();
}

static void dispatch(void) {
  if (!started)
    return;
  uint32_t now = now_ms();
  for (int i = 0; i < bulb_count; i++) {
    bulb_t *b = &bulbs[i];
    bool command = !is_empty(&b->pending);
    if (b->busy || b->rejected || (!command && (int32_t)(now - b->poll_at) < 0))
      continue;
    job_t job = {.bulb = i, .poll = !command, .patch = b->pending};
    if (xQueueSend(jobs, &job, 0) != pdTRUE)
      return;
    b->busy = true;
    b->pending = no_patch;
  }
}

static link_t summarize(void) {
  bool rejected = false;
  if (!bulb_count)
    return LINK_READY;
  for (int i = 0; i < bulb_count; i++) {
    const bulb_t *b = &bulbs[i];
    if (b->answered && b->failures < FAILURES_UNTIL_UNAVAILABLE)
      return LINK_READY;
    rejected |= b->rejected;
  }
  return rejected ? LINK_AUTH_FAILED : LINK_CONNECTING;
}

static bool parse_login(void) {
  const char *hex = TAPO_AUTH_HASH;
  if (strlen(hex) != 2 * sizeof auth_hash)
    return false;
  for (int i = 0; i < sizeof auth_hash; i++) {
    unsigned byte;
    if (sscanf(hex + 2 * i, "%2x", &byte) != 1)
      return false;
    auth_hash[i] = byte;
  }
  return true;
}

static void tapo_init(void) {
  static char hosts[] = TAPO_HOSTS;
  char *rest;
  for (char *host = strtok_r(hosts, ", ", &rest);
       host && bulb_count < MODEL_MAX_LIGHTS;
       host = strtok_r(NULL, ", ", &rest)) {
    bulb_t *b = &bulbs[bulb_count++];
    strlcpy(b->host, host, sizeof b->host);
    b->pending = no_patch;
    model_add_light(host);
  }
  uint8_t uuid[16];
  size_t n;
  esp_fill_random(uuid, sizeof uuid);
  mbedtls_base64_encode((uint8_t *)terminal_uuid, sizeof terminal_uuid, &n,
                        uuid, sizeof uuid);

  jobs = xQueueCreate(MODEL_MAX_LIGHTS, sizeof(job_t));
  reports = xQueueCreate(MODEL_MAX_LIGHTS, sizeof(report_t));
  have_login = parse_login();
  if (!have_login)
    state = LINK_AUTH_FAILED;
  for (int i = 0; have_login && i < WORKERS && i < bulb_count; i++)
    xTaskCreate(worker, "tapo", WORKER_STACK, NULL, 5, NULL);
  // an unreachable bulb would otherwise log a stack of errors on every poll
  esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
  esp_log_level_set("transport_base", ESP_LOG_NONE);
  esp_log_level_set("esp-tls", ESP_LOG_NONE);
}

static void tapo_loop(void) {
  report_t r;
  while (xQueueReceive(reports, &r, 0))
    handle(&r);
  if (!have_login)
    return;
  if (!net_up()) {
    started = false;
    state = LINK_OFFLINE;
    return;
  }
  if (!started) {
    started = true;
    for (int i = 0; i < bulb_count; i++) {
      bulbs[i].rejected = false;
      bulbs[i].poll_at = now_ms();
    }
  }
  state = summarize();
  dispatch();
}

static void tapo_stop(void) {
  started = false;
  state = have_login ? LINK_OFFLINE : LINK_AUTH_FAILED;
}

static link_t tapo_link(void) { return state; }

static void merge(patch_t *p, const light_t *l, prop_t prop, int value) {
  if ((prop == PROP_POWER || prop == PROP_BRIGHTNESS) && !value) {
    *p = no_patch;
    p->on = 0;
    return;
  }
  p->on = 1;
  if (prop == PROP_BRIGHTNESS) {
    p->brightness = value;
  } else if (prop == PROP_HUE && l->has_color) {
    p->hue = value;
    p->kelvin = 0;
  } else if (prop == PROP_KELVIN && l->has_temp) {
    p->kelvin = value < l->min_kelvin   ? l->min_kelvin
                : value > l->max_kelvin ? l->max_kelvin
                                        : value;
    p->hue = -1;
  }
}

static int tapo_set(const char *const *ids, int count, prop_t prop, int value) {
  int matched = 0;
  for (int i = 0; i < count; i++) {
    light_t *l = model_light(ids[i]);
    for (int j = 0; l && j < bulb_count; j++) {
      bulb_t *b = &bulbs[j];
      if (!b->rejected && !strcmp(b->host, ids[i])) {
        merge(&b->pending, l, prop, value);
        matched++;
      }
    }
  }
  dispatch();
  return matched > 0;
}

// the bulbs keep no scenes of their own
static int tapo_scene(const char *id) { return 0; }

// a bulb that is still busy collects the newest values in its pending patch,
// so they never need holding back
static bool tapo_waiting(int id) { return false; }

const backend_t tapo_backend = {
    .name = "Tapo",
    .rejected = "Login rejected",
    .empty = "No bulbs configured",
    .init = tapo_init,
    .loop = tapo_loop,
    .stop = tapo_stop,
    .link = tapo_link,
    .set = tapo_set,
    .scene = tapo_scene,
    .waiting = tapo_waiting,
};
