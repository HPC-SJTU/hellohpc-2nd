#include "rsm_solution_host.h"

#include <algorithm>

namespace {
constexpr uint32_t kVectorCores = 40;
}

void ConfigureSolutionLaunch(uint32_t n, uint32_t d, uint32_t s, float epsilon,
    const int32_t *offsets, RsmSolutionTiling *tiling,
    uint32_t *block_dim, uint32_t *tiling_key)
{
    (void)offsets;
    *tiling = {n, d, s, epsilon};
    *block_dim = std::min(s, kVectorCores);
    *tiling_key = 1;
}
