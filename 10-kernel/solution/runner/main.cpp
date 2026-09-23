#include "rsm_solution_tiling.h"
#include "rsm_solution_host.h"
#include "aclrtlaunch_rsm_solution.h"

#include <acl/acl.h>

aclError LaunchSolution(uint32_t block_dim, aclrtStream stream,
    void *score, void *x, void *offsets, void *mean, void *rstd,
    void *logsumexp, RsmSolutionTiling *tiling, void *workspace)
{
    return ACLRT_LAUNCH_KERNEL(rsm_solution)(
        block_dim, stream, score, x, offsets, mean, rstd, logsumexp,
        workspace, tiling);
}

#define RSM_VARIANT "solution"
#define RSM_TILING_TYPE RsmSolutionTiling
#define RSM_WORKSPACE_BYTES(n, d, s) static_cast<size_t>(1)
#define RSM_CONFIGURE_LAUNCH_WITH_OFFSETS(n, d, s, epsilon, offsets, tiling, block, key) \
    ConfigureSolutionLaunch(n, d, s, epsilon, offsets, tiling, block, key)
#define RSM_LAUNCH(...) LaunchSolution(__VA_ARGS__)
#include "../../tools/common/runner/runner_impl.h"
