
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_attr.h"

#include "common.h"
#include "vl53l0x.h"


static void wait_until_connected(volatile bool *wait_flag, int max_wait_ms)
{
    int waited_ms = 0;
    while (!(*wait_flag) && waited_ms < max_wait_ms) {
        vTaskDelay(pdMS_TO_TICKS(500));
        waited_ms += 500;
    }
}

static void battery_status_publish()
{
    float batt_v = battery_voltage_read();
    int batt_mv = (int)roundf(batt_v * 1000.0f);
    int pct = battery_percent_from_mv(batt_mv);

    char payload[128];
    int len = snprintf(payload, sizeof(payload), "{\"voltage\":%.3f,\"mv\":%d,\"percent\":%d}", batt_v, batt_mv, pct);
    
    mqtt_publish("battery", payload, len);
}

static void fw_version_publish(const char *fw_version)
{
    mqtt_publish("version", fw_version, strlen(fw_version));
}

static void deep_sleep()
{
    config_t *cfg = get_config_ptr();
    int report_interval_seconds = cfg->report_interval_seconds;
    
    mqtt_cleanup();

    wifi_cleanup();

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", report_interval_seconds);
    esp_deep_sleep_try((uint64_t)report_interval_seconds * 1000000ULL);
    esp_deep_sleep_start();
}

static void init()
{
    wifi_init_sta();

    wait_until_connected(get_wifi_connected_ptr(), 5000);

    mqtt_app_start();

    wait_until_connected(get_mqtt_connected_ptr(), 5000);
}



void app_main(void)
{
    ESP_LOGI(TAG, "Starting esp32_mqtt_sensor");
    // publish firmware version and log it
#ifdef PROJECT_VERSION
    const char *fw_version = PROJECT_VERSION;
#else
    const char *fw_version = "unknown";
#endif
    ESP_LOGI(TAG, "Firmware version: %s", fw_version);

    config_init();

    init();

    config_subscribe();

    battery_measure();

    battery_status_publish();

    // VL53L0X sensor init and publish
    if (vl53l0x_init()) {
        int dist = vl53l0x_read_range_mm();
        char payload[64];
        int len = snprintf(payload, sizeof(payload), "%d", dist);
        mqtt_publish("distance", payload, len);
    } else {
        char payload[] = "-1";
        mqtt_publish("distance", payload, sizeof(payload) - 1);
    }

    fw_version_publish(fw_version);

    config_publish();

    vTaskDelay(pdMS_TO_TICKS(500)); // wait a bit to ensure messages are sent before deep sleep

    deep_sleep();    
}
