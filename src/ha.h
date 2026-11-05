#pragma once

#include <stdbool.h>

typedef enum {
  LINK_OFFLINE,
  LINK_CONNECTING,
  LINK_AUTH_FAILED,
  LINK_READY,
} link_t;

void ha_init(void);
void ha_loop(void);
void ha_stop(void);
link_t ha_link(void);

// returns the request id, or 0 when there is no connection to send it on
int ha_call(const char *domain, const char *service, const char *const *ids,
            int count, const char *data);
bool ha_waiting(int id);
