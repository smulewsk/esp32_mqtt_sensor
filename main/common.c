#include "common.h"

#if defined(CONFIG_DISTANCE_ENABLE)
int distance_percent_from_mm(int mm)
{
    config_t *cfg = get_config_ptr();
    int distance_min_mm = cfg->distance_min_mm;
    int distance_max_mm = cfg->distance_max_mm;

    if(distance_min_mm > distance_max_mm) {
        // inverted logic: 0% = far, 100% = near
        if (mm >= distance_min_mm) return 0;
        if (mm <= distance_max_mm) return 100;
        return (int)(((distance_min_mm - mm) * 100) / (distance_min_mm - distance_max_mm));
    } else {
        // normal logic: 0% = near, 100% = far
        if (mm <= distance_min_mm) return 0;
        if (mm >= distance_max_mm) return 100;
        return (int)(((mm - distance_min_mm) * 100) / (distance_max_mm - distance_min_mm));
    }
}
#endif

#if defined(CONFIG_MOISTURE_ENABLE)
int moisture_percent_from_adc(int adc)
{
    config_t *cfg = get_config_ptr();
    int moisture_min_adc = cfg->moisture_min_adc;
    int moisture_max_adc = cfg->moisture_max_adc;

    if (moisture_min_adc > moisture_max_adc) {
        // inverted logic: 0% = dry, 100% = wet
        if (adc >= moisture_min_adc) return 0;
        if (adc <= moisture_max_adc) return 100;
        return (int)(((moisture_min_adc - adc) * 100) / (moisture_min_adc - moisture_max_adc));
    } else {
        // normal logic: 0% = wet, 100% = dry
        if (adc <= moisture_min_adc) return 0;
        if (adc >= moisture_max_adc) return 100;
        return (int)(((adc - moisture_min_adc) * 100) / (moisture_max_adc - moisture_min_adc));
    }
}
#endif