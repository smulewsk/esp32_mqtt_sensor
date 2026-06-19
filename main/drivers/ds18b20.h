#pragma once

#include <stdbool.h>

// Initialize DS18B20 driver (configures GPIO). Returns true if device present.
bool ds18b20_init(void);

// Read temperature in Celsius. Returns true on success and writes to out_celsius.
bool ds18b20_read_celsius(float *out_celsius);
