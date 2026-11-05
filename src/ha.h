#pragma once

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

void ha_call(const char *domain, const char *service, const char *const *ids,
             int count, const char *data);
