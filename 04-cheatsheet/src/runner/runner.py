"""Run one OpenCode agent and independently validate its final kernel."""

from __future__ import annotations

import difflib
import hashlib
import json
import math
import os
import platform
import re
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

try:
    from . import agent_trace as _trace
    from . import opencode_agent as _agent
    from . import sandbox as _sandbox
    from . import submission_surface as _submission
except ImportError:
    import agent_trace as _trace  # type: ignore
    import opencode_agent as _agent  # type: ignore
    import sandbox as _sandbox  # type: ignore
    import submission_surface as _submission  # type: ignore


@dataclass
class SessionRecord:
    session: int
    build_ok: bool
    correctness: str
    median_time_ms: Optional[float]
    variance_pct: Optional[float]
    improved: bool
    tokens_in: int
    tokens_out: int
    tokens_reasoning: int
    tool_calls: int
    agent_note: str = ""


@dataclass
class RunRecord:
    case_key: str
    run_id: str
    workspace: str
    sessions: List[SessionRecord] = field(default_factory=list)
    best_time_ms: Optional[float] = None
    best_correct: bool = False
    final_kernel_path: Optional[str] = None
    final_compile_options_path: Optional[str] = None
    sessions_used: int = 0
    tokens_used: int = 0
    stopped_reason: str = ""
    artifact_dir: str = ""
    tool_error_count: int = 0
    tool_errors: List[Dict[str, object]] = field(default_factory=list)
    skill_status: str = ""
    skill_error: str = ""
    agent_status: str = "not_started"
    agent_returncode: int = 0
    tool_calls: int = 0
    prompt_sha256: str = ""
    initial_kernel_sha256: str = ""
    final_kernel_sha256: str = ""
    changed_kernel: bool = False
    repository_revision: str = ""
    model: str = ""
    reasoning_effort: str = _submission.REASONING_EFFORT


@dataclass(frozen=True)
class _CandidateResult:
    passed: bool
    score: Optional[float]
    variance_pct: Optional[float]
    failure_reason: str = ""


class Runner:
    def __init__(
        self,
        source_root: Path,
        tools_dir: Optional[Path] = None,
        api_key_value: Optional[str] = None,
        model_base_url: str = "https://models.sjtu.edu.cn/api/v1",
        https_proxy: Optional[str] = None,
        opencode_bin: Optional[str] = None,
        ripgrep_bin: Optional[str] = None,
        cxx: str = "g++",
        cxxflags: str = "-O3 -march=native -fopenmp -std=c++17 -funroll-loops",
        san_flags: str = "-fsanitize=undefined -fsanitize-undefined-trap-on-error -g -O1 -fopenmp -std=c++17",
        vec_flags: str = "-fopt-info-vec-optimized -fopt-info-vec-missed",
        cores: str = "0-31",
        omp_threads: int = 32,
        omp_bind: str = "close",
        omp_places: str = "cores",
        round_timeout: float = 3600.0,
        tool_timeout: float = 300.0,
        bench_timeout_seconds: float = 600.0,
        profile_timeout_seconds: float = 120.0,
        variance_threshold_pct: float = 10.0,
        tool_lock_path: Optional[str] = None,
        live_trace: bool = True,
    ) -> None:
        self.source_root = Path(source_root).resolve()
        self.tools_dir = Path(tools_dir).resolve() if tools_dir else self.source_root / "tools"
        self.api_key_value = api_key_value
        self.model_base_url = model_base_url
        self.https_proxy = https_proxy
        self.opencode_bin = opencode_bin
        self.ripgrep_bin = ripgrep_bin
        self.cxx = cxx
        self.cxxflags = cxxflags
        self.san_flags = san_flags
        self.vec_flags = vec_flags
        self.cores = cores
        self.omp_threads = omp_threads
        self.omp_bind = omp_bind
        self.omp_places = omp_places
        self.round_timeout = float(round_timeout)
        self.tool_timeout = float(tool_timeout)
        self.bench_timeout_seconds = float(bench_timeout_seconds)
        self.profile_timeout_seconds = float(profile_timeout_seconds)
        self.variance_threshold_pct = float(variance_threshold_pct)
        self.tool_lock_path = tool_lock_path
        self.live_trace = bool(live_trace)
        if not self.model_base_url.startswith(("https://", "http://")):
            raise ValueError("model base URL must use HTTP or HTTPS")
        if self.round_timeout <= 0 or self.tool_timeout <= 0:
            raise ValueError("timeouts must be positive")
        if self.variance_threshold_pct < 0:
            raise ValueError("variance threshold must be non-negative")
        if self.tool_lock_path:
            lock = Path(self.tool_lock_path)
            if not lock.is_absolute() or lock.is_symlink() or not lock.is_file():
                raise ValueError("tool lock must be an absolute regular file")
            self.tool_lock_path = str(lock.resolve(strict=True))

    def platform_contract(self, omp_threads: int) -> str:
        return (
            f"Execution platform: {platform.system()} {platform.machine()}. "
            f"Compiler: {self.cxx}. "
            f"Default flags: {self.cxxflags}. OpenMP threads: {omp_threads}. "
            "Target intrinsics and compiler options must match this architecture."
        )

    def _tool_env(
        self,
        sb: "_sandbox.Sandbox",
        omp_threads: int,
        cores: Optional[str] = None,
    ) -> Dict[str, str]:
        env: Dict[str, str] = {
            "PATH": "/usr/bin:/bin",
            "LANG": "C",
            "LC_ALL": "C",
            "PYTHONDONTWRITEBYTECODE": "1",
            "OPENBLAS_NUM_THREADS": "1",
            "MKL_NUM_THREADS": "1",
            "OMP_DYNAMIC": "FALSE",
        }
        env.update(
            CHEATSHEET_TASK=sb.case_key,
            CHEATSHEET_SRC=str(self.source_root),
            CHEATSHEET_WORK=str(sb.workspace),
            CHEATSHEET_HARNESS_SRC=str(sb.harness),
            CHEATSHEET_BUILD=str(sb.workspace / "build"),
            CHEATSHEET_CXX=self.cxx,
            CHEATSHEET_PYTHON=sys.executable,
            CHEATSHEET_CXXFLAGS=self.cxxflags,
            CHEATSHEET_SAN_FLAGS=self.san_flags,
            CHEATSHEET_VEC_FLAGS=self.vec_flags,
            CHEATSHEET_BENCH_SECONDS=str(self.bench_timeout_seconds),
            CHEATSHEET_VARIANCE_THRESHOLD_PCT=str(self.variance_threshold_pct),
            CHEATSHEET_PROFILE_SECONDS=str(self.profile_timeout_seconds),
            CHEATSHEET_COMPILE_SECONDS=str(self.tool_timeout),
            CHEATSHEET_CHECK_SECONDS=str(self.tool_timeout),
            CHEATSHEET_CORES=cores or self.cores,
            CHEATSHEET_OMP_THREADS=str(omp_threads),
            CHEATSHEET_OMP_BIND=self.omp_bind,
            CHEATSHEET_OMP_PLACES=self.omp_places,
        )
        if self.tool_lock_path:
            env["CHEATSHEET_TOOL_LOCK"] = self.tool_lock_path
        return env

    def _run_tool(
        self,
        tool: str,
        env: Dict[str, str],
        log_dir: Path,
    ) -> "subprocess_result":
        script = self.tools_dir / tool
        compile_bundle = 4.0 * self.tool_timeout
        timeout = {
            "build.sh": compile_bundle,
            "check.sh": 2.0 * compile_bundle + 5.0 * self.tool_timeout,
            "bench.sh": compile_bundle + 8.0 * self.bench_timeout_seconds,
        }[tool]
        completed = _sandbox.run_pg(
            ["bash", str(script)], timeout=timeout, env=env, capture=True
        )
        _write_tool_artifacts(log_dir, tool, completed.stdout or "", completed.stderr or "")
        return _kv_result(completed)

    def _evaluate_current(
        self,
        sb: "_sandbox.Sandbox",
        env: Dict[str, str],
        label: str,
    ) -> _CandidateResult:
        log_dir = sb.root / "trusted-tools" / label
        build = self._run_tool("build.sh", env, log_dir)
        if build.returncode != 0 or build.kv.get("build") != "ok":
            return _CandidateResult(False, None, None, "compile_error")
        check = self._run_tool("check.sh", env, log_dir)
        if check.returncode != 0:
            return _CandidateResult(False, None, None, "check_runtime_error")
        if check.kv.get("correctness") != "passed":
            return _CandidateResult(False, None, None, "correctness_failed")
        bench = self._run_tool("bench.sh", env, log_dir)
        if bench.returncode != 0:
            reason = "benchmark_timeout" if bench.returncode == 124 else "benchmark_runtime_error"
            return _CandidateResult(False, None, None, reason)
        score = _to_float(bench.kv.get("public_geomean_ms"))
        variance = _to_float(bench.kv.get("public_variance_pct"))
        if score is None or not math.isfinite(score) or score <= 0:
            return _CandidateResult(False, None, variance, "benchmark_score_missing")
        return _CandidateResult(True, score, variance)

    def run(
        self,
        definition: "_sandbox.TaskDefinitionLike",
        skill_path: Path,
        runs_root: Path,
        omp_threads: Optional[int] = None,
        cores: Optional[str] = None,
        run_id: Optional[str] = None,
        root_override: Optional[Path] = None,
    ) -> RunRecord:
        if not self.api_key_value:
            raise EnvironmentError("the model API key is not configured")
        surface = _submission.require_eligible_surface(skill_path, repository_layout=True)
        if surface.operator != definition.case_key:
            raise ValueError("task does not match submission.yaml operator")
        omp_threads = omp_threads or self.omp_threads
        sb = _sandbox.create_sandbox(
            definition,
            self.source_root,
            runs_root,
            run_id=run_id,
            skill_path=surface,
            root_override=root_override,
        )
        rec = RunRecord(
            case_key=sb.case_key,
            run_id=sb.run_id,
            workspace=str(sb.workspace),
            artifact_dir=str(sb.root),
            model=surface.model,
        )
        secret_values = (self.api_key_value,)
        try:
            env = self._tool_env(sb, omp_threads, cores)
            task_md = _read(sb.harness / "TASK.md")
            initial_kernel = _read(sb.kernel_path)
            options_path = sb.workspace / "compile_options.txt"
            (sb.root / "initial-kernel.cpp").write_text(initial_kernel, encoding="utf-8")

            initial = self._evaluate_current(sb, env, "initial")
            if not initial.passed or initial.score is None:
                raise RuntimeError("starter kernel failed public validation: " + initial.failure_reason)

            state = (
                f"task: {sb.case_key}\n"
                "correctness: passed\n"
                f"starter_public_time: {initial.score:.3f} ms\n"
                "instruction: edit the kernel, validate candidates, and leave the best version in place"
            )
            prompt = assemble_prompt(
                task_md,
                initial_kernel,
                state,
                platform_contract=self.platform_contract(omp_threads),
            )
            (sb.root / "PROMPT.md").write_text(prompt, encoding="utf-8")
            (sb.root / "FINALIZATION_PROMPT.md").write_text(
                _agent.FINALIZATION_PROMPT, encoding="utf-8"
            )

            agent_env = dict(env)
            agent_env["CHEATSHEET_MODEL_BASE_URL"] = self.model_base_url
            if self.https_proxy:
                agent_env["HTTPS_PROXY"] = self.https_proxy
                agent_env["https_proxy"] = self.https_proxy
            trace = _trace.AgentTrace(sb.root, secrets=secret_values, terminal=self.live_trace)
            try:
                agent_out = _agent.run_round(
                    workspace=str(sb.workspace),
                    prompt=prompt,
                    timeout=self.round_timeout,
                    api_key_value=self.api_key_value,
                    opencode_bin=self.opencode_bin,
                    ripgrep_bin=self.ripgrep_bin,
                    extra_env=agent_env,
                    on_output=trace.feed,
                    finalize=True,
                    model=surface.model,
                )
                trace.finalize()
            finally:
                trace.close()
            _write_agent_result(sb.root, agent_out, secret_values)

            rec.agent_returncode = int(agent_out.get("returncode", 1))
            rec.skill_status = str(agent_out.get("skill_status", "not_called"))
            rec.skill_error = str(agent_out.get("skill_error", "")).strip()
            rec.tool_error_count = int(agent_out.get("tool_error_count", 0))
            safe_errors = _trace.sanitize_value(agent_out.get("tool_errors", []), secret_values)
            if isinstance(safe_errors, list):
                rec.tool_errors = [item for item in safe_errors if isinstance(item, dict)]
            if agent_out.get("timed_out"):
                rec.agent_status = "timed_out"
                rec.stopped_reason = "OpenCode agent timed out"
            elif rec.agent_returncode != 0:
                rec.agent_status = "exited_nonzero"
                rec.stopped_reason = f"OpenCode agent exited with {rec.agent_returncode}"
            else:
                rec.agent_status = "completed"
                rec.stopped_reason = "session completed"

            skill_loaded = rec.skill_status == "loaded"
            candidate = (
                self._evaluate_current(sb, env, "final")
                if skill_loaded
                else _CandidateResult(False, None, None, "skill_not_loaded")
            )
            improved = bool(
                candidate.passed
                and candidate.score is not None
                and candidate.score < initial.score
            )

            rec.best_correct = skill_loaded and candidate.passed
            rec.best_time_ms = candidate.score if skill_loaded else None
            rec.tokens_used = int(agent_out.get("tokens_total", 0))
            rec.tool_calls = int(agent_out.get("tool_calls", 0))
            if skill_loaded:
                rec.sessions.append(
                    SessionRecord(
                        1,
                        candidate.failure_reason != "compile_error",
                        "passed" if candidate.passed else candidate.failure_reason,
                        candidate.score,
                        candidate.variance_pct,
                        improved,
                        int(agent_out.get("tokens_in", 0)),
                        int(agent_out.get("tokens_out", 0)),
                        int(agent_out.get("tokens_reasoning", 0)),
                        rec.tool_calls,
                        str(agent_out.get("note", "")),
                    )
                )

            final_kernel = _read(sb.kernel_path)
            (sb.root / "final-kernel.cpp").write_text(final_kernel, encoding="utf-8")
            (sb.root / "kernel.diff").write_text(
                "".join(
                    difflib.unified_diff(
                        initial_kernel.splitlines(keepends=True),
                        final_kernel.splitlines(keepends=True),
                        fromfile="initial-kernel.cpp",
                        tofile="final-kernel.cpp",
                    )
                ),
                encoding="utf-8",
            )
            rec.sessions_used = len(rec.sessions)
            rec.prompt_sha256 = _sha256_text(prompt)
            rec.initial_kernel_sha256 = _sha256_text(initial_kernel)
            rec.final_kernel_sha256 = _sha256_text(final_kernel)
            rec.changed_kernel = initial_kernel != final_kernel
            rec.repository_revision = _repository_revision(self.source_root)
            rec.final_kernel_path = str(sb.kernel_path)
            rec.final_compile_options_path = str(options_path)
            metadata = {
                "case_key": sb.case_key,
                "model": rec.model,
                "reasoning_effort": rec.reasoning_effort,
                "run_id": sb.run_id,
                "repository_revision": rec.repository_revision,
                "prompt_sha256": rec.prompt_sha256,
                "initial_kernel_sha256": rec.initial_kernel_sha256,
                "final_kernel_sha256": rec.final_kernel_sha256,
                "changed_kernel": rec.changed_kernel,
            }
            (sb.root / "metadata.json").write_text(
                json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            (sb.root / "run-summary.json").write_text(
                json.dumps(asdict(rec), ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            return rec
        except Exception as exc:
            if sb.root.is_dir():
                (sb.root / "run-error.log").write_text(
                    _trace.clean_text(exc, secret_values) + "\n", encoding="utf-8"
                )
            raise
        finally:
            _sandbox.teardown(sb)


def _sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _repository_revision(source_root: Path) -> str:
    completed = _sandbox.run_pg(
        ["git", "-C", str(Path(source_root).resolve()), "rev-parse", "HEAD"],
        timeout=10.0,
        capture=True,
    )
    value = (completed.stdout or "").strip().lower()
    if completed.returncode != 0 or not re.fullmatch(r"[0-9a-f]{40}", value):
        return "unavailable"
    return value


def _write_tool_artifacts(log_dir: Path, tool: str, stdout: str, stderr: str) -> None:
    log_dir.mkdir(parents=True, exist_ok=True)
    stem = Path(tool).stem
    (log_dir / f"{stem}.stdout").write_text(stdout, encoding="utf-8")
    (log_dir / f"{stem}.stderr").write_text(stderr, encoding="utf-8")


def _write_agent_result(
    output: Path,
    agent_out: Dict[str, object],
    secrets: tuple[str, ...] = (),
) -> None:
    payload = {key: value for key, value in agent_out.items() if key != "raw"}
    safe = _trace.sanitize_value(payload, secrets)
    (output / "agent-result.json").write_text(
        json.dumps(safe, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


class subprocess_result:  # noqa: N801
    def __init__(self, returncode: int, stdout: str, stderr: str, kv: Dict[str, str]):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.kv = kv


def _kv_result(completed) -> subprocess_result:
    values: Dict[str, str] = {}
    for line in (completed.stdout or "").splitlines():
        if line and not line.startswith("#") and ":" in line:
            key, _, value = line.partition(":")
            values[key.strip()] = value.strip()
    return subprocess_result(
        completed.returncode,
        completed.stdout or "",
        completed.stderr or "",
        values,
    )


def _read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def _to_float(value: Optional[str]) -> Optional[float]:
    if value is None:
        return None
    try:
        return float(value.strip().rstrip("%"))
    except ValueError:
        return None


def assemble_prompt(
    task_md: str,
    kernel_src: str,
    state_block: str,
    platform_contract: str = "",
) -> str:
    if not platform_contract:
        platform_contract = f"Execution platform: {platform.system()} {platform.machine()}."
    return (
        "You are optimizing one CPU kernel. Load and follow the native "
        "`cpu-hpc-skill` Skill. Edit only `kernel.cpp` and, when useful, "
        "`compile_options.txt`. Validate every candidate with exactly "
        "`bash tools/test_candidate.sh`; use `bash tools/build.sh` only to repair "
        "compilation, and use `bash tools/profile.sh` or `bash tools/vec_report.sh` "
        "only when useful. Do not invoke check.sh or bench.sh directly. Public "
        "tests are diagnostic only, so preserve correctness for every input allowed "
        "by TASK. Leave the fastest validated version in the workspace before ending.\n\n"
        "=== PLATFORM ===\n"
        + platform_contract.strip()
        + "\n\n=== TASK ===\n"
        + task_md.strip()
        + "\n\n=== CURRENT kernel.cpp ===\n```cpp\n"
        + kernel_src.strip()
        + "\n```\n\n=== CURRENT STATE ===\n"
        + state_block.strip()
    )
