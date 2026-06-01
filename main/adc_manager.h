#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

// Ensure ADC oneshot unit is initialised and the given channel is configured.
// Returns ESP_OK on success.
esp_err_t adc_manager_init_channel(adc_channel_t channel);

// Get the ADC oneshot unit handle (may be NULL if not initialised).
adc_oneshot_unit_handle_t adc_manager_get_handle(void);

// Get the ADC calibration handle (may be NULL if calibration failed) for a
// specific ADC channel.
adc_cali_handle_t adc_manager_get_cali_handle_for_channel(adc_channel_t channel);
