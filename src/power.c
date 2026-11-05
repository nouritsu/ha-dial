#include "power.h"

#include "board.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"

static const int encoder_pins[] = {PIN_ENCODER_A, PIN_ENCODER_B};

bool power_light_sleep(int off_after_minutes) {
  for (int i = 0; i < 2; i++)
    gpio_wakeup_enable(encoder_pins[i], gpio_get_level(encoder_pins[i])
                                            ? GPIO_INTR_LOW_LEVEL
                                            : GPIO_INTR_HIGH_LEVEL);
  gpio_wakeup_enable(PIN_BUTTON, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(PIN_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  if (off_after_minutes)
    esp_sleep_enable_timer_wakeup((uint64_t)off_after_minutes * 60 * 1000000);

  esp_light_sleep_start();
  bool woken = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (int i = 0; i < 2; i++)
    gpio_wakeup_disable(encoder_pins[i]);
  gpio_wakeup_disable(PIN_BUTTON);
  gpio_wakeup_disable(PIN_TOUCH_INT);
  return woken;
}

void power_off(void) {
  board_sleep_display();
  // on battery, releasing the hold line cuts power; on USB the chip stays up,
  // so touch wakes it from deep sleep
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_TOUCH_INT, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(PIN_TOUCH_INT);
  board_power_hold(false);
  esp_deep_sleep_start();
}
