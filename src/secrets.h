#pragma once

#include <stddef.h>

// the values of secrets.ini, which tools/flash.py writes to the secrets
// partition, so the firmware itself carries none of them
void secrets_init(void);
// copies the value of key into out, or an empty string when it has none
void secrets_get(const char *key, char *out, size_t size);
