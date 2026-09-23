# Cheatsheet

小 Q 和蓝色大肥鱼即将参加一场 CPU 算子优化考试。

这是一场开卷考试。考场会提供题目、初始代码、编译器和测试工具，考生可以修改算子实现，也可以反复验证正确性和性能。但由于她们两个什么都不会，于是请求你帮忙准备一张带进考场的 cheat sheet，告诉她们在考试时应该怎么做。

你手头只有一份资料，因此只能交给其中一位带进考场。考试也是从两个算子中选择一个进行优化，你可以提前帮她们做选择。

不过，允许携带的资料长度有限，资料长度会影响得分，并且超过一定长度的资料，不能带入考场。

考试时间同样有限，任何一位考生都需要在规定的时间和轮数内完成优化。

资料中可以记录优化方法和工作流程，但不能直接写入完整的算子实现，也不能针对正式测试数据准备答案。这份参考资料就是你提交的 `SKILL.md`。

聪明的你能帮助她们拿到满分吗？

## 任务

Cheatsheet 是一道 Agent Skill 题目。选手从 Qwen3.8-27B（`qwen3.8-27b`）和 DeepSeek V4 Flash（`deepseek-reasoner`）中选择一个模型，并选择一个算子，提交指导 Agent 优化的 `SKILL.md`，还可提供 `references/` 补充资料。

每次评测使用同一模型、算子和资料进行两次独立运行，取整次性能分较高的结果。模型推理强度固定为 `medium`。每次运行分为：

- **优化阶段**：最多 16 轮、20 分钟，Agent 可以修改代码并测试正确性和性能。
- **收尾阶段**：在同一会话中继续，最多 4 轮、10 分钟。Agent 会被要求停止新优化，恢复并验证已测试正确的最佳代码及配套编译选项；没有有效优化版本则恢复初始实现。评测器不会自动回滚。

运行开始时，提示词中包含算子说明、初始代码、平台与编译参数，以及初始实现的性能摘要。Agent 通过工具加载 Skill，按需读取补充资料，并使用提供的工具编译、测试和分析性能；不能访问测试源码、参考实现或 `spec.yaml`。

运行结束后，评测器重新检查最终代码的正确性，并独立测量性能。

## 两个算子

可选算子如下：

| 算子 | 任务 |
| --- | --- |
| `fft` | 计算批量复数快速傅里叶变换 |
| `bitmatrix` | 选取位矩阵中的部分行，统计各列中 1 的数量 |

各算子的详细说明位于：

```text
src/tasks/<operator>/TASK.md
```

调试与评测使用相同的函数接口、数据布局和正确性要求；完整输入约束以各算子的 `TASK.md` 为准。

## 提交内容

可以提交以下文件：

```text
submission.yaml           必需：选择算子和模型
SKILL.md                  必需
references/**/*.md        可选
```

仓库中的 `SKILL.md` 是一份 baseline，可以直接在此基础上修改。`references/` 可选，可用于存放供 Agent 按需读取的补充资料。

在 `submission.yaml` 中指定算子和模型，例如：

```yaml
operator: bitmatrix
model: qwen3.8-27b
```

`operator` 可选 `bitmatrix` 或 `fft`；`model` 可选 `qwen3.8-27b` 或 `deepseek-reasoner`。两个字段均为必需，不接受其他字段。

`SKILL.md` 顶部保留 Skill 元信息：

```yaml
---
name: cpu-hpc-skill
description: CPU 算子优化指南。
---
```

- `name` 必须为 `cpu-hpc-skill`，`description` 必须为非空字符串；
- `SKILL.md` 和 `references/` 下的文件必须为 UTF-8 编码的纯文本 Markdown 文件。`references/` 支持子目录；
- `SKILL.md` 和 `references/` 合计**不得超过 1,200 tokens**；`submission.yaml` 不计入资料长度，也不作为资料传给 Agent；
- `references/` 目录中的所有文件均计入长度，无论是否被 `SKILL.md` 引用；
- 不得将 Skill 用作分发完整或近完整算子实现、可执行脚本，或经过压缩、编码、加密、混淆等处理后可还原的代码或二进制数据；
- 不得修改、绕过或以任何方式影响 ABI、测试程序、计时方式及评分逻辑；
- 不得通过拆分文件、隐藏内容、外部链接、编码嵌入或其他方式规避上述限制。

所有提交将在赛后接受人工复核。**经确认存在违规的，主办方将在通知参赛者后将该提交记为零分**。

## 运行环境

正式评测运行在 Kunpeng 920 ARM64 Linux 平台，使用评测容器提供的 GCC。任一算子均在单个 CPU 核心上以单线程运行。默认编译参数为：

```text
g++ -O3 -march=native -fopenmp -std=c++17 -funroll-loops
```

## Agent 工作环境

Agent 可以修改：

```text
kernel.cpp
compile_options.txt
```

`compile_options.txt` 用于指定额外编译参数，仅作用于算子代码。外部路径、编译器插件、额外链接库和插桩运行时会被拒绝。

算子工作区提供以下命令：

```bash
bash tools/test_candidate.sh
bash tools/build.sh
bash tools/profile.sh
bash tools/vec_report.sh
```

候选版本的标准验证命令是 `bash tools/test_candidate.sh`；其他脚本用于编译或性能诊断。

Agent 应在结束前把公开测试中表现最好的有效版本留在工作区。

一轮指一次模型响应，读取文件、被拒绝的工具调用和纯文字回答也计入轮数。评测器会提示剩余轮数；优化阶段提前以文字结束或遇到输出长度限制时，在原会话、剩余轮数及原时间上限内继续。相同请求的重试不重复计数。

收尾阶段只用于恢复和验证已测试正确的代码及编译选项，不再进行新优化。若没有优化版本通过，则恢复 starter；评测器不会自动回滚文件，最终留下的文件才参与计分。

编译、正确性和计时工具会提供分阶段诊断，包括错误位置、运行错误和计时噪声。诊断不改变正确性标准或测量有效性要求。

## 本地测试

在比赛提供的环境中，通过 HelloHPC CLI 测试提交。

首次使用时，复制 `.env.example`，并在 `.env` 中填写主办方提供的 OpenCode、ripgrep 绝对路径和模型凭据。若运行环境已配置这些变量，可跳过此步。

```bash
cp .env.example .env
chmod 600 .env
```

`hellohpc test` 会自动加载 `.env`，其中的设置优先于已有环境变量。该文件不进入提交包，测试工作区中的临时副本会在使用它的步骤结束后删除。

在仓库根目录执行：

```bash
hellohpc validate
hellohpc test --output result.json --keep-workspace
```

`validate` 检查题目配置；`test` 校验提交，并使用 `submission.yaml` 中指定的模型和算子进行两次独立运行。每次运行完成优化与收尾后，评测器独立测量性能并计分。

本地测试使用公开算例，OJ 使用正式算例；两者的函数接口与正确性要求相同。

最终结果写入 `result.json`。`--keep-workspace` 保留测试工作区，CLI 会显示其位置；运行日志、最终代码与测量记录归档在该工作区的 `.hellohpc/cheatsheet-runs/` 下。运行目录由评测器自动创建，无需手动配置。

## 评分方式

先根据所选算子的正确性和性能计算性能分 $P$，再根据资料长度乘以相应倍率，换算为百分制成绩。

### 正确性

各测试算例独立计分。未通过正确性检查，或未获得有效计时结果的算例，得 0 分，其余算例正常计分。编译失败时，该次运行得 0 分。

### 加速比

在同一次独立评测中，以同一节点上的初始实现（starter）为基准，计算候选实现的加速比 $S$。starter 在候选实现测量前后各测一次，取较短时间作为基准：

```math
S=\frac{t_{\mathrm{starter}}}{t_{\mathrm{candidate}}}.
```

### 测试算例得分

每个测试算例设定两个加速比阈值，满足 $T>B>0$：

- $B$：零分阈值（Baseline）。
- $T$：满分阈值（Target）。

通过正确性检查且获得有效计时结果的算例，按以下公式计分：

```math
s(S)=\max\left(0,\min\left(1,\frac{S-B}{T-B}\right)\right).
```

### 性能分

所选算子进行两次独立的 Agent 运行。每次运行的各算例得分（包括零分）取算术平均，再取两次运行中较高的分数作为性能分 $P$。

### Skill 长度调整

设 Skill 和补充资料的总长度为 $N$ tokens，得分倍率如下：

| 总长度 | 得分倍率 |
| --- | ---: |
| 不超过 400 tokens | 1.5 |
| 800 tokens | 1.0 |
| 1,200 tokens | 0.5 |

表中相邻节点之间采用线性插值：

```math
m(N)=1.5-\frac{\max(N,400)-400}{800},\qquad N\le1200.
```

超过 1,200 tokens 的提交会被拒绝。

### 最终得分

```math
\mathrm{Final}=100\times\min\left(1,P\cdot m(N)\right).
```

### Skill 长度计算

`SKILL.md` 和 `references/` 的资料长度由仓库内固定的 tokenizer 统一计算，以提交校验结果为准；`submission.yaml` 不计入长度。`hellohpc test` 会在启动 Agent 前检查提交格式和长度限制。

### 公开测试评分参数

下表用于本地公开测试，`B`、`T` 均为相对于 starter 的加速倍数；OJ 使用正式算例及对应评分参数。

- FFT 的 `16x256` 表示 16 组、每组长度为 256。
- Bitmatrix 的 `1024:d50` 表示矩阵大小为 `1024×1024`，约 50% 的行被掩码选中。

| 算子 | 公开算例 | B | T |
| --- | --- | ---: | ---: |
| FFT | `16x256` | 2 | 3.9 |
| FFT | `8x1024` | 2 | 4.1 |
| Bitmatrix | `1024:d50` | 1 | 84 |
| Bitmatrix | `512:d10` | 1 | 62 |

## 打包提交

完成测试后，在仓库根目录运行：

```bash
hellohpc pack
```

生成的 `submission.zip` 包含 `submission.yaml`、`SKILL.md` 和可选的 `references/` 补充资料。

`hellohpc pack` 只负责打包，不代替 `hellohpc test` 中的提交校验。
