#pragma once

#include <stdint.h>

#include "esp_timer.h"

// Home Assistant only: the label that picks the lights and scenes, and the
// weather entity for the clock face
#define DIAL_LABEL "dial"
#define DIAL_WEATHER "weather.forecast_home"
#define DIAL_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define DIAL_NTP_SERVER "pool.ntp.org"

#define ENCODER_COUNTS_PER_DETENT 4
#define ENCODER_REVERSED 1
#define LONG_PRESS_MS 600

#define BRIGHTNESS_STEP 5
#define HUE_STEP 10
#define SATURATION_STEP 5
#define KELVIN_STEP 200

// light values move 2x or 4x per click once the clicks average under these
// gaps; a pause or a change of direction drops back to 1x
#define ACCEL_DOUBLE_MS 60
#define ACCEL_QUADRUPLE_MS 30
#define ACCEL_IDLE_MS 200

// a new value goes out as soon as the backend has finished applying the
// previous one
#define SEND_INTERVAL_MS 50
#define CALL_TIMEOUT_MS 3000
#define TRANSITION_MS 300
#define ECHO_HOLD_MS 2000
#define WIFI_FALLBACK_MS 4000

static inline uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}
