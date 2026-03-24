
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "common.h"


// variables for ADC handles and calibration
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;


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

void battery_measure()
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


float battery_voltage_read(void)
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

int battery_percent_from_mv(int mv)
{
    config_t *cfg = get_config_ptr();
    int battery_min_mv = cfg->battery_min_mv;
    int battery_max_mv = cfg->battery_max_mv;

    if (mv <= battery_min_mv) return 0;
    if (mv >= battery_max_mv) return 100;
    return (int)(((mv - battery_min_mv) * 100) / (battery_max_mv - battery_min_mv));
}