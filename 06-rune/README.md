# Rune

## 一、背景故事

在寰宇的中央，矗立着被称为"**卢恩之树**"的亘古造物。树上悬浮着无数枚**因果卢恩**——每一枚符文都刻着一段尚未发生的时间线。为了让世界平稳运转，这些卢恩必须被依次安放到树根处的"**卢恩基座**"上，由基座灌注世界之力，将其铭刻进现实。

然而，卢恩之间存在着古老的**谐律约束**：若将相互排斥的符文同时放入同一基座，谐律便会破裂，引发**分支时间坍缩**。

您将扮演**卢恩时序编织者**。在每一场**命运占卜**中，您只能看到当前已经浮现的卢恩（不可预知未来），必须在每一个**时脉节点**做出**安放序列**，将所有卢恩安全地送入基座。您的终极目标，是在不触发谐律破裂的前提下，让所有卢恩铭刻完成的那一刹那——**纪元终时**——达到最短。

**占卜法则（确定性）**：每一场占卜中卢恩浮现的次序由"**天命种子**"决定。决策时，未来的卢恩尚未显露真身，您能依靠的只有对当下树相（**Observation**）的洞察——但一切都严格确定：同样的种子与同样的编织策略，必然复现同样的世界线。

## 二、抽象后的调度问题

故事背后的本质是一个**在线确定性调度问题**，运行在模拟集群 `rune-cluster` 上：

- **任务（Task）**：每个任务属于一种任务类型 `TaskType`，携带 CPU 需求、内存预留、所需能力（capabilities）、运行工作量与安装（setup）工作量。任务可来自初始集合、定时 `Release`、完成触发的 `Derivation`，或具有 all-of 依赖的 `WorkflowTemplate` DAG。
- **工作者（Worker）**：每个 Worker 有 CPU 容量、内存容量、能力集合，以及对不同任务类型的时长倍率（`DurationMultiplier`，如 1/4 表示该 Worker 执行此类型任务的速度是基准的 4 倍）。Worker 还可能突发**停机（Outage）**。
- **失败模型**：任务尝试可能失败并重试（由 `FailurePrior` 给出先验分布）；Setup 分组可能被打断。
- **决策点（Decision Point）**：每当集群状态变化（任务释放、完成、失败、Worker 恢复等），评测器调用一次 `choose_placements` 并给出当前 `Observation`。策略返回 `DispatchDecision`：提交一组 `Placement`，或在存在未来事件时显式 `DEFER`。
- **资源安全（Resource Safety）**：被分配任务的 Worker 必须是 `available` 状态，且 Worker 需要具有任务要求的 `required_capabilities`。同一时间同一 Worker 上的内存预留之和不能超过 `memory_capacity`。违反将 **Scenario Invalid**，该场景零分。CPU 超过上限时该 Worker 上的任务按比例变慢（`progress_factor`）。
- **目标**：在**不违反资源安全**的前提下，最小化所有任务完成时的**逻辑时刻（Logical Makespan）**。
- **信息约束**：你只能看到 `PublicScenarioModel`（任务类型、Worker 规格、安全上限等静态参数）和每个决策点的 `Observation`（就绪任务、活跃尝试、Worker 状态、最近事件、各类计数）。Seed 决定的未来事件**不可观测**，但全过程完全确定——同样的 Seed 序列与同样的 Policy 必然复现同样的结果。

### 负载 Profile 字段说明

每个 Profile 是一份 TOML 文件（`profiles/<profile>.toml`），定义了该场景类别的工作负载与集群。选手只需能读懂即可，无需自行编写。字段名与 `PublicScenarioModel` 一一对应，可在 `choose_placements` 中直接读取。下例取自各 Profile 实际字段并附注释（`#` 后为说明）：

```toml
profile = "profile-01"

initial_tasks = [                        # 少量种子任务，用于首次决策点
  { id = "seed-flex-0", task_type = "FlexCpu" },
]

[[releases]]                             # 批量到达：时刻和数量均由 Seed 在公开区间内确定性采样
id = "memory-burst"
task_type = "MemBurst"
at = { min = 90, max = 140 }
count = { min = 10, max = 14 }

[[workflow_templates]]                   # 批量实例化 DAG；每个实例独立，节点等待全部 depends_on 完成
id = "pair"
instances = { min = 24, max = 28 }
nodes = [
  { id = "a", task_type = "TypeA" },
  { id = "b", task_type = "TypeB" },
  { id = "join", task_type = "Join", depends_on = ["a", "b"] },
]

[safety_limits]                          # 框架强制上限（超限场景无效）
max_generated_tasks = 512                # 场景任务总数上限
max_batch_size = 8                       # 每个决策点最多提交的 Placement 数
max_identifier_length = 96               # 标识符长度上限
max_recent_events = 8192                 # Observation.recent_events 保留条数
max_decisions = 50000                    # 决策点次数上限

[[task_types]]
id = "TypeA"
runtime_work = { min = 80, max = 120 }   # 运行工作量区间，实际值由 Seed 确定性采样
cpu_demand = 6                           # 单个任务的 CPU 需求（总和超过 cpu_capacity 只按比例减速，不判失败）
memory_reservation = 4                   # 内存预留（硬约束：Worker 上总和超过 memory_capacity 即失败）
required_capabilities = ["merge"]        # 所需能力，Worker 必须具备才能承接；部分类型带独占能力
setup_work = 20                          # 初始化工作量。**同一 Worker** 上**同一决策点**内放置的**同类型任务**
                                         # 会整组先共同完成 setup 再进入运行阶段
failure_prior = { kind = "truncated_geometric", ... }  # 失败概率：尝试可能中途失败并回到
                                         # 就绪队列重试（进度作废），至多 max_failures 次

[[workers]]
id = "worker-0-fast-a"
cpu_capacity = 18                        # CPU 容量
memory_capacity = 20                     # 内存容量
capabilities = ["merge"]                 # 具备的能力集合
duration_multipliers = [                 # 时长倍率：ratio = 1/4 表示执行该类型任务的速度是基准的 4 倍；
  { task_type = "TypeA", ratio = { numerator = 1, denominator = 4 } },
]
outage_prior = { ... }                   # （部分 Profile）停机先验：Worker 按预定节奏临时停机，停机时其上
                                         # 活跃尝试被打断回就绪队列（进度清零）、安装组作废，恢复后才可再放置

[[derivations]]                          # 派生规则：parent_type 任务完成后，按 fanout 区间
id = "join-tail"                         # 生成 successor_type 后继任务；
parent_type = "Join"
successor_type = "Tail"
fanout = { min = 1, max = 1 }
```

完整字段定义见 `rune_scheduler/api/v1.py`。

## 三、需要编写什么

**你只需要实现根目录的 `policy.py`**，提供 `Policy` 类：

每个 `Placement` 指定一个就绪任务 ID 与一个 Worker ID，均为不透明字符串。`DispatchDecision` 会与当前 `decision_id` 绑定后原子提交。`DispatchAction.DEFER` 必须携带空 placements，并推进到下一个未来事件；空的 `DISPATCH` 不等价于等待。

### 编写指引

框架代码位于 `rune_scheduler/` ，不允许修改，也不参与提交。以下是编写代码可能需要理解的两类数据结构和基本交互。

**静态模型 `PublicScenarioModel`（构造函数入参，整个 Scenario 不变）**：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `task_types` | `tuple[TaskType, ...]` | 任务类型定义（按 `id` 索引） |
| `initial_tasks` | `tuple[InitialTask, ...]` | 开局就绪的种子任务（id + task_type） |
| `releases` | `tuple[Release, ...]` | 定时批量释放规则（task_type、at/count 区间） |
| `workflow_templates` | `tuple[WorkflowTemplate, ...]` | DAG 模板（节点 depends_on 依赖） |
| `derivations` | `tuple[Derivation, ...]` | 完成触发规则（parent_type → successor_type，fanout 区间） |
| `workers` | `tuple[WorkerSpec, ...]` | Worker 静态规格（按 `id` 索引） |
| `safety_limits` | `SafetyLimits` | 框架硬上限（如 `max_batch_size`：单个决策点最多提交的 Placement 数） |

其中 `TaskType` 携带 `runtime_work` / `setup_work`（工作量区间与初始化工作量）、`cpu_demand`、`memory_reservation`、`required_capabilities`、`failure_prior`（失败概率与最大重试次数）；`WorkerSpec` 携带 `cpu_capacity`、`memory_capacity`、`capabilities`、`duration_multipliers`（各任务类型的时长倍率）以及可选的 `outage_prior`（停机节奏）。

**动态观测 `Observation`（每个决策点入参）**：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `decision_id` | `int` | 当前决策点编号，`DispatchDecision` 与之绑定 |
| `logical_time` | `int` | 当前逻辑时刻 |
| `ready_tasks` | `tuple[ReadyTask, ...]` | 当前等待放置的任务（`task_id`、`task_type`、尝试/失败计数、工作流归属） |
| `active_attempts` | `tuple[ActiveAttempt, ...]` | 正在执行的任务尝试（所在 worker、阶段、已消耗 service units 等） |
| `setup_groups` | `tuple[SetupGroup, ...]` | 进行中的安装组（同一 worker 同类型任务整组共享 setup） |
| `workers` | `tuple[WorkerState, ...]` | Worker 实时状态：`available`、`cpu_demand`、`memory_reserved`、`progress_factor`、活跃尝试与安装组引用 |
| `recent_events` | `tuple[RecentEvent, ...]` | 最近发生的事件（任务释放/完成/失败/中断、Worker 停机/恢复、安装组完成/打断） |
| `generated_task_count` 等计数 | `int` | 已生成任务数、已完成任务数、失败/中断尝试数、停机次数 |

**放置一个任务前必须满足的资源安全检查**（违反即 Scenario 无效、计零分）：

1. Worker `available` 为真；
2. Worker 的能力集合包含任务类型的全部 `required_capabilities`；
3. 放置后该 Worker 上内存预留之和不超过 `memory_capacity`；
4. CPU 超容量不会失败，只会使 Worker 上所有任务按比例减速（`progress_factor`）。

提交前框架还会强制：单次决策的 Placement 数 ≤ `max_batch_size`，且引用的 task_id 必须在 `ready_tasks` 中。

**代码骨架**

```python
from rune_scheduler.api.v1 import (
    DispatchAction,
    DispatchDecision,
    Observation,
    Placement,
    PublicScenarioModel,
)


class Policy:
    def __init__(self, model: PublicScenarioModel) -> None:
        # 每个 Scenario 开始时构造一次；可在此把 model 里的 task_types / workers
        # 建成 {id: 对象} 索引、缓存 max_batch_size、初始化你的状态。
        ...

    def choose_placements(self, observation: Observation) -> DispatchDecision:
        # 每个决策点调用一次：
        # 1. 遍历 observation.ready_tasks，按 observation.workers 过滤可放置位置
        #    （available、capabilities、memory 容量，CPU 超容只减速不失败）；
        # 2. placements 元素是 Placement(task_id, worker_id)，数量 ≤
        #    model.safety_limits.max_batch_size；
        # 3. 返回 DispatchDecision(DispatchAction.DISPATCH, placements)；
        #    注意：如果类型是 DISPATCH 但是 placements 为空，会被拒绝
        #    如需等待未来事件，返回 DispatchDecision(DispatchAction.DEFER, ())。
        ...
```

完整字段定义见 `rune_scheduler/api/v1.py`。

**只读文件**（评测资产，打包时会自动排除，篡改无效）：

- `run_scenario.py`、`rune_scheduler/**/*.py` — 调度器框架与场景执行器
- `profiles/*.toml` — 18 份固定的负载 Profile
- `bin/rune-cluster-x86`、`bin/rune-cluster-aarch64` — 按平台选择的预编译集群可执行文件（无需 Rust/Cargo）

修改、删除或绕过以上文件不会提高分数，任何注入或干扰评测器的行为按违规处理。

## 四、如何自测

环境要求：Linux x86_64/aarch64、Python 3.13、兼容 `hellohpc/v0.4.0` 的 HelloHPC CLI。

**先根据你所在的平台选择二进制文件**

```sh
cp bin/rune-cluster-aarch64 bin/rune-cluster # 比赛集群或 Apple Silicon 等 arm 平台
cp bin/rune-cluster-x86 bin/rune-cluster # 本地 x86 平台
```

```bash
hellohpc test \
  --set seed_count=1 \
  --set seed_key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f \
  --set shards=4 \
  --set concurrency=64
```

- `seed_count`：每个公开 Profile 运行多少个确定性 Seed 并取均值，取值 `1`–`64`；`1` 用于快速本地验证，`64` 即完整训练评分。
- `seed_key`：64 位十六进制种子密钥，派生每个 Profile 的 Seed 集合。相同 key、count 与 Policy 可以复现相同结果。
- `shards`：每个 Profile 内部把 Seed 集分给 N 个本地进程并行执行（无结果影响，仅提高测试速度。默认 `1`）。
- `concurrency`：进程内部的 asyncio 并发度（无结果影响，仅提高测试速度。默认 `16`）。
- 工作流会以你的 `policy.py` 跑满 18 个 Profile，逐 Profile 求有效 Logical Makespan 的均值后再计分。

**运行单个 profile：**

```bash
python3 run_scenario.py \
  --profile profiles/profile-01.toml \
  --seed-count 1 \
  --zero-time 1414
```

`--shards N` 将 Seed 集切成 N 份，由 N 个子进程并行运行后按 `seed_ordinal` 合并。上例 `--seed-count 1` 单 Seed 时加 `--shards` 无意义。

`--concurrency N` 在进程内部调整 asyncio 并发度，上例仅单 profile 单种子无意义。

`--zero-time` 取 `problem.yaml` 中对应 Profile 的 `zero_time`；输出 JSON 中的 `candidate_makespan` 与 `scenario_valid` 即单项结果。各 Profile 的 `full_time` / `zero_time` 见下一节的表。

`--profile` 接受任意可读的 TOML 文件路径；场景名称取自 TOML 顶层 `profile` 字段，不再限制为内置文件名。

## 五、单场景 Live 调试

> 此功能可以更好地显示模拟器的运行过程，使用时需要安装 `rich` 依赖，对于完成题目非必须。

Live 入口 `run_live.py` 会显示 TUI 界面。可以用于要观察一个 Profile、一个 Seed 的运行情况：

```bash
uv sync

# 带 TUI 显示地运行 seed-key 下的第一个种子，使用 profile1
uv run python run_live.py \
  --profile profiles/profile-01.toml \
  --seed-key 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f \
  --seed-ordinal 0
```

默认 `--display-rate 20`，单位是 Logical Time units/墙钟秒；指定 `--display-rate 0` 可只刷新状态、不增加等待。每个决策点的等待最多 2 秒。

可以使用 `--no-live` 关闭界面并保留单场景执行。（另一种运行单 profile 单 seed 的方式，建议同时 `--display-rate 0` 关闭等待）。

## 六、测评方式与满分性能

- **评分单元**：18 个公开 Profile（`profile-01` … `profile-18`），每个 Profile 各跑 `seed_count`（正式评测默认 `64`）个由 `seed_key` 派生的确定性 Seed，取有效 Makespan 均值。
- **单个 Profile 得分**：HelloHPC 对每个 Profile 的均值应用一次 `builtin/score-ratio`。均值达到 `full_time` 得满分，劣化到 `zero_time` 得零分；介于两者之间按

  $$
  s = \operatorname{clamp}\left(\frac{F(B-x)}{x(B-F)}, 0, 1\right)
  $$

  给分（$F=$ `full_time`，$B=$ `zero_time`，$x=$ 均值）。该曲线在靠近 $F$ 时更陡：同样的 Makespan 改进，越接近满分加分越多。18 个 Profile 按 matrix `score` **加权**合成总分（各 Profile 满分权重见下表，总分 337）。
- **无效即零**：某 Profile 下任意一个采样 Scenario 无效（违反资源安全、Policy 抛异常、返回畸形 Placement 等 Policy Violation），该 Profile 记零分；其余 Profile 继续评测。评测器/进程/传输/集群启动缺陷属 Infrastructure Failure，不计为选手成绩。

| Profile | 权重（score） | full_time（满分） | zero_time（零分） |
| --- | ---: | ---: | ---: |
| profile-01 | 30 | 1240 | 1490 |
| profile-02 | 2 | 1760 | 2120 |
| profile-03 | 1 | 5700 | 6850 |
| profile-04 | 1 | 2200 | 2650 |
| profile-05 | 1 | 940 | 1130 |
| profile-06 | 30 | 1070 | 1290 |
| profile-07 | 30 | 2600 | 3130 |
| profile-08 | 30 | 1330 | 1600 |
| profile-09 | 2 | 1370 | 1650 |
| profile-10 | 30 | 1760 | 2120 |
| profile-11 | 30 | 1360 | 1640 |
| profile-12 | 30 | 1240 | 1490 |
| profile-13 | 5 | 1340 | 1610 |
| profile-14 | 20 | 1820 | 2190 |
| profile-15 | 30 | 1580 | 1900 |
| profile-16 | 5 | 1460 | 1760 |
| profile-17 | 30 | 4000 | 4810 |
| profile-18 | 30 | 5400 | 6490 |

- 正式测评会打乱 profile 命名和顺序，同时浮动约 5% 数值，以及指定不同的 `seed_key` 。具体性能指标以测评结果为准。
- 任何通过注入、干扰、监听尝试通过不正当手段探测测评种子和 profile 数值等隐藏信息的行为将被视为违规。
- 不允许通过反向**查表**（哈希、profile id 、大量硬编码 profile 数据进行判断等方式）针对特定 profile 设定策略
- 不允许 Hack 测评种子运行时数据，不允许**利用预生成运行时数据**进行机器学习等方式建立策略
- 不允许通过 Hack 和利用框架漏洞绕过任务完成检查，非法完成运行。

## 七、提交

提交**只包含 `policy.py`**。调度器框架、Profile 与集群可执行文件均为只读评测资产。你可以在*提交*页面直接复制你的代码。

## 八、一些提示

- 本题采用的是一个模拟器，你可以在**个人电脑**上选用对应的 `rune-cluster` 二进制文件完成此题而不需要连接集群。
- 由于模拟过程存在**运行时随机性**，建议在必要时使用 `hellohpc test` 获取完整评分集合（每个 profile 运行全部 64 种子）以获得较为准确的性能指标。
