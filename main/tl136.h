// TL-136 analog liquid level sensor (simple driver skeleton)
#pragma once

#include <stdbool.h>

typedef struct {
    int raw; // raw ADC value (0..4095)
    int mm;  // estimated distance in millimetres (mapped from current)
} tl136_reading_t;

// Initialize the TL-136 driver (configure ADC). Returns non-zero on success.
int tl136_init(void);

// Read raw ADC value (0..4095). Returns negative on error.
int tl136_read_raw(void);

// Read estimated distance in millimetres. Returns -1 on error.
int tl136_read_distance_mm(tl136_reading_t *out);

// Control optional power-on pin (if configured via CONFIG_TL136_POWER_ON_GPIO)
// enabled == true -> drive pin high (sensor enabled); false -> drive low.
void tl136_power_on(bool enabled);
