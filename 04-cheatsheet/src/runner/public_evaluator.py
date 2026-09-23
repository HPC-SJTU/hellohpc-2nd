"""Evaluate one checked-in public or official suite."""

from __future__ import annotations

import hashlib
import math
import os
import secrets
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Mapping, Optional, Sequence, Tuple

from src.runner import measure as measure_mod
from src.runner import parallel_agents as parallel_mod
from src.runner import public_scoring
from src.runner import runtime_config
from src.runner import submission_surface as surface_mod
from src.runner import task_registry as registry_mod
from src.scoring import score as score_mod


@dataclass(frozen=True)
class SuiteEvaluation:
    normalized_score: Optional[float]
    manifest: Mapping[str, object]


def _parse_core_ids(value: object) -> Tuple[int, ...]:
    result = []
    for raw_part in str(value).split(","):
        part = raw_part.strip()
        if not part:
            raise RuntimeError("configured CPU set is invalid")
        if "-" in part:
            bounds = part.split("-")
            if len(bounds) != 2:
                raise RuntimeError("configured CPU set is invalid")
            try:
                start, end = (int(item) for item in bounds)
            except ValueError as exc:
                raise RuntimeError("configured CPU set is invalid") from exc
            if start < 0 or end < start:
                raise RuntimeError("configured CPU set is invalid")
            result.extend(range(start, end + 1))
        else:
            try:
                result.append(int(part))
            except ValueError as exc:
                raise RuntimeError("configured CPU set is invalid") from exc
    if not result or len(result) != len(set(result)) or min(result) < 0:
        raise RuntimeError("configured CPU set is invalid")
    return tuple(result)


def _format_core_ids(values: Sequence[int]) -> str:
    ordered = tuple(sorted(int(value) for value in values))
    groups = []
    start = previous = ordered[0]
    for value in ordered[1:]:
        if value == previous + 1:
            previous = value
            continue
        groups.append(str(start) if start == previous else f"{start}-{previous}")
        start = previous = value
    groups.append(str(start) if start == previous else f"{start}-{previous}")
    return ",".join(groups)


def _pinning(cfg: Mapping[str, object], threads: int) -> Tuple[str, int]:
    configured = _parse_core_ids(cfg["pinning"]["cores"])
    if threads > len(configured):
        raise RuntimeError("public task requests more threads than allocated CPUs")
    selected = configured[:threads]
    if not set(selected).issubset(os.sched_getaffinity(0)):
        raise RuntimeError("formal scoring CPUs are outside the evaluation CPU allocation")
    return _format_core_ids(selected), threads


def _tokens(profile: public_scoring.TaskScoreProfile) -> list[str]:
    return [item.token for item in profile.sizes]


def _anchors(
    profile: public_scoring.TaskScoreProfile,
) -> Tuple[Dict[str, float], Dict[str, float]]:
    if any(not item.complete for item in profile.sizes):
        raise RuntimeError(f"{profile.task_id} public score anchors are incomplete")
    return (
        {item.token: float(item.B) for item in profile.sizes},
        {item.token: float(item.T) for item in profile.sizes},
    )


def _pilot_scales(
    cfg: Mapping[str, object], profile: public_scoring.TaskScoreProfile
) -> Dict[str, float]:
    raw = cfg["scoring"].get("timing_pilot_scale", 0.5)
    if (
        isinstance(raw, bool)
        or not isinstance(raw, (int, float))
        or not math.isfinite(float(raw))
        or not 0.0 < float(raw) <= 1.0
    ):
        raise RuntimeError("public timing pilot scale must be in (0, 1]")
    return {token: float(raw) for token in _tokens(profile)}


def _measurement_kwargs(
    cfg: Mapping[str, object],
    profile: public_scoring.TaskScoreProfile,
    artifact_root: Path,
    *,
    build_name: str,
) -> dict:
    compile_cfg = cfg["compile"]
    tools_cfg = cfg["tools"]
    pinning = cfg["pinning"]
    cores, omp_threads = _pinning(cfg, profile.threads)
    task_root = profile.spec_path.parent
    return {
        "source_root": task_root.parents[1],
        "harness_src": task_root,
        "work_binary_dir": artifact_root / build_name,
        "cxxflags": compile_cfg["cxxflags"],
        "sizes": _tokens(profile),
        "cores": cores,
        "omp_threads": omp_threads,
        "omp_bind": pinning["omp_proc_bind"],
        "omp_places": pinning["omp_places"],
        "variance_threshold_pct": float(cfg["scoring"]["variance_threshold_pct"]),
        "cxx": compile_cfg.get("cxx", "/usr/bin/g++"),
        "cxx_readonly_roots": list(compile_cfg.get("cxx_readonly_roots", [])),
        "api_key_env": runtime_config.MODEL_API_KEY_ENV,
        "compile_timeout": float(tools_cfg["subprocess_timeout_seconds"]),
        "bench_timeout": float(tools_cfg["bench_timeout_seconds"]),
        "check_timeout": float(tools_cfg["subprocess_timeout_seconds"]),
    }


def _valid_times(
    measurement: measure_mod.TaskMeasurement, tokens: Sequence[str]
) -> Optional[Dict[str, float]]:
    if (
        not measurement.compiled
        or measurement.runtime_error
        or measurement.timed_out
        or measurement.unstable
        or not measurement.correctness_all
        or tuple(measurement.per_size) != tuple(tokens)
    ):
        return None
    result = {}
    for token in tokens:
        item = measurement.per_size.get(token)
        if (
            item is None
            or not item.correct
            or item.unstable
            or item.median_time_ms is None
            or not math.isfinite(float(item.median_time_ms))
            or float(item.median_time_ms) <= 0.0
        ):
            return None
        result[token] = float(item.median_time_ms)
    return result


def _valid_candidate_times(
    measurement: measure_mod.TaskMeasurement, tokens: Sequence[str]
) -> Dict[str, float]:
    """Return valid candidate times, omitting failed sizes."""
    if not measurement.compiled:
        return {}
    result = {}
    for token in tokens:
        item = measurement.per_size.get(token)
        if (
            item is None
            or item.error
            or not item.correct
            or item.unstable
            or item.median_time_ms is None
            or not math.isfinite(float(item.median_time_ms))
            or float(item.median_time_ms) <= 0.0
        ):
            continue
        result[token] = float(item.median_time_ms)
    return result


def _timing_policy(
    measurement: measure_mod.TaskMeasurement, tokens: Sequence[str]
) -> Optional[Tuple[Dict[str, int], int]]:
    blocks: Dict[str, int] = {}
    samples: Optional[int] = None
    for token in tokens:
        item = measurement.per_size.get(token)
        if item is None or item.block_invocations is None or item.benchmark_samples is None:
            return None
        block_count = item.block_invocations
        sample_count = item.benchmark_samples
        if (
            isinstance(block_count, bool)
            or not isinstance(block_count, int)
            or not 1 <= block_count <= measure_mod.MAX_BLOCK_INVOCATIONS
            or isinstance(sample_count, bool)
            or not isinstance(sample_count, int)
            or not 1 <= sample_count <= measure_mod.MAX_BENCHMARK_SAMPLES
            or sample_count % 2 != 1
        ):
            return None
        blocks[token] = block_count
        if samples is None:
            samples = sample_count
        elif samples != sample_count:
            return None
    return (blocks, samples) if samples is not None else None


def _failed_measurement(task_id: str, message: str) -> measure_mod.TaskMeasurement:
    return measure_mod.TaskMeasurement(
        task=task_id,
        impl="candidate",
        compiled=False,
        compile_log=message,
    )


def _paired_result(
    candidate: measure_mod.TaskMeasurement,
    sentinel_pre: measure_mod.TaskMeasurement,
    sentinel_post: measure_mod.TaskMeasurement,
    tokens: Sequence[str],
    drift_limit_pct: float,
) -> Tuple[score_mod.RunResult, Mapping[str, object]]:
    pre = _valid_times(sentinel_pre, tokens)
    post = _valid_times(sentinel_post, tokens)
    pre_policy = _timing_policy(sentinel_pre, tokens)
    post_policy = _timing_policy(sentinel_post, tokens)
    if pre is None or post is None or pre_policy is None or post_policy is None:
        raise RuntimeError("public starter sentinel failed")
    if pre_policy != post_policy:
        raise RuntimeError("public timing block policy diverged")
    candidate_times = _valid_candidate_times(candidate, tokens)
    blocks, samples = pre_policy
    for token in candidate_times:
        item = candidate.per_size[token]
        if (
            item.block_invocations != blocks[token]
            or item.benchmark_samples != samples
        ):
            raise RuntimeError("public timing block policy diverged")
    starter_baseline = {token: min(pre[token], post[token]) for token in tokens}
    drift = {
        token: abs(pre[token] - post[token]) / starter_baseline[token] * 100.0
        for token in tokens
    }
    drift_ok = all(value <= drift_limit_pct for value in drift.values())
    speeds: Dict[str, float] = {}
    for token, candidate_time in candidate_times.items():
        if drift[token] <= drift_limit_pct:
            S = score_mod.paired_speedup(candidate_time, starter_baseline[token])
            if S is not None:
                speeds[token] = S
    correct = len(speeds) == len(tokens)
    return (
        score_mod.RunResult(correct, speeds),
        {
            "starter_pre_ms": pre,
            "starter_post_ms": post,
            "starter_baseline_ms": starter_baseline,
            "starter_drift_pct": drift,
            "drift_ok": drift_ok,
            "candidate_ms": candidate_times or {},
            "paired_S": speeds,
            "block_invocations": blocks,
            "benchmark_samples": samples,
        },
    )


def _run_seed(registry_sha256: str, task_id: str, run_index: int) -> int:
    digest = hashlib.sha256(
        b"cheatsheet-public-run\0"
        + bytes.fromhex(registry_sha256)
        + b"\0"
        + task_id.encode("ascii")
        + b"\0"
        + str(run_index).encode("ascii")
    ).digest()
    return int.from_bytes(digest[:8], "big") % 2_147_483_646 + 1


def _surface_evidence(surface: surface_mod.SubmissionSurface) -> Mapping[str, object]:
    return {
        "skill_surface_sha256": surface.sha256,
        "skill_estimated_tokens": surface.estimated_tokens,
        "skill_tokenizer_sha256": surface.tokenizer_sha256,
    }


def run_suite(
    cfg: Mapping[str, object],
    skill_path: Path,
    trusted_root: Path,
    *,
    suite: str = "public",
    calibrate: bool = False,
) -> SuiteEvaluation:
    """Run the selected operator twice; calibration needs no score anchors."""
    surface = surface_mod.require_eligible_surface(
        Path(skill_path), repository_layout=True
    )
    registry = registry_mod.load_public_registry(repo_root=runtime_config.REPO_ROOT)
    profile = public_scoring.load_score_profile(
        registry,
        suite=suite,
        operator=surface.operator,
        repo_root=runtime_config.REPO_ROOT,
        require_complete=not calibrate,
    )
    frozen_skill = trusted_root / "submission-snapshot"
    surface_mod.materialize_surface(surface, frozen_skill)

    drift_limit_pct = float(cfg["scoring"]["variance_threshold_pct"])
    if not math.isfinite(drift_limit_pct) or drift_limit_pct <= 0.0:
        raise RuntimeError("public sentinel drift threshold is invalid")
    max_attempts = int(cfg["scoring"].get("sentinel_max_measurement_attempts", 3))
    if not 1 <= max_attempts <= 5:
        raise RuntimeError("public sentinel measurement retry limit is invalid")

    jobs = [
        parallel_mod.AgentJob(run, task.task_id, task.threads)
        for task in profile.tasks
        for run in range(public_scoring.RUNS_PER_SKILL)
    ]
    agent_records = parallel_mod.run_parallel_agents(jobs, frozen_skill, trusted_root)
    from .timing_lock import formal_timing_lock
    with formal_timing_lock():
        cases = []
        for task_profile in profile.tasks:
            tokens = _tokens(task_profile)
            anchors = None if calibrate else _anchors(task_profile)
            starter_path = task_profile.spec_path.parent / "starter_kernel.cpp"
            if starter_path.is_symlink() or not starter_path.is_file():
                raise RuntimeError(f"{task_profile.task_id} starter is missing")
            run_results = []
            public_runs = []

            for run_index in range(public_scoring.RUNS_PER_SKILL):
                record = agent_records[run_index]
                artifact = Path(record.artifact_dir).resolve(strict=True)
                seed = _run_seed(registry.sha256, task_profile.task_id, run_index)
                candidate_path = Path(record.final_kernel_path or "")
                candidate_error = ""
                extra_cxxflags: Sequence[str] = ()
                if record.skill_status != "loaded":
                    candidate_error = "required cpu-hpc-skill was not loaded"
                elif (
                    not candidate_path.is_absolute()
                    or candidate_path.is_symlink()
                    or not candidate_path.is_file()
                ):
                    candidate_error = "final kernel is missing"
                else:
                    try:
                        extra_cxxflags = measure_mod.load_compile_options(
                            record.final_compile_options_path or ""
                        )
                    except (OSError, ValueError):
                        candidate_error = "compile options are invalid"

                attempts = []
                for attempt in range(1, max_attempts + 1):
                    # Never derive the paired key from public evaluation metadata.
                    input_domain = secrets.token_hex(16)
                    suffix = "" if attempt == 1 else f"-attempt-{attempt}"
                    pre_kwargs = _measurement_kwargs(
                        cfg,
                        task_profile,
                        artifact,
                        build_name=f"{suite}-starter-pre{suffix}",
                    )
                    pre_kwargs.update(
                        seed=seed,
                        input_domain=input_domain,
                        derive_block_invocations=True,
                        pilot_scale_by_size=_pilot_scales(cfg, task_profile),
                    )
                    sentinel_pre = measure_mod.measure_task(
                        task_profile.task_id,
                        str(starter_path),
                        **pre_kwargs,
                        extra_cxxflags=(),
                    )
                    pre_policy = _timing_policy(sentinel_pre, tokens)
                    if _valid_times(sentinel_pre, tokens) is None or pre_policy is None:
                        raise RuntimeError(
                            f"{task_profile.task_id} starter sentinel failed"
                        )
                    block_invocations, benchmark_samples = pre_policy

                    if candidate_error:
                        candidate = _failed_measurement(
                            task_profile.task_id, candidate_error
                        )
                    else:
                        candidate_kwargs = _measurement_kwargs(
                            cfg,
                            task_profile,
                            artifact,
                            build_name=f"{suite}-candidate{suffix}",
                        )
                        candidate_kwargs.update(
                            seed=seed,
                            input_domain=input_domain,
                            block_invocations=dict(block_invocations),
                            benchmark_samples=benchmark_samples,
                        )
                        candidate = measure_mod.measure_task(
                            task_profile.task_id,
                            str(candidate_path),
                            **candidate_kwargs,
                            extra_cxxflags=extra_cxxflags,
                        )

                    post_kwargs = _measurement_kwargs(
                        cfg,
                        task_profile,
                        artifact,
                        build_name=f"{suite}-starter-post{suffix}",
                    )
                    post_kwargs.update(
                        seed=seed,
                        input_domain=input_domain,
                        block_invocations=dict(block_invocations),
                        benchmark_samples=benchmark_samples,
                    )
                    sentinel_post = measure_mod.measure_task(
                        task_profile.task_id,
                        str(starter_path),
                        **post_kwargs,
                        extra_cxxflags=(),
                    )
                    run_result, evidence = _paired_result(
                        candidate,
                        sentinel_pre,
                        sentinel_post,
                        tokens,
                        drift_limit_pct,
                    )
                    attempts.append({"attempt": attempt, **evidence})
                    if bool(evidence["drift_ok"]) or not _valid_candidate_times(candidate, tokens):
                        break

                run_results.append(run_result)
                public_runs.append(
                    {
                        "run_index": run_index + 1,
                        "run_id": record.run_id,
                        "agent_status": record.agent_status,
                        "skill_status": record.skill_status,
                        "correct": run_result.correct,
                        "paired_S": dict(run_result.per_size_S),
                        "measurement_attempts": attempts,
                    }
                )

            correct_count = sum(result.correct for result in run_results)
            averaged_S = (
                {
                    token: sum(result.per_size_S[token] for result in run_results)
                    / public_scoring.RUNS_PER_SKILL
                    for token in tokens
                }
                if correct_count == public_scoring.RUNS_PER_SKILL
                else {}
            )
            case = {
                "task": task_profile.task_id,
                "correct_run_count": correct_count,
                "sizes": tokens,
                "runs": public_runs,
            }
            if calibrate:
                case["averaged_S"] = averaged_S
            else:
                assert anchors is not None
                B, T = anchors
                fold = score_mod.stability_fold(run_results, B, T, weights=None)
                task_score = min(1.0, max(0.0, float(fold["effective_score"])))
                case["scored_run_count"] = int(fold["scored_run_count"])
                case["selected_run_index"] = fold["selected_run_index"]
                case["selected_S"] = fold["selected_S"]
                case["normalized_score"] = task_score
            cases.append(case)

        manifest = {
            "status": "calibration" if calibrate else "complete",
            "suite": suite,
            "registry_sha256": registry.sha256,
            "score_profile_sha256": profile.sha256,
            "runs_per_skill": public_scoring.RUNS_PER_SKILL,
            "per_size_weighting": "equal",
            "operator": surface.operator,
            "model": surface.model,
            "reasoning_effort": surface_mod.REASONING_EFFORT,
            "agent_parallelism": parallel_mod.WORKER_COUNT,
            "cases": cases,
            **_surface_evidence(surface),
        }
        if calibrate:
            return SuiteEvaluation(None, manifest)

        performance = float(cases[0]["normalized_score"])
        length = score_mod.length_adjustment(performance, surface.estimated_tokens)
        normalized = float(length["final_score"])
        manifest.update(
            performance_score=performance,
            normalized_score=normalized,
            length_adjustment=length,
        )
        return SuiteEvaluation(normalized, manifest)
