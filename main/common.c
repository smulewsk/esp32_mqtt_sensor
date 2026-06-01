#include "common.h"

int distance_percent_from_mm(int mm)
{
    config_t *cfg = get_config_ptr();
    int distance_min_mm = cfg->distance_min_mm;
    int distance_max_mm = cfg->distance_max_mm;

    if (mm <= distance_min_mm) return 0;
    if (mm >= distance_max_mm) return 100;
    return (int)(((mm - distance_min_mm) * 100) / (distance_max_mm - distance_min_mm));
}
