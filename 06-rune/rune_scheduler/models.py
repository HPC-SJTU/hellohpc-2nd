from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass

from .api.v1 import (
    ActiveAttempt,
    AttemptCompleted,
    AttemptFailed,
    AttemptInterrupted,
    AttemptPhase,
    AttemptRef,
    Derivation,
    DispatchAction,
    DurationMultiplier,
    InitialTask,
    NeverFailurePrior,
    Observation,
    OutagePrior,
    PublicScenarioModel,
    Ratio,
    RatioRange,
    ReadyTask,
    RecentEvent,
    Release,
    SafetyLimits,
    SetupCompleted,
    SetupGroup,
    SetupGroupRef,
    SetupInterrupted,
    TasksReleased,
    TaskType,
    TruncatedGeometricFailurePrior,
    UniformInt,
    WorkerRecovered,
    WorkerSpec,
    WorkerState,
    WorkerUnavailable,
    WorkflowNode,
    WorkflowTemplate,
)


@dataclass(frozen=True, slots=True)
class DispatchBatch:
    decision_id: int
    action: DispatchAction
    placements: tuple[tuple[str, str], ...]


@dataclass(frozen=True, slots=True)
class TerminalResult:
    valid: bool
    reason: str
    terminal_time: int
    logical_makespan: int | None
    decision_id: int
    recent_events: tuple[RecentEvent, ...] = ()
    generated_task_count: int = 0
    completed_task_count: int = 0
    failed_attempt_count: int = 0
    interrupted_attempt_count: int = 0
    worker_outage_count: int = 0
    protocol: str = ""
    ruleset: str = ""
    profile: str = ""
    profile_digest: str = ""


StepResult = Observation | TerminalResult


def model_from_json(data: Mapping[str, object]) -> PublicScenarioModel:
    limits = _map(data, "safety_limits")
    task_types = tuple(
        _task_type(_mapping(x, "task_types[]")) for x in _seq(data, "task_types")
    )
    return PublicScenarioModel(
        protocol=_str(data, "protocol"),
        ruleset=_str(data, "ruleset"),
        profile=_str(data, "profile"),
        profile_digest=_str(data, "profile_digest"),
        task_types=task_types,
        initial_tasks=tuple(
            InitialTask(id=_str(x, "id"), task_type=_str(x, "task_type"))
            for x in (
                _mapping(value, "initial_tasks[]")
                for value in _seq(data, "initial_tasks")
            )
        ),
        releases=tuple(
            Release(
                id=_str(item, "id"),
                task_type=_str(item, "task_type"),
                at=_uniform_int(_map(item, "at")),
                count=_uniform_int(_map(item, "count")),
            )
            for item in (
                _mapping(value, "releases[]") for value in _seq(data, "releases")
            )
        ),
        workflow_templates=tuple(
            WorkflowTemplate(
                id=_str(template, "id"),
                instances=_uniform_int(_map(template, "instances")),
                nodes=tuple(
                    WorkflowNode(
                        id=_str(node, "id"),
                        task_type=_str(node, "task_type"),
                        depends_on=_strings(node, "depends_on"),
                    )
                    for node in (
                        _mapping(value, "nodes[]") for value in _seq(template, "nodes")
                    )
                ),
            )
            for template in (
                _mapping(value, "workflow_templates[]")
                for value in _seq(data, "workflow_templates")
            )
        ),
        derivations=tuple(
            Derivation(
                id=_str(x, "id"),
                parent_type=_str(x, "parent_type"),
                successor_type=_str(x, "successor_type"),
                fanout=_uniform_int(_map(x, "fanout")),
            )
            for x in (
                _mapping(value, "derivations[]") for value in _seq(data, "derivations")
            )
        ),
        workers=tuple(
            _worker_spec(_mapping(x, "workers[]")) for x in _seq(data, "workers")
        ),
        safety_limits=SafetyLimits(
            max_generated_tasks=_int(limits, "max_generated_tasks"),
            max_batch_size=_int(limits, "max_batch_size"),
            max_identifier_length=_int(limits, "max_identifier_length"),
            max_recent_events=_int(limits, "max_recent_events"),
            max_decisions=_int(limits, "max_decisions"),
        ),
    )


def step_result_from_json(data: Mapping[str, object]) -> StepResult:
    kind = _str(data, "kind")
    if kind == "terminal":
        t = _map(data, "terminal")
        makespan = t.get("logical_makespan")
        if makespan is not None and (
            isinstance(makespan, bool) or not isinstance(makespan, int)
        ):
            raise ValueError("logical_makespan must be integer or null")
        return TerminalResult(
            valid=_bool(t, "valid"),
            reason=_str(t, "reason"),
            terminal_time=_int(t, "terminal_time"),
            logical_makespan=makespan,
            decision_id=_int(t, "decision_id"),
            recent_events=tuple(
                _recent_event(_mapping(value, "recent_events[]"))
                for value in _seq(t, "recent_events")
            ),
            generated_task_count=_int(t, "generated_task_count"),
            completed_task_count=_int(t, "completed_task_count"),
            failed_attempt_count=_int(t, "failed_attempt_count"),
            interrupted_attempt_count=_int(t, "interrupted_attempt_count"),
            worker_outage_count=_int(t, "worker_outage_count"),
            protocol=_str(t, "protocol"),
            ruleset=_str(t, "ruleset"),
            profile=_str(t, "profile"),
            profile_digest=_str(t, "profile_digest"),
        )
    if kind != "observation":
        raise ValueError(f"unknown result kind: {kind}")
    o = _map(data, "observation")
    ready = tuple(
        ReadyTask(
            task_id=_str(x, "task_id"),
            task_type=_str(x, "task_type"),
            parent_task_id=_optional_str(x, "parent_task_id"),
            relation_id=_optional_str(x, "relation_id"),
            workflow_instance_id=_optional_str(x, "workflow_instance_id"),
            workflow_node_id=_optional_str(x, "workflow_node_id"),
            next_attempt_number=_int(x, "next_attempt_number"),
            failed_attempts=_int(x, "failed_attempts"),
            interrupted_attempts=_int(x, "interrupted_attempts"),
        )
        for x in (_mapping(value, "ready_tasks[]") for value in _seq(o, "ready_tasks"))
    )
    active_attempts = tuple(
        _active_attempt(_mapping(value, "active_attempts[]"))
        for value in _seq(o, "active_attempts")
    )
    setup_groups = tuple(
        _setup_group(_mapping(value, "setup_groups[]"))
        for value in _seq(o, "setup_groups")
    )
    workers = tuple(
        WorkerState(
            worker_id=_str(x, "worker_id"),
            available=_bool(x, "available"),
            cpu_demand=_int(x, "cpu_demand"),
            progress_factor=_ratio(_map(x, "progress_factor")),
            memory_reserved=_int(x, "memory_reserved"),
            active_attempts=tuple(
                _attempt_ref(_mapping(value, "active_attempts[]"))
                for value in _seq(x, "active_attempts")
            ),
            setup_groups=tuple(
                _setup_group_ref(_mapping(value, "setup_groups[]"))
                for value in _seq(x, "setup_groups")
            ),
        )
        for x in (_mapping(value, "workers[]") for value in _seq(o, "workers"))
    )
    recent_events = tuple(
        _recent_event(_mapping(value, "recent_events[]"))
        for value in _seq(o, "recent_events")
    )
    return Observation(
        decision_id=_int(o, "decision_id"),
        logical_time=_int(o, "logical_time"),
        ready_tasks=ready,
        active_attempts=active_attempts,
        setup_groups=setup_groups,
        workers=workers,
        recent_events=recent_events,
        generated_task_count=_int(o, "generated_task_count"),
        completed_task_count=_int(o, "completed_task_count"),
        failed_attempt_count=_int(o, "failed_attempt_count"),
        interrupted_attempt_count=_int(o, "interrupted_attempt_count"),
        worker_outage_count=_int(o, "worker_outage_count"),
    )


def batch_to_json(batch: DispatchBatch) -> dict[str, object]:
    return {
        "decision_id": batch.decision_id,
        "action": batch.action.value,
        "placements": [{"task_id": t, "worker_id": w} for t, w in batch.placements],
    }


def _task_type(x: Mapping[str, object]) -> TaskType:
    failure = _map(x, "failure_prior")
    kind = _str(failure, "kind")
    if kind == "never":
        failure_prior = NeverFailurePrior()
    elif kind == "truncated_geometric":
        fraction = _map(failure, "failure_fraction")
        failure_prior = TruncatedGeometricFailurePrior(
            failure_probability=_ratio(_map(failure, "failure_probability")),
            max_failures=_int(failure, "max_failures"),
            failure_fraction=RatioRange(
                min=_ratio(_map(fraction, "min")),
                max=_ratio(_map(fraction, "max")),
            ),
        )
    else:
        raise ValueError(f"unknown failure prior kind: {kind}")
    return TaskType(
        id=_str(x, "id"),
        runtime_work=_uniform_int(_map(x, "runtime_work")),
        setup_work=_int(x, "setup_work"),
        cpu_demand=_int(x, "cpu_demand"),
        memory_reservation=_int(x, "memory_reservation"),
        required_capabilities=_strings(x, "required_capabilities"),
        failure_prior=failure_prior,
    )


def _worker_spec(x: Mapping[str, object]) -> WorkerSpec:
    outage = _optional_map(x, "outage_prior")
    return WorkerSpec(
        id=_str(x, "id"),
        cpu_capacity=_int(x, "cpu_capacity"),
        memory_capacity=_int(x, "memory_capacity"),
        capabilities=_strings(x, "capabilities"),
        duration_multipliers=tuple(
            DurationMultiplier(
                task_type=_str(multiplier, "task_type"),
                ratio=_ratio(_map(multiplier, "ratio")),
            )
            for multiplier in (
                _mapping(value, "duration_multipliers[]")
                for value in _seq(x, "duration_multipliers")
            )
        ),
        outage_prior=(
            None
            if outage is None
            else OutagePrior(
                count=_uniform_int(_map(outage, "count")),
                initial_delay=_uniform_int(_map(outage, "initial_delay")),
                uptime=_uniform_int(_map(outage, "uptime")),
                duration=_uniform_int(_map(outage, "duration")),
            )
        ),
    )


def _attempt_ref(x: Mapping[str, object]) -> AttemptRef:
    return AttemptRef(
        task_id=_str(x, "task_id"), attempt_number=_int(x, "attempt_number")
    )


def _setup_group_ref(x: Mapping[str, object]) -> SetupGroupRef:
    return SetupGroupRef(
        decision_id=_int(x, "decision_id"),
        worker_id=_str(x, "worker_id"),
        task_type=_str(x, "task_type"),
    )


def _active_attempt(x: Mapping[str, object]) -> ActiveAttempt:
    setup_group = _optional_map(x, "setup_group")
    return ActiveAttempt(
        attempt=_attempt_ref(_map(x, "attempt")),
        task_type=_str(x, "task_type"),
        parent_task_id=_optional_str(x, "parent_task_id"),
        relation_id=_optional_str(x, "relation_id"),
        workflow_instance_id=_optional_str(x, "workflow_instance_id"),
        workflow_node_id=_optional_str(x, "workflow_node_id"),
        worker_id=_str(x, "worker_id"),
        phase=AttemptPhase(_str(x, "phase")),
        attempt_start_time=_int(x, "attempt_start_time"),
        phase_start_time=_int(x, "phase_start_time"),
        service_units=_int(x, "service_units"),
        failed_attempts=_int(x, "failed_attempts"),
        interrupted_attempts=_int(x, "interrupted_attempts"),
        setup_group=(None if setup_group is None else _setup_group_ref(setup_group)),
    )


def _setup_group(x: Mapping[str, object]) -> SetupGroup:
    return SetupGroup(
        reference=_setup_group_ref(_map(x, "reference")),
        setup_work=_int(x, "setup_work"),
        service_units=_int(x, "service_units"),
        start_time=_int(x, "start_time"),
        members=tuple(
            _attempt_ref(_mapping(value, "members[]")) for value in _seq(x, "members")
        ),
    )


def _recent_event(x: Mapping[str, object]) -> RecentEvent:
    kind = _str(x, "kind")
    if kind == "worker_unavailable":
        return WorkerUnavailable(worker_id=_str(x, "worker_id"), time=_int(x, "time"))
    if kind == "setup_interrupted":
        return SetupInterrupted(
            reference=_setup_group_ref(_map(x, "reference")),
            service_units=_int(x, "service_units"),
            time=_int(x, "time"),
        )
    if kind == "attempt_interrupted":
        return AttemptInterrupted(
            attempt=_attempt_ref(_map(x, "attempt")),
            worker_id=_str(x, "worker_id"),
            phase=AttemptPhase(_str(x, "phase")),
            service_units=_int(x, "service_units"),
            time=_int(x, "time"),
        )
    if kind == "attempt_failed":
        return AttemptFailed(
            attempt=_attempt_ref(_map(x, "attempt")),
            worker_id=_str(x, "worker_id"),
            service_units=_int(x, "service_units"),
            time=_int(x, "time"),
        )
    if kind == "attempt_completed":
        return AttemptCompleted(
            attempt=_attempt_ref(_map(x, "attempt")),
            worker_id=_str(x, "worker_id"),
            service_units=_int(x, "service_units"),
            time=_int(x, "time"),
        )
    if kind == "setup_completed":
        return SetupCompleted(
            reference=_setup_group_ref(_map(x, "reference")), time=_int(x, "time")
        )
    if kind == "worker_recovered":
        return WorkerRecovered(worker_id=_str(x, "worker_id"), time=_int(x, "time"))
    if kind == "tasks_released":
        return TasksReleased(
            release_id=_str(x, "release_id"),
            task_ids=_strings(x, "task_ids"),
            time=_int(x, "time"),
        )
    raise ValueError(f"unknown recent event kind: {kind}")


def _uniform_int(x: Mapping[str, object]) -> UniformInt:
    return UniformInt(min=_int(x, "min"), max=_int(x, "max"))


def _ratio(x: Mapping[str, object]) -> Ratio:
    return Ratio(numerator=_int(x, "numerator"), denominator=_int(x, "denominator"))


def _map(x: Mapping[str, object], k: str) -> Mapping[str, object]:
    return _mapping(x.get(k), k)


def _optional_map(x: Mapping[str, object], k: str) -> Mapping[str, object] | None:
    if k not in x:
        raise ValueError(f"{k} must be object or null")
    value = x[k]
    if value is None:
        return None
    return _mapping(value, k)


def _mapping(x: object, k: str) -> Mapping[str, object]:
    if not isinstance(x, Mapping):
        raise ValueError(f"{k} must be object")
    return x


def _seq(x: Mapping[str, object], k: str) -> Sequence[object]:
    value = x.get(k)
    if isinstance(value, (str, bytes)) or not isinstance(value, Sequence):
        raise ValueError(f"{k} must be array")
    return value


def _str(x: Mapping[str, object], k: str) -> str:
    value = x.get(k)
    if not isinstance(value, str):
        raise ValueError(f"{k} must be string")
    return value


def _optional_str(x: Mapping[str, object], k: str) -> str | None:
    value = x.get(k)
    if value is not None and not isinstance(value, str):
        raise ValueError(f"{k} must be string or null")
    return value


def _int(x: Mapping[str, object], k: str) -> int:
    value = x.get(k)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{k} must be integer")
    return value


def _bool(x: Mapping[str, object], k: str) -> bool:
    value = x.get(k)
    if not isinstance(value, bool):
        raise ValueError(f"{k} must be boolean")
    return value


def _strings(x: Mapping[str, object], k: str) -> tuple[str, ...]:
    values = _seq(x, k)
    if not all(isinstance(v, str) for v in values):
        raise ValueError(f"{k} must contain strings")
    return tuple(values)  # type: ignore[arg-type]
