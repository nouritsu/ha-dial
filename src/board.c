#include "board.h"

#include <stdio.h>

#include "config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/base64.h"

#define WIDTH 240
#define HEIGHT 240
#define BUFFER_BYTES (WIDTH * 40 * 2)
#define CAPTURE_CHUNK 768
#define COLOR_FORMAT LV_COLOR_FORMAT_RGB565
#define CLICK_HZ 4000
#define CLICK_MS 8

#define PIN_BUZZER 3
#define PIN_LCD_DC 4
#define PIN_LCD_MOSI 5
#define PIN_LCD_SCLK 6
#define PIN_LCD_CS 7
#define PIN_LCD_RST 8
#define PIN_LCD_BL 9
#define PIN_SDA 11
#define PIN_SCL 12

static lv_display_t *display;
static esp_lcd_panel_io_handle_t panel_io;
static esp_lcd_panel_handle_t panel;
static uint8_t *rotated;
static i2c_master_bus_handle_t i2c;
static i2c_master_dev_handle_t touch_dev, rtc_dev;
static pcnt_unit_handle_t encoder;
static int encoder_last, encoder_remainder;
static int backlight;
static bool asleep = true, touch_held, capturing;
static lv_indev_t *touch;
static esp_timer_handle_t click_timer;
static struct {
  int x, y;
  uint32_t until;
} injected;

void board_power_hold(bool on) { gpio_set_level(PIN_HOLD, on); }

static void init_gpio(void) {
  gpio_config_t out = {.pin_bit_mask = 1ULL << PIN_HOLD | 1ULL << PIN_BUZZER,
                       .mode = GPIO_MODE_OUTPUT};
  gpio_config(&out);
  gpio_set_level(PIN_HOLD, 1);
  gpio_set_level(PIN_BUZZER, 0);
  gpio_config_t in = {
      .pin_bit_mask = 1ULL << PIN_BUTTON | 1ULL << PIN_TOUCH_INT,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  gpio_config(&in);
}

static void apply_backlight(int percent) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 255 / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void init_backlight(void) {
  gpio_hold_dis(PIN_LCD_BL);
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = 44100,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));
  ledc_channel_config_t channel = {
      .gpio_num = PIN_LCD_BL,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void board_set_backlight(int percent) {
  backlight = percent;
  if (!asleep)
    apply_backlight(percent);
}

static void end_click(void *arg) {
  ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
}

static void init_buzzer(void) {
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_1,
      .freq_hz = CLICK_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));
  ledc_channel_config_t channel = {
      .gpio_num = PIN_BUZZER,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_1,
      .timer_sel = LEDC_TIMER_1,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&channel));
  esp_timer_create_args_t args = {.callback = end_click, .name = "click"};
  ESP_ERROR_CHECK(esp_timer_create(&args, &click_timer));
}

void board_click(void) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 128);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  esp_timer_stop(click_timer);
  esp_timer_start_once(click_timer, CLICK_MS * 1000);
}

void board_sleep_display(void) {
  asleep = true;
  // forces the pin low right away; a duty change only lands at the end of the
  // PWM period
  ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  esp_lcd_panel_io_tx_param(panel_io, 0x10, NULL, 0);
}

void board_hold_backlight_off(void) {
  gpio_reset_pin(PIN_LCD_BL);
  gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_LCD_BL, 0);
  gpio_hold_en(PIN_LCD_BL);
}

void board_wake_display(void) {
  esp_lcd_panel_io_tx_param(panel_io, 0x11, NULL, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  asleep = false;
  apply_backlight(backlight);
}

static void capture_area(const lv_area_t *area, const uint8_t *px) {
  int w = lv_area_get_width(area), h = lv_area_get_height(area);
  size_t size = (size_t)w * h * 2, n;
  unsigned char out[CAPTURE_CHUNK / 3 * 4 + 1];
  printf("A %d %d %d %d ", (int)area->x1, (int)area->y1, w, h);
  for (size_t i = 0; i < size; i += CAPTURE_CHUNK) {
    mbedtls_base64_encode(out, sizeof out, &n, px + i,
                          size - i < CAPTURE_CHUNK ? size - i : CAPTURE_CHUNK);
    fwrite(out, 1, n, stdout);
  }
  fputc('\n', stdout);
}

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  lv_area_t target = *area;
  lv_display_rotation_t rotation = lv_display_get_rotation(disp);
  if (rotation != LV_DISPLAY_ROTATION_0) {
    int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    lv_display_rotate_area(disp, &target);
    lv_draw_rotate(
        px, rotated, w, h, lv_draw_buf_width_to_stride(w, COLOR_FORMAT),
        lv_draw_buf_width_to_stride(lv_area_get_width(&target), COLOR_FORMAT),
        rotation, COLOR_FORMAT);
    px = rotated;
  }
  if (capturing)
    capture_area(&target, px);
  lv_draw_rgb565_swap(px, lv_area_get_size(&target));
  esp_lcd_panel_draw_bitmap(panel, target.x1, target.y1, target.x2 + 1,
                            target.y2 + 1, px);
}

void board_set_rotation(int degrees) {
  // LVGL turns the content counter-clockwise; the setting is clockwise
  static const lv_display_rotation_t rotations[] = {
      LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_270, LV_DISPLAY_ROTATION_180,
      LV_DISPLAY_ROTATION_90};
  lv_display_set_rotation(display, rotations[degrees / 90 % 4]);
}

static bool flush_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *event, void *ctx) {
  lv_display_flush_ready(ctx);
  return false;
}

static void init_display(void) {
  display = lv_display_create(WIDTH, HEIGHT);
  lv_display_set_flush_cb(display, flush);
  void *a =
      heap_caps_malloc(BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *b =
      heap_caps_malloc(BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(display, a, b, BUFFER_BYTES,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  rotated =
      heap_caps_malloc(BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

  spi_bus_config_t bus = {
      .sclk_io_num = PIN_LCD_SCLK,
      .mosi_io_num = PIN_LCD_MOSI,
      .miso_io_num = -1,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = BUFFER_BYTES,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t io =
      GC9A01_PANEL_IO_SPI_CONFIG(PIN_LCD_CS, PIN_LCD_DC, flush_done, display);
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                           &io, &panel_io));
  esp_lcd_panel_dev_config_t config = {
      .reset_gpio_num = PIN_LCD_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &config, &panel));
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  esp_lcd_panel_invert_color(panel, true);
  // M5GFX sets the source scan direction (B6h) instead, which the driver leaves
  // at its default
  esp_lcd_panel_mirror(panel, true, false);
  esp_lcd_panel_disp_on_off(panel, true);
}

static void keep_driven_in_sleep(void) {
  // light sleep isolates every pad by default, which would float the power
  // hold line, reset the panel and let the backlight come on
  static const int pins[] = {PIN_HOLD, PIN_BUZZER, PIN_LCD_CS, PIN_LCD_RST,
                             PIN_LCD_BL};
  for (int i = 0; i < sizeof pins / sizeof *pins; i++)
    gpio_sleep_sel_dis(pins[i]);
}

static i2c_master_dev_handle_t add_device(uint16_t address) {
  i2c_device_config_t config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = address,
      .scl_speed_hz = 400000,
  };
  i2c_master_dev_handle_t dev;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c, &config, &dev));
  return dev;
}

static void init_i2c(void) {
  i2c_master_bus_config_t bus = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = PIN_SDA,
      .scl_io_num = PIN_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c));
  touch_dev = add_device(0x38);
  rtc_dev = add_device(0x51);
  // the RFID reader shares the LCD reset line, so it is powered down after the
  // panel reset
  i2c_master_dev_handle_t rfid = add_device(0x28);
  uint8_t power_down[] = {0x01, 0x10};
  i2c_master_transmit(rfid, power_down, sizeof power_down, 50);
  i2c_master_bus_rm_device(rfid);
}

// leaves x and y alone when nothing is touching the panel
static bool read_finger(int *x, int *y) {
  uint8_t reg = 0x02, buf[5];
  if (i2c_master_transmit_receive(touch_dev, &reg, 1, buf, sizeof buf, 20) !=
      ESP_OK)
    return false;
  int touches = buf[0] & 0x0F;
  if (touches != 1 && touches != 2)
    return false;
  *x = (buf[1] & 0x0F) << 8 | buf[2];
  *y = (buf[3] & 0x0F) << 8 | buf[4];
  return true;
}

bool board_touched(void) {
  int x, y;
  return read_finger(&x, &y);
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data) {
  static int x, y;
  bool pressed;
  if (injected.until && now_ms() < injected.until) {
    pressed = true;
    x = injected.x;
    y = injected.y;
  } else {
    injected.until = 0;
    pressed = read_finger(&x, &y);
  }
  if (touch_held) {
    touch_held = pressed;
    pressed = false;
  }
  data->point.x = x;
  data->point.y = y;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void init_touch(void) {
  touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, read_touch);
  lv_indev_set_long_press_time(touch, LONG_PRESS_MS);
}

void board_set_touch(bool on) {
  if (!on)
    lv_indev_reset(touch, NULL);
  // a finger already down when touch comes back is ignored until it lifts
  touch_held = on;
  lv_indev_enable(touch, on);
}

static void init_encoder(void) {
  pcnt_unit_config_t unit = {
      .low_limit = -1000, .high_limit = 1000, .flags.accum_count = true};
  ESP_ERROR_CHECK(pcnt_new_unit(&unit, &encoder));
  pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 1000};
  pcnt_unit_set_glitch_filter(encoder, &filter);
  pcnt_chan_config_t a = {.edge_gpio_num = PIN_ENCODER_A,
                          .level_gpio_num = PIN_ENCODER_B};
  pcnt_chan_config_t b = {.edge_gpio_num = PIN_ENCODER_B,
                          .level_gpio_num = PIN_ENCODER_A};
  pcnt_channel_handle_t ca, cb;
  ESP_ERROR_CHECK(pcnt_new_channel(encoder, &a, &ca));
  ESP_ERROR_CHECK(pcnt_new_channel(encoder, &b, &cb));
  pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                               PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                               PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_unit_add_watch_point(encoder, -1000);
  pcnt_unit_add_watch_point(encoder, 1000);
  gpio_pullup_en(PIN_ENCODER_A);
  gpio_pullup_en(PIN_ENCODER_B);
  pcnt_unit_enable(encoder);
  pcnt_unit_clear_count(encoder);
  pcnt_unit_start(encoder);
}

int board_take_steps(void) {
  int count = 0;
  pcnt_unit_get_count(encoder, &count);
  int delta = count - encoder_last;
  encoder_last = count;
  encoder_remainder += ENCODER_REVERSED ? -delta : delta;
  int steps = encoder_remainder / ENCODER_COUNTS_PER_DETENT;
  encoder_remainder -= steps * ENCODER_COUNTS_PER_DETENT;
  return steps;
}

bool board_button_down(void) { return gpio_get_level(PIN_BUTTON) == 0; }

void board_ignore_held_input(void) {
  touch_held = true;
  board_take_steps();
  encoder_remainder = 0;
}

void board_inject_touch(int x, int y, uint32_t ms) {
  injected.x = x;
  injected.y = y;
  injected.until = now_ms() + ms;
}

void board_capture(void) {
  printf("SHOT %d %d\n", WIDTH, HEIGHT);
  capturing = true;
  lv_obj_invalidate(lv_screen_active());
  lv_obj_invalidate(lv_layer_top());
  lv_refr_now(display);
  capturing = false;
  printf("SHOT END\n");
  fflush(stdout);
}

static int from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t to_bcd(int v) { return (uint8_t)((v / 10) << 4 | v % 10); }

static int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + doe - 719468;
}

bool board_rtc_read(time_t *utc) {
  uint8_t reg = 0x02, r[7];
  if (i2c_master_transmit_receive(rtc_dev, &reg, 1, r, sizeof r, 50) !=
          ESP_OK ||
      r[0] & 0x80)
    return false;
  int64_t days = days_from_civil(2000 + from_bcd(r[6]), from_bcd(r[5] & 0x1F),
                                 from_bcd(r[3] & 0x3F));
  *utc = (time_t)(days * 86400 + from_bcd(r[2] & 0x3F) * 3600 +
                  from_bcd(r[1] & 0x7F) * 60 + from_bcd(r[0] & 0x7F));
  return true;
}

void board_rtc_write(time_t utc) {
  struct tm tm;
  gmtime_r(&utc, &tm);
  uint8_t r[] = {
      0x02,
      to_bcd(tm.tm_sec),
      to_bcd(tm.tm_min),
      to_bcd(tm.tm_hour),
      to_bcd(tm.tm_mday),
      (uint8_t)tm.tm_wday,
      to_bcd(tm.tm_mon + 1),
      to_bcd(tm.tm_year % 100),
  };
  i2c_master_transmit(rtc_dev, r, sizeof r, 50);
}

void board_init(void) {
  init_gpio();
  init_backlight();
  init_buzzer();
  lv_init();
  lv_tick_set_cb(now_ms);
  init_display();
  keep_driven_in_sleep();
  init_i2c();
  init_touch();
  init_encoder();
}
