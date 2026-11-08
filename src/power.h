#pragma once

#include <stdbool.h>

bool power_light_sleep(int off_after_minutes, bool touch_wakes);
void power_off(bool touch_wakes);
