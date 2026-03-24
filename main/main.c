
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "esp_netif.h"

#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_attr.h"


#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "common.h"

// variables for WiFi/MQTT state and callbacks
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;


static volatile bool wifi_connected = false;



// RTC-stored battery readings (preserved across deep sleep)
#define RTC_BATT_SAMPLES 10
RTC_DATA_ATTR static int rtc_batt_values[RTC_BATT_SAMPLES] = {0};
RTC_DATA_ATTR static int rtc_batt_index = 0;

static void rtc_batt_add_sample(int mv)
{
    rtc_batt_values[rtc_batt_index] = mv;
    rtc_batt_index = (rtc_batt_index + 1) % RTC_BATT_SAMPLES;
}

static int rtc_batt_avg_mv(void)
{
    int count = 0;
    long sum = 0;
    for (int i = 0; i < RTC_BATT_SAMPLES; i++) {
        if(rtc_batt_values[i] > 0) {
            sum += rtc_batt_values[i];
            count++;
        }
    }
    return (int)(sum / count);
}

static void wait_until_connected(volatile bool *wait_flag, int max_wait_ms)
{
    int waited_ms = 0;
    while (!(*wait_flag) && waited_ms < max_wait_ms) {
        vTaskDelay(pdMS_TO_TICKS(500));
        waited_ms += 500;
    }
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
    int raw_avg = 0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, (adc_channel_t)BAT_ADC_CHANNEL, &raw));

    rtc_batt_add_sample(raw);

    raw_avg = rtc_batt_avg_mv(); // update RTC-stored average (for use across deep sleep)

    int voltage_mv = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw_avg, &voltage_mv));

    float divider_factor = ((float)DIV_R1_OHMS + (float)DIV_R2_OHMS) / (float)DIV_R2_OHMS;
    float battery_mv = (float)voltage_mv * divider_factor;

    return battery_mv / 1000.0f; // volts
}

static int battery_percent_from_mv(int mv)
{
    config_t *cfg = get_config_ptr();
    int battery_min_mv = cfg->battery_min_mv;
    int battery_max_mv = cfg->battery_max_mv;

    if (mv <= battery_min_mv) return 0;
    if (mv >= battery_max_mv) return 100;
    return (int)(((mv - battery_min_mv) * 100) / (battery_max_mv - battery_min_mv));
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

static void fw_version_publish(const char *fw_version)
{
    mqtt_publish("version", fw_version, strlen(fw_version));
}

static void deep_sleep()
{
    config_t *cfg = get_config_ptr();
    int report_interval_seconds = cfg->report_interval_seconds;
    
    mqtt_cleanup();

    esp_wifi_stop();

    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", report_interval_seconds);
    esp_deep_sleep_try((uint64_t)report_interval_seconds * 1000000ULL);
    esp_deep_sleep_start();
}

static void init()
{
    wifi_init_sta();

    wait_until_connected(&wifi_connected, 30000);

    mqtt_app_start();

    wait_until_connected(get_mqtt_connected_ptr(), 30000);
}

static void battery_measure()
{
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

    fw_version_publish(fw_version);

    config_publish();

    vTaskDelay(pdMS_TO_TICKS(500)); // wait a bit to ensure messages are sent before deep sleep

    deep_sleep();    
}
