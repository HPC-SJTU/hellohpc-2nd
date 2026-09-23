#include "rsm_baseline_tiling.h"
#include "rsm_baseline_core.h"

extern "C" __global__ __aicore__ void rsm_baseline(
    GM_ADDR score, GM_ADDR x, GM_ADDR offsets, GM_ADDR mean, GM_ADDR rstd,
    GM_ADDR logsumexp, GM_ADDR workspace, RsmBaselineTiling tiling)
{
    (void)workspace;
    RsmBaseline::ComputeCore core;
    core.Init(score, x, offsets, mean, rstd, logsumexp,
        tiling.n, tiling.d, tiling.epsilon);
    const uint32_t block = AscendC::GetBlockIdx();
    const uint32_t blocks = AscendC::GetBlockNum();
    for (uint32_t segment = block; segment < tiling.s; segment += blocks) {
        core.ProcessSegment(segment);
    }
}
