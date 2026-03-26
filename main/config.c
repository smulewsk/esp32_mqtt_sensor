#include "nvs_flash.h"
#include "nvs.h"

#include "common.h"

// NVS key for stored report interval
#define NVS_NAMESPACE "storage"
#define NVS_KEY_REPORT_INTERVAL "report_interval"

config_t config_params = {0};

esp_err_t save_int_to_nvs(const char *key, int value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t load_int_from_nvs(const char *key, volatile int *out_value, int default_value)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t h;
    if((err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h)) == ESP_OK) {
        int32_t val = 0;
        if((err = nvs_get_i32(h, key, &val)) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded %s from NVS: %d", key, val);
            if (out_value) *out_value = val;
        } else {
            ESP_LOGI(TAG, "NVS key %s error; using default", key);
            *out_value = default_value;
        }
        nvs_close(h);
    }
    return err;
}

void load_str_from_nvs(const char *key, char *out, size_t out_len, const char *def)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = out_len;
        if (nvs_get_str(h, key, out, &len) == ESP_OK && out[0] != '\0') {
            ESP_LOGI(TAG, "Loaded %s from NVS", key);
            nvs_close(h);
            return;
        }
        nvs_close(h);
    }
    strncpy(out, def, out_len - 1);
    out[out_len - 1] = '\0';
    ESP_LOGI(TAG, "Using default for %s", key);
}

esp_err_t save_str_to_nvs(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

void config_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    load_int_from_nvs("report_interval", (int *)&config_params.report_interval_seconds, REPORT_INTERVAL_SECONDS);
    load_int_from_nvs("battery_min_mv", (int *)&config_params.battery_min_mv, BAT_MIN_MV);
    load_int_from_nvs("battery_max_mv", (int *)&config_params.battery_max_mv, BAT_MAX_MV);

    // Load network credentials from NVS — fall back to Kconfig compile-time defaults
    load_str_from_nvs("wifi_ssid",   config_params.wifi_ssid,   sizeof(config_params.wifi_ssid),   WIFI_SSID);
    load_str_from_nvs("wifi_pass",   config_params.wifi_pass,   sizeof(config_params.wifi_pass),   WIFI_PASS);
    load_str_from_nvs("mqtt_uri",    config_params.mqtt_uri,    sizeof(config_params.mqtt_uri),    MQTT_URI);
    load_str_from_nvs("mqtt_user",   config_params.mqtt_user,   sizeof(config_params.mqtt_user),   MQTT_USER);
    load_str_from_nvs("mqtt_pass",   config_params.mqtt_pass,   sizeof(config_params.mqtt_pass),   MQTT_PASS);
    load_str_from_nvs("mqtt_topic",  config_params.mqtt_topic,  sizeof(config_params.mqtt_topic),  MQTT_TOPIC);
}

void config_subscribe()
{
    mqtt_subscribe_config("report_interval");
    mqtt_subscribe_config("battery_min_mv");
    mqtt_subscribe_config("battery_max_mv");
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
        r = save_int_to_nvs("report_interval", value);
        if (r == ESP_OK) config_params.report_interval_seconds = value;
    } else if(strcmp(name, "battery_min_mv") == 0 && value != config_params.battery_min_mv) {
        r = save_int_to_nvs("battery_min_mv", value);
        if (r == ESP_OK) config_params.battery_min_mv = value;
    } else if(strcmp(name, "battery_max_mv") == 0 && value != config_params.battery_max_mv) {
        r = save_int_to_nvs("battery_max_mv", value);
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