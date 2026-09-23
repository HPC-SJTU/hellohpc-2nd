#!/usr/bin/env python3
"""Public self-test, requiring only Python and NumPy."""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from timing import CallTimeout, measured_process, worker_timer

ROOT = Path(__file__).resolve().parent
NAMES = ("points", "centers", "weights", "scales", "bias", "trig_scale", "trig_vec")
CASES = {"sample": (0, 0, 0), "public-large-b": (50, 1200, 35000)}


def load_solver(path):
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location("solver", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(command, timeout, cwd, env=None):
    started = time.perf_counter()
    process = subprocess.Popen(command, cwd=cwd, env=env, start_new_session=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        output, _ = process.communicate(timeout=timeout)
        elapsed = (time.perf_counter() - started) * 1000
        if output:
            print(output, end="", flush=True)
        if process.returncode:
            raise RuntimeError(f"worker exited with status {process.returncode}")
        return elapsed
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def select_cpus(text, small):
    available = os.sched_getaffinity(0)
    def core(cpu):
        base = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
        return ((base / "physical_package_id").read_text().strip(),
                (base / "core_id").read_text().strip())
    selected, seen = [], set()
    if text:
        for part in text.split(","):
            ends = list(map(int, part.split("-")))
            if len(ends) == 1:
                selected.append(ends[0])
            elif len(ends) == 2 and ends[0] <= ends[1]:
                selected.extend(range(ends[0], ends[1] + 1))
            else:
                raise ValueError("invalid CPU range")
    else:
        for cpu in sorted(available):
            key = core(cpu)
            if key not in seen:
                seen.add(key)
                selected.append(cpu)
            if len(selected) == 32:
                break
    if not selected or len(set(selected)) != len(selected) or not set(selected) <= available:
        raise ValueError("CPU IDs must be unique and available")
    if len({core(cpu) for cpu in selected}) != len(selected):
        raise ValueError("CPU IDs must represent different physical cores")
    if len(selected) != 32 and not (small and len(selected) < 32):
        raise ValueError("performance estimation requires 32 physical cores")
    os.sched_setaffinity(0, selected)
    return selected


def worker(args):
    import numpy as np
    if args._prepare:
        assert callable(load_solver(args.solver).compute_field)
        return
    invoke_measured = worker_timer(args.timing_fd)
    with np.load(args.input, allow_pickle=False) as archive:
        data = {name: archive[name] for name in NAMES}
    for array in data.values():
        array.flags.writeable = False
    solver = load_solver(args.solver)
    if args.case != "sample":
        warmup = {name: np.ascontiguousarray(value[:min(2048, max(1, len(data["points"]) - 1)) if name == "points" else 48])
                  for name, value in data.items()}
        solver.compute_field(**warmup)
    output = invoke_measured(solver.compute_field, data)
    if (not isinstance(output, np.ndarray) or output.dtype != np.float32
            or output.shape != (len(data["points"]),) or not np.isfinite(output).all()):
        raise ValueError("output must be a finite float32 array of shape (Q,)")
    with args.output.open("wb") as stream:
        np.save(stream, output, allow_pickle=False)
        stream.flush()
        os.fsync(stream.fileno())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", nargs="?", choices=CASES, default="sample")
    parser.add_argument("--solver", type=Path, default=ROOT / "src/solver.py")
    parser.add_argument("--env", type=Path, default=ROOT / "env.sh")
    parser.add_argument("--cpu-list", help="32 physical CPUs, e.g. 0-31")
    parser.add_argument("--json", type=Path)
    for flag in ("_loaded", "_prepare", "_worker"):
        parser.add_argument("--" + flag, action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--timing-fd", type=str, help=argparse.SUPPRESS)
    parser.add_argument("--input", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--output", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args._worker or args._prepare:
        worker(args)
        return 0
    if not args._loaded:
        # Preserve only exported variables, like the backend environment snapshot.
        with tempfile.TemporaryDirectory(prefix="kernel-env-") as tmp:
            snapshot = Path(tmp) / "environment"
            capture = "import os,sys; open(sys.argv[1],'wb').write(b'\\0'.join(k+b'='+v for k,v in os.environb.items()))"
            run(["bash", "-l", "-c", 'set -e; source "$1"; "$2" -c "$3" "$4"',
                 "kernel-env", str(args.env.resolve()), sys.executable, capture, str(snapshot)],
                300, ROOT)
            environment = dict(item.split(b"=", 1) for item in snapshot.read_bytes().split(b"\0") if item)
        os.execve(sys.executable, [sys.executable, str(Path(__file__).resolve()),
                  *sys.argv[1:], "--_loaded"], environment)
    cpus = select_cpus(args.cpu_list, args.case == "sample")
    os.environ.update(OMP_NUM_THREADS="32", OMP_THREAD_LIMIT="32", OMP_DYNAMIC="FALSE",
                      OMP_PROC_BIND="close", OMP_PLACES="cores", OPENBLAS_NUM_THREADS="32",
                      MKL_NUM_THREADS="32", PYTHONHASHSEED="0")
    cap = 64 * 1024**3
    _, hard = resource.getrlimit(resource.RLIMIT_AS)
    cap = min(cap, hard) if hard != resource.RLIM_INFINITY else cap
    resource.setrlimit(resource.RLIMIT_AS, (cap, cap))
    import numpy as np
    input_path = ROOT / "data" / f"{args.case}.npz"
    expected = np.loadtxt(ROOT / "data" / f"{args.case}.ref.txt", dtype=np.float32, ndmin=1)
    args.solver = args.solver.resolve()
    if args.json:
        destination = args.json.resolve()
        if destination in (args.solver, args.env.resolve(), Path(__file__).resolve()) or ROOT / "data" in destination.parents:
            raise ValueError("JSON destination cannot overwrite source, env.sh, or data")
    samples, outputs = [], []
    print(f"case={args.case}; physical CPUs={cpus}", flush=True)
    with tempfile.TemporaryDirectory(prefix="kernel-benchmark-") as tmp:
        temporary = Path(tmp)
        prepared = temporary / "prepared"
        shutil.copytree(args.solver.parent, prepared)
        run([sys.executable, __file__, "--_prepare", "--solver",
             str(prepared / args.solver.name)], 300, temporary)
        for index in range(1 if args.case == "sample" else 3):
            folder = temporary / str(index)
            shutil.copytree(prepared, folder / "src")
            shutil.copyfile(input_path, folder / "input.npz")
            (folder / "home").mkdir()
            (folder / "tmp").mkdir()
            env = os.environ.copy()
            env.update(HOME=str(folder / "home"), TMPDIR=str(folder / "tmp"),
                       PYTHONPYCACHEPREFIX=str(folder / "tmp/pycache"))
            elapsed, worker_output = measured_process([sys.executable, __file__, args.case, "--_worker",
                "--solver", str(folder / "src" / args.solver.name),
                "--input", str(folder / "input.npz"), "--output", str(folder / "output.npy")],
                folder, env, total_seconds=120, call_seconds=70)
            if worker_output:
                print(worker_output, end="")
            outputs.append(np.load(folder / "output.npy", allow_pickle=False))
            samples.append(elapsed)
            print(f"sample {index+1}: {elapsed:.3f} ms", flush=True)
    passed = all(x.shape == expected.shape and np.isfinite(x).all()
                 and np.allclose(x, expected, rtol=1e-4, atol=1e-5) for x in outputs)
    elapsed = max(samples)
    weight, full, zero = CASES[args.case]
    score = weight * max(0.0, min(1.0, math.log(zero / elapsed) / math.log(zero / full))) if passed and weight else 0
    print(f"correctness: {'passed' if passed else 'FAILED'}; maximum: {elapsed:.3f} ms")
    print(f"estimated proxy score: {score:.4f}/{weight} (configured thresholds; local estimate only)" if weight
          else "sample: unscored; no total-score estimate")
    if args.json:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(dict(case=args.case, samples_ms=samples,
            runtime_ms=elapsed, timing_scope="compute_field-call", correctness=passed, score=score, max_score=weight,
            cpus=cpus, provisional=False), indent=2) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (subprocess.TimeoutExpired, CallTimeout):
        print("TIMEOUT: no score; optimize the solver before testing large inputs.", file=sys.stderr)
        raise SystemExit(1)
    except (RuntimeError, ValueError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
