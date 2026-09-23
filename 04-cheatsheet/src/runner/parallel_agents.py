"""Internal single-core Agent launcher with NUMA-local tool locks."""

from __future__ import annotations

import dataclasses
import json
import os
import re
import shutil
import stat
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

from src.runner import public_scoring
from src.runner import runner as runner_mod
from src.runner import task_registry
from src.runner import submission_surface


REPO = Path(__file__).resolve().parents[2]
WORKER_COUNT = public_scoring.RUNS_PER_SKILL
TOOL_LOCK_ENV = "CHEATSHEET_TOOL_LOCK"

_WORKER_RUN_INDEX = "CHEATSHEET_INTERNAL_RUN_INDEX"
_WORKER_CASE_KEY = "CHEATSHEET_INTERNAL_CASE_KEY"
_WORKER_THREADS = "CHEATSHEET_INTERNAL_THREADS"
_WORKER_CORES = "CHEATSHEET_INTERNAL_CORES"
_WORKER_ROOT = "CHEATSHEET_INTERNAL_RUN_ROOT"
_WORKER_SKILL = "CHEATSHEET_INTERNAL_SKILL_ROOT"
_WORKER_NUMA_NODE = "CHEATSHEET_INTERNAL_NUMA_NODE"
_TRUSTED_OUTPUT_ENV = "CHEATSHEET_TRUSTED_OUTPUT_ROOT"
_WORKER_INHERITED_ENV = (
    "TMPDIR",
    "SJTU_API_KEY",
    "CHEATSHEET_OPENCODE_BIN",
    "CHEATSHEET_RIPGREP_BIN",
    "CHEATSHEET_MODEL_BASE_URL",
    "HTTPS_PROXY",
    "https_proxy",
)
_CPU_LIST_RE = re.compile(r"^[0-9,-]+$")
_NODE_RE = re.compile(r"node([0-9]+)$")


@dataclass(frozen=True)
class AgentJob:
    run_index: int
    case_key: str
    threads: int


@dataclass(frozen=True)
class AgentPlacement:
    job: AgentJob
    numa_node: int
    cpu_ids: tuple[int, ...]

    @property
    def cores(self) -> str:
        return _format_cpu_ids(self.cpu_ids)


def _parse_cpu_list(value: str) -> tuple[int, ...]:
    text = value.strip()
    if not text or not _CPU_LIST_RE.fullmatch(text):
        raise RuntimeError("NUMA CPU list is invalid")
    result: list[int] = []
    for part in text.split(","):
        if "-" in part:
            raw_start, raw_end = part.split("-", 1)
            start, end = int(raw_start), int(raw_end)
            if end < start:
                raise RuntimeError("NUMA CPU list is invalid")
            result.extend(range(start, end + 1))
        else:
            result.append(int(part))
    if not result or len(result) != len(set(result)):
        raise RuntimeError("NUMA CPU list is invalid")
    return tuple(sorted(result))


def _format_cpu_ids(values: Sequence[int]) -> str:
    cores = tuple(sorted(int(value) for value in values))
    if not cores or len(cores) != len(set(cores)) or cores[0] < 0:
        raise RuntimeError("Agent CPU allocation is invalid")
    groups: list[str] = []
    start = previous = cores[0]
    for core in cores[1:]:
        if core == previous + 1:
            previous = core
            continue
        groups.append(str(start) if start == previous else f"{start}-{previous}")
        start = previous = core
    groups.append(str(start) if start == previous else f"{start}-{previous}")
    return ",".join(groups)


def discover_numa_cpus(
    node_root: Path = Path("/sys/devices/system/node"),
) -> dict[int, tuple[int, ...]]:
    """Return allocated CPUs grouped by NUMA node."""
    if not hasattr(os, "sched_getaffinity"):
        raise RuntimeError("CPU affinity is unavailable")
    allowed = set(int(core) for core in os.sched_getaffinity(0))
    if len(allowed) < WORKER_COUNT:
        raise RuntimeError(f"evaluation requires {WORKER_COUNT} allocated CPUs")
    result: dict[int, tuple[int, ...]] = {}
    for path in Path(node_root).glob("node[0-9]*"):
        match = _NODE_RE.fullmatch(path.name)
        if not match:
            continue
        try:
            present = set(_parse_cpu_list((path / "cpulist").read_text(encoding="ascii")))
        except OSError as exc:
            raise RuntimeError("NUMA topology is unreadable") from exc
        cpus = tuple(sorted(allowed.intersection(present)))
        if cpus:
            result[int(match.group(1))] = cpus
    if len({cpu for cpus in result.values() for cpu in cpus}) < WORKER_COUNT:
        raise RuntimeError("allocated NUMA topology has insufficient CPUs")
    return result


def build_agent_placements(
    jobs: Sequence[AgentJob],
    numa_cpus: Mapping[int, Sequence[int]],
) -> tuple[AgentPlacement, ...]:
    """Place two single-thread agents on distinct CPUs, spread across NUMA nodes."""
    ordered_jobs = tuple(jobs)
    operators = {job.case_key for job in ordered_jobs}
    if (
        len(ordered_jobs) != WORKER_COUNT
        or len(operators) != 1
        or not operators.issubset(task_registry.EXPECTED_PUBLIC_TASKS)
        or {job.run_index for job in ordered_jobs} != set(range(WORKER_COUNT))
    ):
        raise RuntimeError("parallel Agent assignment set is invalid")

    if any(job.threads != 1 for job in ordered_jobs):
        raise RuntimeError("all challenge operators must use one thread")
    nodes = [(int(node), tuple(sorted(int(cpu) for cpu in cpus)))
             for node, cpus in sorted(numa_cpus.items()) if cpus]
    all_cpus = [cpu for _, cpus in nodes for cpu in cpus]
    if len(all_cpus) != len(set(all_cpus)) or any(cpu < 0 for cpu in all_cpus):
        raise RuntimeError("NUMA CPU assignments must be distinct and nonnegative")
    if len(all_cpus) < WORKER_COUNT:
        raise RuntimeError(f"evaluation requires {WORKER_COUNT} allocated CPUs")
    # Round-robin by NUMA node, then by core within each node. Keep per-node
    # timing locks: fewer threads does not remove shared-cache interference.
    slots = [(node, cpus[index])
             for index in range(max(len(cpus) for _, cpus in nodes))
             for node, cpus in nodes if index < len(cpus)]
    return tuple(AgentPlacement(job, node, (cpu,))
                 for job, (node, cpu) in zip(ordered_jobs, slots))


def _read_run_record(path: Path) -> runner_mod.RunRecord:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RuntimeError("parallel Agent result is missing or invalid") from exc
    if not isinstance(payload, dict):
        raise RuntimeError("parallel Agent result must be an object")
    run_fields = {item.name for item in dataclasses.fields(runner_mod.RunRecord)}
    if set(payload) != run_fields or not isinstance(payload.get("sessions"), list):
        raise RuntimeError("parallel Agent result fields are invalid")
    session_fields = {item.name for item in dataclasses.fields(runner_mod.SessionRecord)}
    sessions = []
    for raw in payload.pop("sessions"):
        if not isinstance(raw, dict) or set(raw) != session_fields:
            raise RuntimeError("parallel Agent session fields are invalid")
        sessions.append(runner_mod.SessionRecord(**raw))
    return runner_mod.RunRecord(sessions=sessions, **payload)


def _worker_environment(trusted_root: Path) -> dict[str, str]:
    """Return the complete, explicit environment inherited by worker processes."""
    env = {
        "PATH": "/usr/bin:/bin",
        "LANG": "C",
        "LC_ALL": "C",
        "PYTHONDONTWRITEBYTECODE": "1",
        _TRUSTED_OUTPUT_ENV: str(trusted_root),
    }
    env.update(
        {
            key: os.environ[key]
            for key in _WORKER_INHERITED_ENV
            if key in os.environ
        }
    )
    return env


def run_parallel_agents(
    jobs: Sequence[AgentJob],
    skill_path: Path,
    trusted_root: Path,
) -> dict[int, runner_mod.RunRecord]:
    """Run both Agent jobs concurrently and return their trusted records."""
    root = Path(trusted_root).resolve(strict=True)
    skill = Path(skill_path).resolve(strict=True)
    try:
        skill.relative_to(root)
    except ValueError as exc:
        raise RuntimeError("frozen Skill escaped the trusted output root") from exc
    numactl = shutil.which("numactl")
    if not numactl:
        raise RuntimeError("numactl is unavailable")
    placements = build_agent_placements(jobs, discover_numa_cpus())

    runs_root = root / "agent-runs"
    logs_root = root / "agent-logs"
    locks_root = root / "tool-locks"
    for directory in (runs_root, logs_root, locks_root):
        directory.mkdir(mode=0o700, exist_ok=False)
    worker_base_env = _worker_environment(root)
    lock_paths: dict[int, Path] = {}
    for node in sorted({placement.numa_node for placement in placements}):
        lock = locks_root / f"numa-{node}.lock"
        shared_lock = os.environ.get("CHEATSHEET_NODE_TIMING_LOCK")
        if shared_lock:
            source = Path(shared_lock)
            if (not source.is_absolute() or source.name != f"numa-{node}.lock"
                    or not stat.S_ISREG(os.lstat(source).st_mode)):
                raise RuntimeError("invalid cross-evaluation timing lock")
            # Same inode, inside the worker's existing trusted lock boundary.
            os.link(source, lock, follow_symlinks=False)
        else:
            lock.touch(mode=0o600, exist_ok=False)
        lock_paths[node] = lock

    processes: list[tuple[AgentPlacement, Path, subprocess.Popen[bytes], object]] = []
    for placement in placements:
        job = placement.job
        name = f"{job.case_key}-run-{job.run_index + 1}"
        run_root = runs_root / name
        log_path = logs_root / f"{name}.log"
        stream = log_path.open("wb")
        child_env = dict(worker_base_env)
        child_env.update(
            {
                _WORKER_RUN_INDEX: str(job.run_index),
                _WORKER_CASE_KEY: job.case_key,
                _WORKER_THREADS: str(job.threads),
                _WORKER_CORES: placement.cores,
                _WORKER_ROOT: str(run_root),
                _WORKER_SKILL: str(skill),
                _WORKER_NUMA_NODE: str(placement.numa_node),
                TOOL_LOCK_ENV: str(lock_paths[placement.numa_node]),
            }
        )
        command = [
            numactl,
            f"--physcpubind={placement.cores}",
            f"--membind={placement.numa_node}",
            sys.executable,
            "-m",
            "src.runner.parallel_agents",
            "--worker",
        ]
        try:
            process = subprocess.Popen(
                command,
                cwd=REPO,
                env=child_env,
                stdout=stream,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        except BaseException:
            stream.close()
            for _prior_placement, _prior_root, prior, prior_stream in processes:
                try:
                    prior.terminate()
                except OSError:
                    pass
            for _prior_placement, _prior_root, prior, prior_stream in processes:
                try:
                    prior.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    prior.kill()
                    prior.wait()
                prior_stream.close()
            raise
        processes.append((placement, run_root, process, stream))

    failures: list[str] = []
    for placement, _run_root, process, stream in processes:
        returncode = process.wait()
        stream.close()
        if returncode != 0:
            job = placement.job
            failures.append(f"{job.case_key}/run-{job.run_index + 1}: exit {returncode}")
    if failures:
        raise RuntimeError("parallel Agent worker failed: " + "; ".join(failures))

    records: dict[int, runner_mod.RunRecord] = {}
    for placement, run_root, _process, _stream in processes:
        job = placement.job
        record = _read_run_record(run_root / "run-summary.json")
        expected_id = f"{job.case_key}-run-{job.run_index + 1}"
        if (
            record.case_key != job.case_key
            or record.run_id != expected_id
            or Path(record.artifact_dir).resolve(strict=True) != run_root.resolve(strict=True)
        ):
            raise RuntimeError("parallel Agent worker identity diverged")
        records[job.run_index] = record
    if len(records) != WORKER_COUNT:
        raise RuntimeError("parallel Agent result set is incomplete")
    return records


def _required_int(name: str) -> int:
    raw = os.environ.get(name, "")
    try:
        value = int(raw)
    except ValueError as exc:
        raise RuntimeError("parallel Agent worker environment is invalid") from exc
    if value < 0:
        raise RuntimeError("parallel Agent worker environment is invalid")
    return value


def _required_child_path(name: str) -> Path:
    raw = os.environ.get(name, "")
    path = Path(raw)
    if not raw or not path.is_absolute() or ".." in path.parts:
        raise RuntimeError("parallel Agent worker path is invalid")
    return path


def _worker_main() -> int:
    secret = os.environ.get("SJTU_API_KEY", "")
    try:
        from src.runner import agent_trace
        from src.runner import runtime_config
        from src.runner import sandbox

        trusted_root = Path(os.environ[_TRUSTED_OUTPUT_ENV]).resolve(strict=True)
        run_index = _required_int(_WORKER_RUN_INDEX)
        threads = _required_int(_WORKER_THREADS)
        node = _required_int(_WORKER_NUMA_NODE)
        case_key = os.environ.get(_WORKER_CASE_KEY, "")
        cores = os.environ.get(_WORKER_CORES, "")
        run_root = _required_child_path(_WORKER_ROOT)
        skill = _required_child_path(_WORKER_SKILL).resolve(strict=True)
        lock = Path(os.environ.get(TOOL_LOCK_ENV, ""))
        expected_parent = (trusted_root / "agent-runs").resolve(strict=True)
        if (
            run_root.parent.resolve(strict=True) != expected_parent
            or run_root.exists()
            or skill != (trusted_root / "submission-snapshot").resolve(strict=True)
            or not lock.is_absolute()
            or lock.parent.resolve(strict=True) != (trusted_root / "tool-locks").resolve(strict=True)
            or lock.name != f"numa-{node}.lock"
        ):
            raise RuntimeError("parallel Agent worker paths diverged")
        lock_info = os.lstat(lock)
        if stat.S_ISLNK(lock_info.st_mode) or not stat.S_ISREG(lock_info.st_mode):
            raise RuntimeError("parallel Agent tool lock is invalid")
        cpu_ids = _parse_cpu_list(cores)
        if set(cpu_ids) != set(os.sched_getaffinity(0)):
            raise RuntimeError("parallel Agent CPU affinity diverged")

        surface = submission_surface.require_eligible_surface(skill)
        task_id = surface.operator
        registry = task_registry.load_public_registry(repo_root=REPO)
        profile = public_scoring.load_score_profile(
            registry,
            suite="public",
            operator=task_id,
            repo_root=REPO,
            require_complete=False,
        )
        if run_index >= public_scoring.RUNS_PER_SKILL:
            raise RuntimeError("parallel Agent worker assignment is out of range")
        task_profile = profile.tasks[0]
        if task_id != case_key or task_profile.threads != threads:
            raise RuntimeError("parallel Agent worker assignment diverged")
        definition = sandbox.load_task_definition(
            task_id, runtime_config.SOURCE_ROOT, repo_root=REPO
        )

        owner = runtime_config.build_runner(runtime_config.load_config())
        record = owner.run(
            definition,
            skill,
            trusted_root / "agent-runs",
            omp_threads=threads,
            cores=cores,
            run_id=f"{case_key}-run-{run_index + 1}",
            root_override=run_root,
        )
        return 0 if record.case_key == case_key else 4
    except Exception as exc:
        message = str(exc)
        if secret:
            message = agent_trace.clean_text(message, (secret,)) if "agent_trace" in locals() else "redacted"
        print(f"parallel Agent worker failed: {type(exc).__name__}: {message}", file=sys.stderr)
        return 4


def main(argv: Sequence[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if args != ["--worker"]:
        print("this module is an internal evaluator worker", file=sys.stderr)
        return 2
    return _worker_main()


if __name__ == "__main__":
    raise SystemExit(main())
