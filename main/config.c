#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "common.h"

// NVS key for stored report interval
#define NVS_NAMESPACE "storage"
#define NVS_KEY_REPORT_INTERVAL "report_interval"

config_t config_params = {0};

esp_err_t save_to_nvs(const char *key, int value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t load_from_nvs(const char *key, volatile int *out_value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    int32_t val = 0;
    err = nvs_get_i32(h, key, &val);
    if (err == ESP_OK && out_value) *out_value = val;
    nvs_close(h);
    return err;
}

static void update_from_nvs(const char *key, int *out_value, int default_value)
{
    // load stored from NVS (if present)
    if (load_from_nvs(key, out_value) == ESP_OK && *out_value > 0) {
        ESP_LOGI(TAG, "Loaded %s from NVS: %d", key, *out_value);
    } else {
        ESP_LOGI(TAG, "Using default %s: %d", key, default_value);
        save_to_nvs(key, default_value);
        *out_value = default_value;
    }
}

void config_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
}

void config_subscribe()
{
    update_from_nvs("report_interval", (int *)&config_params.report_interval_seconds, REPORT_INTERVAL_SECONDS);
    mqtt_subscribe_config("report_interval");
    update_from_nvs("battery_min_mv", (int *)&config_params.battery_min_mv, BAT_MIN_MV);
    mqtt_subscribe_config("battery_min_mv");
    update_from_nvs("battery_max_mv", (int *)&config_params.battery_max_mv, BAT_MAX_MV);
    mqtt_subscribe_config("battery_max_mv");

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait a bit for any retained message to arrive and be processed
}

void config_publish()
{
    mqtt_publish_config("report_interval", config_params.report_interval_seconds);
    mqtt_publish_config("battery_min_mv", config_params.battery_min_mv);
    mqtt_publish_config("battery_max_mv", config_params.battery_max_mv);
}

void config_update_value_in_nvs(const char *name, int value)
{
    esp_err_t r = ESP_ERR_INVALID_ARG;

    if(strcmp(name, "report_interval") == 0 && value != config_params.report_interval_seconds) {
        r = save_to_nvs("report_interval", value);
        if (r == ESP_OK) config_params.report_interval_seconds = value;
    } else if(strcmp(name, "battery_min_mv") == 0 && value != config_params.battery_min_mv) {
        r = save_to_nvs("battery_min_mv", value);
        if (r == ESP_OK) config_params.battery_min_mv = value;
    } else if(strcmp(name, "battery_max_mv") == 0 && value != config_params.battery_max_mv) {
        r = save_to_nvs("battery_max_mv", value);
        if (r == ESP_OK) config_params.battery_max_mv = value;
    } else {
        ESP_LOGW(TAG, "Unknown config key or value not changed: %s", name);
        return;
    }

    if (r == ESP_OK) {
        ESP_LOGI(TAG, "Saved %s %d to NVS", name, value);
    } else {
        ESP_LOGE(TAG, "Failed to save %s to NVS: %s", name, esp_err_to_name(r));
    }
}

config_t* get_config_ptr()
{
    return &config_params;
}