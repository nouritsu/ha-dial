#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t brightness;
  uint16_t clock_after;
  uint16_t sleep_after;
  uint16_t power_off_after;
  uint16_t rotation;
  uint16_t touch;
  uint16_t clicks;
  uint16_t hide_unavailable;
  uint16_t bulb_refresh;
} settings_t;

// what the buzzer clicks for
enum { CLICKS_OFF, CLICKS_TOUCH, CLICKS_ALL };

// settings that only some backends have use for; a backend lists the ones it
// offers in backend_t.settings, and the others keep their defaults whatever
// another build saved
typedef enum {
  SETTING_HIDE_UNAVAILABLE = 1 << 0,
  SETTING_BULB_REFRESH = 1 << 1,
} setting_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);
// true for 0, which stands for the settings every backend has
bool settings_offered(setting_t setting);
