"""Compile and independently measure a submitted kernel on official sizes.

The harness links the public correctness reference, never a performance answer.
Performance scores are compared with numeric anchors from the selected task spec.

Build recipe (the same one used by tools/_common.sh):
    g++ -I<harness_src> -I<src> <CXXFLAGS> \
        <harness_src>/harness_main.cpp <harness_src>/kernel_reference.cpp \
        <src>/profiler/hwcounters.cpp -ldl -o <out>
    g++ -I<harness_src> -I<src> <CXXFLAGS> \
        <src>/worker/kernel_worker_main.cpp -ldl -o <out>.worker
    g++ -I<harness_src> <CXXFLAGS> -fPIC -shared -nostartfiles \
        <impl.cpp> -o <empty-stage>/kernel.so

The trusted harness deliberately does not link the submitted object.  It loads
the latter in a restricted worker process; only the parent emits/validates
results and owns the clock.

Measurement: clean recompile, official sizes, taskset -c cores,
fixed OMP_NUM_THREADS/PROC_BIND/PLACES, fresh verified warmups + median and CoV,
re-measure on high variance.
"""

from __future__ import annotations

import math
import os
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Tuple

try:
    from . import sandbox as _sandbox
    from . import isolation as _isolation
    from . import compile_options as _compile_options
except ImportError:
    import sandbox as _sandbox  # type: ignore
    import isolation as _isolation  # type: ignore
    import compile_options as _compile_options  # type: ignore


# Named kernels intentionally include only the public starter. Formal evaluation
# normally supplies an explicit agent-generated kernel.cpp path.
IMPL_FILES = {
    "starter": "starter_kernel.cpp",
}

_INPUT_DOMAIN_RE = re.compile(r"[0-9a-f]{32}\Z")

TRUSTED_BLOCK_TARGET_MS = 50.0
BENCHMARK_SAMPLES = 11
PILOT_SAMPLES = 3
MAX_BLOCK_INVOCATIONS = 4096
MAX_BENCHMARK_SAMPLES = 101

def _validate_input_domain(input_domain: Optional[str]) -> Optional[str]:
    if input_domain is None:
        return None
    if not isinstance(input_domain, str) or not _INPUT_DOMAIN_RE.fullmatch(input_domain):
        raise ValueError("input_domain must be exactly 32 lowercase hexadecimal characters")
    return input_domain


def load_compile_options(path: Path | str) -> List[str]:
    """Load the validated kernel-only flags selected by an Agent run."""
    return _compile_options.load_options(path)


def resolve_task_pinning(
    spec: Mapping[str, object], pinning: Mapping[str, object]
) -> Tuple[str, int]:
    """Return the exact CPU list/thread count used by local and formal runs."""
    threads = int(spec.get("threads", pinning["omp_num_threads"]))
    cores = str(pinning["cores"])
    if threads == 1:
        # OMP_NUM_THREADS=1 alone does not prevent a submission from creating
        # pthreads, so single-thread tasks are also confined to one logical CPU.
        cores = cores.split(",", 1)[0].split("-", 1)[0]
    return cores, threads


@dataclass
class CompileResult:
    ok: bool
    binary: Optional[str]
    log: str
    warnings: int = 0


@dataclass
class SizeMeasurement:
    size: str
    correct: bool
    median_time_ms: Optional[float]
    gflops: Optional[float]
    variance_pct: Optional[float]
    max_abs_err: Optional[float] = None
    max_rel_err: Optional[float] = None
    block_median_time_ms: Optional[float] = None
    block_invocations: Optional[int] = None
    benchmark_samples: Optional[int] = None
    unstable: bool = False
    variance_retried: bool = False
    error: str = ""  # non-empty => runtime error / crash for this size


@dataclass
class TaskMeasurement:
    task: str
    impl: str
    compiled: bool
    compile_log: str
    per_size: Dict[str, SizeMeasurement] = field(default_factory=dict)
    correctness_all: bool = False  # all measured sizes passed check
    runtime_error: bool = False    # any size crashed (non-zero exit / no output)
    timed_out: bool = False        # any required check/bench hit the hard timeout
    unstable: bool = False         # final selected CoV exceeded trusted threshold


def resolve_impl_source(task_dir: Path, impl: str) -> Path:
    """Resolve an impl spec to a .cpp path.

    impl may be ``starter`` or a path to a kernel.cpp produced by an agent run.
    """
    p = task_dir / IMPL_FILES[impl] if impl in IMPL_FILES else Path(impl)
    if p.is_symlink():
        raise FileNotFoundError(
            f"impl source must be a regular file, not a symlink: {impl}"
        )
    if p.is_file():
        return p.resolve(strict=True)
    raise FileNotFoundError(f"impl source not found: {impl}")


def _paths_overlap(first: Path, second: Path) -> bool:
    try:
        first.relative_to(second)
        return True
    except ValueError:
        pass
    try:
        second.relative_to(first)
        return True
    except ValueError:
        return False


def _validate_submitted_compiler_roots(
    roots: Sequence[Path], protected_paths: Sequence[Path]
) -> None:
    """Reject compiler mounts that would also expose submitted/trusted data."""
    protected = tuple(Path(path).resolve() for path in protected_paths)
    for raw_root in roots:
        root = Path(raw_root).resolve(strict=True)
        if root == Path(root.anchor):
            raise _isolation.IsolationPolicyError(
                "compiler read-only root cannot be a filesystem root"
            )
        for path in protected:
            if _paths_overlap(root, path):
                raise _isolation.IsolationPolicyError(
                    f"compiler read-only root overlaps protected task data: {root}"
                )


def compile_harness(
    source_root: Path,
    harness_src: Path,
    impl_cpp: Path,
    cxxflags: str,
    out_binary: Path,
    cxx: str = "g++",
    cxx_readonly_roots: Optional[List[str]] = None,
    timeout: float = 300.0,
    api_key_env: str = "SJTU_API_KEY",
    extra_cxxflags: Optional[Sequence[str]] = None,
) -> CompileResult:
    """Compile the harness in a killable process group with a hard timeout."""
    source_root = Path(source_root).resolve()
    harness_src = Path(harness_src).resolve()
    impl_input = Path(impl_cpp)
    kernel_header_input = harness_src / "kernel.h"
    for label, path in (
        ("submitted kernel", impl_input),
        ("kernel SDK header", kernel_header_input),
    ):
        if path.is_symlink() or not path.is_file():
            return CompileResult(
                ok=False,
                binary=None,
                log=f"{label} must be an existing regular file\n",
            )
    impl_cpp = impl_input.resolve(strict=True)
    kernel_header = kernel_header_input.resolve(strict=True)

    compiler = _isolation.resolve_compiler_runtime(
        cxx,
        readonly_roots=cxx_readonly_roots or (),
    )
    _validate_submitted_compiler_roots(
        compiler.readonly_roots,
        (
            source_root,
            harness_src,
            impl_cpp.parent,
            out_binary.parent.resolve(),
        ),
    )
    cxx = str(compiler.executable)
    kernel_extra = _compile_options.validate_options(extra_cxxflags or ())
    out_binary.parent.mkdir(parents=True, exist_ok=True)
    worker_binary = Path(str(out_binary) + ".worker")
    kernel_so = Path(str(out_binary) + ".kernel.so")
    for artifact in (out_binary, worker_binary, kernel_so):
        try:
            artifact.unlink()
        except FileNotFoundError:
            pass
        except OSError as exc:
            return CompileResult(
                ok=False,
                binary=None,
                log=f"cannot prepare compile output: {exc}\n",
            )
    cmd = [
        cxx,
        f"-I{harness_src}", f"-I{source_root}",
        *cxxflags.split(),
        str(harness_src / "harness_main.cpp"),
        str(harness_src / "kernel_reference.cpp"),
        str(source_root / "profiler" / "hwcounters.cpp"),
        "-ldl",
        "-o", str(out_binary),
    ]
    worker_cmd = [
        cxx,
        f"-I{harness_src}", f"-I{source_root}",
        *cxxflags.split(),
        str(source_root / "worker" / "kernel_worker_main.cpp"),
        "-ldl", "-o", str(worker_binary),
    ]
    # ``impl_cpp`` is agent-generated input.  A plain subprocess.run() could
    # leave a compiler (or one of its children) alive until HelloHPC's 2-hour
    # workflow timeout, so use the same process-group timeout as every other
    # untrusted build/run boundary.
    # Formal compilation is hermetic: never let ambient include/library/GCC or
    # OpenMP variables change the attested compiler, link result or thread
    # policy.  ``run_isolated`` supplies its own fixed PATH/locale and accepts
    # only the explicit entries below.
    env: Dict[str, str] = {}
    trusted_readonly_inputs = [
        source_root, harness_src, *compiler.readonly_roots
    ]
    submitted_readonly_inputs = [
        impl_cpp, kernel_header, *compiler.readonly_roots
    ]
    compiler_runtime_paths = (
        *_isolation.DEFAULT_RUNTIME_PATHS,
        *(str(path) for path in compiler.readonly_roots),
    )
    writable_outputs = [out_binary.parent]
    proc = _isolation.run_isolated(
        cmd,
        timeout=timeout,
        readonly_paths=trusted_readonly_inputs,
        writable_paths=writable_outputs,
        env=env,
        cwd=out_binary.parent,
        api_key_env=api_key_env,
        runtime_paths=compiler_runtime_paths,
        capture=True,
    )
    log = (proc.stdout or "") + (proc.stderr or "")
    kernel_so = Path(str(out_binary) + ".kernel.so")
    if proc.returncode == 0:
        worker_proc = _isolation.run_isolated(
            worker_cmd,
            timeout=timeout,
            readonly_paths=trusted_readonly_inputs,
            writable_paths=writable_outputs,
            env=env,
            cwd=out_binary.parent,
            api_key_env=api_key_env,
            runtime_paths=compiler_runtime_paths,
            capture=True,
        )
        log += (worker_proc.stdout or "") + (worker_proc.stderr or "")
        if worker_proc.returncode != 0:
            proc = worker_proc
    if proc.returncode == 0:
        try:
            # The submitted compiler gets a fresh output directory.  In
            # particular it cannot read a previously built trusted harness via
            # an include or assembler .incbin directive.
            with tempfile.TemporaryDirectory(
                prefix=".cheatsheet-submitted-kernel-", dir=out_binary.parent
            ) as stage_name:
                stage = Path(stage_name).resolve()
                staged_kernel = stage / "kernel.so"
                kernel_cmd = [
                    cxx,
                    f"-I{harness_src}",
                    *cxxflags.split(),
                    *kernel_extra,
                    "-fPIC", "-shared", "-nostartfiles",
                    str(impl_cpp), "-o", str(staged_kernel),
                ]
                kernel_proc = _isolation.run_isolated(
                    kernel_cmd,
                    timeout=timeout,
                    readonly_paths=submitted_readonly_inputs,
                    writable_paths=[stage],
                    env=env,
                    cwd=stage,
                    api_key_env=api_key_env,
                    runtime_paths=compiler_runtime_paths,
                    capture=True,
                )
                log += (kernel_proc.stdout or "") + (kernel_proc.stderr or "")
                if kernel_proc.returncode != 0:
                    proc = kernel_proc
                elif not staged_kernel.is_file():
                    proc = subprocess.CompletedProcess(
                        kernel_cmd, 70, "", "submitted compiler produced no object\n"
                    )
                    log += proc.stderr or ""
                else:
                    os.replace(staged_kernel, kernel_so)
        except OSError as exc:
            proc = subprocess.CompletedProcess(
                [cxx], 70, "", f"submitted compile staging failed: {exc}\n"
            )
            log += proc.stderr or ""
    warnings = log.count("warning:")
    ok = (
        proc.returncode == 0
        and out_binary.is_file()
        and worker_binary.is_file()
        and kernel_so.is_file()
    )
    return CompileResult(ok=ok, binary=str(out_binary) if ok else None, log=log, warnings=warnings)


def _parse_kv(stdout: str) -> Dict[str, str]:
    kv: Dict[str, str] = {}
    for line in stdout.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        k, _, v = line.partition(":")
        kv[k.strip()] = v.strip()
    return kv


def _run_harness(
    binary: Path, mode: str, size: str, cores: str, omp_threads: int,
    omp_bind: str, omp_places: str, timeout: float, reps: Optional[int] = None,
    seed: Optional[int] = None, single_thread: bool = False,
    api_key_env: str = "SJTU_API_KEY",
    input_domain: Optional[str] = None,
    block_invocations: Optional[int] = None,
) -> subprocess.CompletedProcess:
    input_domain = _validate_input_domain(input_domain)
    # Do not inherit ambient OMP/GOMP or compiler search settings.  In
    # particular, OMP_DYNAMIC=TRUE would silently shrink the measured team.
    env: Dict[str, str] = {}
    env["OMP_NUM_THREADS"] = "1" if single_thread else str(omp_threads)
    env["OMP_PROC_BIND"] = omp_bind
    env["OMP_PLACES"] = omp_places
    env["OMP_DYNAMIC"] = "FALSE"
    # Also pin the reference/BLAS-ish env knobs proposal §9 asks to fix.
    env["OPENBLAS_NUM_THREADS"] = "1"
    env["MKL_NUM_THREADS"] = "1"
    kernel_so = Path(str(binary) + ".kernel.so")
    worker_binary = Path(str(binary) + ".worker")
    if not kernel_so.is_file() or not worker_binary.is_file():
        return subprocess.CompletedProcess(
            [str(binary)], 70, "", "isolated kernel shared object missing"
        )
    env["CHEATSHEET_KERNEL_SO"] = str(kernel_so.resolve())
    env["CHEATSHEET_KERNEL_WORKER"] = str(worker_binary.resolve())
    stdin_text = None
    if input_domain is not None:
        # argv/env reveal only that paired inputs are in use.  The secret
        # domain itself crosses the isolation boundary solely via stdin.
        env["CHEATSHEET_PAIRED_INPUTS"] = "stdin"
        stdin_text = input_domain + "\n"
    cmd = ["taskset", "-c", cores, str(binary), mode, size]
    if reps is not None:
        cmd += ["--reps", str(reps)]
    if block_invocations is not None:
        cmd += ["--block-invocations", str(block_invocations)]
    if seed is not None:
        cmd += ["--seed", str(seed)]
    # Runtime receives no writable host mount at all.  The trusted parent,
    # minimal worker and submitted shared object are visible only read-only;
    # HOME and /tmp are private and the network namespace has no interfaces.
    return _isolation.run_isolated(
        cmd,
        timeout=timeout,
        readonly_paths=[binary, worker_binary, kernel_so],
        writable_paths=(),
        env=env,
        cwd=None,
        api_key_env=api_key_env,
        capture=True,
        stdin_text=stdin_text,
    )


def _to_float(s: Optional[str]) -> Optional[float]:
    if s is None:
        return None
    s = s.strip().rstrip("%")
    try:
        return float(s)
    except ValueError:
        return None


def _to_positive_int(s: Optional[str]) -> Optional[int]:
    if s is None or re.fullmatch(r"[1-9][0-9]*", s.strip()) is None:
        return None
    return int(s)


def _validate_benchmark_samples(value: int) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < 1
        or value > MAX_BENCHMARK_SAMPLES
        or value % 2 != 1
    ):
        raise ValueError("benchmark_samples must be a bounded positive odd integer")
    return value


def _validate_block_map(
    sizes: Sequence[str], values: Optional[Mapping[str, int]]
) -> Optional[Dict[str, int]]:
    if values is None:
        return None
    if not isinstance(values, Mapping) or set(values) != set(sizes):
        raise ValueError("block_invocations must contain exactly the measured sizes")
    result: Dict[str, int] = {}
    for size in sizes:
        value = values[size]
        if (
            isinstance(value, bool)
            or not isinstance(value, int)
            or value < 1
            or value > MAX_BLOCK_INVOCATIONS
        ):
            raise ValueError("block invocation count is outside the trusted bound")
        result[size] = value
    return result


def _validate_pilot_scales(
    sizes: Sequence[str], values: Optional[Mapping[str, float]]
) -> Dict[str, float]:
    if values is None:
        return {size: 1.0 for size in sizes}
    if not isinstance(values, Mapping) or set(values) != set(sizes):
        raise ValueError("pilot_scale_by_size must contain exactly the measured sizes")
    result: Dict[str, float] = {}
    for size in sizes:
        raw = values[size]
        if isinstance(raw, bool) or not isinstance(raw, (int, float)):
            raise ValueError("pilot scale must be numeric")
        value = float(raw)
        if not math.isfinite(value) or value <= 0.0 or value > 1.0:
            raise ValueError("pilot scale must be finite and in (0, 1]")
        result[size] = value
    return result


@dataclass(frozen=True)
class _ParsedBenchmark:
    median_time_ms: float
    block_median_time_ms: float
    variance_pct: float
    gflops: Optional[float]
    block_invocations: int
    benchmark_samples: int


def _parse_benchmark(
    stdout: str,
    *,
    expected_size: str,
    expected_block_invocations: int,
    expected_samples: int,
) -> Optional[_ParsedBenchmark]:
    required = {
        "median_time_ms",
        "block_median_time_ms",
        "variance",
        "block_invocations",
        "benchmark_samples",
        "size",
    }
    seen = set()
    for raw_line in stdout.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key = line.partition(":")[0].strip()
        if key in required:
            if key in seen:
                return None
            seen.add(key)
    if seen != required:
        return None
    values = _parse_kv(stdout)
    if values.get("size") != expected_size:
        return None
    median = _to_float(values.get("median_time_ms"))
    block_median = _to_float(values.get("block_median_time_ms"))
    variance = _to_float(values.get("variance"))
    block_count = _to_positive_int(values.get("block_invocations"))
    samples = _to_positive_int(values.get("benchmark_samples"))
    if (
        median is None
        or block_median is None
        or variance is None
        or not math.isfinite(median)
        or not math.isfinite(block_median)
        or not math.isfinite(variance)
        or median <= 0.0
        or block_median <= 0.0
        or variance < 0.0
        or block_count != expected_block_invocations
        or samples != expected_samples
    ):
        return None
    # Both fields come from the trusted clock.  Check their relationship too,
    # allowing only the bounded decimal formatting error of the harness.
    expected_block_ms = median * block_count
    tolerance_ms = 2e-6 + block_count * 1e-6
    if abs(block_median - expected_block_ms) > tolerance_ms:
        return None
    return _ParsedBenchmark(
        median, block_median, variance, _to_float(values.get("gflops")),
        block_count, samples
    )


def _block_count_from_pilot(pilot_ms: float, scale: float) -> Optional[int]:
    effective_ms = pilot_ms * scale
    if not math.isfinite(effective_ms) or effective_ms <= 0.0:
        return None
    # Test before ceil so an anomalously tiny pilot cannot create an unbounded
    # loop or integer conversion.  The trusted cap also bounds total runtime.
    if effective_ms * MAX_BLOCK_INVOCATIONS < TRUSTED_BLOCK_TARGET_MS:
        return None
    return max(1, int(math.ceil(TRUSTED_BLOCK_TARGET_MS / effective_ms)))


def measure_task(
    task: str,
    impl: str,
    source_root: Path,
    cxxflags: str,
    sizes: List[str],
    weights: Optional[Dict[str, float]] = None,
    cores: str = "0-31",
    omp_threads: int = 32,
    omp_bind: str = "close",
    omp_places: str = "cores",
    bench_timeout: float = 600.0,
    check_timeout: float = 300.0,
    compile_timeout: float = 300.0,
    variance_threshold_pct: float = 10.0,
    cxx: str = "g++",
    cxx_readonly_roots: Optional[List[str]] = None,
    harness_src: Optional[Path] = None,
    work_binary_dir: Optional[Path] = None,
    seed: int = 12345,
    api_key_env: str = "SJTU_API_KEY",
    extra_cxxflags: Optional[Sequence[str]] = None,
    input_domain: Optional[str] = None,
    block_invocations: Optional[Mapping[str, int]] = None,
    derive_block_invocations: bool = False,
    pilot_scale_by_size: Optional[Mapping[str, float]] = None,
    benchmark_samples: int = BENCHMARK_SAMPLES,
) -> TaskMeasurement:
    """Independent measurement of one (task, impl) over the given sizes.

    Clean recompile, then per size: check (correctness vs reference) + bench
    (median per-invocation time from verified blocks). Re-benches once if
    variance exceeds the threshold, then rejects a still-unstable result.

    Formal callers derive block counts only from the attested starter and pass
    the returned exact counts to candidate and post-sentinel measurements.
    """
    input_domain = _validate_input_domain(input_domain)
    if not sizes or len(sizes) != len(set(sizes)):
        raise ValueError("sizes must be a non-empty unique ordered list")
    samples = _validate_benchmark_samples(benchmark_samples)
    selected_blocks = _validate_block_map(sizes, block_invocations)
    if not isinstance(derive_block_invocations, bool):
        raise ValueError("derive_block_invocations must be a boolean")
    if derive_block_invocations and selected_blocks is not None:
        raise ValueError("cannot derive and supply block counts together")
    if not derive_block_invocations and pilot_scale_by_size is not None:
        raise ValueError("pilot scales require trusted block derivation")
    pilot_scales = (
        _validate_pilot_scales(sizes, pilot_scale_by_size)
        if derive_block_invocations
        else {}
    )
    variance_threshold_pct = float(variance_threshold_pct)
    if not math.isfinite(variance_threshold_pct) or variance_threshold_pct < 0.0:
        raise ValueError("variance_threshold_pct must be finite and non-negative")
    source_root = Path(source_root).resolve()
    task_dir = source_root / "tasks" / task
    harness_src = Path(harness_src).resolve() if harness_src else task_dir
    impl_cpp = resolve_impl_source(task_dir, impl)

    tm = TaskMeasurement(task=task, impl=impl, compiled=False, compile_log="")

    tmpdir = None
    if work_binary_dir is None:
        tmpdir = tempfile.mkdtemp(prefix=f"cheatsheet_{task}_{_safe(impl)}_")
        bin_dir = Path(tmpdir)
    else:
        bin_dir = Path(work_binary_dir)
        bin_dir.mkdir(parents=True, exist_ok=True)

    try:
        out_bin = bin_dir / "harness"
        cr = compile_harness(
            source_root,
            harness_src,
            impl_cpp,
            cxxflags,
            out_bin,
            cxx=cxx,
            cxx_readonly_roots=cxx_readonly_roots,
            timeout=compile_timeout,
            api_key_env=api_key_env,
            extra_cxxflags=extra_cxxflags,
        )
        tm.compile_log = cr.log
        tm.compiled = cr.ok
        if not cr.ok:
            return tm

        for size in sizes:
            sm = SizeMeasurement(size=size, correct=False, median_time_ms=None,
                                 gflops=None, variance_pct=None)
            # --- correctness ---
            cp = _run_harness(out_bin, "check", size, cores, omp_threads, omp_bind,
                              omp_places, timeout=check_timeout, seed=seed,
                              api_key_env=api_key_env,
                              input_domain=input_domain)
            if cp.returncode != 0:
                if cp.returncode == 124:
                    sm.error = "check timed out"
                    tm.timed_out = True
                else:
                    sm.error = f"check exit {cp.returncode}"
                    tm.runtime_error = True
                tm.per_size[size] = sm
                continue
            ck = _parse_kv(cp.stdout)
            sm.correct = ck.get("correctness") == "passed"
            sm.max_abs_err = _to_float(ck.get("max_abs_err"))
            sm.max_rel_err = _to_float(ck.get("max_rel_err"))

            if not sm.correct:
                tm.per_size[size] = sm
                continue

            # --- trusted block policy ---
            if derive_block_invocations:
                pilot = _run_harness(
                    out_bin, "bench", size, cores, omp_threads, omp_bind,
                    omp_places, timeout=bench_timeout, reps=PILOT_SAMPLES,
                    seed=seed, api_key_env=api_key_env,
                    input_domain=input_domain, block_invocations=1,
                )
                if pilot.returncode != 0:
                    if pilot.returncode == 124:
                        sm.error = "benchmark pilot timed out"
                        tm.timed_out = True
                    else:
                        sm.error = f"benchmark pilot exit {pilot.returncode}"
                        tm.runtime_error = True
                    tm.per_size[size] = sm
                    continue
                pilot_result = _parse_benchmark(
                    pilot.stdout,
                    expected_size=size,
                    expected_block_invocations=1,
                    expected_samples=PILOT_SAMPLES,
                )
                if pilot_result is None:
                    sm.error = "invalid benchmark pilot output"
                    tm.runtime_error = True
                    tm.per_size[size] = sm
                    continue
                block_count = _block_count_from_pilot(
                    pilot_result.median_time_ms, pilot_scales[size]
                )
                if block_count is None:
                    sm.error = "trusted timing window exceeds safe block cap"
                    sm.unstable = True
                    tm.unstable = True
                    tm.per_size[size] = sm
                    continue
            else:
                block_count = (
                    selected_blocks[size] if selected_blocks is not None else 1
                )

            # --- verified block benchmark ---
            cp = _run_harness(out_bin, "bench", size, cores, omp_threads, omp_bind,
                              omp_places, timeout=bench_timeout, reps=samples,
                              seed=seed,
                              api_key_env=api_key_env,
                              input_domain=input_domain,
                              block_invocations=block_count)
            if cp.returncode != 0:
                if cp.returncode == 124:
                    sm.error = "bench timed out"
                    tm.timed_out = True
                else:
                    sm.error = f"bench exit {cp.returncode}"
                    tm.runtime_error = True
                tm.per_size[size] = sm
                continue
            selected = _parse_benchmark(
                cp.stdout,
                expected_size=size,
                expected_block_invocations=block_count,
                expected_samples=samples,
            )
            if selected is None:
                sm.error = "invalid benchmark output"
                tm.runtime_error = True
                tm.per_size[size] = sm
                continue

            # Re-bench exactly once on high variance, with the same frozen K,
            # seed and hidden input domain.  A second unstable result is not
            # silently accepted.
            if selected.variance_pct > variance_threshold_pct:
                sm.variance_retried = True
                retry = _run_harness(
                    out_bin, "bench", size, cores, omp_threads, omp_bind,
                    omp_places, timeout=bench_timeout, reps=samples, seed=seed,
                    api_key_env=api_key_env, input_domain=input_domain,
                    block_invocations=block_count,
                )
                if retry.returncode == 0:
                    retry_result = _parse_benchmark(
                        retry.stdout,
                        expected_size=size,
                        expected_block_invocations=block_count,
                        expected_samples=samples,
                    )
                    if retry_result is None:
                        sm.error = "invalid variance retry output"
                        tm.runtime_error = True
                    elif retry_result.variance_pct < selected.variance_pct:
                        selected = retry_result
                elif retry.returncode == 124:
                    sm.error = "variance retry timed out"
                    tm.timed_out = True
                else:
                    sm.error = f"variance retry exit {retry.returncode}"
                    tm.runtime_error = True

            sm.median_time_ms = selected.median_time_ms
            sm.block_median_time_ms = selected.block_median_time_ms
            sm.gflops = selected.gflops
            sm.variance_pct = selected.variance_pct
            sm.block_invocations = selected.block_invocations
            sm.benchmark_samples = selected.benchmark_samples
            if selected.variance_pct > variance_threshold_pct:
                sm.unstable = True
                tm.unstable = True
            tm.per_size[size] = sm

        measured = [m for m in tm.per_size.values() if not m.error]
        tm.correctness_all = bool(measured) and all(m.correct for m in measured) and \
            len(measured) == len(sizes)
        return tm
    finally:
        if tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)


def _safe(s: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in s)[:40]
