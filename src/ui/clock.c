#include <math.h>
#include <stdio.h>
#include <time.h>

#include "icons.h"
#include "model.h"
#include "screen.h"
#include "ui.h"

#define TIME_VALID_AFTER 1735689600

static lv_obj_t *weather, *weather_icon, *temperature, *clock_time, *date;
static lv_timer_t *ticker;

static void refresh(void) {
  static const char *const days[] = {"Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
  static const char *const months[] = {"Jan", "Feb", "Mar", "Apr",
                                       "May", "Jun", "Jul", "Aug",
                                       "Sep", "Oct", "Nov", "Dec"};
  char text[24];
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  bool valid = now >= TIME_VALID_AFTER;

  if (valid)
    snprintf(text, sizeof text, "%02d:%02d", tm.tm_hour, tm.tm_min);
  else
    snprintf(text, sizeof text, "--:--");
  ui_set_text(clock_time, text);
  if (valid)
    snprintf(text, sizeof text, "%s %d %s", days[tm.tm_wday], tm.tm_mday,
             months[tm.tm_mon]);
  else
    text[0] = 0;
  ui_set_text(date, text);

  lv_obj_set_hidden(weather, !model_weather.valid);
  if (model_weather.valid) {
    ui_set_text(weather_icon, icon_weather(model_weather.condition));
    snprintf(text, sizeof text, "%ld\u00B0",
             lroundf(model_weather.temperature));
    ui_set_text(temperature, text);
  }
}

static void tick(lv_timer_t *timer) { refresh(); }

static void on_tap(lv_event_t *e) { ui_show(SCREEN_HOME, NULL); }

static void build(lv_obj_t *root) {
  ui_on_click(root, on_tap, NULL);

  weather = lv_obj_create(root);
  lv_obj_set_size(weather, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(weather, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(weather, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(weather, 6, 0);
  ui_inert(weather);
  lv_obj_align(weather, LV_ALIGN_TOP_MID, 0, 46);
  weather_icon = ui_label(weather, &icons_28, COLOR_PRIMARY);
  temperature = ui_label(weather, &text_20, COLOR_ON_SURFACE_VARIANT);

  clock_time = ui_label(root, &clock_64, COLOR_ON_SURFACE);
  lv_obj_align(clock_time, LV_ALIGN_CENTER, 0, 0);

  date = ui_label(root, &text_20, COLOR_ON_SURFACE_VARIANT);
  lv_obj_align(date, LV_ALIGN_CENTER, 0, 48);

  ticker = lv_timer_create(tick, 1000, NULL);
  lv_timer_pause(ticker);
}

static void enter(const char *target) { lv_timer_resume(ticker); }

static void leave(void) { lv_timer_pause(ticker); }

screen_t clock_screen = {
    .build = build,
    .enter = enter,
    .leave = leave,
    .refresh = refresh,
};
