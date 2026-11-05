#pragma once

#include <stdbool.h>

void net_init(void);
void net_start(void);
void net_stop(void);
void net_loop(void);
bool net_up(void);
