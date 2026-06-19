#include "common.h"
#include "esp_log.h"
#include "moisture.h"

static const char *TAG = "moisture_sensor";

bool moisture_sensor_init_and_publish(void)
{
#ifdef CONFIG_MOISTURE_ENABLE
    if (moisture_init()) {
        int raw = moisture_read_raw();
        if (raw >= 0) {
            char payload[64];
            int len = snprintf(payload, sizeof(payload), "{\"raw\":%d,\"percent\":%d}", raw, moisture_percent_from_adc(raw));
            mqtt_publish("moisture", payload, len);
            ESP_LOGI(TAG, "Published moisture: %d%%", moisture_percent_from_adc(raw));
            moisture_power_on(false);
            return true;
        } else {
            ESP_LOGW(TAG, "Failed to read moisture value");
        }
    } else {
        ESP_LOGW(TAG, "Moisture sensor init failed or not present");
    }
#else
    ESP_LOGW(TAG, "Moisture sensor support not compiled in");
#endif
    char payload_none[] = "-1";
    mqtt_publish("moisture", payload_none, sizeof(payload_none) - 1);
    return false;
}
