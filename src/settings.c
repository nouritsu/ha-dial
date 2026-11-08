#include "settings.h"

#include <string.h>

#include "backend.h"
#include "nvs.h"

settings_t settings = {
    .brightness = 60,
    .clock_after = 10,
    .sleep_after = 30,
    .power_off_after = 30,
    .touch = 1,
    .clicks = CLICKS_TOUCH,
    .bulb_refresh = 2,
};

static settings_t saved;

static const struct {
  const char *key;
  uint16_t *value;
  setting_t optional;
} fields[] = {
    {"bright", &settings.brightness},
    {"clock", &settings.clock_after},
    {"sleep", &settings.sleep_after},
    {"off", &settings.power_off_after},
    {"rot", &settings.rotation},
    {"touch", &settings.touch},
    {"clicks", &settings.clicks},
    {"hide", &settings.hide_unavailable, SETTING_HIDE_UNAVAILABLE},
    {"refresh", &settings.bulb_refresh, SETTING_BULB_REFRESH},
};

bool settings_offered(setting_t setting) {
  return !setting || (backend->settings & setting);
}

void settings_load(void) {
  nvs_handle_t nvs;
  if (nvs_open("dial", NVS_READONLY, &nvs) == ESP_OK) {
    for (int i = 0; i < sizeof fields / sizeof *fields; i++)
      if (settings_offered(fields[i].optional))
        nvs_get_u16(nvs, fields[i].key, fields[i].value);
    nvs_close(nvs);
  }
  saved = settings;
}

void settings_save(void) {
  nvs_handle_t nvs;
  if (!memcmp(&saved, &settings, sizeof settings) ||
      nvs_open("dial", NVS_READWRITE, &nvs) != ESP_OK)
    return;
  for (int i = 0; i < sizeof fields / sizeof *fields; i++)
    if (settings_offered(fields[i].optional))
      nvs_set_u16(nvs, fields[i].key, *fields[i].value);
  nvs_commit(nvs);
  nvs_close(nvs);
  saved = settings;
}
