#!/usr/bin/env python3
"""Run isolated, checked MaiMoe benchmark samples for HelloHPC."""

from __future__ import annotations

import argparse
import contextlib
import ctypes
import json
import math
import os
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from pathlib import Path
from typing import Callable, Iterable, Optional, Sequence, Tuple


EXIT_TIMEOUT = 10
EXIT_CANDIDATE = 11
EXIT_CHECKER = 12
EXIT_HARNESS = 13
LOG_LIMIT_BYTES = 16 * 1024 * 1024
LOG_TRUNCATION_MARKER = b"\n... run_benchmark log truncated ...\n"
TERMINATE_GRACE_SECONDS = 0.5
CHECKER_TIMEOUT_SECONDS = 120.0
PR_SET_CHILD_SUBREAPER = 36
UNSAFE_CHILD_ENVIRONMENT = {
    "BASH_ENV",
    "CDPATH",
    "ENV",
    "GLOBIGNORE",
    "HELLOHPC",
    "HELLOHPC_OUTPUT",
    "HELLOHPC_PROBLEM_ID",
    "HELLOHPC_RUN_ID",
    "LD_AUDIT",
    "LD_LIBRARY_PATH",
    "LD_PRELOAD",
    "PYTHONINSPECT",
    "PYTHONPATH",
}


class BenchmarkFailure(Exception):
    def __init__(
        self, exit_code: int, message: str, *, wait_method: Optional[str] = None
    ) -> None:
        super().__init__(message)
        self.exit_code = exit_code
        self.wait_method = wait_method


class ArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise BenchmarkFailure(EXIT_HARNESS, f"invalid arguments: {message}")


def parse_nonnegative(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return parsed


def parse_positive(value: str) -> int:
    parsed = parse_nonnegative(value)
    if parsed == 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def parse_timeout(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if not math.isfinite(parsed) or parsed <= 0.0:
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return parsed


def parse_cpu_list(value: str) -> Tuple[int, ...]:
    cpus = set()
    if not value.strip():
        raise argparse.ArgumentTypeError("must not be empty")

    for raw_part in value.split(","):
        part = raw_part.strip()
        if not part:
            raise argparse.ArgumentTypeError("contains an empty item")
        if "-" in part:
            bounds = part.split("-")
            if len(bounds) != 2 or not bounds[0].strip() or not bounds[1].strip():
                raise argparse.ArgumentTypeError(f"invalid CPU range: {part!r}")
            try:
                first = int(bounds[0].strip(), 10)
                last = int(bounds[1].strip(), 10)
            except ValueError as error:
                raise argparse.ArgumentTypeError(f"invalid CPU range: {part!r}") from error
            if first < 0 or last < 0 or first > last:
                raise argparse.ArgumentTypeError(f"invalid CPU range: {part!r}")
            cpus.update(range(first, last + 1))
        else:
            try:
                cpu = int(part, 10)
            except ValueError as error:
                raise argparse.ArgumentTypeError(f"invalid CPU: {part!r}") from error
            if cpu < 0:
                raise argparse.ArgumentTypeError(f"invalid CPU: {part!r}")
            cpus.add(cpu)

    if not cpus:
        raise argparse.ArgumentTypeError("must select at least one CPU")
    return tuple(sorted(cpus))


def build_parser() -> ArgumentParser:
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--checker", required=True, type=Path)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument("--expected", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--cpus", required=True, type=parse_cpu_list, metavar="CPU_LIST")
    parser.add_argument("--warmup", required=True, type=parse_nonnegative, metavar="N")
    parser.add_argument("--repeat", required=True, type=parse_positive, metavar="N")
    parser.add_argument(
        "--timeout-seconds", required=True, type=parse_timeout, metavar="S"
    )
    parser.add_argument("--result-file", type=Path)
    parser.add_argument("--hellohpc-soft-fail", action="store_true")
    return parser


def resolve_existing_file(path: Path, name: str, executable: bool = False) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise BenchmarkFailure(EXIT_HARNESS, f"{name} is not a file: {path}")
    if executable and os.name == "posix" and not os.access(str(resolved), os.X_OK):
        raise BenchmarkFailure(EXIT_HARNESS, f"{name} is not executable: {path}")
    return resolved


def resolve_existing_directory(path: Path, name: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_dir():
        raise BenchmarkFailure(EXIT_HARNESS, f"{name} is not a directory: {path}")
    return resolved


def prepare_output_root(path: Path) -> Path:
    expanded = path.expanduser()
    try:
        if expanded.exists():
            if expanded.is_symlink() or not expanded.is_dir():
                raise BenchmarkFailure(
                    EXIT_HARNESS, f"output root is not a regular directory: {path}"
                )
            if any(expanded.iterdir()):
                raise BenchmarkFailure(EXIT_HARNESS, f"output root is not empty: {path}")
        else:
            expanded.mkdir(parents=True, exist_ok=False)
    except BenchmarkFailure:
        raise
    except OSError as error:
        raise BenchmarkFailure(
            EXIT_HARNESS, f"cannot prepare output root {path}: {error}"
        ) from error
    return expanded.resolve()


def resolve_hellohpc_output(result_file: Optional[Path] = None) -> Path:
    raw_path = str(result_file) if result_file is not None else os.environ.get("HELLOHPC_OUTPUT")
    if not raw_path:
        raise BenchmarkFailure(EXIT_HARNESS, "neither --result-file nor HELLOHPC_OUTPUT is set")
    path = Path(raw_path).expanduser().resolve()
    if not path.parent.is_dir():
        raise BenchmarkFailure(
            EXIT_HARNESS, f"HELLOHPC_OUTPUT parent is not a directory: {path.parent}"
        )
    if path.exists() and not path.is_file():
        raise BenchmarkFailure(EXIT_HARNESS, f"HELLOHPC_OUTPUT is not a file: {path}")
    return path


def validate_cpu_selection(cpus: Tuple[int, ...]) -> None:
    sched_getaffinity = getattr(os, "sched_getaffinity", None)
    if sched_getaffinity is None:
        return
    try:
        available = set(sched_getaffinity(0))
    except OSError as error:
        raise BenchmarkFailure(
            EXIT_HARNESS, f"cannot query available CPU affinity: {error}"
        ) from error
    unavailable = sorted(set(cpus) - available)
    if unavailable:
        text = ",".join(str(cpu) for cpu in unavailable)
        raise BenchmarkFailure(EXIT_HARNESS, f"unavailable CPUs requested: {text}")


def child_environment() -> dict:
    return {
        key: value
        for key, value in os.environ.items()
        if key not in UNSAFE_CHILD_ENVIRONMENT
    }


def candidate_preexec(cpus: Tuple[int, ...]) -> Optional[Callable[[], None]]:
    if os.name != "posix":
        return None

    def setup_child() -> None:
        sched_setaffinity = getattr(os, "sched_setaffinity", None)
        if sched_setaffinity is not None:
            sched_setaffinity(0, cpus)

    return setup_child


def session_options() -> dict:
    if os.name == "posix":
        return {"start_new_session": True}
    create_group = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    return {"creationflags": create_group} if create_group else {}


def enable_child_subreaper() -> None:
    if not sys.platform.startswith("linux"):
        return
    try:
        library = ctypes.CDLL(None, use_errno=True)
        prctl = library.prctl
        prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
        prctl.restype = ctypes.c_int
        if prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
            error_number = ctypes.get_errno()
            raise OSError(error_number, os.strerror(error_number))
    except (AttributeError, OSError, TypeError, ValueError) as error:
        raise BenchmarkFailure(EXIT_HARNESS, f"cannot enable child subreaper: {error}") from error


def linux_descendants(root_pid: int) -> set[int]:
    if not sys.platform.startswith("linux"):
        return set()
    parents: dict[int, int] = {}
    try:
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                text = (entry / "stat").read_text(encoding="ascii")
                fields = text[text.rfind(")") + 2 :].split()
                if len(fields) >= 2:
                    parents[int(entry.name)] = int(fields[1])
            except (FileNotFoundError, PermissionError, ProcessLookupError, ValueError):
                continue
    except OSError as error:
        raise BenchmarkFailure(EXIT_HARNESS, f"cannot inspect candidate descendants: {error}") from error

    descendants: set[int] = set()
    frontier = {root_pid}
    while frontier:
        children = {pid for pid, parent in parents.items() if parent in frontier and pid not in descendants}
        descendants.update(children)
        frontier = children
    return descendants


def reap_children() -> None:
    if os.name != "posix":
        return
    while True:
        try:
            pid, _ = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        except OSError:
            return
        if pid == 0:
            return


def process_group_exists(process: subprocess.Popen) -> bool:
    process.poll()
    if os.name != "posix":
        return process.poll() is None
    try:
        os.killpg(process.pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False


def signal_process_group(process: subprocess.Popen, sig: int) -> None:
    if os.name != "posix":
        return
    try:
        os.killpg(process.pid, sig)
    except (OSError, ProcessLookupError):
        pass
    try:
        descendants = linux_descendants(os.getpid())
    except BenchmarkFailure:
        return
    for pid in descendants:
        try:
            os.kill(pid, sig)
        except (OSError, ProcessLookupError):
            pass


def terminate_process_group(process: subprocess.Popen) -> None:
    if os.name == "posix":
        signal_process_group(process, signal.SIGTERM)
        deadline = time.monotonic() + TERMINATE_GRACE_SECONDS
        while time.monotonic() < deadline:
            reap_children()
            if not process_group_exists(process) and not linux_descendants(os.getpid()):
                break
            time.sleep(0.01)
        descendants = linux_descendants(os.getpid())
        if process_group_exists(process) or descendants:
            signal_process_group(process, signal.SIGKILL)
    else:
        if process.poll() is not None:
            return
        break_signal = getattr(signal, "CTRL_BREAK_EVENT", None)
        if break_signal is not None:
            try:
                process.send_signal(break_signal)
                process.wait(timeout=TERMINATE_GRACE_SECONDS)
            except (OSError, subprocess.TimeoutExpired):
                pass
        if process.poll() is None:
            try:
                process.kill()
            except OSError:
                pass

    try:
        process.wait(timeout=TERMINATE_GRACE_SECONDS)
    except (OSError, subprocess.TimeoutExpired):
        try:
            process.kill()
        except OSError:
            pass
        try:
            process.wait(timeout=TERMINATE_GRACE_SECONDS)
        except (OSError, subprocess.TimeoutExpired):
            pass
    reap_children()


def waitstatus_to_exitcode(status: int) -> int:
    convert = getattr(os, "waitstatus_to_exitcode", None)
    if convert is not None:
        return convert(status)
    if os.WIFSIGNALED(status):
        return -os.WTERMSIG(status)
    return os.WEXITSTATUS(status)


def resource_usage_dict(rusage) -> Optional[dict]:
    if rusage is None:
        return None
    if hasattr(rusage.ru_utime, "tv_sec"):
        milliseconds = lambda tv: (tv.tv_sec * 1_000_000 + tv.tv_usec) / 1000.0
    else:
        milliseconds = lambda seconds: seconds * 1000.0
    return {
        "utime_ms": milliseconds(rusage.ru_utime),
        "stime_ms": milliseconds(rusage.ru_stime),
        "maxrss_kb": int(rusage.ru_maxrss),
        "minflt": int(rusage.ru_minflt),
        "majflt": int(rusage.ru_majflt),
        "nvcsw": int(rusage.ru_nvcsw),
        "nivcsw": int(rusage.ru_nivcsw),
    }


class CandidateWatchdog:
    """Daemon watchdog enforcing the candidate deadline on Linux.

    Sleeps until the deadline (cancelable), then SIGTERMs the candidate
    process group, waits TERMINATE_GRACE_SECONDS, and escalates to SIGKILL.
    The main thread is not involved in any polling: it blocks in os.wait4
    until the process group actually dies.
    """

    def __init__(self, process: subprocess.Popen, deadline_ns: int) -> None:
        self.process = process
        self.deadline_ns = deadline_ns
        self.fired = threading.Event()
        self.done = threading.Event()
        self.thread = threading.Thread(
            target=self._run, name="maimoe-candidate-watchdog", daemon=True
        )

    def _run(self) -> None:
        try:
            delay = (self.deadline_ns - time.perf_counter_ns()) / 1_000_000_000.0
            if delay > 0:
                self.done.wait(delay)
            if self.done.is_set():
                return
            self.fired.set()
            signal_process_group(self.process, signal.SIGTERM)
            if self.done.wait(TERMINATE_GRACE_SECONDS):
                return
            signal_process_group(self.process, signal.SIGKILL)
            signal_process_group(self.process, signal.SIGCONT)
        except BaseException:
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
                os.killpg(self.process.pid, signal.SIGCONT)
            except OSError:
                pass

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.done.set()
        self.thread.join(TERMINATE_GRACE_SECONDS + 1.0)


class BoundedCapture:
    def __init__(self) -> None:
        self.head_limit = (LOG_LIMIT_BYTES - len(LOG_TRUNCATION_MARKER)) // 2
        self.tail_limit = LOG_LIMIT_BYTES - len(LOG_TRUNCATION_MARKER) - self.head_limit
        self.head = bytearray()
        self.tail = deque()
        self.tail_bytes = 0
        self.total = 0

    def add(self, data: bytes) -> None:
        self.total += len(data)
        head_bytes = min(len(data), self.head_limit - len(self.head))
        self.head.extend(data[:head_bytes])
        tail_data = data[head_bytes:]
        if tail_data:
            self.tail.append(tail_data)
            self.tail_bytes += len(tail_data)
        while self.tail_bytes > self.tail_limit:
            excess = self.tail_bytes - self.tail_limit
            first = self.tail[0]
            if len(first) <= excess:
                self.tail.popleft()
                self.tail_bytes -= len(first)
            else:
                self.tail[0] = first[excess:]
                self.tail_bytes -= excess

    def bytes(self) -> bytes:
        tail = b"".join(self.tail)
        if self.total <= LOG_LIMIT_BYTES:
            return bytes(self.head) + tail
        return bytes(self.head) + LOG_TRUNCATION_MARKER + tail


def capture_pipe(stream, capture: BoundedCapture, errors: list[BaseException]) -> None:
    try:
        while True:
            data = stream.read(64 * 1024)
            if not data:
                break
            capture.add(data)
    except BaseException as error:
        errors.append(error)
    finally:
        try:
            stream.close()
        except OSError:
            pass


def start_capture_threads(process: subprocess.Popen):
    if process.stdout is None or process.stderr is None:
        raise BenchmarkFailure(EXIT_HARNESS, "process output pipes are unavailable")
    stdout_capture = BoundedCapture()
    stderr_capture = BoundedCapture()
    errors: list[BaseException] = []
    threads = [
        threading.Thread(
            target=capture_pipe,
            args=(process.stdout, stdout_capture, errors),
            daemon=True,
        ),
        threading.Thread(
            target=capture_pipe,
            args=(process.stderr, stderr_capture, errors),
            daemon=True,
        ),
    ]
    for thread in threads:
        thread.start()
    return stdout_capture, stderr_capture, errors, threads


def write_captured_log(path: Path, data: bytes) -> None:
    try:
        with path.open("xb") as output:
            output.write(data)
    except OSError as error:
        raise BenchmarkFailure(EXIT_HARNESS, f"cannot write process log {path}: {error}") from error


def finish_captures(
    process: subprocess.Popen,
    capture_state,
    stdout_path: Path,
    stderr_path: Path,
) -> None:
    stdout_capture, stderr_capture, errors, threads = capture_state
    for thread in threads:
        thread.join(TERMINATE_GRACE_SECONDS)
    if any(thread.is_alive() for thread in threads):
        for stream in (process.stdout, process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass
        for thread in threads:
            thread.join(TERMINATE_GRACE_SECONDS)
    if any(thread.is_alive() for thread in threads):
        raise BenchmarkFailure(EXIT_HARNESS, "process log capture did not terminate")
    if errors:
        raise BenchmarkFailure(EXIT_HARNESS, f"cannot capture process log: {errors[0]}")
    write_captured_log(stdout_path, stdout_capture.bytes())
    write_captured_log(stderr_path, stderr_capture.bytes())


def write_empty_logs(stdout_path: Path, stderr_path: Path) -> None:
    write_captured_log(stdout_path, b"")
    write_captured_log(stderr_path, b"")


def read_snapshot_bytes(path: Path, name: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as error:
        raise BenchmarkFailure(EXIT_HARNESS, f"cannot snapshot {name} {path}: {error}") from error


def create_sealed_memfd(name: str, data: bytes, executable: bool) -> int:
    try:
        import fcntl

        flags = os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING
        descriptor = os.memfd_create(name, flags)
        try:
            offset = 0
            while offset < len(data):
                written = os.write(descriptor, data[offset:])
                if written == 0:
                    raise OSError("short write while creating snapshot")
                offset += written
            os.fchmod(descriptor, 0o500 if executable else 0o400)
            seals = fcntl.F_SEAL_SEAL | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE
            fcntl.fcntl(descriptor, fcntl.F_ADD_SEALS, seals)
            return descriptor
        except BaseException:
            os.close(descriptor)
            raise
    except (AttributeError, ImportError, OSError) as error:
        raise BenchmarkFailure(EXIT_HARNESS, f"cannot create sealed {name} snapshot: {error}") from error


@contextlib.contextmanager
def snapshot_checker_and_expected(checker: Path, expected: Path):
    checker_bytes = read_snapshot_bytes(checker, "checker")
    expected_bytes = read_snapshot_bytes(expected, "expected file")
    if sys.platform.startswith("linux") and hasattr(os, "memfd_create"):
        checker_fd = create_sealed_memfd("maimoe-checker", checker_bytes, True)
        try:
            expected_fd = create_sealed_memfd("maimoe-expected", expected_bytes, False)
        except BaseException:
            os.close(checker_fd)
            raise
        try:
            yield (
                Path(f"/proc/self/fd/{checker_fd}"),
                Path(f"/proc/self/fd/{expected_fd}"),
                (checker_fd, expected_fd),
            )
        finally:
            os.close(expected_fd)
            os.close(checker_fd)
        return

    with tempfile.TemporaryDirectory(prefix="maimoe-trusted-") as temporary:
        root = Path(temporary)
        checker_snapshot = root / checker.name
        expected_snapshot = root / "expected.bin"
        try:
            checker_snapshot.write_bytes(checker_bytes)
            expected_snapshot.write_bytes(expected_bytes)
            checker_snapshot.chmod(0o500)
            expected_snapshot.chmod(0o400)
        except OSError as error:
            raise BenchmarkFailure(EXIT_HARNESS, f"cannot create trusted snapshots: {error}") from error
        yield checker_snapshot, expected_snapshot, ()


def run_candidate(
    executable: Path,
    dataset: Path,
    run_output: Path,
    cpus: Tuple[int, ...],
    timeout_seconds: float,
    stdout_path: Path,
    stderr_path: Path,
) -> dict:
    if run_output.exists() or run_output.is_symlink():
        raise BenchmarkFailure(
            EXIT_HARNESS, f"run output path already exists: {run_output}"
        )

    if stdout_path.exists() or stderr_path.exists():
        raise BenchmarkFailure(EXIT_HARNESS, "candidate log path already exists")

    process = None
    capture_state = None
    wait_method = "Popen.wait(timeout)"
    usage = None
    watchdog_fired = False
    enable_child_subreaper()
    command = [str(executable), str(dataset), str(run_output)]
    preexec_fn = candidate_preexec(cpus)
    process_options = session_options()
    try:
        start_ns = time.perf_counter_ns()
        deadline_ns = start_ns + int(timeout_seconds * 1_000_000_000)
        try:
            process = subprocess.Popen(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=child_environment(),
                shell=False,
                preexec_fn=preexec_fn,
                **process_options,
            )
            capture_state = start_capture_threads(process)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            write_empty_logs(stdout_path, stderr_path)
            raise BenchmarkFailure(
                EXIT_CANDIDATE,
                f"candidate failed to start: {error}; logs: {stdout_path}, {stderr_path}",
            ) from error

        if sys.platform.startswith("linux") and hasattr(os, "wait4"):
            watchdog = CandidateWatchdog(process, deadline_ns)
            watchdog.start()
            try:
                _, status, rusage = os.wait4(process.pid, 0)
                process.returncode = waitstatus_to_exitcode(status)
                wait_method = "linux_wait4"
                usage = resource_usage_dict(rusage)
            except OSError as error:
                watchdog.stop()
                terminate_process_group(process)
                raise BenchmarkFailure(
                    EXIT_HARNESS, f"cannot wait for candidate: {error}"
                ) from error
            except BaseException:
                watchdog.stop()
                terminate_process_group(process)
                raise
            end_ns = time.perf_counter_ns()
            watchdog_fired = watchdog.fired.is_set()
            watchdog.stop()
            if end_ns >= deadline_ns:
                terminate_process_group(process)
                raise BenchmarkFailure(
                    EXIT_TIMEOUT,
                    f"candidate timed out after {timeout_seconds:g}s; "
                    f"artifacts: {run_output}, {stdout_path}, {stderr_path}",
                    wait_method=wait_method,
                )
        else:
            try:
                remaining_seconds = max(
                    0.0, (deadline_ns - time.perf_counter_ns()) / 1_000_000_000.0
                )
                process.wait(timeout=remaining_seconds)
            except subprocess.TimeoutExpired as error:
                terminate_process_group(process)
                raise BenchmarkFailure(
                    EXIT_TIMEOUT,
                    f"candidate timed out after {timeout_seconds:g}s; "
                    f"artifacts: {run_output}, {stdout_path}, {stderr_path}",
                    wait_method=wait_method,
                ) from error
            except BaseException:
                terminate_process_group(process)
                raise
            end_ns = time.perf_counter_ns()
        if process_group_exists(process) or linux_descendants(os.getpid()):
            terminate_process_group(process)
            raise BenchmarkFailure(
                EXIT_CANDIDATE,
                "candidate exited while descendant processes were still running; "
                f"artifacts: {run_output}, {stdout_path}, {stderr_path}",
            )
    except OSError as error:
        if process is not None:
            terminate_process_group(process)
        raise BenchmarkFailure(EXIT_HARNESS, f"candidate process failure: {error}") from error
    finally:
        if process is not None and capture_state is not None:
            finish_captures(process, capture_state, stdout_path, stderr_path)

    if process.returncode != 0:
        raise BenchmarkFailure(
            EXIT_CANDIDATE,
            f"candidate exited with status {process.returncode}; "
            f"artifacts: {run_output}, {stdout_path}, {stderr_path}",
        )
    return {
        "wall_ms": (end_ns - start_ns) / 1_000_000.0,
        "wait_method": wait_method,
        "usage": usage,
        "watchdog_fired": watchdog_fired,
    }


def run_checker(
    checker: Path,
    dataset: Path,
    expected: Path,
    run_output: Path,
    stdout_path: Path,
    stderr_path: Path,
    pass_fds: Tuple[int, ...] = (),
) -> None:
    if stdout_path.exists() or stderr_path.exists():
        raise BenchmarkFailure(EXIT_HARNESS, "checker log path already exists")

    process = None
    capture_state = None
    process_options = session_options()
    if os.name == "posix" and pass_fds:
        process_options["pass_fds"] = pass_fds
    try:
        try:
            process = subprocess.Popen(
                [str(checker), str(dataset), str(expected), str(run_output)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=child_environment(),
                shell=False,
                **process_options,
            )
            capture_state = start_capture_threads(process)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            write_empty_logs(stdout_path, stderr_path)
            raise BenchmarkFailure(
                EXIT_HARNESS,
                f"checker failed to start: {error}; logs: {stdout_path}, {stderr_path}",
            ) from error
        try:
            return_code = process.wait(timeout=CHECKER_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as error:
            terminate_process_group(process)
            raise BenchmarkFailure(
                EXIT_HARNESS,
                f"checker timed out after {CHECKER_TIMEOUT_SECONDS:g}s; "
                f"artifacts: {run_output}, {stdout_path}, {stderr_path}",
            ) from error
        except BaseException:
            terminate_process_group(process)
            raise
    except OSError as error:
        if process is not None:
            terminate_process_group(process)
        raise BenchmarkFailure(EXIT_HARNESS, f"checker process failure: {error}") from error
    finally:
        if process is not None and capture_state is not None:
            finish_captures(process, capture_state, stdout_path, stderr_path)

    if return_code != 0:
        raise BenchmarkFailure(
            EXIT_CHECKER,
            f"checker rejected output with status {return_code}; "
            f"artifacts: {run_output}, {stdout_path}, {stderr_path}",
        )


def best_effort_syncfs(output_root: Path) -> None:
    if not sys.platform.startswith("linux"):
        return

    descriptor = None
    try:
        flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
        descriptor = os.open(str(output_root), flags)
        library = ctypes.CDLL(None, use_errno=True)
        syncfs = getattr(library, "syncfs", None)
        if syncfs is None:
            return
        syncfs.argtypes = [ctypes.c_int]
        syncfs.restype = ctypes.c_int
        if syncfs(descriptor) != 0:
            error_number = ctypes.get_errno()
            print(
                f"run_benchmark: warning: syncfs failed: {os.strerror(error_number)}",
                file=sys.stderr,
            )
    except (AttributeError, OSError, TypeError, ValueError):
        pass
    finally:
        if descriptor is not None:
            try:
                os.close(descriptor)
            except OSError:
                pass


def warm_dataset(dataset: Path) -> None:
    buffer = bytearray(1024 * 1024)
    try:
        paths = sorted(path for path in dataset.rglob("*") if path.is_file())
        for path in paths:
            if path.is_symlink():
                raise BenchmarkFailure(
                    EXIT_HARNESS, f"dataset contains a symlink: {path}"
                )
            with path.open("rb", buffering=0) as source:
                while source.readinto(buffer) != 0:
                    pass
    except BenchmarkFailure:
        raise
    except OSError as error:
        raise BenchmarkFailure(
            EXIT_HARNESS, f"cannot warm dataset page cache: {error}"
        ) from error


def remove_run_output(path: Path) -> None:
    try:
        if path.is_symlink() or path.is_file():
            path.unlink()
        elif path.exists():
            shutil.rmtree(str(path))
    except OSError as error:
        raise BenchmarkFailure(EXIT_HARNESS, f"cannot remove run output {path}: {error}") from error


def remove_logs(paths: Iterable[Path]) -> None:
    for path in paths:
        try:
            path.unlink()
        except FileNotFoundError:
            continue
        except OSError as error:
            raise BenchmarkFailure(EXIT_HARNESS, f"cannot remove successful log {path}: {error}") from error


def write_hellohpc_document(path: Path, document: dict) -> None:
    descriptor = None
    temporary_path = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=str(path.parent)
        )
        temporary_path = Path(temporary_name)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            descriptor = None
            json.dump(document, output, allow_nan=False, sort_keys=True, separators=(",", ":"))
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(str(temporary_path), str(path))
        temporary_path = None
    except (OSError, TypeError, ValueError) as error:
        raise BenchmarkFailure(
            EXIT_HARNESS, f"cannot write HELLOHPC_OUTPUT {path}: {error}"
        ) from error
    finally:
        if descriptor is not None:
            try:
                os.close(descriptor)
            except OSError:
                pass
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except OSError:
                pass


def write_hellohpc_output(
    path: Path,
    samples: Sequence[float],
    wait_method: str,
    run_diagnostics: Sequence[dict],
) -> None:
    mean_ms = statistics.fmean(samples)
    write_hellohpc_document(
        path,
        {
            "metrics": {
                "wall-time": {
                    "aggregation": "mean",
                    "samples": list(samples),
                    "unit": "ms",
                    "value": mean_ms,
                }
            },
            "outputs": {
                "samples": list(samples),
                "status": "ok",
                "runs": [dict(record) for record in run_diagnostics],
                "wait_method": wait_method,
            },
        },
    )


def write_hellohpc_failure(path: Path, error: BenchmarkFailure) -> None:
    statuses = {
        EXIT_TIMEOUT: "time_limit_exceeded",
        EXIT_CANDIDATE: "runtime_error",
        EXIT_CHECKER: "wrong_answer",
        EXIT_HARNESS: "infrastructure_error",
    }
    document = {"outputs": {"message": str(error), "status": statuses[error.exit_code]}}
    if error.wait_method is not None:
        document["outputs"]["wait_method"] = error.wait_method
    write_hellohpc_document(path, document)


def run_benchmark(arguments: argparse.Namespace) -> int:
    executable = resolve_existing_file(arguments.executable, "executable", executable=True)
    checker = resolve_existing_file(arguments.checker, "checker", executable=True)
    dataset = resolve_existing_directory(arguments.dataset, "dataset")
    expected = resolve_existing_file(arguments.expected, "expected")
    hellohpc_output = resolve_hellohpc_output(arguments.result_file)
    validate_cpu_selection(arguments.cpus)
    output_root = prepare_output_root(arguments.output_root)

    cpu_text = ",".join(str(cpu) for cpu in arguments.cpus)
    if getattr(os, "sched_setaffinity", None) is None:
        print("run_benchmark: warning: CPU affinity is unavailable", file=sys.stderr)
    print(
        f"run_benchmark: cpus={cpu_text} warmup={arguments.warmup} repeat={arguments.repeat}"
    )

    samples = []
    wait_method = None
    run_diagnostics = []
    with snapshot_checker_and_expected(checker, expected) as trusted:
        trusted_checker, trusted_expected, pass_fds = trusted
        total_runs = arguments.warmup + arguments.repeat
        for ordinal in range(total_runs):
            is_warmup = ordinal < arguments.warmup
            phase = "warmup" if is_warmup else "sample"
            phase_index = ordinal + 1 if is_warmup else ordinal - arguments.warmup + 1
            phase_total = arguments.warmup if is_warmup else arguments.repeat
            run_name = f"{ordinal + 1:04d}-{phase}-{phase_index:04d}"
            run_output = output_root / f"{run_name}-output"
            candidate_stdout = output_root / f"{run_name}-candidate.stdout.log"
            candidate_stderr = output_root / f"{run_name}-candidate.stderr.log"
            checker_stdout = output_root / f"{run_name}-checker.stdout.log"
            checker_stderr = output_root / f"{run_name}-checker.stderr.log"

            warm_dataset(dataset)
            candidate_run = run_candidate(
                executable,
                dataset,
                run_output,
                arguments.cpus,
                arguments.timeout_seconds,
                candidate_stdout,
                candidate_stderr,
            )
            elapsed_ms = candidate_run["wall_ms"]
            if wait_method is None:
                wait_method = candidate_run["wait_method"]
            run_diagnostics.append(
                {
                    "watchdog_fired": candidate_run["watchdog_fired"],
                    "usage": candidate_run["usage"],
                }
            )
            run_checker(
                trusted_checker,
                dataset,
                trusted_expected,
                run_output,
                checker_stdout,
                checker_stderr,
                pass_fds,
            )
            best_effort_syncfs(output_root)
            remove_run_output(run_output)
            remove_logs(
                (candidate_stdout, candidate_stderr, checker_stdout, checker_stderr)
            )
            best_effort_syncfs(output_root)

            if is_warmup:
                print(f"run_benchmark: warmup {phase_index}/{phase_total} passed")
            else:
                samples.append(elapsed_ms)
                print(
                    f"run_benchmark: sample {phase_index}/{phase_total}: {elapsed_ms:.3f} ms"
                )

    write_hellohpc_output(hellohpc_output, samples, wait_method, run_diagnostics)
    print(f"run_benchmark: mean: {statistics.fmean(samples):.3f} ms")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = None
    try:
        arguments = build_parser().parse_args(argv)
        return run_benchmark(arguments)
    except BenchmarkFailure as error:
        print(f"run_benchmark: {error}", file=sys.stderr)
        if arguments is not None and arguments.hellohpc_soft_fail:
            try:
                write_hellohpc_failure(resolve_hellohpc_output(arguments.result_file), error)
                return 0
            except BenchmarkFailure as output_error:
                print(f"run_benchmark: {output_error}", file=sys.stderr)
                return EXIT_HARNESS
        return error.exit_code
    except KeyboardInterrupt:
        print("run_benchmark: interrupted", file=sys.stderr)
        return EXIT_HARNESS
    except Exception as error:
        print(f"run_benchmark: infrastructure error: {error}", file=sys.stderr)
        return EXIT_HARNESS


if __name__ == "__main__":
    raise SystemExit(main())
