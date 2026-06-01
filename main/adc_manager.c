#include "adc_manager.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "adc_mgr";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
// store per-channel calibration handles (ADC1 channels 0..7)
#define ADC_MANAGER_MAX_CHANNELS 8
static adc_cali_handle_t s_adc_cali_by_channel[ADC_MANAGER_MAX_CHANNELS] = {0};

esp_err_t adc_manager_init_channel(adc_channel_t channel)
{
    esp_err_t err = ESP_OK;
    if (s_adc_handle == NULL) {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = ADC_UNIT_1,
        };
        err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "adc_oneshot_new_unit failed: %d", err);
            return err;
        }
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_config_channel failed: %d", err);
        return err;
    }

    // create calibration handle for this channel if not already present
    if (channel >= 0 && channel < ADC_MANAGER_MAX_CHANNELS) {
        if (s_adc_cali_by_channel[channel] == NULL) {
            adc_cali_curve_fitting_config_t cali_cfg = {
                .unit_id = ADC_UNIT_1,
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
            };
            if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_by_channel[channel]) != ESP_OK) {
                ESP_LOGW(TAG, "adc_cali_create_scheme_curve_fitting failed for channel %d", channel);
                s_adc_cali_by_channel[channel] = NULL; // continue without calibration for this channel
            }
        }
    }

    return ESP_OK;
}

adc_oneshot_unit_handle_t adc_manager_get_handle(void)
{
    return s_adc_handle;
}

adc_cali_handle_t adc_manager_get_cali_handle_for_channel(adc_channel_t channel)
{
    if (channel >= 0 && channel < ADC_MANAGER_MAX_CHANNELS) {
        return s_adc_cali_by_channel[channel];
    }
    return NULL;
}
