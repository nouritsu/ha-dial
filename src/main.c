#include "actions.h"
#include "app.h"
#include "backend.h"
#include "board.h"
#include "config.h"
#include "console.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "model.h"
#include "net.h"
#include "nvs_flash.h"
#include "power.h"
#include "settings.h"
#include "ui/ui.h"

#define DEBOUNCE_MS 20

static const char *TAG = "app";

static bool sleep_requested, keep_awake, ignore_button;

static bool wake_from_clock(void) {
  lv_display_trigger_activity(NULL);
  if (ui_current() != SCREEN_CLOCK)
    return false;
  ui_show(SCREEN_HOME, NULL);
  return true;
}

void app_rotate(int steps) {
  if (!wake_from_clock())
    ui_rotate(steps);
}

void app_click(void) {
  if (!wake_from_clock())
    ui_click();
}

void app_long_press(void) {
  if (!wake_from_clock())
    ui_long_press();
}

void app_request_sleep(void) { sleep_requested = true; }

void app_keep_awake(bool keep) { keep_awake = keep; }

static void poll_button(void) {
  static bool raw, stable, fired;
  static uint32_t changed_at, pressed_at;
  uint32_t now = now_ms();
  bool down = board_button_down();
  if (down != raw) {
    raw = down;
    changed_at = now;
  }
  if (raw != stable && now - changed_at >= DEBOUNCE_MS) {
    stable = raw;
    if (stable) {
      pressed_at = now;
      fired = false;
    } else {
      if (!fired && !ignore_button)
        app_click();
      ignore_button = false;
    }
  }
  if (stable && !fired && !ignore_button && now - pressed_at >= LONG_PRESS_MS) {
    fired = true;
    app_long_press();
  }
}

static void go_to_sleep(void) {
  ESP_LOGI(TAG, "sleeping");
  backend->stop();
  net_stop();
  board_sleep_display();
  if (!power_light_sleep(settings.power_off_after, settings.touch)) {
    ESP_LOGI(TAG, "powering off");
    power_off(settings.touch);
  }
  // a tap wakes to the clock, the knob and the button straight to the lights
  bool tapped = settings.touch && board_touched() && !board_button_down();
  board_ignore_held_input();
  ignore_button = board_button_down();
  ui_show(tapped ? SCREEN_CLOCK : SCREEN_HOME, NULL);
  lv_refr_now(NULL);
  board_wake_display();
  lv_display_trigger_activity(NULL);
  net_start();
}

static void check_idle(void) {
  uint32_t idle = lv_display_get_inactive_time(NULL);
  screen_id_t screen = ui_current();
  if (sleep_requested || (!keep_awake && settings.sleep_after &&
                          idle >= settings.sleep_after * 1000u)) {
    sleep_requested = false;
    go_to_sleep();
  } else if (settings.clock_after && idle >= settings.clock_after * 1000u &&
             screen != SCREEN_CLOCK) {
    ui_show(SCREEN_CLOCK, NULL);
  }
}

void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  board_init();
  settings_load();
  board_set_rotation(settings.rotation);
  board_set_touch(settings.touch);
  console_init();
  ui_init();
  // restores the time from the RTC, which the clock needs for its first frame
  net_init();
  // a tap that woke the Dial from power off on USB shows the clock, as one
  // that wakes it from sleep does
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    board_ignore_held_input();
    ui_show(SCREEN_CLOCK, NULL);
  }
  lv_refr_now(NULL);
  board_wake_display();
  backend->init();
  net_start();

  for (;;) {
    poll_button();
    int steps = board_take_steps();
    if (steps)
      app_rotate(steps);
    console_loop();
    net_loop();
    backend->loop();
    actions_loop();
    model_loop();
    ui_loop();
    lv_timer_handler();
    check_idle();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
