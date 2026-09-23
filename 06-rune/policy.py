from __future__ import annotations

from rune_scheduler.api.v1 import (
    DispatchAction,
    DispatchDecision,
    Observation,
    Placement,
    PublicScenarioModel,
)


class Policy:
    """你在本文件中实现调度策略。

    - 构造函数在每个 Scenario 开始时调用一次，可读取静态模型、初始化状态。
    - choose_placements 在每个决策点被调用，返回一个 DispatchDecision：
      提交一组 Placement（DispatchAction.DISPATCH，placements 非空），或在存在未来事件时
      显式等待（DispatchAction.DEFER，必须携带空 placements）。
    - 违反资源安全（内存预留 / capabilities / available）或返回畸形决策即 Scenario 无效；
      CPU 超容量不判失败，只按比例减速（progress_factor）。

    完整 API 见 rune_scheduler/api/v1.py。
    """

    def __init__(self, model: PublicScenarioModel) -> None:
        self._model = model

    def choose_placements(self, observation: Observation) -> DispatchDecision:
        raise NotImplementedError("请在 policy.py 中实现你的调度策略")
