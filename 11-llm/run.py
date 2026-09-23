#!/usr/bin/env python3
"""Build and run local Stage0/1/2 self-tests."""
from __future__ import annotations

import argparse
import contextlib
import fcntl
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid

CASES = {"stage1": "qwen-performance", "stage2": "dpsk-stage2"}
MODELS = {"stage1": "Qwen3-14B", "stage2": "DeepSeek-V4-Flash-0731-w8a8"}
BUDGETS = {"stage0": 7200, "stage1": 1020, "stage2": 4800}
ROOT = Path(__file__).resolve().parent
BUILDER = "/nfs/bin/ubuntu-26.04-arm64-builder.sif"
BUILD_SCRIPT = '''set -eu
unset SINGULARITY_BIND SINGULARITY_BINDPATH APPTAINER_BIND APPTAINER_BINDPATH
mkdir -p /build/tmp /build/cache /build/home
export TMPDIR=/build/tmp HOME=/build/home APPTAINER_TMPDIR=/build/tmp SINGULARITY_TMPDIR=/build/tmp APPTAINER_CACHEDIR=/build/cache SINGULARITY_CACHEDIR=/build/cache
singularity build --fakeroot --ignore-fakeroot-command --mksquashfs-args "-mem 4G -processors 12" /build/image.sif build.def
'''


def command(argv, *, deadline, log, cwd, env=None):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("stage budget exhausted")
    with log.open("ab") as stream:
        process = subprocess.Popen(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                   stdout=stream, stderr=stream, start_new_session=True)
        marker = log.parent / f".process-{process.pid}"
        marker.touch()
        try:
            code = process.wait(timeout=remaining)
        except BaseException:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
        finally:
            marker.unlink(missing_ok=True)
    if code:
        raise RuntimeError(f"command exited {code}: {argv[0]} (log: {log})")


@contextlib.contextmanager
def cleanup_watcher(output, *, executable=None, instance=None, env=None, temporary=None, build_dir=None):
    """EOF survives CLI SIGKILL; watcher owns cleanup of this invocation only."""
    read_fd, write_fd = os.pipe()
    ready_read, ready_write = os.pipe()
    watcher = os.fork()
    if watcher == 0:
        os.close(write_fd)
        os.close(ready_read)
        os.setsid()
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        with open(os.devnull, "r+b") as null:
            for descriptor in (0, 1, 2):
                os.dup2(null.fileno(), descriptor)
        os.write(ready_write, b"1")
        os.close(ready_write)
        try:
            while os.read(read_fd, 1):
                pass
            for marker in output.glob(".process-*"):
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(int(marker.name.split("-")[-1]), signal.SIGKILL)
                marker.unlink(missing_ok=True)
            if instance:
                command([executable, "instance", "stop", "--force", instance],
                        deadline=time.monotonic() + 30, log=output / "cleanup.log", cwd=output, env=env)
            if temporary:
                temporary.unlink(missing_ok=True)
            if build_dir and build_dir.exists():
                shutil.rmtree(build_dir)
        except BaseException:
            os._exit(1)
        os._exit(0)
    os.close(read_fd)
    os.close(ready_write)
    ready = os.read(ready_read, 1)
    os.close(ready_read)
    if ready != b"1":
        os.close(write_fd)
        os.waitpid(watcher, 0)
        raise RuntimeError("cleanup watcher did not initialize")
    try:
        yield
    finally:
        os.close(write_fd)
        old_handlers = {sig: signal.signal(sig, signal.SIG_IGN) for sig in (signal.SIGINT, signal.SIGTERM)}
        try:
            _, status = os.waitpid(watcher, 0)
        finally:
            for sig, handler in old_handlers.items():
                signal.signal(sig, handler)
        if status:
            raise RuntimeError(f"cleanup failed ({instance or 'build'}); see {output}")


def work_directory(args):
    value = args.work_dir or (os.environ.get("PWD", "") if args.cli else str(Path.cwd()))
    source = "--work-dir / input work_dir" if args.work_dir else ("inherited PWD" if args.cli else "cwd")
    path = Path(value).expanduser()
    if not value or not path.is_absolute() or not path.is_dir() or not os.access(path, os.W_OK | os.X_OK):
        raise ValueError("work_dir must be an existing absolute writable directory; use --set work_dir=/absolute/path (or --work-dir)")
    path = path.resolve()
    print(f"work_dir: {path} (source: {source})", flush=True)
    return path


def resolve(work, value):
    return (work / Path(value).expanduser()).resolve()


def apptainer_for(work, requested):
    name = requested or "apptainer"
    executable = str(resolve(work, name)) if "/" in name else shutil.which(name)
    if not executable or not Path(executable).is_file() or not os.access(executable, os.X_OK):
        raise ValueError("Apptainer executable not found or not executable; add it to PATH "
                         "or specify --apptainer (hellohpc test: --set apptainer=...)")
    # PATH entries may be relative to the caller's cwd; subprocess cwd can differ.
    return str(Path(executable).resolve())


@contextlib.contextmanager
def locks(paths):
    with contextlib.ExitStack() as stack:
        for path in sorted(set(paths)):
            path.parent.mkdir(parents=True, exist_ok=True)
            stream = stack.enter_context(path.open("a"))
            try:
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise RuntimeError(f"resource busy: lock {path}") from None
        yield


def devices_for(stage, requested):
    visible = None
    for key in ("ASCEND_RT_VISIBLE_DEVICES", "ASCEND_VISIBLE_DEVICES"):
        if key in os.environ:
            ids = os.environ[key].split(",")
            visible = set(ids) if visible is None else visible.intersection(ids)
    devices = requested.split(",") if requested else (["0"] if stage == "stage1" else
              sorted(visible, key=lambda x: int(x) if x.isdigit() else -1) if visible is not None else list(map(str, range(8))))
    count = 1 if stage == "stage1" else 8
    if len(devices) != count or len(set(devices)) != count or any(not x.isdigit() for x in devices):
        raise ValueError(f"{stage} requires {count} distinct numeric devices")
    if visible is not None and not set(devices) <= visible:
        raise ValueError("requested devices are outside inherited NPU visibility")
    return devices


def check_npus(devices, output):
    # Vendor CLI only: no framework imports, allocations or inference probes.
    smi = shutil.which("npu-smi") or "/usr/local/Ascend/driver/tools/npu-smi"
    deadline = time.monotonic() + 30
    for device in devices:
        node = Path(f"/dev/davinci{device}")
        if not node.exists() or not os.access(node, os.R_OK | os.W_OK):
            raise RuntimeError(f"NPU unavailable or inaccessible: {node}; arrange device/group access externally")
        for query in ("health", "usages", "proc"):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("NPU preflight exceeded 30 seconds")
            query_args = ["info", "-t", "proc-mem" if query == "proc" else query, "-i", device, "-c", "0"]
            result = subprocess.run([smi, *query_args],
                                    capture_output=True, text=True, timeout=min(5, remaining))
            text = result.stdout + result.stderr
            with (output / "npu-check.log").open("a") as stream:
                stream.write(f"device={device} query={query}\n{text}\n")
            if result.returncode:
                raise RuntimeError(f"npu-smi {query} failed for {device}; see npu-check.log")
            if query == "health" and not re.search(r"\b(OK|Healthy)\b", text, re.I):
                raise RuntimeError(f"NPU {device} health is not OK")
            if query == "proc" and not re.search(r"no (running )?process|process\s*(?:count|number)\s*:\s*0\b", text, re.I):
                raise RuntimeError(f"NPU {device} is busy or process listing is unrecognized; see npu-check.log")


def protocol(report, stage):
    evaluation = report["evaluation"]
    maximum = 30 if stage == "stage1" else 70
    score = evaluation.get("score")
    if score is None:
        score = 0
    if type(score) not in (int, float) or not math.isfinite(score) or not 0 <= score <= maximum:
        raise ValueError("invalid evaluator score")
    metrics = {}
    summaries = {"qwen": report.get("summary")} if stage == "stage1" else {
        name: item.get("summary") for name, item in report.get("cases", {}).items()}
    for name, summary in summaries.items():
        if not summary:
            continue
        for key, unit, objective in (("output_throughput", "output_token/s", "maximize"),
                                     ("p99_ttft", "s", "minimize"), ("p99_tpot", "ms/output_token", "minimize")):
            value = summary.get(key)
            if type(value) in (int, float) and math.isfinite(value):
                metrics[f"{name}-{key}"] = {"value": value, "unit": unit, "objective": objective}
    return {"outputs": {"score-coordinate": 2 / (1 + score / maximum),
                         "score": score, "message": "Local self-test; not an official score"},
            "metrics": metrics}


def local(args):
    work = work_directory(args)
    sif = resolve(work, args.sif or ".hellohpc-local/image.sif")
    print(f"image: {sif}", flush=True)
    submission = resolve(work, args.submission_dir) if args.submission_dir else Path.cwd() / "submission"
    print(f"submission_dir: {submission}", flush=True)
    output = resolve(work, args.output_dir) if args.output_dir else work / ".hellohpc-local/runs" / args.stage / uuid.uuid4().hex
    output.mkdir(parents=True, exist_ok=False)
    print(f"output_dir: {output}", flush=True)
    try:
        executable = apptainer_for(work, args.apptainer)
        if args.stage == "stage0":
            builder = resolve(work, args.builder or BUILDER)
            if not builder.is_file() or not os.access(builder, os.R_OK):
                raise ValueError(f"missing or unreadable builder SIF: {builder}")
            if not Path("/dev/fuse").is_char_device() or not os.access("/dev/fuse", os.R_OK | os.W_OK):
                raise ValueError("/dev/fuse must be an accessible read/write character device")
            if not (submission / "build.def").is_file():
                raise ValueError(f"missing build definition: {submission / 'build.def'}")
            sif.parent.mkdir(parents=True, exist_ok=True)
            with locks([sif.with_suffix(sif.suffix + ".lock")]):
                build_dir = Path(tempfile.mkdtemp(prefix=".stage0-build-", dir=sif.parent))
                temporary = build_dir / "image.sif"
                try:
                    with cleanup_watcher(output, build_dir=build_dir):
                        options = ["--containall", "--cleanenv", "--no-home", "--no-mount", "hostfs,cwd",
                                   "--writable-tmpfs", "--bind", "/dev/fuse:/dev/fuse:rw"]
                        for source, target, mode in ((submission, "/submission", "ro"), (build_dir, "/build", "rw")):
                            if any(c in str(source) for c in (",", ":", "\n")):
                                raise ValueError("Apptainer bind paths cannot contain comma, colon or newline")
                            options += ["--bind", f"{source}:{target}:{mode}"]
                        host_env = {k: v for k, v in os.environ.items()
                                    if not k.startswith(("APPTAINER", "SINGULARITY"))}
                        command([executable, "exec", *options, "--pwd", "/submission", str(builder),
                                 "/bin/sh", "-c", BUILD_SCRIPT],
                                deadline=time.monotonic() + BUDGETS[args.stage], log=output / "build.log",
                                cwd=Path("/"), env=host_env)
                        if not temporary.is_file() or not 0 < temporary.stat().st_size <= 20 * 1024**3:
                            raise RuntimeError("built SIF must be nonempty and at most 20 GiB")
                        os.replace(temporary, sif)
                    print(f"generated SIF: {sif}", flush=True)
                finally:
                    if build_dir.exists():
                        shutil.rmtree(build_dir)
            payload = {"outputs": {"score-coordinate": 1.0, "message": "Build succeeded (0 points)"}, "metrics": {}}
        else:
            if not sif.is_file():
                raise ValueError(f"missing SIF: {sif}; run stage0 or set sif; no implicit build")
            model = resolve(work, args.model_path or f"/data/models/weights/{MODELS[args.stage]}")
            if not model.is_dir() or not (submission / "start.sh").is_file():
                raise ValueError("model directory or submission/start.sh is missing")
            devices = devices_for(args.stage, args.devices)
            lock_root = Path(tempfile.gettempdir()) / f"hellohpc-npu-{os.getuid()}"
            lock_paths = [lock_root / f"{x}.lock" for x in devices]
            if args.lock_path:
                lock_paths.append(resolve(work, args.lock_path))
            with locks(lock_paths):
                check_npus(devices, output)
                with socket.socket() as probe:
                    probe.bind(("127.0.0.1", args.port))
                run_service(args, executable, sif, model, submission, devices, output)
            payload = protocol(json.loads((output / "report.json").read_text()), args.stage)
        (output / "case-result.json").write_text(json.dumps(payload, allow_nan=False) + "\n")
        if args.cli:
            Path(".hellohpc-case-result.json").write_text(json.dumps(payload, allow_nan=False) + "\n")
    except BaseException as exc:
        (output / "failure.json").write_text(json.dumps({"error": str(exc), "type": type(exc).__name__}) + "\n")
        raise
    finally:
        print(f"output_dir: {output}", flush=True)


def run_service(args, executable, sif, model, submission, devices, output):
    instance = "hellohpc-" + uuid.uuid4().hex
    started = time.monotonic()  # Includes instance creation, launch and readiness.
    deadline = started + BUDGETS[args.stage]
    for directory in ("logs", "tmp", "home"):
        (output / directory).mkdir()
    service_env = {"HELLOHPC_MODEL_PATH": "/models/target", "HELLOHPC_MODEL_ID": MODELS[args.stage],
                   "HELLOHPC_SERVICE_BASE_URL": f"http://127.0.0.1:{args.port}",
                   "HELLOHPC_SERVICE_LOG": "/logs/service.log", "HELLOHPC_ASSIGNED_NPU_COUNT": str(len(devices)),
                   "HELLOHPC_CASE_ID": CASES[args.stage], "HELLOHPC_STAGE": args.stage,
                   "ASCEND_RT_VISIBLE_DEVICES": ",".join(devices), "ASCEND_VISIBLE_DEVICES": ",".join(devices),
                   "HOME": "/home/local", "TMPDIR": "/tmp"}
    host_env = {k: v for k, v in os.environ.items() if not k.startswith(("APPTAINER", "SINGULARITY"))}
    options = ["--cleanenv", "--contain", "--no-home", "--no-mount", "hostfs,cwd,bind-paths"]
    binds = [(model, "/models/target", "ro"), (submission, "/submission", "ro"),
             (output / "logs", "/logs", "rw"), (output / "tmp", "/tmp", "rw"), (output / "home", "/home/local", "rw")]
    for path in ("/usr/local/Ascend/driver", "/usr/local/Ascend/ascend-toolkit", "/usr/local/Ascend/cann-9.1.0",
                 "/usr/local/Ascend/nnal", "/etc/ascend_install.info", "/etc/hccn.conf",
                 "/dev/davinci_manager", "/dev/devmm_svm", "/dev/hisi_hdc", *[f"/dev/davinci{x}" for x in devices]):
        if Path(path).exists():
            binds.append((Path(path), path, "rw" if path.startswith("/dev/") else "ro"))
    for source, target, mode in binds:
        if any(c in str(source) for c in (",", ":", "\n")):
            raise ValueError("Apptainer bind paths cannot contain comma, colon or newline")
        options += ["--bind", f"{source}:{target}:{mode}"]
    env_options = [item for k, v in service_env.items() for item in ("--env", f"{k}={v}")]
    with cleanup_watcher(output, executable=executable, instance=instance, env=host_env):
        command([executable, "instance", "start", *options, *env_options, str(sif), instance],
                deadline=deadline, log=output / "instance.log", cwd=Path("/"), env=host_env)
        command([executable, "exec", "--cleanenv", "--no-home", "--pwd", "/", *env_options,
                 f"instance://{instance}", "bash", "/submission/start.sh"],
                deadline=deadline, log=output / "start.log", cwd=output, env=host_env)
        evaluator = [sys.executable, str(ROOT / "evaluate.py"), CASES[args.stage],
                     "--base-url", service_env["HELLOHPC_SERVICE_BASE_URL"], "--model", MODELS[args.stage],
                     "--output", str(output / "report.json")]
        if args.stage == "stage2":
            evaluator += ["--stage-start-monotonic", str(started)]
        command(evaluator, deadline=deadline, log=output / "evaluate.log", cwd=ROOT)


def main(argv=None):
    if (sys.argv[1:] if argv is None else argv) == ["collect"]:
        raw = json.loads(Path(".hellohpc-case-result.json").read_text())
        Path(os.environ["HELLOHPC_OUTPUT"]).write_text(json.dumps({
            "outputs": raw["outputs"], "metrics": raw.get("metrics", {})}, allow_nan=False) + "\n")
        return 0
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("stage", choices=BUDGETS)
    parser.add_argument("--cli", action="store_true", help="use inherited PWD when work_dir is omitted")
    for name in ("work-dir", "submission-dir", "sif", "model-path", "devices", "output-dir", "lock-path"):
        parser.add_argument("--" + name, default="", help="override; relative paths resolve against work_dir")
    parser.add_argument("--apptainer", default="",
                        help="executable name on PATH (default: apptainer), or path relative to work_dir")
    parser.add_argument("--builder", default="", help=f"Stage0 builder SIF (default: {BUILDER})")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f"signal {signum}")
    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, interrupted)
    try:
        local(args)
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        print(f"{args.stage} failed: {exc}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
