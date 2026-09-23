# 公开工具与数据

日常开发请在题目包根目录使用 `hellohpc test`，步骤见 `../README.md`。
本目录供查阅，不得修改。

| 路径 | 内容 |
|---|---|
| `reference/reference.py` | CPU 参考计算，作为精度基准 |
| `data/public_cases.json` | 公开正确性、性能和压力用例的生成配置 |
| `data/scoring_weights.json` | 供评测程序读取的权重机器清单；逐例权重见根目录 `README.md` |
| `correctness/` | 输入生成、正确性与运行时鲁棒性检查 |
| `benchmark/` | 性能测量与综合加速比计算 |
| `compliance/` | 源码和提交结构检查 |
| `common/runner/runner_impl.h` | 公共设备 runner：负责输入输出、内存管理与计时 |

公开数据清单位于 `tools/data/public_cases.json`；日常开发无需手工生成数据，运行根目录的 `hellohpc test` 即可。
