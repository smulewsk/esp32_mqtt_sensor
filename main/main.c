#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "mqtt_client.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"


static const char *TAG = "esp32_mqtt_sensor";

// Kconfig-provided values
#define WIFI_SSID CONFIG_WIFI_SSID
#define WIFI_PASS CONFIG_WIFI_PASSWORD
#define MQTT_URI CONFIG_MQTT_URI
#define MQTT_USER CONFIG_MQTT_USERNAME
#define MQTT_PASS CONFIG_MQTT_PASSWORD
#define MQTT_TOPIC CONFIG_MQTT_TOPIC
#define REPORT_INTERVAL_SECONDS CONFIG_REPORT_INTERVAL_S
#define BAT_ADC_CHANNEL CONFIG_BAT_ADC_CHANNEL
#define DIV_R1_OHMS CONFIG_BATTERY_DIVIDER_R1
#define DIV_R2_OHMS CONFIG_BATTERY_DIVIDER_R2
#define BAT_MIN_MV CONFIG_BATTERY_MIN_MV
#define BAT_MAX_MV CONFIG_BATTERY_MAX_MV

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static volatile bool mqtt_connected = false;
static volatile bool wifi_connected = false;

static void wait_until_connected(volatile bool *wait_flag, int max_wait_ms)
{
    int waited_ms = 0;
    while (!(*wait_flag) && waited_ms < max_wait_ms) {
        vTaskDelay(pdMS_TO_TICKS(500));
        waited_ms += 500;
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)

{
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;
    default:
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };

    mqtt_client = esp_mqtt_client_init(&cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return;
    }
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected");
        wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "Got IP: %s", ip_str);
        wifi_connected = true;
    }
}

static void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static float read_battery_voltage(void)
{
    int raw = 0;
    int raw_sum = 0;
    int total_readings = 10;
    for (int i = 0; i < total_readings; i++) { // take multiple readings for stability
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, (adc_channel_t)BAT_ADC_CHANNEL, &raw));
        raw_sum += raw;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    raw = raw_sum / total_readings;

    int voltage_mv = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage_mv));

    float divider_factor = ((float)DIV_R1_OHMS + (float)DIV_R2_OHMS) / (float)DIV_R2_OHMS;
    float battery_mv = (float)voltage_mv * divider_factor;

    return battery_mv / 1000.0f; // volts
}

static int battery_percent_from_mv(int mv)
{
    if (mv <= BAT_MIN_MV) return 0;
    if (mv >= BAT_MAX_MV) return 100;
    return (int)(((mv - BAT_MIN_MV) * 100) / (BAT_MAX_MV - BAT_MIN_MV));
}

static void mqtt_publish(const char *topic_suffix, const char *payload, int len)
{
    if (mqtt_connected && mqtt_client) {
        char full_topic[128];
        snprintf(full_topic, sizeof(full_topic), "%s/%s", MQTT_TOPIC, topic_suffix);
        int msg_id = esp_mqtt_client_publish(mqtt_client, full_topic, payload, len, 1, 0);
        ESP_LOGI(TAG, "Published: %s, msg_id=%d", payload, msg_id);
    } else {
        ESP_LOGW(TAG, "MQTT not connected; cannot publish %s: %s", topic_suffix, payload);
    }
}

static void battery_status_publish()
{
    float batt_v = read_battery_voltage();
    int batt_mv = (int)roundf(batt_v * 1000.0f);
    int pct = battery_percent_from_mv(batt_mv);

    char payload[128];
    int len = snprintf(payload, sizeof(payload), "{\"voltage\":%.3f,\"mv\":%d,\"percent\":%d}", batt_v, batt_mv, pct);
    
    mqtt_publish("battery", payload, len);
}

void fw_version_publish(const char *fw_version)
{
    mqtt_publish("version", fw_version, strlen(fw_version));
}

void deep_sleep()
{
    // cleanup MQTT and WiFi before deep sleep
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    esp_wifi_stop();

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", REPORT_INTERVAL_SECONDS);
    esp_deep_sleep_try((uint64_t)REPORT_INTERVAL_SECONDS * 1000000ULL);
    esp_deep_sleep_start();
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

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ADC init
    adc_oneshot_unit_init_cfg_t adc1_init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc1_init_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, (adc_channel_t)BAT_ADC_CHANNEL, &adc_chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc1_cali_handle));

    wifi_init_sta();

    wait_until_connected(&wifi_connected, 30000);

    mqtt_app_start();

    wait_until_connected(&mqtt_connected, 30000);

    fw_version_publish(fw_version);

    battery_status_publish();

    deep_sleep();    
}
