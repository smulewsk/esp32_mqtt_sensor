#include "ota.h"

#include "esp_crt_bundle.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common.h"
#include <ctype.h>

static const char *TAG = "ota";

// buffer size for HTTP download (increase to avoid "Out of buffer" errors)
#define OTA_HTTP_BUF_SIZE 4096

static void mqtt_clear_ota_url_retained()
{
    // publish empty retained payload to clear retained URL on broker
    mqtt_publish_retained("config/ota_url/set", "", 0);
}


esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

esp_err_t ota_perform_update(const char *url)
{
    if (!url || strlen(url) == 0) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Starting OTA from URL: %s", url);

    ESP_LOGI(TAG, "Free heap before OTA: %d", esp_get_free_heap_size());

    /* sanitize URL: copy and trim whitespace/newlines which may come from MQTT payloads */
    char url_sanitized[256];
    size_t ulen = strlen(url);
    if (ulen >= sizeof(url_sanitized)) ulen = sizeof(url_sanitized) - 1;
    memcpy(url_sanitized, url, ulen);
    url_sanitized[ulen] = '\0';
    /* trim leading whitespace */
    char *start = url_sanitized;
    while (*start && isspace((unsigned char)*start)) start++;
    /* trim trailing whitespace */
    char *end = start + strlen(start) - 1;
    while (end >= start && isspace((unsigned char)*end)) { *end = '\0'; end--; }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 50000, // 50 second timeout
        .buffer_size = OTA_HTTP_BUF_SIZE,
        .buffer_size_tx = OTA_HTTP_BUF_SIZE,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .event_handler = _http_event_handler,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true, // enable partial download to reduce memory usage
    };
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    /* point config.url to the sanitized string */
    config.url = start;
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed with error 0x%x, %s", ret, esp_err_to_name(ret));
        return ret;
    }

    // Clear retained OTA URL so broker doesn't re-trigger update
    mqtt_clear_ota_url_retained();

    // small delay before restarting
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();

    return ESP_OK; // should never be reached
}
