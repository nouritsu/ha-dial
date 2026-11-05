#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define PIN_ENCODER_A 41
#define PIN_ENCODER_B 40
#define PIN_BUTTON 42
#define PIN_TOUCH_INT 14
#define PIN_HOLD 46

void board_init(void);

int board_take_steps(void);
bool board_button_down(void);

void board_set_backlight(int percent);
void board_set_rotation(int degrees);
void board_sleep_display(void);
void board_wake_display(void);
void board_hold_backlight_off(void);
void board_ignore_held_input(void);

void board_inject_touch(int x, int y, uint32_t ms);
void board_capture(void);

bool board_rtc_read(time_t *utc);
void board_rtc_write(time_t utc);

void board_power_hold(bool on);
