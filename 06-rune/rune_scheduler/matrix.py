from __future__ import annotations

import asyncio
import hashlib
import importlib.util
import json
import os
import secrets
import tomllib
from collections.abc import Awaitable, Callable, Iterable
from dataclasses import asdict, dataclass
from hashlib import sha256
from pathlib import Path
from types import ModuleType
from typing import Any

from .api.v1 import PolicyProtocol, PublicScenarioModel
from .client import ClusterClient
from .models import TerminalResult
from .scheduler import PolicyViolation, Scheduler


def profile_name_from_path(path: Path) -> str:
    with path.open("rb") as stream:
        data = tomllib.load(stream)
    profile = data.get("profile")
    if not isinstance(profile, str) or not profile:
        raise ValueError(f"profile TOML must define a non-empty string: {path}")
    return profile


@dataclass(frozen=True, slots=True)
class ScenarioRequest:
    contract_version: str
    profile: str
    profile_path: Path
    seed: str
    seed_ordinal: int


@dataclass(frozen=True, slots=True)
class PolicySelection:
    role: str
    source_digest: str
    factory: Callable[[PublicScenarioModel], PolicyProtocol]


@dataclass(frozen=True, slots=True)
class ScenarioResult:
    contract_version: str
    profile: str
    profile_digest: str
    ruleset_digest: str
    seed_set_kind: str
    seed_set_version: str
    seed_ordinal: int
    policy_role: str
    policy_source_digest: str
    validity: str
    reason: str
    logical_makespan: int | None
    counters: dict[str, int]


ScenarioExecutor = Callable[
    [ScenarioRequest, PolicySelection], Awaitable[ScenarioResult]
]


async def run_matrix(
    requests: Iterable[ScenarioRequest],
    policy: PolicySelection,
    *,
    execute: ScenarioExecutor,
    concurrency: int = 1,
    profile_order: Iterable[str] | None = None,
) -> tuple[ScenarioResult, ...]:
    if concurrency < 1:
        raise ValueError("concurrency must be positive")
    semaphore = asyncio.Semaphore(concurrency)

    async def guarded(request: ScenarioRequest) -> ScenarioResult:
        async with semaphore:
            return await execute(request, policy)

    results = await asyncio.gather(*(guarded(request) for request in requests))
    return _ordered_results(results, profile_order)


def canonical_report(
    results: Iterable[ScenarioResult], *, profile_order: Iterable[str] | None = None
) -> str:
    payload = {
        "schema_version": "rune-results-v1",
        "results": [
            asdict(result) for result in _ordered_results(results, profile_order)
        ],
    }
    return json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n"


def _ordered_results(
    results: Iterable[ScenarioResult], profile_order: Iterable[str] | None
) -> tuple[ScenarioResult, ...]:
    profile_positions = {
        profile: position for position, profile in enumerate(profile_order or ())
    }
    return tuple(
        sorted(
            results,
            key=lambda item: (
                profile_positions.get(item.profile, len(profile_positions)),
                item.profile if item.profile not in profile_positions else "",
                item.seed_ordinal,
                item.policy_role,
            ),
        )
    )


def load_policy_factory(role: str, source: Path) -> PolicySelection:
    source_bytes = source.read_bytes()
    digest = hashlib.sha256(source_bytes).hexdigest()
    spec = importlib.util.spec_from_file_location(f"rune_submission_{digest}", source)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load Policy source: {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    factory = getattr(module, "Policy", None)
    if not isinstance(factory, type):
        raise ValueError("Policy source must define a Policy class")
    return PolicySelection(role, digest, factory)


def source_digest(module: ModuleType) -> str:
    path = getattr(module, "__file__", None)
    if not isinstance(path, str):
        raise ValueError("Policy module has no source file")
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


async def _wait_for_uds(
    path: Path, process: asyncio.subprocess.Process, *, timeout: float
) -> None:
    """Wait for the cluster's UDS socket to appear; surface server stderr on death."""
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    waiter = asyncio.ensure_future(process.wait())
    try:
        while not path.exists():
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise RuntimeError(f"cluster socket never appeared: {path}")
            done, _ = await asyncio.wait({waiter}, timeout=min(0.05, remaining))
            if waiter in done:
                stderr = b"" if process.stderr is None else await process.stderr.read()
                detail = stderr.decode(errors="replace").strip()
                raise RuntimeError(
                    f"cluster exited with code {process.returncode} "
                    f"before opening {path}: {detail}"
                )
    finally:
        if not waiter.done():
            waiter.cancel()


async def execute_scenario(
    request: ScenarioRequest,
    policy: PolicySelection,
    *,
    cluster_binary: Path,
    seed_set_kind: str = "training",
    seed_set_version: str = "training-v1",
) -> ScenarioResult:
    token = secrets.token_hex(32)
    client: ClusterClient | None = None
    uds_path: str | None = None
    process = await asyncio.create_subprocess_exec(
        cluster_binary,
        "--listen",
        "127.0.0.1:0",
        "--profile",
        request.profile_path,
        "--seed",
        request.seed,
        "--bearer-token",
        token,
        "--uds-socket",
        str(
            Path("/tmp")
            / f"rune-cluster-{request.seed[:12]}-{os.getpid()}-{id(request)}.sock"
        ),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        assert process.stdout is not None
        line = await asyncio.wait_for(process.stdout.readline(), timeout=10)
        handshake: dict[str, Any] = json.loads(line)
        if handshake.get("protocol") != "rune-http-v1":
            raise RuntimeError("cluster protocol mismatch")
        uds = handshake.get("uds")
        uds_path = uds if isinstance(uds, str) else None
        if uds_path is not None:
            await _wait_for_uds(Path(uds_path), process, timeout=5.0)
        client = ClusterClient(str(handshake["address"]), token, uds_path=uds_path)
        model = await client.model()
        if (
            model.profile != request.profile
            or model.profile_digest != handshake.get("profile_digest")
            or model.ruleset != handshake.get("ruleset")
        ):
            raise RuntimeError("cluster handshake mismatch")
        try:
            selected_policy = policy.factory(model)
        except Exception as error:
            return _failure_result(
                request,
                policy,
                model.profile_digest,
                seed_set_kind,
                seed_set_version,
                "policy_violation",
                f"Policy failed with {type(error).__name__}",
            )
        scheduler = Scheduler(client, model, selected_policy)
        try:
            await scheduler.run()
        except PolicyViolation as error:
            return _failure_result(
                request,
                policy,
                model.profile_digest,
                seed_set_kind,
                seed_set_version,
                "policy_violation",
                str(error),
            )
        terminal = scheduler.terminal
        if terminal is None:
            raise RuntimeError("Scheduler ended without a Terminal Result")
        return _terminal_result(
            request,
            policy,
            terminal,
            scheduler.peak_ready_task_count,
            seed_set_kind,
            seed_set_version,
        )
    finally:
        if process.returncode is None:
            process.terminate()
        await process.wait()
        if uds_path is not None:
            Path(uds_path).unlink(missing_ok=True)
        if client is not None:
            await client.close()


def _terminal_result(
    request: ScenarioRequest,
    policy: PolicySelection,
    terminal: TerminalResult,
    peak_ready_tasks: int,
    seed_set_kind: str,
    seed_set_version: str,
) -> ScenarioResult:
    return ScenarioResult(
        contract_version=request.contract_version,
        ruleset_digest=sha256(terminal.ruleset.encode()).hexdigest(),
        profile=request.profile,
        profile_digest=terminal.profile_digest,
        seed_set_kind=seed_set_kind,
        seed_set_version=seed_set_version,
        seed_ordinal=request.seed_ordinal,
        policy_role=policy.role,
        policy_source_digest=policy.source_digest,
        validity="valid" if terminal.valid else "scenario_invalidation",
        reason=terminal.reason,
        logical_makespan=terminal.logical_makespan,
        counters={
            "generated_tasks": terminal.generated_task_count,
            "completed_tasks": terminal.completed_task_count,
            "accepted_decisions": terminal.decision_id,
            "failed_attempts": terminal.failed_attempt_count,
            "interrupted_attempts": terminal.interrupted_attempt_count,
            "worker_outages": terminal.worker_outage_count,
            "peak_ready_tasks": peak_ready_tasks,
        },
    )


def _failure_result(
    request: ScenarioRequest,
    policy: PolicySelection,
    profile_digest: str,
    seed_set_kind: str,
    seed_set_version: str,
    validity: str,
    reason: str,
) -> ScenarioResult:
    return ScenarioResult(
        contract_version=request.contract_version,
        profile=request.profile,
        profile_digest=profile_digest,
        ruleset_digest="0" * 64,
        seed_set_kind=seed_set_kind,
        seed_set_version=seed_set_version,
        seed_ordinal=request.seed_ordinal,
        policy_role=policy.role,
        policy_source_digest=policy.source_digest,
        validity=validity,
        reason=reason,
        logical_makespan=None,
        counters={
            "generated_tasks": 0,
            "completed_tasks": 0,
            "accepted_decisions": 0,
            "failed_attempts": 0,
            "interrupted_attempts": 0,
            "worker_outages": 0,
            "peak_ready_tasks": 0,
        },
    )
