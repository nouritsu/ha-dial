#pragma once

typedef enum { PROP_BRIGHTNESS, PROP_COLOR, PROP_KELVIN, PROP_POWER } prop_t;

// a PROP_COLOR value carries a hue (0-359) and a saturation (0-100) together,
// as lights only take them as a pair
static inline int color_value(int hue, int saturation) {
  return hue << 8 | saturation;
}
static inline int color_hue(int value) { return value >> 8; }
static inline int color_saturation(int value) { return value & 0xFF; }

void actions_set(const char *target, prop_t prop, int value);
void actions_toggle(const char *target);
void actions_scene(const char *id);
void actions_loop(void);
