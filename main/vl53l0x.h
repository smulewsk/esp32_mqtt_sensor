// Simple VL53L0X driver skeleton - default I2C pins and APIs
#pragma once

#include <stdbool.h>
#include <stdint.h>

// default pins (can be changed before init if needed)
#define VL53L0X_I2C_SDA_GPIO 21
#define VL53L0X_I2C_SCL_GPIO 22
#define VL53L0X_I2C_PORT I2C_NUM_0
#define VL53L0X_I2C_FREQ_HZ 400000

// initialize I2C and probe VL53L0X at 0x29
bool vl53l0x_init(void);

// read range in millimetres; returns -1 on error/not-implemented
int vl53l0x_read_range_mm(void);
