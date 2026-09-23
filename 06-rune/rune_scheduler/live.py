from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable
from fractions import Fraction
from typing import Protocol

from rich.console import Console, Group
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from .api.v1 import (
    ActiveAttempt,
    AttemptPhase,
    AttemptRef,
    Observation,
    PublicScenarioModel,
    Ratio,
)
from .models import TerminalResult

MAX_DISPLAY_WAIT = 2.0
REFRESH_RATE = 10
TERMINAL_HOLD = 1.0


class LiveView(Protocol):
    async def start(self, state: Observation) -> None: ...

    async def transition(
        self, previous: Observation, result: Observation | TerminalResult
    ) -> None: ...

    async def close(self) -> None: ...


class ScenarioLiveView:
    """Read-only presentation of authoritative Scenario states."""

    def __init__(
        self,
        model: PublicScenarioModel,
        *,
        console: Console,
        display_rate: float = 0,
        sleep: Callable[[float], Awaitable[None]] = asyncio.sleep,
        terminal_hold: float = TERMINAL_HOLD,
    ) -> None:
        self._model = model
        self._display_rate = display_rate
        self._sleep = sleep
        self._terminal_hold = terminal_hold
        self._workers = {worker.id: worker for worker in model.workers}
        self._multipliers = {
            (worker.id, multiplier.task_type): multiplier.ratio
            for worker in model.workers
            for multiplier in worker.duration_multipliers
        }
        self._live = Live(
            console=console,
            refresh_per_second=REFRESH_RATE,
            screen=True,
            auto_refresh=False,
        )
        self._started = False
        self._last_state: Observation | None = None
        self._display_service: dict[AttemptRef, Fraction] = {}

    async def start(self, state: Observation) -> None:
        self._live.start(refresh=True)
        self._started = True
        self._last_state = state
        self._live.update(self.render(state), refresh=True)

    async def transition(
        self, previous: Observation, result: Observation | TerminalResult
    ) -> None:
        if isinstance(result, Observation) and self._display_rate > 0:
            logical_delta = result.logical_time - previous.logical_time
            requested = logical_delta / self._display_rate
            duration = min(requested, MAX_DISPLAY_WAIT)
            compressed = requested > MAX_DISPLAY_WAIT
            frames = int(duration * REFRESH_RATE)
            for frame in range(1, frames + 1):
                elapsed = Fraction(frame, REFRESH_RATE)
                logical_elapsed = min(
                    Fraction(logical_delta),
                    elapsed * Fraction.from_float(self._display_rate),
                )
                self._live.update(
                    self.render(
                        self._interpolate(previous, logical_elapsed),
                        compressed=compressed,
                    ),
                    refresh=True,
                )
                await self._sleep(1 / REFRESH_RATE)
            remainder = duration - frames / REFRESH_RATE
            if remainder > 0:
                await self._sleep(remainder)
        self._display_service.clear()
        self._last_state = result if isinstance(result, Observation) else previous
        self._live.update(self.render(result), refresh=True)
        if isinstance(result, TerminalResult) and self._terminal_hold > 0:
            await self._sleep(self._terminal_hold)

    async def close(self) -> None:
        if self._started:
            self._live.stop()
            self._started = False

    def render(
        self, state: Observation | TerminalResult, *, compressed: bool = False
    ) -> Group:
        if isinstance(state, TerminalResult):
            terminal = state
            current = self._last_state
            status = terminal.reason
            logical_time = terminal.terminal_time
            decision_id = terminal.decision_id
            counters = terminal
        else:
            current = state
            status = "running"
            logical_time = state.logical_time
            decision_id = state.decision_id
            counters = state
        ready_count = len(current.ready_tasks) if current is not None else 0
        active_count = len(current.active_attempts) if current is not None else 0
        title = Text("Scenario Live View - presentation only", style="bold cyan")
        summary = Table.grid(expand=True)
        summary.add_column()
        summary.add_column(justify="right")
        summary.add_row(
            f"Profile: {self._model.profile}  Status: {status}",
            f"Logical Time: {logical_time}  Decision: {decision_id}",
        )
        summary.add_row(
            f"Ready: {ready_count}  "
            f"Active: {active_count}  "
            f"Completed: {counters.completed_task_count} / Generated: "
            f"{counters.generated_task_count}",
            f"Failed Attempts: {counters.failed_attempt_count}  "
            f"Interrupted: {counters.interrupted_attempt_count}  "
            f"Worker Outages: {counters.worker_outage_count}",
        )
        if compressed:
            summary.add_row("Display gap compressed (2 s cap)", "")
        return Group(
            title,
            Panel(summary),
            self._worker_table(current),
            self._event_panel(counters.recent_events),
        )

    def _worker_table(self, state: Observation | None) -> Table:
        table = Table(title="Workers", expand=True)
        for heading in ("Worker", "State", "CPU", "Factor", "Memory", "Attempts"):
            table.add_column(heading)
        worker_states = (
            {worker.worker_id: worker for worker in state.workers}
            if state is not None
            else {}
        )
        attempts: dict[str, list[ActiveAttempt]] = {}
        if state is not None:
            for attempt in state.active_attempts:
                attempts.setdefault(attempt.worker_id, []).append(attempt)
        for worker_id, spec in self._workers.items():
            worker = worker_states.get(worker_id)
            rows = sorted(
                attempts.get(worker_id, ()),
                key=lambda item: (item.attempt.task_id, item.attempt.attempt_number),
            )
            detail = []
            for attempt in rows[:3]:
                phase = (
                    "setup"
                    if attempt.phase == AttemptPhase.AWAITING_SETUP
                    else "running"
                )
                service = self._display_service.get(
                    attempt.attempt, Fraction(attempt.service_units)
                )
                detail.append(
                    f"{attempt.attempt.task_id}#{attempt.attempt.attempt_number} "
                    f"{phase} svc={_format_service(service)}"
                )
            if len(rows) > 3:
                detail.append(f"+{len(rows) - 3} more")
            setups = sum(
                1
                for group in (state.setup_groups if state is not None else ())
                if group.reference.worker_id == worker_id
            )
            if setups:
                detail.append(f"{setups} setup group(s)")
            factor = worker.progress_factor if worker is not None else Ratio(1, 1)
            table.add_row(
                worker_id,
                "available"
                if worker is not None and worker.available
                else "unavailable",
                f"{worker.cpu_demand if worker is not None else 0}/{spec.cpu_capacity}",
                f"{factor.numerator}/{factor.denominator}",
                f"{worker.memory_reserved if worker is not None else 0}/{spec.memory_capacity}",
                "\n".join(detail) or "idle",
            )
        return table

    def _event_panel(self, events: tuple[object, ...]) -> Panel:
        lines = [_format_event(event) for event in events[-8:]]
        return Panel("\n".join(lines) or "No recent events", title="Recent Events")

    def _interpolate(self, state: Observation, elapsed: Fraction) -> Observation:
        workers = {worker.worker_id: worker for worker in state.workers}
        self._display_service = {
            attempt.attempt: (
                Fraction(attempt.service_units)
                + self._service_delta(
                    elapsed,
                    workers[attempt.worker_id].progress_factor,
                    self._multiplier(attempt.worker_id, attempt.task_type),
                )
                if attempt.phase == AttemptPhase.RUNNING
                else Fraction(attempt.service_units)
            )
            for attempt in state.active_attempts
        }
        return state

    def _multiplier(self, worker_id: str, task_type: str) -> Ratio:
        return self._multipliers.get((worker_id, task_type), Ratio(1, 1))

    @staticmethod
    def _service_delta(elapsed: Fraction, factor: Ratio, multiplier: Ratio) -> Fraction:
        return (
            elapsed
            * factor.numerator
            * multiplier.denominator
            / (factor.denominator * multiplier.numerator)
        )


def _format_service(service: Fraction) -> str:
    if service.denominator == 1:
        return str(service.numerator)
    return f"{float(service):.2f}"


def _format_event(event: object) -> str:
    fields = getattr(event, "__dataclass_fields__", {})
    values = " ".join(
        f"{name}={_format_reference(getattr(event, name))}" for name in fields
    )
    return f"{type(event).__name__} {values}".rstrip()


def _format_reference(value: object) -> str:
    if isinstance(value, AttemptRef):
        return f"{value.task_id}#{value.attempt_number}"
    if hasattr(value, "decision_id") and hasattr(value, "worker_id"):
        return (
            f"d{value.decision_id}/{value.worker_id}/{getattr(value, 'task_type', '?')}"
        )
    return str(value)
