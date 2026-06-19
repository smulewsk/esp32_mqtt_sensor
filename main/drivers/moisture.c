#include "moisture.h"
#include "common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "adc_manager.h"
#include "driver/gpio.h"

static const char *TAG = "moisture";
static bool s_inited = false;
static int s_power_gpio = -1;

#ifndef CONFIG_MOISTURE_ADC_CHANNEL
#define CONFIG_MOISTURE_ADC_CHANNEL 0
#endif

#ifndef CONFIG_MOISTURE_POWER_ON_GPIO
#define CONFIG_MOISTURE_POWER_ON_GPIO -1
#endif

bool moisture_init(void)
{
    s_power_gpio = CONFIG_MOISTURE_POWER_ON_GPIO;
    if (s_power_gpio >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_power_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
    }

    if (adc_manager_init_channel((adc_channel_t)CONFIG_MOISTURE_ADC_CHANNEL) != ESP_OK) {
        ESP_LOGW(TAG, "adc_manager_init_channel failed");
        return false;
    }

    moisture_power_on(true);
    // allow sensor to stabilise
    vTaskDelay(pdMS_TO_TICKS(500));

    // Do a direct ADC read to verify sensor presence (avoid using moisture_read_raw which checks s_inited)
    int raw = 0;
    if (adc_oneshot_read(adc_manager_get_handle(), (adc_channel_t)CONFIG_MOISTURE_ADC_CHANNEL, &raw) != ESP_OK) {
        ESP_LOGW(TAG, "Moisture sensor adc read failed (ch=%d)", CONFIG_MOISTURE_ADC_CHANNEL);
        moisture_power_on(false);
        return false;
    }

    s_inited = true;
    ESP_LOGI(TAG, "Moisture sensor ADC channel %d initialized", CONFIG_MOISTURE_ADC_CHANNEL);
    return true;
}

int moisture_read_raw(void)
{
    if (!s_inited) return -1;
    int raw = 0;
    if (adc_oneshot_read(adc_manager_get_handle(), (adc_channel_t)CONFIG_MOISTURE_ADC_CHANNEL, &raw) != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_read failed (ch=%d)", CONFIG_MOISTURE_ADC_CHANNEL);
        return -1;
    }
    return raw;
}

void moisture_power_on(bool enabled)
{
    if (s_power_gpio < 0) return;
    gpio_set_level(s_power_gpio, enabled ? 1 : 0);
}
