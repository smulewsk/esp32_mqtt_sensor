#include "mqtt_client.h"
#include "esp_event.h"
#include "esp_mac.h"

#include "common.h"


// MQTT client handle and connection state
static esp_mqtt_client_handle_t mqtt_client = NULL;
static volatile bool mqtt_connected = false;

// base topic that includes device unique id (MAC-based)
static char base_mqtt_topic[128] = {0};

static void create_base_mqtt_topic(void)
{
    uint8_t mac[6];
    const char *topic_base = get_config_ptr()->mqtt_topic;
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char id[13];
        snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(base_mqtt_topic, sizeof(base_mqtt_topic), "%s-%s", topic_base, id);
        ESP_LOGI(TAG, "Base MQTT topic set to: %s", base_mqtt_topic);
    } else {
        // fallback to topic_base if MAC read fails
        strncpy(base_mqtt_topic, topic_base, sizeof(base_mqtt_topic) - 1);
        base_mqtt_topic[sizeof(base_mqtt_topic) - 1] = '\0';
        ESP_LOGW(TAG, "Failed to read MAC; using MQTT topic: %s", base_mqtt_topic);
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
    case MQTT_EVENT_DATA: {
        esp_mqtt_event_handle_t event = event_data;
        // copy topic
        char topic[128];
        int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, tlen);
        topic[tlen] = '\0';

        char name[64];
        // retrieve the parameter name from topic (if matches expected pattern)
        int matches = sscanf(topic, "%*[^/]/%*[^/]/%[^/]", name);

        if (matches) {
            // copy payload
            char payload[64];
            int plen = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;
            memcpy(payload, event->data, plen);
            payload[plen] = '\0';
            ESP_LOGI(TAG, "Received %s payload: %s", name, payload);
            
            int received_value = atoi(payload);
            config_update_value_in_nvs(name, received_value);
        } else {
            ESP_LOGW(TAG, "Received message on unexpected topic pattern: %s", topic);
        }
        break;
    }
    default:
        break;
    }
}

void mqtt_app_start(void)
{
    config_t *net_cfg = get_config_ptr();

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = net_cfg->mqtt_uri,
        .credentials.username = net_cfg->mqtt_user,
        .credentials.authentication.password = net_cfg->mqtt_pass,
        .session.disable_clean_session = true, // to receive retained messages on subscribe
    };

    // create MQTT base topic that includes device unique id
    create_base_mqtt_topic();

    mqtt_client = esp_mqtt_client_init(&cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return;
    }
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_cleanup()
{
    // cleanup MQTT and WiFi before deep sleep
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
}

void mqtt_publish(const char *topic_suffix, const char *payload, int len)
{
    if (mqtt_connected && mqtt_client) {
        char full_topic[256];
        snprintf(full_topic, sizeof(full_topic), "%s/%s", base_mqtt_topic, topic_suffix);
        int msg_id = esp_mqtt_client_publish(mqtt_client, full_topic, payload, len, 1, 0);
        ESP_LOGI(TAG, "Published: %s, msg_id=%d", payload, msg_id);
    } else {
        ESP_LOGW(TAG, "MQTT not connected; cannot publish %s: %s", topic_suffix, payload);
    }
}

void mqtt_subscribe(const char *topic_suffix)
{
    if (mqtt_connected && mqtt_client) {
        char full_topic[256];
        snprintf(full_topic, sizeof(full_topic), "%s/%s", base_mqtt_topic, topic_suffix);
        int msg_id = esp_mqtt_client_subscribe(mqtt_client, full_topic, 1);
        ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", full_topic, msg_id);
    } else {
        ESP_LOGW(TAG, "MQTT not connected; cannot subscribe to %s", topic_suffix);
    }
}

void mqtt_publish_config(const char *key, int value)
{
    char payload[32];
    char pub_topic[128];
    int len = snprintf(payload, sizeof(payload), "%d", value);
    snprintf(pub_topic, sizeof(pub_topic), "config/%s/state", key);
    mqtt_publish(pub_topic, payload, len);
}

void mqtt_subscribe_config(const char *key)
{
    // subscribe to key topic to receive retained value (if any)
    char sub_topic[128];
    snprintf(sub_topic, sizeof(sub_topic), "config/%s/set", key);
    mqtt_subscribe(sub_topic);
}

volatile bool* get_mqtt_connected_ptr()
{
    return (volatile bool*)&mqtt_connected;
}