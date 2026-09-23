#include "rsm_baseline_tiling.h"
#include "rsm_baseline_host.h"
#include "aclrtlaunch_rsm_baseline.h"

#include <acl/acl.h>

aclError LaunchBaseline(uint32_t block_dim, aclrtStream stream,
    void *score, void *x, void *offsets, void *mean, void *rstd,
    void *logsumexp, RsmBaselineTiling *tiling, void *workspace)
{
    return ACLRT_LAUNCH_KERNEL(rsm_baseline)(
        block_dim, stream, score, x, offsets, mean, rstd, logsumexp,
        workspace, tiling);
}

#define RSM_VARIANT "baseline"
#define RSM_TILING_TYPE RsmBaselineTiling
#define RSM_WORKSPACE_BYTES(n, d, s) static_cast<size_t>(1)
#define RSM_CONFIGURE_LAUNCH_WITH_OFFSETS(n, d, s, epsilon, offsets, tiling, block, key) \
    ConfigureBaselineLaunch(n, d, s, epsilon, offsets, tiling, block, key)
#define RSM_LAUNCH(...) LaunchBaseline(__VA_ARGS__)
#include "../../tools/common/runner/runner_impl.h"
