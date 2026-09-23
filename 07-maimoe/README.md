# MaiMoe

## 题目背景

小K想要玩MaiMoe DX，但是机厅太多人了，他根本上不了机。所以他在网上找到了KstroDX模拟器，这下小K可以在平板上玩MaiMoe了！

在玩MaiMoe前，小K需要将谱面数据导入模拟器。模拟器对每一张谱面都要进行读写与安全性核验，而小K的平板真的是太卡了，等了一个下午也没有将数据全部导入。

小K想尽快在平板上玩到MaiMoe，因此他特意来到机厅找到还在排卡的你，想让你帮他优化这个模拟器的工作效率。事成之后，小K打算请你一个暑假的游戏币。

## 问题描述

本项目是一个使用 C++20 实现的 MaiMoe 谱面导入与安全检查模拟器。程序读取数据集中的
谱面文件，完成解析、检查、状态计算与渲染，并将结果写入输出目录。你的目标是让程序的运行时间尽可能短。

你仅被允许修改以下文件：

```text
solution/Kernel.cpp
solution/KstroParam.toml
```

你的代码将被运行在 arm 平台上，至多可以使用 8 核 CPU 。

## 输入与输出

样例数据位于`/vault/public/xflops/maimoe_data`

每组样例包含 `manifest.mmf` 与 `charts/` 谱面文件，目录结构如下：

```text
Data/Sample1/input/       # 算例 1 输入
Data/Sample1/expected.bin # 算例 1 正确性参照
Data/Sample2/...
Data/Sample3/...
Data/Sample4/...
```

每张谱面必须输出三个普通文件：

```text
charts/<bucket>/<id16>.frame_begin.rgba
charts/<bucket>/<id16>.frame_end.rgba
charts/<bucket>/<id16>.result.mmr
```

其中 `<bucket>` 为谱面 ID 低 8 位的两位十六进制目录（共 256 个，均须存在），`<id16>` 为 16 位十六进制谱面 ID。输出树不允许额外或缺失的文件。

保证评测数据集与样例数据集性质一致。

## 计时与评分

评测以 `maimoe` 进程的 wall time 为性能指标，不包含编译、输出正确性检查或评测框架的其他开销。每个正式算例默认执行 1 次 warmup 和 3 次正式测量，使用 3 次正式测量的平均时间评分。编译失败、程序运行失败、超时或 checker 未通过时，该算例得 0 分。

| 算例 | 分数占比 | 满分时间(ms) | 基础时间(ms) |
| ---  | --- | --- | --- |
| 1 | 10% | 100 | 400 |
| 2 | 20% | 160 | 2333 |
| 3 | 20% | 165 | 2333 |
| 4 | 50% | 650 | 11451 |

每个算例的分数计算公式为

$$\min(\max( \frac{基础时间-运行时间}{基础时间-满分时间},0),1)^2 \times 100$$

## 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target maimoe maimoe_kernel maimoe_check -j
```

## 运行

```bash
./build/maimoe <dataset_dir> <output_dir> [--config <toml_path>]
```

默认从当前目录读取 `solution/KstroParam.toml` 作为运行配置；也可用 `--config` 指定其他参数配置路径

## 正确性测试

```bash
./build/maimoe_check <dataset_dir> <expected.bin> out
```

正确时，checker 将输出处理统计信息并以状态码 0 退出。

失败时，checker 会向标准错误输出具体原因，并以非零状态退出。例如：

```text
maimoe_check: output directory does not exist
```

## 快速评测

`tools/` 内提供自测脚本，评测口径与正式评测一致（运行在系统临时目录，结束后自动清理；结果与报告写回 `tools/report/`）。

评测数据由附件提供，默认从 `Data/` 读取（也可用 `--data` 指定目录）：

```bash
bash tools/build.sh                          # 构建脚本
bash tools/test.sh                           # 全量自测 Sample1..4（repeat=3 warmup=1）
bash tools/test.sh --sample 3                # 只测第 3 个算例
bash tools/test.sh --sample 1,4              # 测 Sample1 与 Sample4
bash tools/test.sh --config <toml>           # 用指定 Tune 配置（默认 solution/KstroParam.toml）
bash tools/test.sh --data <dir> --repeat 5   # 自定义数据目录与测量次数
```

评测数据会缓存到系统临时目录（首次复制后复用，加速重复评测）。评测数据附件更新/替换时、数据缓存损坏或怀疑不完整时，需要清除缓存后重测。

```bash
bash tools/test.sh --refresh   # 运行前强制重建数据缓存
rm -rf /tmp/maimoe-data-cache-*  # 手动删除缓存（下次运行自动重建）
```
