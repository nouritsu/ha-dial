#include "secrets.h"

#include <stdbool.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define PARTITION "secrets"

static const char *TAG = "secrets";

static nvs_handle_t nvs;
static bool opened;

void secrets_init(void) {
  opened = nvs_flash_init_partition(PARTITION) == ESP_OK &&
           nvs_open_from_partition(PARTITION, "secrets", NVS_READONLY, &nvs) ==
               ESP_OK;
  if (!opened)
    ESP_LOGE(TAG, "none written: flash secrets.ini with tools/flash.py");
}

void secrets_get(const char *key, char *out, size_t size) {
  esp_err_t err = opened ? nvs_get_str(nvs, key, out, &size) : ESP_FAIL;
  if (err == ESP_ERR_NVS_INVALID_LENGTH)
    ESP_LOGE(TAG, "%s is too long", key);
  if (err != ESP_OK)
    out[0] = 0;
}
