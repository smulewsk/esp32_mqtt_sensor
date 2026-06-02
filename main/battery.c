#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "adc_manager.h"
#include "common.h"


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


void battery_measure_and_publish()
{
    // Ensure ADC unit and channel are initialised via adc_manager
    ESP_ERROR_CHECK(adc_manager_init_channel((adc_channel_t)BAT_ADC_CHANNEL));

    float batt_v = battery_voltage_read();
    int batt_mv = (int)roundf(batt_v * 1000.0f);
    int pct = battery_percent_from_mv(batt_mv);

    char payload[128];
    int len = snprintf(payload, sizeof(payload), "{\"voltage\":%.3f,\"mv\":%d,\"percent\":%d}", batt_v, batt_mv, pct);
    
    mqtt_publish("battery", payload, len);
}


float battery_voltage_read(void)
{
    int raw = 0;
    int raw_avg = 0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_manager_get_handle(), (adc_channel_t)BAT_ADC_CHANNEL, &raw));

    rtc_batt_add_sample(raw);

    raw_avg = rtc_batt_avg_mv(); // update RTC-stored average (for use across deep sleep)

    int voltage_mv = 0;
    adc_cali_handle_t cal = adc_manager_get_cali_handle_for_channel((adc_channel_t)BAT_ADC_CHANNEL);
    if (cal) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cal, raw_avg, &voltage_mv));
    } else {
        // fallback approximation
        const int raw_max = 4095;
        voltage_mv = (raw_avg * 3300) / raw_max;
    }

    float divider_factor = ((float)DIV_R1_OHMS + (float)DIV_R2_OHMS) / (float)DIV_R2_OHMS;
    float battery_mv = (float)voltage_mv * divider_factor;

    return battery_mv / 1000.0f; // volts
}

int battery_percent_from_mv(int mv)
{
    config_t *cfg = get_config_ptr();
    int battery_min_mv = cfg->battery_min_mv;
    int battery_max_mv = cfg->battery_max_mv;

    if (mv <= battery_min_mv) return 0;
    if (mv >= battery_max_mv) return 100;
    return (int)(((mv - battery_min_mv) * 100) / (battery_max_mv - battery_min_mv));
}