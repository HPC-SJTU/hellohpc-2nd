#pragma once

#include "../op_kernel/rsm_baseline_tiling.h"

#include <cstdint>

void ConfigureBaselineLaunch(uint32_t n, uint32_t d, uint32_t s, float epsilon,
    const int32_t *offsets,
    RsmBaselineTiling *tiling, uint32_t *block_dim, uint32_t *tiling_key);
