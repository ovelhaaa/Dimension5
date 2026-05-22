#include <assert.h>

#include "../src/dimension_dsp.h"

int main(void) {
    DimensionDSP dsp;
    float inL[DIMENSION_MAX_BLOCK_SIZE] = {0.0f};
    float inR[DIMENSION_MAX_BLOCK_SIZE] = {0.0f};
    float outL[DIMENSION_MAX_BLOCK_SIZE] = {0.0f};
    float outR[DIMENSION_MAX_BLOCK_SIZE] = {0.0f};

    Dimension_Init(&dsp, 48000.0f);
    Dimension_SetMode(&dsp, DIMENSION_MODE_II);
    Dimension_ProcessBlock(&dsp, inL, inR, outL, outR, 16U);

    for (unsigned i = 0; i < 16U; ++i) {
        assert(outL[i] == 0.0f);
        assert(outR[i] == 0.0f);
    }

    return 0;
}
