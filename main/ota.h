#pragma once

#include "esp_err.h"

// Start OTA update from URL. This function is safe to call from a FreeRTOS task.
esp_err_t ota_perform_update(const char *url);
