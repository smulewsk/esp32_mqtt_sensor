#include "common.h"
#include "esp_log.h"
#if defined(CONFIG_VL53L0X_ENABLE)
#include "vl53l0x.h"
#endif
#if defined(CONFIG_VL53L1X_ENABLE)
#include "vl53l1x.h"
#endif
#if defined(CONFIG_TL136_ENABLE)
#include "tl136.h"
#endif

static const char *TAG = "distance";

bool distance_sensor_init_and_publish(void)
{
    config_t *cfg = get_config_ptr();
    const char *sensor = cfg->distance_sensor[0] ? cfg->distance_sensor : "auto";
    char payload[128];

    esp_log_level_set("distance", ESP_LOG_INFO); // distance sensor module logs

#ifdef CONFIG_VL53L0X_ENABLE
    esp_log_level_set("vl53l0x", ESP_LOG_INFO); // and VL53L0X sensor logs
#endif
#ifdef CONFIG_VL53L1X_ENABLE
    esp_log_level_set("vl53l1x", ESP_LOG_INFO); // and VL53L1X sensor logs
#endif
#ifdef CONFIG_TL136_ENABLE
    esp_log_level_set("tl136", ESP_LOG_INFO); // TL-136 analog sensor logs
#endif

    // Try sensors according to runtime selection and compile-time availability.
    if (strcmp(sensor, "auto") == 0) {
#if defined(CONFIG_VL53L1X_ENABLE)
        if (vl53l1x_init()) {
            ESP_LOGI(TAG, "Using VL53L1X (auto)");
            int dist_mm = vl53l1x_read_range_mm();
            int pct = distance_percent_from_mm(dist_mm);
            int len = snprintf(payload, sizeof(payload), "{\"mm\":%d,\"percent\":%d}", dist_mm, pct);
            mqtt_publish("distance", payload, len);
            return true;
        }
#endif
#if defined(CONFIG_VL53L0X_ENABLE)
        if (vl53l0x_init()) {
            ESP_LOGI(TAG, "Using VL53L0X (auto)");
            int dist_mm = vl53l0x_read_range_mm();
            int pct = distance_percent_from_mm(dist_mm);
            int len = snprintf(payload, sizeof(payload), "{\"mm\":%d,\"percent\":%d}", dist_mm, pct);
            mqtt_publish("distance", payload, len);
            return true;
        }
#endif
#if defined(CONFIG_TL136_ENABLE)
        if (tl136_init()) {
            ESP_LOGI(TAG, "Using TL-136 (auto)");
            tl136_reading_t reading = {0};
            tl136_read_distance_mm(&reading);
            int pct = distance_percent_from_mm(reading.mm);
            int len = snprintf(payload, sizeof(payload), "{\"raw\":%d,\"mm\":%d,\"percent\":%d}", reading.raw, reading.mm, pct);
            tl136_power_on(false);
            mqtt_publish("distance", payload, len);
            return true;
        }
#endif
    } else if (strcmp(sensor, "vl53l1x") == 0) {
#if defined(CONFIG_VL53L1X_ENABLE)
        if (vl53l1x_init()) {
            ESP_LOGI(TAG, "Using VL53L1X");
            int dist_mm = vl53l1x_read_range_mm();
            int pct = distance_percent_from_mm(dist_mm);
            int len = snprintf(payload, sizeof(payload), "{\"mm\":%d,\"percent\":%d}", dist_mm, pct);
            mqtt_publish("distance", payload, len);
            return true;
        }
#else
        ESP_LOGW(TAG, "VL53L1X not compiled in");
#endif
    } else if (strcmp(sensor, "vl53l0x") == 0) {
#if defined(CONFIG_VL53L0X_ENABLE)
        if (vl53l0x_init()) {
            ESP_LOGI(TAG, "Using VL53L0X");
            int dist_mm = vl53l0x_read_range_mm();
            int pct = distance_percent_from_mm(dist_mm);
            int len = snprintf(payload, sizeof(payload), "{\"mm\":%d,\"percent\":%d}", dist_mm, pct);
            mqtt_publish("distance", payload, len);
            return true;
        }
#else
        ESP_LOGW(TAG, "VL53L0X not compiled in");
#endif
    } else if (strcmp(sensor, "tl136") == 0) {
#if defined(CONFIG_TL136_ENABLE)
        if (tl136_init()) {
            ESP_LOGI(TAG, "Using TL-136");
            tl136_reading_t reading = {0};
            tl136_read_distance_mm(&reading);
            int pct = distance_percent_from_mm(reading.mm);
            int len = snprintf(payload, sizeof(payload), "{\"raw\":%d,\"mm\":%d,\"percent\":%d}", reading.raw, reading.mm, pct);
            tl136_power_on(false);
            mqtt_publish("distance", payload, len);
            return true;
        }
#else
        ESP_LOGW(TAG, "TL-136 not compiled in");
#endif
    } else if (strcmp(sensor, "none") == 0) {
        return true; // nothing to publish, but considered successful
    }

    // If no sensor was published, publish -1 to indicate unavailable
    char payload_none[] = "-1";
    mqtt_publish("distance", payload_none, sizeof(payload_none) - 1);
    return false;
}
