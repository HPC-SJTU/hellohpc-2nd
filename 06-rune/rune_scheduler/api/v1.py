from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import Protocol


@dataclass(frozen=True, slots=True)
class Ratio:
    numerator: int
    denominator: int


@dataclass(frozen=True, slots=True)
class RatioRange:
    min: Ratio
    max: Ratio


@dataclass(frozen=True, slots=True)
class NeverFailurePrior:
    pass


@dataclass(frozen=True, slots=True)
class TruncatedGeometricFailurePrior:
    failure_probability: Ratio
    max_failures: int
    failure_fraction: RatioRange


FailurePrior = NeverFailurePrior | TruncatedGeometricFailurePrior


@dataclass(frozen=True, slots=True)
class UniformInt:
    min: int
    max: int


@dataclass(frozen=True, slots=True)
class TaskType:
    id: str
    runtime_work: UniformInt
    setup_work: int
    cpu_demand: int
    memory_reservation: int
    required_capabilities: tuple[str, ...]
    failure_prior: FailurePrior


@dataclass(frozen=True, slots=True)
class InitialTask:
    id: str
    task_type: str


@dataclass(frozen=True, slots=True)
class Release:
    id: str
    task_type: str
    at: UniformInt
    count: UniformInt


@dataclass(frozen=True, slots=True)
class WorkflowNode:
    id: str
    task_type: str
    depends_on: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class WorkflowTemplate:
    id: str
    instances: UniformInt
    nodes: tuple[WorkflowNode, ...]


@dataclass(frozen=True, slots=True)
class Derivation:
    id: str
    parent_type: str
    successor_type: str
    fanout: UniformInt


@dataclass(frozen=True, slots=True)
class DurationMultiplier:
    task_type: str
    ratio: Ratio


@dataclass(frozen=True, slots=True)
class OutagePrior:
    count: UniformInt
    initial_delay: UniformInt
    uptime: UniformInt
    duration: UniformInt


@dataclass(frozen=True, slots=True)
class WorkerSpec:
    id: str
    cpu_capacity: int
    memory_capacity: int
    capabilities: tuple[str, ...]
    duration_multipliers: tuple[DurationMultiplier, ...]
    outage_prior: OutagePrior | None


@dataclass(frozen=True, slots=True)
class SafetyLimits:
    max_generated_tasks: int
    max_batch_size: int
    max_identifier_length: int
    max_recent_events: int
    max_decisions: int


@dataclass(frozen=True, slots=True)
class PublicScenarioModel:
    protocol: str
    ruleset: str
    profile: str
    profile_digest: str
    task_types: tuple[TaskType, ...]
    initial_tasks: tuple[InitialTask, ...]
    releases: tuple[Release, ...]
    workflow_templates: tuple[WorkflowTemplate, ...]
    derivations: tuple[Derivation, ...]
    workers: tuple[WorkerSpec, ...]
    safety_limits: SafetyLimits


@dataclass(frozen=True, slots=True)
class ReadyTask:
    task_id: str
    task_type: str
    parent_task_id: str | None
    relation_id: str | None
    workflow_instance_id: str | None
    workflow_node_id: str | None
    next_attempt_number: int
    failed_attempts: int
    interrupted_attempts: int


class AttemptPhase(StrEnum):
    AWAITING_SETUP = "awaiting_setup"
    RUNNING = "running"


@dataclass(frozen=True, slots=True)
class AttemptRef:
    task_id: str
    attempt_number: int


@dataclass(frozen=True, slots=True)
class SetupGroupRef:
    decision_id: int
    worker_id: str
    task_type: str


@dataclass(frozen=True, slots=True)
class ActiveAttempt:
    attempt: AttemptRef
    task_type: str
    parent_task_id: str | None
    relation_id: str | None
    workflow_instance_id: str | None
    workflow_node_id: str | None
    worker_id: str
    phase: AttemptPhase
    attempt_start_time: int
    phase_start_time: int
    service_units: int
    failed_attempts: int
    interrupted_attempts: int
    setup_group: SetupGroupRef | None


@dataclass(frozen=True, slots=True)
class SetupGroup:
    reference: SetupGroupRef
    setup_work: int
    service_units: int
    start_time: int
    members: tuple[AttemptRef, ...]


@dataclass(frozen=True, slots=True)
class WorkerUnavailable:
    worker_id: str
    time: int


@dataclass(frozen=True, slots=True)
class SetupInterrupted:
    reference: SetupGroupRef
    service_units: int
    time: int


@dataclass(frozen=True, slots=True)
class AttemptInterrupted:
    attempt: AttemptRef
    worker_id: str
    phase: AttemptPhase
    service_units: int
    time: int


@dataclass(frozen=True, slots=True)
class AttemptFailed:
    attempt: AttemptRef
    worker_id: str
    service_units: int
    time: int


@dataclass(frozen=True, slots=True)
class AttemptCompleted:
    attempt: AttemptRef
    worker_id: str
    service_units: int
    time: int


@dataclass(frozen=True, slots=True)
class SetupCompleted:
    reference: SetupGroupRef
    time: int


@dataclass(frozen=True, slots=True)
class WorkerRecovered:
    worker_id: str
    time: int


@dataclass(frozen=True, slots=True)
class TasksReleased:
    release_id: str
    task_ids: tuple[str, ...]
    time: int


RecentEvent = (
    WorkerUnavailable
    | SetupInterrupted
    | AttemptInterrupted
    | AttemptFailed
    | AttemptCompleted
    | SetupCompleted
    | WorkerRecovered
    | TasksReleased
)


@dataclass(frozen=True, slots=True)
class WorkerState:
    worker_id: str
    available: bool
    cpu_demand: int
    progress_factor: Ratio
    memory_reserved: int
    active_attempts: tuple[AttemptRef, ...]
    setup_groups: tuple[SetupGroupRef, ...]


@dataclass(frozen=True, slots=True)
class Observation:
    decision_id: int
    logical_time: int
    ready_tasks: tuple[ReadyTask, ...]
    active_attempts: tuple[ActiveAttempt, ...]
    setup_groups: tuple[SetupGroup, ...]
    workers: tuple[WorkerState, ...]
    recent_events: tuple[RecentEvent, ...]
    generated_task_count: int
    completed_task_count: int
    failed_attempt_count: int
    interrupted_attempt_count: int
    worker_outage_count: int


@dataclass(frozen=True, slots=True)
class Placement:
    task_id: str
    worker_id: str


class DispatchAction(StrEnum):
    DISPATCH = "dispatch"
    DEFER = "defer"


@dataclass(frozen=True, slots=True)
class DispatchDecision:
    action: DispatchAction
    placements: tuple[Placement, ...] = ()


class PolicyProtocol(Protocol):
    def __init__(self, model: PublicScenarioModel) -> None: ...
    def choose_placements(self, observation: Observation) -> DispatchDecision: ...
