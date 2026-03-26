#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "common.h"

#define AP_SSID      "ESP32-Setup"
#define AP_CHANNEL   1
#define AP_MAX_STA   4
#define NVS_NAMESPACE "storage"

#define AP_MODE_TIMEOUT_MS 300000 // 5 minutes

// HTML config page — uses snprintf with %s placeholders for current values.
// Note: %% is used wherever the HTML/CSS needs a literal % character.
static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 Setup</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:440px;margin:28px auto;padding:0 14px;background:#f0f2f5}"
    "h1{font-size:1.3em;color:#222;margin-bottom:18px}"
    ".card{background:#fff;border-radius:8px;padding:16px;margin-bottom:14px;box-shadow:0 1px 4px rgba(0,0,0,.12)}"
    ".card b{font-size:.95em;color:#444}"
    "label{display:block;font-size:.82em;color:#666;margin:10px 0 3px}"
    "input{width:100%%;box-sizing:border-box;padding:7px 9px;border:1px solid #ccc;border-radius:5px;font-size:.95em}"
    "input:focus{outline:none;border-color:#0078d4}"
    "button{margin-top:16px;width:100%%;padding:11px;background:#0078d4;color:#fff;border:none;"
    "border-radius:6px;font-size:1em;cursor:pointer}"
    "button:hover{background:#005ea2}"
    "p.hint{font-size:.78em;color:#888;margin:6px 0 0}"
    "</style></head><body>"
    "<h1>ESP32 Device Setup</h1>"
    "<form method='POST' action='/save'>"
    "<div class='card'>"
    "<b>WiFi</b>"
    "<label>SSID</label><input name='ssid' value='%s' autocomplete='off'>"
    "<label>Password</label><input name='pass' type='password' value='%s'>"
    "</div>"
    "<div class='card'>"
    "<b>MQTT Broker</b>"
    "<label>URI (e.g. mqtt://192.168.1.100 or mqtts://host:8883)</label>"
    "<input name='mqtt_uri' value='%s'>"
    "<label>Username <span style='font-weight:normal'>(optional)</span></label>"
    "<input name='mqtt_user' value='%s' autocomplete='off'>"
    "<label>Password <span style='font-weight:normal'>(optional)</span></label>"
    "<input name='mqtt_pass' type='password' value='%s'>"
    "<label>Topic prefix</label><input name='mqtt_topic' value='%s'>"
    "</div>"
    "<button type='submit'>Save &amp; Restart</button>"
    "<p class='hint'>Device will restart and connect with the new settings.</p>"
    "</form></body></html>";

// URL-decode src (up to src_len bytes) into dst (max dst_len-1 chars + NUL).
static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t di = 0;
    for (size_t i = 0; i < src_len && di < dst_len - 1; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            char hex[3] = { src[i + 1], src[i + 2], '\0' };
            char byte = (char)strtol(hex, NULL, 16);
            if (byte != '\0') { // skip null bytes
                dst[di++] = byte;
            }
            i += 2;
        } else if (src[i] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

// Extract and URL-decode a field from an application/x-www-form-urlencoded body.
// Handles both "field=value&..." and "&field=value&..." patterns correctly.
static bool extract_field(const char *body, const char *field, char *out, size_t out_len)
{
    size_t flen = strlen(field);
    const char *p = NULL;

    // Check if body starts with "field="
    if (strncmp(body, field, flen) == 0 && body[flen] == '=') {
        p = body + flen + 1;
    } else {
        // Search for "&field=" to avoid partial prefix matches
        for (const char *s = body; *s; s++) {
            if (*s == '&' && strncmp(s + 1, field, flen) == 0 && s[1 + flen] == '=') {
                p = s + 1 + flen + 1;
                break;
            }
        }
    }

    if (!p) {
        out[0] = '\0';
        return false;
    }

    const char *end = strchr(p, '&');
    size_t encoded_len = end ? (size_t)(end - p) : strlen(p);
    url_decode(out, out_len, p, encoded_len);
    return true;
}

static esp_err_t save_str_nvs(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// GET / — serve the config form pre-filled with current values
static esp_err_t get_handler(httpd_req_t *req)
{
    config_t *cfg = get_config_ptr();
    int needed = snprintf(NULL, 0, HTML_PAGE,
                          cfg->wifi_ssid, cfg->wifi_pass,
                          cfg->mqtt_uri, cfg->mqtt_user, cfg->mqtt_pass, cfg->mqtt_topic);
    if (needed <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to format page");
        return ESP_FAIL;
    }

    char *html = malloc((size_t)needed + 1);
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    snprintf(html, (size_t)needed + 1, HTML_PAGE,
             cfg->wifi_ssid, cfg->wifi_pass,
             cfg->mqtt_uri, cfg->mqtt_user, cfg->mqtt_pass, cfg->mqtt_topic);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, needed);
    free(html);
    return ESP_OK;
}

// POST /save — parse form, save to NVS, restart
static esp_err_t save_handler(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0 || content_len >= 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    char body[512] = {0};
    int received = 0;
    while (received < content_len) {
        int r = httpd_req_recv(req, body + received, content_len - received);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';

    config_t *cfg = get_config_ptr();
    extract_field(body, "ssid",       cfg->wifi_ssid,  sizeof(cfg->wifi_ssid));
    extract_field(body, "pass",       cfg->wifi_pass,  sizeof(cfg->wifi_pass));
    extract_field(body, "mqtt_uri",   cfg->mqtt_uri,   sizeof(cfg->mqtt_uri));
    extract_field(body, "mqtt_user",  cfg->mqtt_user,  sizeof(cfg->mqtt_user));
    extract_field(body, "mqtt_pass",  cfg->mqtt_pass,  sizeof(cfg->mqtt_pass));
    extract_field(body, "mqtt_topic", cfg->mqtt_topic, sizeof(cfg->mqtt_topic));

    save_str_nvs("wifi_ssid",   cfg->wifi_ssid);
    save_str_nvs("wifi_pass",   cfg->wifi_pass);
    save_str_nvs("mqtt_uri",    cfg->mqtt_uri);
    save_str_nvs("mqtt_user",   cfg->mqtt_user);
    save_str_nvs("mqtt_pass",   cfg->mqtt_pass);
    save_str_nvs("mqtt_topic",  cfg->mqtt_topic);

    ESP_LOGI(TAG, "Config saved — restarting");

    const char *resp = "<!DOCTYPE html><html><body style='font-family:sans-serif;max-width:440px;"
                       "margin:40px auto;text-align:center'>"
                       "<h2>Saved!</h2><p>Device is restarting with the new settings.</p>"
                       "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, (int)strlen(resp));

    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

void ap_config_start(void)
{
    ESP_LOGI(TAG, "Starting AP config mode — SSID: %s, IP: 192.168.4.1", AP_SSID);

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = (uint8_t)strlen(AP_SSID),
            .channel        = AP_CHANNEL,
            .authmode       = WIFI_AUTH_OPEN,
            .max_connection = AP_MAX_STA,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_handle_t server = NULL;
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&server, &http_cfg));

    static const httpd_uri_t uri_get = {
        .uri = "/", .method = HTTP_GET, .handler = get_handler,
    };
    static const httpd_uri_t uri_save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_handler,
    };
    httpd_register_uri_handler(server, &uri_get);
    httpd_register_uri_handler(server, &uri_save);

    ESP_LOGI(TAG, "Config portal ready — connect to WiFi '%s', open http://192.168.4.1", AP_SSID);

    // Block here; HTTP server tasks run independently.
    // The save_handler will call esp_restart() after saving.
    // To avoid staying in AP mode indefinitely if the user doesn't save, we can optionally add a timeout here.
    int elapsed_ms = 0;
    while (elapsed_ms < AP_MODE_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        elapsed_ms += 1000;
    }

    ESP_LOGI(TAG, "AP mode timeout reached — restarting device");
    esp_restart();
}
