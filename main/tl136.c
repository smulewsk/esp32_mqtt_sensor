#include "tl136.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"

static const char *TAG = "tl136";
static bool s_inited = false;

// ADC handles for oneshot + calibration are provided by adc_manager
#include "adc_manager.h"
static int s_power_on_gpio = -1;

// Defaults: these can be tuned via Kconfig
#ifndef CONFIG_TL136_ADC_CHANNEL
#define CONFIG_TL136_ADC_CHANNEL 1
#endif

#ifndef CONFIG_TL136_MAX_DISTANCE_MM
#define CONFIG_TL136_MAX_DISTANCE_MM 5000
#endif

#ifndef CONFIG_TL136_MIN_DISTANCE_MM
#define CONFIG_TL136_MIN_DISTANCE_MM 0
#endif

#ifndef CONFIG_TL136_RESISTOR_R1_OHMS 
#define CONFIG_TL136_RESISTOR_R1_OHMS 100
#endif

#ifndef CONFIG_TL136_RANGE_MM
#define CONFIG_TL136_RANGE_MM 5000
#endif

// TL-136 current limits (mA)
#define TL136_MAX_CURRENT_MA 20
#define TL136_MIN_CURRENT_MA 4

int tl136_init(void)
{

// Configure optional power-on GPIO if provided
#ifndef CONFIG_TL136_POWER_ON_GPIO
#define CONFIG_TL136_POWER_ON_GPIO -1
#endif
    s_power_on_gpio = CONFIG_TL136_POWER_ON_GPIO;
    if (s_power_on_gpio >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_power_on_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }

    // Initialise ADC channel via shared adc_manager
    if (adc_manager_init_channel((adc_channel_t)CONFIG_TL136_ADC_CHANNEL) != ESP_OK) {
        ESP_LOGW(TAG, "adc_manager_init_channel failed");
        return 0;
    }


    tl136_power_on(true);

    vTaskDelay(pdMS_TO_TICKS(2000));

    s_inited = true;
    ESP_LOGI(TAG, "TL-136 ADC channel %d initialized (atten DB_12)", CONFIG_TL136_ADC_CHANNEL);


    return 1;
}

int tl136_read_raw(void)
{
    if (!s_inited) return -1;
    int raw = 0;
    if (adc_oneshot_read(adc_manager_get_handle(), (adc_channel_t)CONFIG_TL136_ADC_CHANNEL, &raw) != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_read failed (ch=%d)", CONFIG_TL136_ADC_CHANNEL);
        return -1;
    }
    return raw;
}

int tl136_read_distance_mm(tl136_reading_t *out)
{

    const int voltage_max_mv = 3300;
    if (!s_inited) return -1;

    int raw = tl136_read_raw();
    if (raw < 0) return -1;

    // Convert raw to voltage (mV) using calibration if available
    int voltage_mv = 0;
    adc_cali_handle_t cal = adc_manager_get_cali_handle_for_channel((adc_channel_t)CONFIG_TL136_ADC_CHANNEL);
    if (cal) {
        if (adc_cali_raw_to_voltage(cal, raw, &voltage_mv) != ESP_OK) {
            voltage_mv = 0;
        }
    } else {
        // Fallback: approximate using 12-bit scale and 3300mV reference
        const int raw_max = 4095;
        voltage_mv = (raw * voltage_max_mv) / raw_max;
    }

    ESP_LOGI(TAG, "TL-136 raw=%d -> voltage=%d mV", raw, voltage_mv);

    // Map amps (4..20 mA) to range.

    int max_voltage_mv = (TL136_MAX_CURRENT_MA * CONFIG_TL136_RESISTOR_R1_OHMS);
    int min_voltage_mv = (TL136_MIN_CURRENT_MA * CONFIG_TL136_RESISTOR_R1_OHMS);

    if (voltage_mv <= 0) return -1;
    if (voltage_mv < min_voltage_mv) voltage_mv = min_voltage_mv;
    if (voltage_mv > max_voltage_mv) voltage_mv = max_voltage_mv;

    int mm =  (voltage_mv - min_voltage_mv) * CONFIG_TL136_RANGE_MM / (max_voltage_mv - min_voltage_mv);
    
    if (out) {
        out->raw = raw;
        out->mm = mm;
    }
    return 0;
}

void tl136_power_on(bool enabled)
{
    if (s_power_on_gpio < 0) return; // not configured
    ESP_LOGI(TAG, "Setting TL-136 power on pin %d to %d (enabled=%d)", s_power_on_gpio, enabled ? 1 : 0, enabled);
    gpio_set_level(s_power_on_gpio, enabled ? 1 : 0);
}
