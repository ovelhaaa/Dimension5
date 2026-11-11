#include "dimension_bbd_clock.h"
#include <string.h>

void DimensionBBDClock_Init(DimensionBBDClock* clock) {
    if (clock == NULL) {
        return;
    }
    memset(clock, 0, sizeof(*clock));
    clock->chL.instantaneousClockHz = DIMENSION_BBD_NOMINAL_CLOCK_HZ;
    clock->chR.instantaneousClockHz = DIMENSION_BBD_NOMINAL_CLOCK_HZ;
}

void DimensionBBDClock_Reset(DimensionBBDClock* clock) {
    if (clock == NULL) {
        return;
    }
    memset(clock, 0, sizeof(*clock));
    clock->chL.instantaneousClockHz = DIMENSION_BBD_NOMINAL_CLOCK_HZ;
    clock->chR.instantaneousClockHz = DIMENSION_BBD_NOMINAL_CLOCK_HZ;
}
