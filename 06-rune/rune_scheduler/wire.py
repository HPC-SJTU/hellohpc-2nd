"""Typed request/response schemas for the UDS data plane.

Encoders produce postcard payloads whose field order mirrors the Rust
struct declaration order in ``rune-cluster/src/{cluster,profile,wire}.rs``.
Decoders parse postcard payloads straight into the domain dataclasses from
``api.v1`` (single decode pass, no intermediate dict layer).
"""

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from .api.v1 import (
    ActiveAttempt,
    AttemptCompleted,
    AttemptFailed,
    AttemptInterrupted,
    AttemptPhase,
    AttemptRef,
    Derivation,
    DurationMultiplier,
    FailurePrior,
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
from .codec import FORMAT_POSTCARD, FrameError, _Reader, encode_message
from .models import StepResult, TerminalResult

# ---------------------------------------------------------------------------
# enum variant tables (order == Rust declaration order == discriminant)
# ---------------------------------------------------------------------------

_STEP_ACTION_VARIANTS = ("dispatch", "defer")
_ATTEMPT_PHASE_VARIANTS = ("awaiting_setup", "running")
_RECENT_EVENT_VARIANTS = (
    "worker_unavailable",
    "setup_interrupted",
    "attempt_interrupted",
    "attempt_failed",
    "attempt_completed",
    "setup_completed",
    "worker_recovered",
    "tasks_released",
)
_STEP_RESULT_VARIANTS = ("observation", "terminal")
_RESPONSE_VARIANTS = ("model", "state", "step", "error")
_FAILURE_PRIOR_VARIANTS = ("never", "truncated_geometric")


# ---------------------------------------------------------------------------
# requests (client → server)
# ---------------------------------------------------------------------------
def _enum(variants: Sequence[str], variant: str, **values: Any) -> Mapping[str, Any]:
    return {
        "__kind__": "enum",
        "variants": [{"name": v} for v in variants],
        "variant": variant,
        "values": values,
    }


def encode_hello(token: str, fmt: int = FORMAT_POSTCARD) -> bytes:
    return encode_message(_struct_pair(("token",), {"token": token}), fmt)


def encode_request_model(fmt: int = FORMAT_POSTCARD) -> bytes:
    return encode_message(_enum(("model", "state", "step"), "model"), fmt)


def encode_request_state(fmt: int = FORMAT_POSTCARD) -> bytes:
    return encode_message(_enum(("model", "state", "step"), "state"), fmt)


def encode_request_step(
    decision_id: int,
    action: str,
    placements: Sequence[Mapping[str, str]],
    fmt: int = FORMAT_POSTCARD,
) -> bytes:
    return encode_message(
        _enum(
            ("model", "state", "step"),
            "step",
            decision_id=decision_id,
            action=_enum(_STEP_ACTION_VARIANTS, action),
            placements=[
                _struct_pair(("task_id", "worker_id"), placement)
                for placement in placements
            ],
        ),
        fmt,
    )


def _struct_pair(fields: Sequence[str], values: Mapping[str, Any]) -> Mapping[str, Any]:
    return {
        "__kind__": "struct",
        "fields": tuple(fields),
        "values": {k: values[k] for k in fields},
    }


@dataclass(frozen=True, slots=True)
class WireError:
    """Error variant of the wire ``Response`` enum."""

    code: str
    message: str


# ---------------------------------------------------------------------------
# responses (server → client, postcard decoded directly into domain dataclasses)
# ---------------------------------------------------------------------------


def decode_response_message(
    payload: bytes,
) -> PublicScenarioModel | StepResult | WireError:
    """Decode a postcard ``Response`` frame body into typed domain objects.

    The wire ``Response``/``WireStepResult``/``WireRecentEvent`` enums are
    externally tagged (variant discriminant first, then named fields).
    """
    reader = _Reader(payload, 0)
    kind = _RESPONSE_VARIANTS[reader.read_u8()]
    if kind == "error":
        return WireError(reader.read_str(), reader.read_str())
    if kind == "model":
        return _decode_model(reader)
    return _decode_wire_step_result(reader)


def _decode_wire_step_result(reader: _Reader) -> StepResult:
    variant = _STEP_RESULT_VARIANTS[reader.read_u8()]
    if variant == "observation":
        return _decode_observation(reader)
    return _decode_terminal(reader)


def _decode_recent_event(reader: _Reader) -> RecentEvent:
    name = _RECENT_EVENT_VARIANTS[reader.read_u8()]
    if name == "worker_unavailable":
        return WorkerUnavailable(worker_id=reader.read_str(), time=reader.read_varint())
    if name == "setup_interrupted":
        return SetupInterrupted(
            reference=_decode_setup_group_ref(reader),
            service_units=reader.read_varint(),
            time=reader.read_varint(),
        )
    if name in ("attempt_interrupted", "attempt_failed", "attempt_completed"):
        attempt = _decode_attempt_ref(reader)
        worker_id = reader.read_str()
        phase = (
            AttemptPhase(_ATTEMPT_PHASE_VARIANTS[reader.read_u8()])
            if name == "attempt_interrupted"
            else AttemptPhase.RUNNING
        )
        service_units = reader.read_varint()
        time = reader.read_varint()
        if name == "attempt_interrupted":
            return AttemptInterrupted(
                attempt=attempt,
                worker_id=worker_id,
                phase=phase,
                service_units=service_units,
                time=time,
            )
        if name == "attempt_failed":
            return AttemptFailed(
                attempt=attempt,
                worker_id=worker_id,
                service_units=service_units,
                time=time,
            )
        return AttemptCompleted(
            attempt=attempt, worker_id=worker_id, service_units=service_units, time=time
        )
    if name == "setup_completed":
        return SetupCompleted(
            reference=_decode_setup_group_ref(reader), time=reader.read_varint()
        )
    if name == "worker_recovered":
        return WorkerRecovered(worker_id=reader.read_str(), time=reader.read_varint())
    if name == "tasks_released":
        return TasksReleased(
            release_id=reader.read_str(),
            task_ids=tuple(reader.read_str() for _ in range(reader.read_len())),
            time=reader.read_varint(),
        )
    raise FrameError(f"unknown recent event index {name}")


def _decode_attempt_ref(reader: _Reader) -> AttemptRef:
    return AttemptRef(task_id=reader.read_str(), attempt_number=reader.read_varint())


def _decode_setup_group_ref(reader: _Reader) -> SetupGroupRef:
    return SetupGroupRef(
        decision_id=reader.read_varint(),
        worker_id=reader.read_str(),
        task_type=reader.read_str(),
    )


def _decode_ratio(reader: _Reader) -> Ratio:
    return Ratio(numerator=reader.read_varint(), denominator=reader.read_varint())


def _decode_uniform_int(reader: _Reader) -> UniformInt:
    return UniformInt(min=reader.read_varint(), max=reader.read_varint())


def _decode_observation(reader: _Reader) -> Observation:
    return Observation(
        decision_id=reader.read_varint(),
        logical_time=reader.read_varint(),
        ready_tasks=tuple(_decode_ready_task(reader) for _ in range(reader.read_len())),
        active_attempts=tuple(
            _decode_active_attempt(reader) for _ in range(reader.read_len())
        ),
        setup_groups=tuple(
            _decode_setup_group(reader) for _ in range(reader.read_len())
        ),
        workers=tuple(_decode_worker_state(reader) for _ in range(reader.read_len())),
        recent_events=tuple(
            _decode_recent_event(reader) for _ in range(reader.read_len())
        ),
        generated_task_count=reader.read_varint(),
        completed_task_count=reader.read_varint(),
        failed_attempt_count=reader.read_varint(),
        interrupted_attempt_count=reader.read_varint(),
        worker_outage_count=reader.read_varint(),
    )


def _decode_ready_task(reader: _Reader) -> ReadyTask:
    return ReadyTask(
        task_id=reader.read_str(),
        task_type=reader.read_str(),
        parent_task_id=_decode_option_string(reader),
        relation_id=_decode_option_string(reader),
        workflow_instance_id=_decode_option_string(reader),
        workflow_node_id=_decode_option_string(reader),
        next_attempt_number=reader.read_varint(),
        failed_attempts=reader.read_varint(),
        interrupted_attempts=reader.read_varint(),
    )


def _decode_active_attempt(reader: _Reader) -> ActiveAttempt:
    return ActiveAttempt(
        attempt=_decode_attempt_ref(reader),
        task_type=reader.read_str(),
        parent_task_id=_decode_option_string(reader),
        relation_id=_decode_option_string(reader),
        worker_id=reader.read_str(),
        phase=AttemptPhase(_ATTEMPT_PHASE_VARIANTS[reader.read_u8()]),
        attempt_start_time=reader.read_varint(),
        phase_start_time=reader.read_varint(),
        service_units=reader.read_varint(),
        failed_attempts=reader.read_varint(),
        workflow_instance_id=_decode_option_string(reader),
        workflow_node_id=_decode_option_string(reader),
        interrupted_attempts=reader.read_varint(),
        setup_group=_decode_option_setup_group_ref(reader),
    )


def _decode_option_setup_group_ref(reader: _Reader) -> SetupGroupRef | None:
    if reader.read_u8() == 0:
        return None
    return _decode_setup_group_ref(reader)


def _decode_setup_group(reader: _Reader) -> SetupGroup:
    return SetupGroup(
        reference=_decode_setup_group_ref(reader),
        setup_work=reader.read_varint(),
        service_units=reader.read_varint(),
        start_time=reader.read_varint(),
        members=tuple(_decode_attempt_ref(reader) for _ in range(reader.read_len())),
    )


def _decode_worker_state(reader: _Reader) -> WorkerState:
    return WorkerState(
        worker_id=reader.read_str(),
        available=reader.read_bool(),
        cpu_demand=reader.read_varint(),
        progress_factor=_decode_ratio(reader),
        memory_reserved=reader.read_varint(),
        active_attempts=tuple(
            _decode_attempt_ref(reader) for _ in range(reader.read_len())
        ),
        setup_groups=tuple(
            _decode_setup_group_ref(reader) for _ in range(reader.read_len())
        ),
    )


def _decode_terminal(reader: _Reader) -> TerminalResult:
    return TerminalResult(
        valid=reader.read_bool(),
        reason=reader.read_str(),
        terminal_time=reader.read_varint(),
        logical_makespan=_decode_option_u64(reader),
        decision_id=reader.read_varint(),
        recent_events=tuple(
            _decode_recent_event(reader) for _ in range(reader.read_len())
        ),
        generated_task_count=reader.read_varint(),
        completed_task_count=reader.read_varint(),
        failed_attempt_count=reader.read_varint(),
        interrupted_attempt_count=reader.read_varint(),
        worker_outage_count=reader.read_varint(),
        protocol=reader.read_str(),
        ruleset=reader.read_str(),
        profile=reader.read_str(),
        profile_digest=reader.read_str(),
    )


def _decode_option_string(reader: _Reader) -> str | None:
    if reader.read_u8() == 0:
        return None
    return reader.read_str()


def _decode_option_u64(reader: _Reader) -> int | None:
    if reader.read_u8() == 0:
        return None
    return reader.read_varint()


def _decode_model(reader: _Reader) -> PublicScenarioModel:
    return PublicScenarioModel(
        protocol=reader.read_str(),
        ruleset=reader.read_str(),
        profile=reader.read_str(),
        profile_digest=reader.read_str(),
        task_types=tuple(_decode_task_type(reader) for _ in range(reader.read_len())),
        initial_tasks=tuple(
            InitialTask(id=reader.read_str(), task_type=reader.read_str())
            for _ in range(reader.read_len())
        ),
        releases=tuple(_decode_release(reader) for _ in range(reader.read_len())),
        workflow_templates=tuple(
            _decode_workflow_template(reader) for _ in range(reader.read_len())
        ),
        derivations=tuple(
            Derivation(
                id=reader.read_str(),
                parent_type=reader.read_str(),
                successor_type=reader.read_str(),
                fanout=_decode_uniform_int(reader),
            )
            for _ in range(reader.read_len())
        ),
        workers=tuple(_decode_worker_spec(reader) for _ in range(reader.read_len())),
        safety_limits=SafetyLimits(
            max_generated_tasks=reader.read_varint(),
            max_batch_size=reader.read_varint(),
            max_identifier_length=reader.read_varint(),
            max_recent_events=reader.read_varint(),
            max_decisions=reader.read_varint(),
        ),
    )


def _decode_task_type(reader: _Reader) -> TaskType:
    return TaskType(
        id=reader.read_str(),
        runtime_work=_decode_uniform_int(reader),
        setup_work=reader.read_varint(),
        cpu_demand=reader.read_varint(),
        memory_reservation=reader.read_varint(),
        required_capabilities=tuple(
            sorted(reader.read_str() for _ in range(reader.read_len()))
        ),
        failure_prior=_decode_failure_prior(reader),
    )


def _decode_failure_prior(reader: _Reader) -> FailurePrior:
    # serde derives internally-tagged enums through the struct path, which
    # postcard serializes as: length-prefixed variant-name string + fields.
    name = reader.read_str()
    if name == "never":
        return NeverFailurePrior()
    return TruncatedGeometricFailurePrior(
        failure_probability=_decode_ratio(reader),
        max_failures=reader.read_varint(),
        failure_fraction=RatioRange(
            min=_decode_ratio(reader), max=_decode_ratio(reader)
        ),
    )


def _decode_release(reader: _Reader) -> Release:
    return Release(
        id=reader.read_str(),
        task_type=reader.read_str(),
        at=_decode_uniform_int(reader),
        count=_decode_uniform_int(reader),
    )


def _decode_workflow_template(reader: _Reader) -> WorkflowTemplate:
    return WorkflowTemplate(
        id=reader.read_str(),
        instances=_decode_uniform_int(reader),
        nodes=tuple(
            WorkflowNode(
                id=reader.read_str(),
                task_type=reader.read_str(),
                depends_on=tuple(reader.read_str() for _ in range(reader.read_len())),
            )
            for _ in range(reader.read_len())
        ),
    )


def _decode_worker_spec(reader: _Reader) -> WorkerSpec:
    return WorkerSpec(
        id=reader.read_str(),
        cpu_capacity=reader.read_varint(),
        memory_capacity=reader.read_varint(),
        capabilities=tuple(sorted(reader.read_str() for _ in range(reader.read_len()))),
        duration_multipliers=tuple(
            DurationMultiplier(task_type=reader.read_str(), ratio=_decode_ratio(reader))
            for _ in range(reader.read_len())
        ),
        outage_prior=_decode_option_outage_prior(reader),
    )


def _decode_option_outage_prior(reader: _Reader) -> OutagePrior | None:
    if reader.read_u8() == 0:
        return None
    return OutagePrior(
        count=_decode_uniform_int(reader),
        initial_delay=_decode_uniform_int(reader),
        uptime=_decode_uniform_int(reader),
        duration=_decode_uniform_int(reader),
    )
