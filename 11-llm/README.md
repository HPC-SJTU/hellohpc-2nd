# Ascend LLM Serving

> 本题建议在 Agent 辅助下完成。

上一世，你是超算队里最拼命的主力。

就在 ICT 进入最后 80 分钟倒计时、全校师生都以为你们稳拿一等奖时，意外发生了：Stage2 评测运行到第 79 分钟，D2 轮次那高达 98Ki 的恐怖上下文瞬间撑爆显存，控制台一片猩红的 OOM；更惨烈的是，量化精度发生坍塌，GSM8K 最终只答对了 1211 题——距离 1212 题的生死门槛，仅仅差了 绝望的 1 题！

评测总分瞬间清零，金奖梦碎，国奖被隔壁组顺位拿走。在暴雨倾盆的图书馆台阶上，你捧着发烫的旧笔记本电脑，道心彻底破碎……

“叮！检测到宿主强烈的保研执念，【昇腾极致性能系统】已重塑！”

一阵眩晕袭来，你猛地从机房椅子上弹起。耳边是队友熟悉的敲键盘声，屏幕右下角的倒计时依然充足，终端光标安静地跳动着，正是 Stage0 镜像刚准备构建的最初起点！
这一世，未经优化的低效算子休想再偷走你哪怕 1 毫秒的 TPOT；这一世，哪怕面对六轮连续轰炸的 98Ki 超长会话，你也绝不会让显存轻易驱逐哪怕 1 个 Token 的 KV Cache

“上一世失去的满分和国奖，这一世，我要亲手拿回来！”

## 1. 任务概览

在昇腾平台上，通过 Apptainer 部署高性能、OpenAI-compatible 的大模型推理服务。选手可选择推理框架和启动参数，使用平台挂载的模型与分配的 NPU。总分 **100 分**。

| 阶段 | 任务 | 模型 | NPU 数量 | 满分 | 时限（分钟） |
| --- | --- | --- | ---: | ---: | ---: |
| Stage0 | 构建 SIF 镜像 | / | / | 0 | 120 |
| Stage1 | Qwen 性能评测 | `Qwen3-14B` | 1 | 30 | 17 |
| Stage2 | D1 → D2 → GSM8K 统一评测 | `DeepSeek-V4-Flash-0731-w8a8` | 8 | 70 | 80 |

**Stage1 满分后才解锁 Stage2**；Stage2 使用单机 8 NPU。表格中的 `/` 表示不适用。

模型已下载到服务器：

| 模型 | 服务器路径 |
| --- | --- |
| `Qwen3-14B` | `/data/models/weights/Qwen3-14B` |
| `DeepSeek-V4-Flash-0731-w8a8` | `/data/models/weights/DeepSeek-V4-Flash-0731-w8a8` |

## 2. 提交与服务要求

### 提交文件与镜像

在 OJ 提交页面选择 `stage0`、`stage1` 或 `stage2`，并根据对应 stage 的要求提交下述两个文件之一：

| 文件 | 用途 |
| --- | --- |
| `submission/build.def` | Apptainer 镜像构建定义 |
| `submission/start.sh` | 服务启动脚本 |

- 修改 `build.def` 后，先提交 `stage0` 构建镜像。SIF 大小限制为 **20G**，模型由平台单独挂载。
- `stage1`、`stage2` 复用最近一次成功构建的 SIF，**不会隐式构建**；没有成功构建的镜像时不能运行。
- Stage0 可以联网，网络环境与 NPU 开发机一致。提交前请在开发机上测试基础镜像仓库、软件源及下载 URL 的可达性。
- **Stage1、Stage2 运行时断网**，所需软件与依赖须在 Stage0 准备好，不能依赖在线下载。

### 启动约定

Stage1、Stage2 由平台创建容器、挂载模型并配置 NPU，然后**在容器内执行本次提交的 `submission/start.sh`**，不是在宿主机上执行。

- 脚本直接使用镜像内的推理框架，启动后台服务后立即退出；不要自行调用 Apptainer 再创建容器。
- 平台保持容器及服务存活至本阶段评测结束，并负责清理。
- 服务仅监听平台提供的回环地址，使用平台设置的设备可见性。

平台注入以下环境变量，路径均为容器内可访问的路径：

| 环境变量 | 用途 | 取值或约束 |
| --- | --- | --- |
| `HELLOHPC_MODEL_PATH` | 读取模型 | 平台挂载的模型路径 |
| `HELLOHPC_MODEL_ID` | 设置 API 模型 ID | 与请求中的模型 ID 一致 |
| `HELLOHPC_SERVICE_BASE_URL` | 设置服务监听地址 | 平台提供的回环地址 |
| `HELLOHPC_SERVICE_LOG` | 写入服务日志 | 平台指定的日志路径 |
| `HELLOHPC_ASSIGNED_NPU_COUNT` | 获取 NPU 数量 | Stage1 为 `1`；Stage2 为 `8` |
| `HELLOHPC_CASE_ID` | 区分评测任务 | Stage1 为 `qwen-performance`；Stage2 为 `dpsk-stage2` |
| `HELLOHPC_STAGE` | 区分 OJ 所选阶段 | Stage1 为 `stage1`；Stage2 为 `stage2` |

### 服务接口

实现 OpenAI-compatible 的 **`GET /v1/models`** 和 **`POST /v1/completions`**，模型 ID 为 `HELLOHPC_MODEL_ID`。支持字符串及整数 token-ID 数组输入、SSE 流式输出和 token usage 统计。

性能请求固定生成规定长度；GSM8K 关闭 thinking、允许自然结束，输出上限为 1024 token。请求扩展参数及响应校验细节见 [评测入口](evaluate.py) 和 [Stage2 评测器](dpsk_benchmark.py)。

## 3. 评测流程

### 负载与调度

| 负载 | 并发 | 输入长度（token） | 输出长度（token） | 预热请求数 | 正式请求数 |
| --- | ---: | --- | ---: | ---: | ---: |
| Qwen | 1 | 约 8192 | 1024 | 1 | 5 |
| D1 Decode | 20 | 32768 | 4096 | 20 | 60 |
| D2 Coding sessions | 4 | 各轮依次为 2、32、64、96、97、98 Ki | 128 | 0 | 144 |
| GSM8K | 20 | 随题目变化 | ≤ 1024 | 0 | 1319 |

Ki = 1024 token。预热（warmup）不计分；正式（formal）请求中，Qwen 包含 1 道数学题，D1、D2 各包含 3 道。GSM8K 使用完整测试集，独立检查精度。

- 请求采用固定并发闭环调度：完成一个请求后补交下一个。
- Stage2 从一次冷启动开始，以同一服务、同一组参数依次执行 **D1 → D2 → GSM8K**，含预热共 **1543 次请求**。阶段内不得重启服务、重置缓存或换参。
- D2 包含 24 个 session，每个 6 轮，共 144 次请求。每轮输入由数据集预设，完成本轮全部 session 后进入下一轮，六轮连续计时。

### Stage2 时限

Stage2 共用 **4800 秒（80 分钟）**总预算，包含启动、就绪检查（readiness）和 GSM8K。以下各项上限同时受剩余总预算约束：

| 检查项 | 时限（秒） |
| --- | ---: |
| 就绪检查 | 1800 |
| D1/D2 单请求 | 600 |
| GSM8K 单请求 | 300 |
| GSM8K 整个精度阶段 | 1200 |

## 4. 评分与判零

### 指标定义

- **吞吐 P**：正式请求完成的输出 token 总数／整个正式阶段耗时，单位 output token/s；D2 包含轮间切换时间。
- **TTFT**：请求开始至首个非空文本 chunk 的时间，单位秒。
- **TPOT**：`1000 × (完成时间 − 首个文本 chunk 时间) / (输出 token 数 − 1)`，单位毫秒/output token。
- **p99**：全部正式请求对应指标的 nearest-rank 第 99 百分位数。

### 评分公式

令归一化吞吐为：

$$
u = \operatorname{clamp}\left(\frac{P - P_0}{P_f - P_0},\, 0,\, 1\right)
$$

通过下述门槛后：

$$
\begin{aligned}
S_{\mathrm{Qwen}} &= 20 + 10 \times u_{\mathrm{Qwen}}^{1.5} \\
S_{\mathrm{D1}} &= 30 \times u_{\mathrm{D1}}^{1.5} \\
S_{\mathrm{D2}} &= 40 \times u_{\mathrm{D2}}^{1.5} \\
S_{\mathrm{Stage2}} &= S_{\mathrm{D1}} + S_{\mathrm{D2}} \\
S_{\mathrm{Total}} &= S_{\mathrm{Qwen}} + S_{\mathrm{Stage2}}
\end{aligned}
$$

| 负载 | $P_0$（output token/s） | $P_f$（output token/s） | TTFT SLA（秒） | TPOT SLA（毫秒/output token） |
| --- | ---: | ---: | ---: | ---: |
| Qwen | 7.2 | 32.0 | / | / |
| D1 | 181.5 | 490.6 | 96.4 | 74 |
| D2 | 5.837 | 25.58 | 60.5 | 380 |

Qwen 不设 TTFT/TPOT SLA。D1/D2 使用表中 **4 位有效数字**的 $P_0$、$P_f$ 计分；吞吐测量及公式计算保留精度。参数文件为 [`data/dpsk-final/calibration.json`](data/dpsk-final/calibration.json)。

### 正确性门槛

- 数学答案按第一条独立的 `FINAL: <integer|rational|decimal>` 数值行精确判定。Qwen 须通过 1/1，D1、D2 各须通过 3/3。
- GSM8K 参考正确数为 **1275/1319**，正确数相对下降须严格小于 5%：`candidate_correct × 20 > 1275 × 19`，即至少答对 **1212 题**。达到输出上限而截断的回答判错。

### 判零范围

| 适用对象 | 触发条件 | 判零范围 |
| --- | --- | --- |
| Qwen | 正式请求未全部完成；API、输出长度或超时检查失败；数学题未通过 1/1 | Qwen |
| D1、D2 | 数学题未通过 3/3 | 对应负载 |
| D1、D2 | 正式请求的 p99 TTFT 或 TPOT 超过 SLA；任一正式请求的 TTFT 或 TPOT 超过对应 SLA 的 2 倍 | 对应负载 |
| Stage2 | 请求未完整完成；API 或性能输出长度检查失败；超时 | 整个 Stage2 |
| GSM8K | 未达到精度门槛 | 整个 Stage2 |
| 所有提交 | 审核认定作弊 | 整个提交 |

任一触发条件成立，即将对应范围记为 0 分。

## 5. 本地自测与提交

先实现 `submission/build.def` 和 `submission/start.sh`，再在题目目录下按顺序自测、打包；下发的模板没有实质内容，不能直接通过评测。

> 未优化时全量测试可能耗时较久，建议先做 smoke test。

```bash
hellohpc test --case stage0
hellohpc test --case stage1
hellohpc test --case stage2
```

- Stage0 构建 `.hellohpc-local/image.sif`，以 `status` 判断成功（分数固定为 0）；Stage1/2 复用该镜像。
- 本地 Stage0 通过宿主 Apptainer 在 `/nfs/bin/ubuntu-26.04-arm64-builder.sif` 内运行 Singularity fakeroot 构建，需要 ARM64 开发机及可读写的 `/dev/fuse`。构建允许联网；你仅可以依赖于 `build.def` 完成所有构建工作，不得放入其他的文件。临时文件、缓存与 HOME 均放在私有构建目录内并在结束时清理；仅非空且不超过 20 GiB 的成功产物会原子替换旧 SIF，失败保留旧镜像。
- 日志与报告路径会在运行时打印。
- 打包只包含第 2 节的两个文件。上传 `submission.zip` 后，在 OJ 选择要运行的阶段。
- **本地模拟不提供正式平台的网络隔离与审核，成绩以平台正式评测为准。**

更多用法与参数见 `hellohpc test --help` 和 `hellohpc pack --help`。

### 直接评测已有服务

容器内服务启动后，在容器外按对应模型运行（服务地址按实际情况替换）：

```bash
export HELLOHPC_SERVICE_BASE_URL="http://127.0.0.1:8000"

# Qwen
export HELLOHPC_MODEL_ID="Qwen3-14B"
python3 evaluate.py qwen-performance

# DeepSeek
export HELLOHPC_MODEL_ID="DeepSeek-V4-Flash-0731-w8a8"
python3 evaluate.py dpsk-stage2
```

此方式不包含服务启动耗时，仅供自测；更多参数见 `python3 evaluate.py --help`。

## 6. 竞赛规则

- 提供真实、通用的模型推理服务，使用指定模型及分配的 NPU。
- 正式评测使用独立数据。禁止打表、题目识别、答案查找和数据特化分支。
- 镜像和提交包不得包含模型权重、凭据、宿主专用路径、评测状态或缓存。
- 所有提交均接受 Agent 审核，范围包括提交文件、镜像构建链及运行证据；认定作弊时整个提交记 0 分，并保留人工追查权。
