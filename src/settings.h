#pragma once

#include <stdint.h>

typedef struct {
  uint16_t brightness;
  uint16_t clock_after;
  uint16_t sleep_after;
  uint16_t power_off_after;
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);
