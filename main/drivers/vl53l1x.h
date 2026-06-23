// Simple VL53L1X driver skeleton - default I2C pins and APIs
#pragma once

#include <stdbool.h>
#include <stdint.h>

// default pins (can be changed before init if needed)
#define VL53L1X_I2C_SDA_GPIO CONFIG_VL53L1X_I2C_SDA_GPIO
#define VL53L1X_I2C_SCL_GPIO CONFIG_VL53L1X_I2C_SCL_GPIO
#define VL53L1X_I2C_PORT CONFIG_VL53L1X_I2C_PORT
#define VL53L1X_I2C_FREQ_HZ CONFIG_VL53L1X_I2C_FREQ_HZ

// initialize I2C and probe VL53L1X at 0x29
bool vl53l1x_init(void);

// read range in millimetres; returns -1 on error/not-implemented
int vl53l1x_read_range_mm(void);
