#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "common.h"

static const char *TAG = "main";

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
        ESP_LOGI(TAG, "WAKEUP pin not held at boot");
        return false; // not held
    }

    ESP_LOGI(TAG, "WAKEUP pin held — waiting 5s to enter AP config mode...");
    for (int i = 0; i < 50; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (gpio_get_level(WAKEUP_PIN) != 0) {
            ESP_LOGI(TAG, "WAKEUP pin released after %d ms", (i + 1) * 100);
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

static void fw_version_publish(const char *fw_version)
{
    mqtt_publish("version", fw_version, strlen(fw_version));
}

static void deep_sleep(bool restart)
{
    config_t *cfg = get_config_ptr();
    int deep_sleep_seconds = 0;
    if(restart) {
        deep_sleep_seconds = cfg->restart_interval_seconds;
    } else {
        deep_sleep_seconds = cfg->report_interval_seconds;
    }
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

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds (WAKEUP button will wake device)", deep_sleep_seconds);
    esp_sleep_enable_ext1_wakeup((1ULL << WAKEUP_PIN), ESP_EXT1_WAKEUP_ANY_LOW); // wake on low level

    err = esp_deep_sleep_try((uint64_t)deep_sleep_seconds * 1000000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_deep_sleep_try() failed: %s", esp_err_to_name(err));
        // wait briefly to flush logs then attempt to start deep sleep anyway
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    esp_deep_sleep_start();
}

static bool init()
{
    wifi_platform_init();
    wifi_init_sta();
    wait_until_connected(get_wifi_connected_ptr(), 5000);
    
    mqtt_app_start();
    wait_until_connected(get_mqtt_connected_ptr(), 5000);

    if (*get_wifi_connected_ptr() && *get_mqtt_connected_ptr()) {
        return true;
    }

    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting esp32_mqtt_sensor");

    esp_log_level_set("*", ESP_LOG_NONE);    // disable all logs
    esp_log_level_set("main", ESP_LOG_INFO);    // except for our own main, which we set to INFO level
    //esp_log_level_set("wifi", ESP_LOG_INFO);    // and WiFi logs, which are often useful to debug connectivity issues
    esp_log_level_set("mqtt", ESP_LOG_INFO);    // enable MQTT logs
    esp_log_level_set("ota", ESP_LOG_INFO);    // enable OTA logs
    esp_log_level_set("config", ESP_LOG_INFO); // enable config-related logs
    esp_log_level_set("ap_config", ESP_LOG_INFO); // enable AP config logs


    // publish firmware version and log it
#ifdef PROJECT_VERSION
    const char *fw_version = PROJECT_VERSION;
#else
    const char *fw_version = "unknown";
#endif
    ESP_LOGI(TAG, "Firmware version: %s", fw_version);

    config_init();

    if(rtc_gpio_is_valid_gpio(WAKEUP_PIN)) {
        ESP_LOGI(TAG, "Use pin %d to enter to AP mode", WAKEUP_PIN);
    } else {
        ESP_LOGW(TAG, "WAKEUP pin %d is NOT RTC GPIO capable; wake from deep sleep via WAKEUP pin will not work!", WAKEUP_PIN);
    }

    // If WAKEUP_PIN is held at boot for 5 seconds, start AP config portal immediately (overrides normal WiFi/MQTT init and deep sleep flow)
    if (boot_pin_held_for_5s()) {
        wifi_platform_init();
        ap_config_start(); // serves config portal; restarts device on save
    }

    // If WiFi SSID or MQTT URI are not set, start AP config portal immediately
    config_t *cfg = get_config_ptr();
    if (cfg->wifi_ssid[0] == '\0' || cfg->mqtt_uri[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID or MQTT URI not set; starting AP config portal");
        wifi_platform_init();
        ap_config_start(); // serves config portal; restarts device on save
    }

    if(!init()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi/MQTT. Deep sleep...");
        deep_sleep(true); // restart after RESTART_INTERVAL_SECONDS
    }

    config_subscribe();

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait a bit for any retained message to arrive and be processed

    battery_measure_and_publish();

    // Temperature sensor init + publish (optional)
#if CONFIG_DS18B20_ENABLE
    temperature_sensor_init_and_publish();
#endif

    // Moisture sensor init + publish (optional)
#if CONFIG_MOISTURE_ENABLE
    moisture_sensor_init_and_publish();
#endif

    // Distance sensor init + publish handled by distance_sensor module
#if CONFIG_DISTANCE_ENABLE
    distance_sensor_init_and_publish();
#endif

    fw_version_publish(fw_version);

    config_publish();

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait a bit to ensure messages are sent before deep sleep

    deep_sleep(false);    
}
