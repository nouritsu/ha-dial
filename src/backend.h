#pragma once

#include <stdbool.h>

#include "actions.h"

typedef enum {
  LINK_OFFLINE,
  LINK_CONNECTING,
  LINK_AUTH_FAILED,
  LINK_READY,
} link_t;

typedef struct {
  const char *name;
  // status lines for the home screen
  const char *rejected;
  const char *empty;
  // the optional SETTING_ values it shows on the settings screen
  unsigned settings;
  // all of these run on the main task, the only one that may touch the model
  void (*init)(void);
  void (*loop)(void);
  void (*stop)(void);
  link_t (*link)(void);
  // these return a request id, or 0 when there is no connection to send it
  // on; a brightness of 0 turns the lights off
  int (*set)(const char *const *ids, int count, prop_t prop, int value);
  int (*scene)(const char *id);
  bool (*waiting)(int id);
} backend_t;

// the DIAL_BACKEND CMake option, <name>, picks <name>_backend from
// backends/<name>.c; every backend is compiled, and the linker drops the rest
extern const backend_t DIAL_BACKEND;
#define backend (&DIAL_BACKEND)
