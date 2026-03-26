#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "esp_log.h"

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

#define TAG "esp32_mqtt_sensor"


// ADC and battery measurement
void battery_measure();
float battery_voltage_read(void);
int battery_percent_from_mv(int mv);

// Config
typedef struct config_t {
    int report_interval_seconds;
    int battery_min_mv;
    int battery_max_mv;
    char wifi_ssid[64];
    char wifi_pass[64];
    char mqtt_uri[128];
    char mqtt_user[64];
    char mqtt_pass[64];
    char mqtt_topic[64];
} config_t;

void config_init();
void config_publish();
void config_subscribe();
void config_update_value_in_nvs(const char *name, int value);
void load_str_from_nvs(const char *key, char *buf, size_t buf_size, const char *default_val);
esp_err_t load_int_from_nvs(const char *key, volatile int *out_value, int default_value);
esp_err_t save_int_to_nvs(const char *key, int value);
esp_err_t save_str_to_nvs(const char *key, const char *value);
config_t* get_config_ptr();

// MQTT
void mqtt_app_start();
void mqtt_cleanup();
void mqtt_publish(const char *subtopic, const char *payload, int len);
void mqtt_subscribe(const char *subtopic);
void mqtt_publish_config(const char *key, int value);
void mqtt_subscribe_config(const char *key);
volatile bool* get_mqtt_connected_ptr();

// WiFi
void wifi_platform_init();
void wifi_init_sta();
void wifi_cleanup();
volatile bool* get_wifi_connected_ptr();

// AP config portal
void ap_config_start(void);
