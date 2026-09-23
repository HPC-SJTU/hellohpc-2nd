#pragma once

#include "../op_kernel/rsm_solution_tiling.h"

#include <cstdint>

void ConfigureSolutionLaunch(uint32_t n, uint32_t d, uint32_t s, float epsilon,
    const int32_t *offsets,
    RsmSolutionTiling *tiling, uint32_t *block_dim, uint32_t *tiling_key);
