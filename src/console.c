#include "console.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "ha.h"
#include "model.h"
#include "ui/ui.h"

static void print_state(void) {
  printf("link=%d screen=%d heap=%u min=%u\n", ha_link(), ui_current(),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  for (int i = 0; i < model_light_count; i++) {
    const light_t *l = &model_lights[i];
    const light_state_t *s = &l->state;
    printf("%s \"%s\" available=%d on=%d brightness=%d hue=%d sat=%d kelvin=%d "
           "color_temp=%d\n",
           l->id, l->name, s->available, s->on, s->brightness, s->hue,
           s->saturation, s->kelvin, s->color_temp);
  }
  for (int i = 0; i < model_scene_count; i++)
    printf("%s \"%s\"\n", model_scenes[i].id, model_scenes[i].name);
}

static void run(char *line) {
  char *command = strtok(line, " "), *a = strtok(NULL, " "),
       *b = strtok(NULL, " ");
  if (!command)
    return;
  if (!strcmp(command, "shot")) {
    board_capture();
  } else if (!strcmp(command, "rot") && a) {
    app_rotate(atoi(a));
  } else if (!strcmp(command, "click")) {
    app_click();
  } else if (!strcmp(command, "long")) {
    app_long_press();
  } else if (!strcmp(command, "tap") && b) {
    board_inject_touch(atoi(a), atoi(b), 100);
  } else if (!strcmp(command, "hold") && b) {
    board_inject_touch(atoi(a), atoi(b), 1000);
  } else if (!strcmp(command, "state")) {
    print_state();
  } else if (!strcmp(command, "nosleep")) {
    app_keep_awake(true);
  } else if (!strcmp(command, "sleep")) {
    app_keep_awake(false);
    app_request_sleep();
  } else if (!strcmp(command, "reboot")) {
    esp_restart();
  } else {
    printf("unknown command: %s\n", command);
  }
  fflush(stdout);
}

void console_init(void) {
  // without the driver, non-blocking reads never see the receive FIFO
  usb_serial_jtag_driver_config_t config =
      USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  usb_serial_jtag_driver_install(&config);
  usb_serial_jtag_vfs_use_driver();
  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

void console_loop(void) {
  static char line[64];
  static int length;
  char c;
  while (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == '\n' || c == '\r') {
      line[length] = 0;
      if (length)
        run(line);
      length = 0;
    } else if (length < sizeof line - 1) {
      line[length++] = c;
    }
  }
}
