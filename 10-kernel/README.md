# Ragged Softmax Moments

本题要求使用 **Ascend C** 优化运行于 **Ascend 910B3 NPU** 的算子。算子对变长分段分别计算加权均值、加权方差和 `logsumexp`，在满足正确性要求的前提下优化执行时间。

`solution/` 是待修改的初始实现，`baseline/` 是固定的性能基线，不得修改。输入生成、结果检查和计时由仓库提供的工具完成。本文中的路径均相对于仓库根目录。

## 1. 算子定义

输入包括 `N` 行、`D` 列的矩阵 `x`，每行一个分数 `score`，以及描述分段边界的 `offsets`。共有 `S` 个分段，第 `s` 段包含行 `offsets[s]` 至 `offsets[s+1] - 1`。

每个分段独立计算。设该段最高分为 $m$，则行 $i$ 的 Softmax 权重为

$$
w_i = \frac{\exp(\mathrm{score}_i-m)}{\sum_j\exp(\mathrm{score}_j-m)}.
$$

同一行的权重用于该行的所有列。对每一列 $d$，输出

$$
\begin{aligned}
\mathrm{mean}_d &= \sum_i w_i x_{i,d},\\
\mathrm{var}_d &= \sum_i w_i(x_{i,d}-\mathrm{mean}_d)^2,\\
\mathrm{rstd}_d &= \frac{1}{\sqrt{\mathrm{var}_d+\epsilon}}.
\end{aligned}
$$

同时输出该段的

$$
\mathrm{logsumexp}=m+\log\left(\sum_i\exp(\mathrm{score}_i-m)\right).
$$

减去最高分用于避免指数溢出，不改变计算结果。`var` 为中间量，不输出。最大值、求和及统计量必须使用 FP32 或更高精度计算；算法和求和顺序不限，但结果必须满足下文的精度要求。

### 输入与输出

所有数组连续存储，采用行主序。`x[i,d]` 的偏移为 `i * D + d`，`mean[s,d]` 和 `rstd[s,d]` 的偏移为 `s * D + d`。

| 名称 | 方向 | 类型 | 形状 | 含义 |
|---|---|---|---|---|
| `score` | 输入 | `float16` | `[N]` | 每行的分数 |
| `x` | 输入 | `float16` | `[N, D]` | 输入矩阵 |
| `offsets` | 输入 | `int32` | `[S + 1]` | 分段边界，最后一项为 `N` |
| `mean` | 输出 | `float32` | `[S, D]` | 加权均值 |
| `rstd` | 输出 | `float32` | `[S, D]` | `1 / sqrt(方差 + epsilon)` |
| `logsumexp` | 输出 | `float32` | `[S]` | 每段分数的 log-sum-exp |

`epsilon` 是本次调用共用的 `float` 参数，默认值为 `1e-5`，但实现必须使用调用时传入的值。

例如，`offsets = [0, 2, 3]` 表示两段：第一段包含第 0、1 行，第二段包含第 2 行。若第一段两行的 `score` 均为 0，且对应列的值分别为 1 和 3，则权重均为 `0.5`，均值为 2，方差为 1；单行分段的权重为 1，方差为 0。

## 2. 输入范围与正确性

| 项目 | 范围 |
|---|---|
| 分段数 `S` | 1 ～ 8192 |
| 总行数 `N` | `S` ～ 1,048,576 |
| 列数 `D` | 32、48、64、80、96、128、160、192、256 |
| `N × D` | 不超过 16,777,216 |
| 单段行数 | 1 ～ 16384 |
| `epsilon` | 3e-6 ～ 1e-2 |

`offsets[0] = 0`，`offsets[S] = N`，且严格递增。`score` 和 `x` 为有限 FP16 值，不包含 NaN 或 Inf。分段边界是运行时输入，同一形状可能对应不同的分段方式。

测试会覆盖低权重行具有大幅值、长段稀疏扰动、不同分段长度、权重、动态范围和 `epsilon` 的情况。三个输出必须全部写满且为有限值，`rstd` 必须严格大于 0。统计量的计算应避免消减误差和微小更新的舍入丢失；正确性以仓库提供的公开和正式测试结果为准。

## 3. 开发、测试与提交

### 环境与测试

在包含本文件和 `problem.yaml` 的仓库根目录执行：

```bash
source /nfs/scripts/kernel-dev.sh
hellohpc test --stream-output --artifacts-dir artifacts/runs
```

测试依次执行合规检查、编译、公开正确性检查、运行时鲁棒性检查和公开性能测试。初始实现与基线的计算代码一致，性能应接近基线。

测试结果保存在 CLI 输出的 `Artifacts:` 路径（通常为 `artifacts/runs/<run-id>/`），主要文件包括：

- `public_correctness_results.json`：公开正确性结果；
- `runtime_robustness_results.json`：运行时鲁棒性结果；
- `public_benchmark.json`：公开性能耗时、加速比和 `G_ref_public`；
- `public_benchmark_raw/`：原始计时记录；
- `logs/` 和 `hellohpc-logs/`：测试日志。

### 可修改文件

以下路径相对于 `solution/`。除这些文件外，不得修改其他代码、脚本、评测配置或工具；不得新增源文件、头文件或符号链接。README 可以修改，但不参与打包和计分。

| 文件 | 作用 |
|---|---|
| `op_kernel/rsm_solution_core.h` | 核心计算逻辑 |
| `op_kernel/rsm_solution.cpp` | Kernel 入口 |
| `op_kernel/rsm_solution_tiling.h` | Host 与 Kernel 共用的参数结构 |
| `op_host/rsm_solution_host.cpp`、`op_host/rsm_solution_host.h` | Host 配置 |
| `runner/main.cpp` | Host 配置与 Kernel 启动的连接代码 |
| `CMakeLists.txt` | 构建配置 |

CPU 侧配置代码称为 Host，NPU 上执行的函数称为 Kernel；Host 通过 Tiling 参数结构传递形状和任务划分信息。临时内存和多 Kernel 启动接口见 `solution/README.md`。

每次修改后重新运行测试。测试通过后执行：

```bash
hellohpc pack --output artifacts/rsm_submission.zip
```

将生成的 `artifacts/rsm_submission.zip` 上传到赛事评测平台。

## 4. 实现限制

- 输入只读。只能使用 Ascend C 基础构件，不得调用 ACLNN、ATB、框架算子或现成的高阶 Softmax、Norm、Moments、LogSumExp 算子。
- 额外设备全局临时内存（workspace）最多 4 MiB（4,194,304 字节）。该限制不包括 Unified Buffer、寄存器、Local Memory 和 Tiling 结构。
- Host 可以读取本次调用的 `N`、`D`、`S`、`epsilon` 和 `offsets`，但不得通过完整 `(S,N,D)` 组合、用例 ID、随机种子、预置分段表或预计算答案识别用例。
- 一次 dispatch 可以启动多个 Kernel，并可在本次调用内读写输出和 workspace。所有设备计算必须在计时区间内完成，结束时三个输出必须完整正确。
- 每次调用必须独立计算，不得依赖输出或 workspace 的初值、上一次调用的结果、文件或 Host 全局缓存。本次调用上传的任务元数据可以读取。
- `runner/main.cpp` 必须恰好包含一次官方 `runner_impl.h`，不得自定义 `main`、读回设备结果或改变计时流程。`CMakeLists.txt` 不得执行外部进程、写文件或添加自定义命令。

## 5. 性能与计分

总分为 100 分，全部来自性能。未通过正确性、鲁棒性或合规检查时，得分为 0。

### 测量与用例

基线和提交在同一块 NPU 上以独立进程交替测量，每次测量使用单条 stream。计时包含一次 dispatch 启动的全部 Kernel，不包含输入准备与首次上传、Host 配置、Tiling 和任务元数据生成与上传、输出回传及正确性比较。重复迭代复用同一组输出和 workspace。

正式性能评测包含 15 个用例：5 个公开用例和 10 个隐藏用例。用例分为五类工作负载，每类包含 3 个用例并占综合性能的 20%。公开性能集每类选择一个代表用例，其他性能用例只在私有评测中使用。

| 性能用例 | S | N | D | 工作负载 | 正式权重 |
|---|---:|---:|---:|---|---:|
| `public_perf_small_s` | 4 | 8,192 | 192 | long_segment | 1/20 |
| `public_perf_many_short` | 2,048 | 16,384 | 64 | short_segment | 1/20 |
| `public_perf_medium` | 128 | 16,384 | 96 | general | 1/15 |
| `public_perf_power` | 256 | 32,768 | 128 | boundary_and_pressure | 1/15 |
| `public_perf_large_d` | 1 | 8,192 | 256 | large_d | 1/15 |
| `hidden_perf_01` | 隐藏 | 隐藏 | — | large_d | 1/15 |
| `hidden_perf_02` | 隐藏 | 隐藏 | — | short_segment | 1/10 |
| `hidden_perf_03` | 隐藏 | 隐藏 | — | general | 1/15 |
| `hidden_perf_04` | 隐藏 | 隐藏 | — | long_segment | 1/10 |
| `hidden_perf_05` | 隐藏 | 隐藏 | — | general | 1/15 |
| `hidden_perf_06` | 隐藏 | 隐藏 | — | boundary_and_pressure | 1/15 |
| `hidden_perf_07` | 隐藏 | 隐藏 | — | large_d | 1/15 |
| `hidden_perf_08` | 隐藏 | 隐藏 | — | long_segment | 1/20 |
| `hidden_perf_09` | 隐藏 | 隐藏 | — | short_segment | 1/20 |
| `hidden_perf_10` | 隐藏 | 隐藏 | — | boundary_and_pressure | 1/15 |

公开用例的 `epsilon` 均为 `1e-5`，并参与正确性检查；隐藏用例满足第 2 节的数据范围，但不公开具体输入和逐例结果。

第 `i` 个用例的加速比为

$$r_i = \frac{\text{baseline 耗时}}{\text{solution 耗时}}.$$

评测先在每个工作负载 family 内按用例权重归一化计算几何平均，再对五个 family 等权计算综合加速比。设 $C_f$ 是 family $f$ 的用例集合，$\alpha_i$ 是用例的正式权重，则

$$G_f = \prod_{i\in C_f} r_i^{\alpha_i / \sum_{j\in C_f}\alpha_j},\qquad
G = \prod_{f=1}^{5} G_f^{1/5}.$$

本地 `G_ref_public` 仅使用 5 个公开用例，五类工作负载各占 20%；因此它仍然是公开参考值，不等于包含隐藏用例的正式成绩。

### 分数

满分线初始为 8 倍；若有效榜首的综合加速比超过 8 倍，则满分线提高到榜首的加速比。设满分线为 $H$、提交的综合加速比为 $G$：

$$
P = \begin{cases}
0, & G\le1,\\
100\left(\dfrac{G-1}{H-1}\right)^{1.5}, & 1<G\le H.
\end{cases}
$$

榜首超过 8 倍后，所有有效提交按新的满分线重新计算；比赛结束时按最终榜首结算。
