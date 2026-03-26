
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "common.h"
#include "vl53l0x.h"

// GPIO7 is the WAKEUP button on ESP32-C6 (and most ESP32 dev boards).
// Change this if your board uses a different pin.
#define WAKEUP_PIN 7

static bool boot_pin_held_for_5s(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << WAKEUP_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    if (gpio_get_level(WAKEUP_PIN) != 0) {
        return false; // not held
    }

    ESP_LOGI(TAG, "WAKEUP pin held — waiting 5s to enter AP config mode...");
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (gpio_get_level(WAKEUP_PIN) != 0) {
            return false; // released early
        }
    }
    ESP_LOGI(TAG, "AP config mode triggered!");
    return true;
}


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
    esp_err_t err;
    
    mqtt_cleanup();

    wifi_cleanup();

    // ensure RTC pad is initialised and pull-up is enabled so ext1 wake works
    if (rtc_gpio_init(WAKEUP_PIN) == ESP_OK) {
        rtc_gpio_set_direction(WAKEUP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_set_direction_in_sleep(WAKEUP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(WAKEUP_PIN);
    } else {
        ESP_LOGW(TAG, "rtc_gpio_init failed for %d", WAKEUP_PIN);
    }

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds (WAKEUP button will wake device)", report_interval_seconds);
    esp_sleep_enable_ext1_wakeup((1ULL << WAKEUP_PIN), ESP_EXT1_WAKEUP_ANY_LOW); // wake on low level

    err = esp_deep_sleep_try((uint64_t)report_interval_seconds * 1000000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_deep_sleep_try() failed: %s", esp_err_to_name(err));
        // wait briefly to flush logs then attempt to start deep sleep anyway
        vTaskDelay(pdMS_TO_TICKS(200));
    }

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

    wifi_platform_init();

    // If we woke from deep sleep due to the WAKEUP pin, allow entering AP mode
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "Woke from deep sleep via WAKEUP pin");
        if (boot_pin_held_for_5s()) {
            ap_config_start(); // serves config portal; restarts device on save
        }
    }

    // If WiFi SSID or MQTT URI are not set, start AP config portal immediately
    config_t *cfg = get_config_ptr();
    if (cfg->wifi_ssid[0] == '\0' || cfg->mqtt_uri[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID or MQTT URI not set; starting AP config portal");
        ap_config_start(); // serves config portal; restarts device on save
    }

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
