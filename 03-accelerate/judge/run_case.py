#!/usr/bin/env python3
"""Measure the formal compute_field call, excluding preparation and validation."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
from types import ModuleType

import numpy as np


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from timing import CallTimeout, measured_process, worker_timer
_INPUT_NAMES = (
    "points",
    "centers",
    "weights",
    "scales",
    "bias",
    "trig_scale",
    "trig_vec",
)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run one Kernel Field case")
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--kind", choices=("sample", "correctness", "performance"), required=True)
    parser.add_argument("--warmup", type=int, required=True)
    parser.add_argument("--repeat", type=int, required=True)
    parser.add_argument(
        "--worker-timeout-seconds",
        type=float,
        default=120.0,
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--timing-fd", type=str, help=argparse.SUPPRESS)
    parser.add_argument("--call-timeout-seconds", type=float, default=None)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--worker-protocol", type=Path, help=argparse.SUPPRESS)
    return parser.parse_args()


def _load_solver(path: Path) -> ModuleType:
    resolved = path.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"solver not found: {resolved}")
    sys.path.insert(0, str(resolved.parent))
    spec = importlib.util.spec_from_file_location("contestant_solver", resolved)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load solver: {resolved}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, "compute_field", None)):
        raise TypeError("solver.py must define callable compute_field")
    return module


def _load_case(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=False) as archive:
        missing = [name for name in _INPUT_NAMES if name not in archive]
        if missing:
            raise ValueError(f"input archive is missing: {', '.join(missing)}")
        case = {
            name: np.ascontiguousarray(archive[name], dtype=np.float32)
            for name in _INPUT_NAMES
        }
    for value in case.values():
        value.flags.writeable = False
    return case


def _make_worker_warmup(case: dict[str, np.ndarray]) -> dict[str, np.ndarray]:
    """Build a small same-dimension input that cannot contain the full answer."""
    formal_q_count = int(case["points"].shape[0])
    q_count = min(2048, max(1, formal_q_count - 1))
    c_count = min(48, int(case["centers"].shape[0]))
    warmup = {
        "points": np.ascontiguousarray(case["points"][:q_count]),
        "centers": np.ascontiguousarray(case["centers"][:c_count]),
        "weights": np.ascontiguousarray(case["weights"][:c_count]),
        "scales": np.ascontiguousarray(case["scales"][:c_count]),
        "bias": np.ascontiguousarray(case["bias"][:c_count]),
        "trig_scale": np.ascontiguousarray(case["trig_scale"][:c_count]),
        "trig_vec": np.ascontiguousarray(case["trig_vec"][:c_count]),
    }
    for value in warmup.values():
        value.flags.writeable = False
    return warmup


def _validate_output(value: object, q_count: int) -> np.ndarray:
    if not isinstance(value, np.ndarray):
        raise TypeError("compute_field must return numpy.ndarray")
    if value.dtype != np.float32:
        raise TypeError(f"compute_field must return float32, got {value.dtype}")
    if value.shape != (q_count,):
        raise ValueError(f"compute_field must return shape ({q_count},), got {value.shape}")
    if not np.isfinite(value).all():
        raise ValueError("compute_field returned NaN or infinity")
    return np.ascontiguousarray(value)


def _invoke(solver: ModuleType, case: dict[str, np.ndarray]) -> np.ndarray:
    result = solver.compute_field(**case)
    return _validate_output(result, int(case["points"].shape[0]))


@contextmanager
def _silence_contestant_output():
    """Keep contestant stdout/stderr out of the trusted runner protocol."""
    sys.stdout.flush()
    sys.stderr.flush()
    saved_stdout = os.dup(1)
    saved_stderr = os.dup(2)
    null_fd = os.open(os.devnull, os.O_WRONLY)
    try:
        os.dup2(null_fd, 1)
        os.dup2(null_fd, 2)
        yield
    finally:
        for stream in (sys.stdout, sys.stderr):
            try:
                stream.flush()
            except (OSError, ValueError):
                pass
        os.dup2(saved_stdout, 1)
        os.dup2(saved_stderr, 2)
        os.close(null_fd)
        os.close(saved_stdout)
        os.close(saved_stderr)


def _write_output(path: Path, values: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        with temporary.open("w", encoding="utf-8") as stream:
            np.savetxt(stream, values, fmt="%.9g")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _write_protocol_output(path: Path, timings_ms: list[float], timed_out=False) -> None:
    payload = {
        "outputs": {
            "runtime_ms": max(timings_ms) if timings_ms else 0.0,
            "timed_out": timed_out,
            "timing_scope": "compute_field-call",
            "samples": timings_ms,
        }
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        temporary.write_text(
            json.dumps(payload, separators=(",", ":"), allow_nan=False) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _write_worker_output(path: Path, values: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    array_path = path.with_suffix(".npy")
    array_temporary = array_path.with_name(f".{array_path.name}.tmp-{os.getpid()}")
    protocol_temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        with array_temporary.open("wb") as stream:
            np.save(stream, values, allow_pickle=False)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(array_temporary, array_path)
        protocol_temporary.write_text(
            json.dumps({"status": "completed"}, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        os.replace(protocol_temporary, path)
    finally:
        array_temporary.unlink(missing_ok=True)
        protocol_temporary.unlink(missing_ok=True)


def _run_worker(args: argparse.Namespace) -> int:
    if args.worker_protocol is None:
        raise ValueError("--worker-protocol is required in worker mode")
    invoke_measured = worker_timer(args.timing_fd)
    case = _load_case(args.input)
    with _silence_contestant_output():
        solver = _load_solver(args.solver)
        if args.kind == "performance":
            warmup_case = _make_worker_warmup(case)
            for _ in range(args.warmup):
                _invoke(solver, warmup_case)
        raw_candidate = invoke_measured(solver.compute_field, case)
    candidate = _validate_output(raw_candidate, int(case["points"].shape[0]))
    _write_worker_output(args.worker_protocol, candidate)
    return 0


def _invoke_fresh_worker(
    args: argparse.Namespace,
    temporary_root: Path,
    label: str,
    expected_q_count: int,
) -> tuple[np.ndarray, float]:
    worker_root = temporary_root / label
    protocol = worker_root / "result.json"
    worker_source = worker_root / "src"
    shutil.copytree(args.solver.resolve().parent, worker_source)
    worker_input = worker_root / "input.npz"
    shutil.copyfile(args.input.resolve(), worker_input)
    worker_home = worker_root / "home"
    worker_tmp = worker_root / "tmp"
    worker_home.mkdir()
    worker_tmp.mkdir()
    worker_solver = worker_source / args.solver.name
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--worker",
        "--worker-protocol",
        str(protocol),
        "--solver",
        str(worker_solver),
        "--input",
        str(worker_input),
        "--output",
        str(worker_root / "unused-output.txt"),
        "--kind",
        args.kind,
        f"--warmup={args.warmup}",
        "--repeat=1",
    ]
    environment = os.environ.copy()
    environment.pop("HELLOHPC_OUTPUT", None)
    environment.update(
        {
            "HOME": str(worker_home),
            "TMPDIR": str(worker_tmp),
            "PYTHONPYCACHEPREFIX": str(worker_tmp / "pycache"),
            "OMP_NUM_THREADS": "32",
            "OMP_THREAD_LIMIT": "32",
            "OMP_DYNAMIC": "FALSE",
            "OMP_PROC_BIND": "close",
            "OMP_PLACES": "cores",
            "OPENBLAS_NUM_THREADS": "32",
            "MKL_NUM_THREADS": "32",
            "PYTHONHASHSEED": "0",
        }
    )
    call_limit = args.call_timeout_seconds
    if call_limit is None:
        call_limit = 5.0 if args.kind == "correctness" else 70.0
    elapsed_ms, _ = measured_process(command, worker_root, environment,
        total_seconds=args.worker_timeout_seconds, call_seconds=call_limit)
    try:
        payload = json.loads(protocol.read_text(encoding="utf-8"))
        if payload.get("status") != "completed":
            raise ValueError("worker status is not completed")
        with (protocol.with_suffix(".npy")).open("rb") as stream:
            candidate = np.load(stream, allow_pickle=False)
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"isolated timing worker {label} produced invalid output") from exc
    candidate = _validate_output(candidate, expected_q_count)
    if not np.isfinite(elapsed_ms) or elapsed_ms <= 0.0:
        raise RuntimeError(f"isolated timing worker {label} produced invalid runtime")
    return candidate, elapsed_ms


def run_case(args: argparse.Namespace) -> tuple[np.ndarray, list[float]]:
    if args.warmup < 0:
        raise ValueError("warmup must be non-negative")
    if args.repeat < 1:
        raise ValueError("repeat must be positive")
    if not np.isfinite(args.worker_timeout_seconds) or args.worker_timeout_seconds <= 0.0:
        raise ValueError("worker timeout must be a positive finite number")

    if args.call_timeout_seconds is not None and (not np.isfinite(args.call_timeout_seconds) or args.call_timeout_seconds <= 0):
        raise ValueError("call timeout must be finite and positive")
    repeat = args.repeat if args.kind == "performance" else 1
    with np.load(args.input, allow_pickle=False) as archive:
        if "points" not in archive:
            raise ValueError("input archive is missing: points")
        expected_q_count = int(archive["points"].shape[0])
    with tempfile.TemporaryDirectory(prefix="kernel-field-timing-") as temporary:
        temporary_root = Path(temporary)
        outputs: list[np.ndarray] = []
        timings_ms: list[float] = []
        for index in range(repeat):
            candidate, elapsed_ms = _invoke_fresh_worker(
                args,
                temporary_root,
                f"sample-{index}",
                expected_q_count,
            )
            outputs.append(candidate)
            timings_ms.append(elapsed_ms)

    output = outputs[-1]
    for index, candidate in enumerate(outputs[:-1]):
        if not np.allclose(candidate, output, rtol=1.0e-4, atol=1.0e-5):
            raise RuntimeError(f"timed sample {index} does not match the final sample")
    return output, timings_ms


def main() -> int:
    args = _parse_args()
    protocol_path = os.environ.pop("HELLOHPC_OUTPUT", None)
    if args.worker:
        return _run_worker(args)
    if not protocol_path:
        raise RuntimeError("HELLOHPC_OUTPUT is not set")
    try:
        output, timings_ms = run_case(args)
    except CallTimeout as exc:
        print(f"timing timeout: {exc}", file=sys.stderr)
        _write_protocol_output(Path(protocol_path), [], timed_out=True)
        return 0
    _write_output(args.output, output)
    _write_protocol_output(Path(protocol_path), timings_ms)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
