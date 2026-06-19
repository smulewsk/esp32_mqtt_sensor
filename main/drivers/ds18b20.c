#include "ds18b20.h"
#include "driver/gpio.h"
#include "esp_rom/ets_sys.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ds18b20";

// Timing constants (microseconds) for standard speed
#define RESET_PULSE_US 480
#define PRESENCE_WAIT_US 70
#define PRESENCE_SAMPLE_US 410
#define SLOT_TIME_US 70

#ifndef CONFIG_DS18B20_GPIO
#define CONFIG_DS18B20_GPIO 4
#endif

static inline void line_low(void)
{
    gpio_set_level(CONFIG_DS18B20_GPIO, 0);
    gpio_set_direction(CONFIG_DS18B20_GPIO, GPIO_MODE_OUTPUT);
}

static inline void line_release(void)
{
    gpio_set_direction(CONFIG_DS18B20_GPIO, GPIO_MODE_INPUT);
}

static inline int line_read(void)
{
    return gpio_get_level(CONFIG_DS18B20_GPIO);
}

static bool ow_reset(void)
{
    line_low();
    ets_delay_us(RESET_PULSE_US);
    line_release();
    ets_delay_us(PRESENCE_WAIT_US);
    int present = line_read();
    ets_delay_us(PRESENCE_SAMPLE_US);
    return (present == 0);
}

static void ow_write_bit(int bit)
{
    line_low();
    if (bit) {
        ets_delay_us(6);
        line_release();
        ets_delay_us(SLOT_TIME_US - 6);
    } else {
        ets_delay_us(60);
        line_release();
        ets_delay_us(SLOT_TIME_US - 60);
    }
}

static int ow_read_bit(void)
{
    int bit;
    line_low();
    ets_delay_us(6);
    line_release();
    ets_delay_us(9);
    bit = line_read();
    ets_delay_us(SLOT_TIME_US - 15);
    return bit;
}

static void ow_write_byte(uint8_t val)
{
    for (int i = 0; i < 8; ++i) {
        ow_write_bit((val >> i) & 1);
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; ++i) {
        int b = ow_read_bit();
        v |= (b << i);
    }
    return v;
}

bool ds18b20_init(void)
{
    gpio_reset_pin(CONFIG_DS18B20_GPIO);
    gpio_set_pull_mode(CONFIG_DS18B20_GPIO, GPIO_PULLUP_ONLY);
    line_release();
    ets_delay_us(10);
    bool present = ow_reset();
    if (!present) {
        ESP_LOGW(TAG, "No DS18B20 presence detected on GPIO %d", CONFIG_DS18B20_GPIO);
        return false;
    }
    ESP_LOGI(TAG, "DS18B20 presence detected on GPIO %d", CONFIG_DS18B20_GPIO);
    return true;
}

bool ds18b20_read_celsius(float *out_celsius)
{
    if (!ow_reset()) return false;
    // Skip ROM, start conversion
    ow_write_byte(0xCC);
    ow_write_byte(0x44);
    // Wait for conversion; DS18B20 signals by pulling line low when busy, but we'll wait up to 750ms
    for (int i = 0; i < 750; i += 10) {
        ets_delay_us(10000);
        // could poll for completion here by reading bit, but simple delay is fine
    }

    if (!ow_reset()) return false;
    // Skip ROM, read scratchpad
    ow_write_byte(0xCC);
    ow_write_byte(0xBE);
    uint8_t temp_l = ow_read_byte();
    uint8_t temp_h = ow_read_byte();
    int16_t raw = (int16_t)((temp_h << 8) | temp_l);
    // DS18B20 default resolution is 0.0625°C (1/16)
    float c = raw / 16.0f;
    if (out_celsius) *out_celsius = c;
    return true;
}
