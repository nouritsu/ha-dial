#pragma once

typedef enum { PROP_BRIGHTNESS, PROP_HUE, PROP_KELVIN, PROP_POWER } prop_t;

void actions_set(const char *target, prop_t prop, int value);
void actions_toggle(const char *target);
void actions_scene(const char *id);
void actions_loop(void);
