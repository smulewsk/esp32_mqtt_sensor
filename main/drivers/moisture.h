#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool moisture_init(void);
int moisture_read_raw(void);
void moisture_power_on(bool enabled);

#ifdef __cplusplus
}
#endif
