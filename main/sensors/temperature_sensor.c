#include "common.h"
#include "esp_log.h"
#if defined(CONFIG_DS18B20_ENABLE)
#include "ds18b20.h"
#endif

static const char *TAG = "temperature";

bool temperature_sensor_init_and_publish(void)
{
#if defined(CONFIG_DS18B20_ENABLE)
    if (ds18b20_init()) {
        float c = 0.0f;
        if (ds18b20_read_celsius(&c)) {
            char payload[64];
            int len = snprintf(payload, sizeof(payload), "{\"celsius\":%.2f}", c);
            mqtt_publish("temperature", payload, len);
            ESP_LOGI(TAG, "Published temperature: %.2f C", c);
            return true;
        } else {
            ESP_LOGW(TAG, "Failed to read DS18B20 temperature");
        }
    } else {
        ESP_LOGW(TAG, "DS18B20 not present or init failed");
    }
#else
    ESP_LOGW(TAG, "No temperature sensor support compiled in");
#endif

    char payload_none[] = "-1";
    mqtt_publish("temperature", payload_none, sizeof(payload_none) - 1);
    return false;
}
