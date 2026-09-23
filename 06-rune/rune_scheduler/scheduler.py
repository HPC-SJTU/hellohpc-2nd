from __future__ import annotations

from typing import TYPE_CHECKING

from .api.v1 import DispatchDecision, Observation, PolicyProtocol, PublicScenarioModel
from .client import ClusterClient
from .models import DispatchBatch, TerminalResult

if TYPE_CHECKING:
    from .live import LiveView


class PolicyViolation(RuntimeError):
    """The Competitor Policy failed to produce a valid Placement Plan."""


class Scheduler:
    def __init__(
        self,
        client: ClusterClient,
        model: PublicScenarioModel,
        policy: PolicyProtocol,
        live_view: LiveView | None = None,
    ) -> None:
        self._client = client
        self._model = model
        self._policy = policy
        self._live_view = live_view
        self.terminal: TerminalResult | None = None
        self.peak_ready_task_count = 0

    async def run(self) -> int:
        result = await self._client.state()
        last_decision = -1
        try:
            if isinstance(result, Observation) and self._live_view:
                await self._live_view.start(result)
            while isinstance(result, Observation):
                self.peak_ready_task_count = max(
                    self.peak_ready_task_count, len(result.ready_tasks)
                )
                if result.decision_id == last_decision:
                    raise RuntimeError(
                        "cluster repeated a decision after an accepted step"
                    )
                try:
                    decision = self._policy.choose_placements(result)
                    if not isinstance(decision, DispatchDecision):
                        raise TypeError("Policy must return DispatchDecision")
                    batch = DispatchBatch(
                        result.decision_id,
                        decision.action,
                        tuple(
                            (placement.task_id, placement.worker_id)
                            for placement in decision.placements
                        ),
                    )
                except Exception as error:
                    raise PolicyViolation(
                        f"Policy failed with {type(error).__name__}"
                    ) from error
                last_decision = result.decision_id
                previous = result
                result = await self._client.step(batch)
                if self._live_view:
                    await self._live_view.transition(previous, result)
            self.terminal = result
            return 0 if result.valid else 1
        finally:
            if self._live_view:
                await self._live_view.close()
