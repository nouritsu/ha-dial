#pragma once

#include <stdint.h>

#include "esp_timer.h"

#define DIAL_LABEL "dial"
#define DIAL_WEATHER "weather.forecast_home"
#define DIAL_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define DIAL_NTP_SERVER "pool.ntp.org"

#define ENCODER_COUNTS_PER_DETENT 4
#define ENCODER_REVERSED 0
#define LONG_PRESS_MS 600

#define BRIGHTNESS_STEP 5
#define HUE_STEP 10
#define KELVIN_STEP 200

#define SEND_INTERVAL_MS 250
#define ECHO_HOLD_MS 2000
#define WIFI_FALLBACK_MS 4000

static inline uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}
