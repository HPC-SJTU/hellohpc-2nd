"""Linux call-only wall timing, with the clock kept in the parent process.

READY / GO / DONE bracket one synchronous function call. The measured interval
includes the small pipe scheduling overhead, never worker-supplied times.
This protocol is not an adversarial sandbox: submissions must not forge events,
inspect the harness, or precompute formal answers during preparation/warmup.
"""
import os
import select
import signal
import subprocess
import tempfile
import time

CLOCK = time.perf_counter_ns


class CallTimeout(TimeoutError):
    pass


def worker_timer(fd):
    outgoing, incoming = map(int, fd.split(","))
    write, read = os.write, os.read

    def invoke(function, arguments):
        write(outgoing, b"R")
        if read(incoming, 1) != b"G":
            raise RuntimeError("missing timing permission")
        result = function(**arguments)
        write(outgoing, b"D")
        return result

    return invoke


def measured_process(command, cwd, env=None, total_seconds=120, call_seconds=70):
    """Run one fresh worker, return (call_ms, captured_output)."""
    parent_read, child_write = os.pipe()
    child_read, parent_write = os.pipe()
    descriptors = {parent_read, child_write, child_read, parent_write}
    process = None
    previous = signal.getsignal(signal.SIGTERM)
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    try:
        signal.signal(signal.SIGTERM, terminate)
        with tempfile.TemporaryFile() as captured:
            process = subprocess.Popen([*command, "--timing-fd", f"{child_write},{child_read}"],
                cwd=cwd, env=env, pass_fds=(child_write, child_read),
                start_new_session=True, stdout=captured, stderr=captured)
            for fd in (child_write, child_read):
                os.close(fd)
                descriptors.remove(fd)
            total_deadline = CLOCK() + int(total_seconds * 1e9)
            def event(deadline, phase):
                remaining = (deadline - CLOCK()) / 1e9
                if remaining <= 0 or not select.select([parent_read], [], [], remaining)[0]:
                    raise CallTimeout(f"{phase} exceeded its deadline")
                value = os.read(parent_read, 1)
                if not value:
                    try:
                        process.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        pass
                    captured.seek(0)
                    details = captured.read().decode(errors="replace")
                    raise RuntimeError(f"worker exited before {phase} completed: {details}")
                return value
            if event(total_deadline, "worker preparation") != b"R":
                raise RuntimeError("invalid READY timing event")
            started = CLOCK()
            os.write(parent_write, b"G")
            call_deadline = started + int(call_seconds * 1e9)
            deadline = min(total_deadline, call_deadline)
            phase = "compute_field call" if call_deadline <= total_deadline else "worker total"
            if event(deadline, phase) != b"D":
                raise RuntimeError("invalid DONE timing event")
            elapsed_ms = (CLOCK() - started) / 1e6
            if elapsed_ms > call_seconds * 1000:
                raise CallTimeout("compute_field call exceeded its deadline")
            try:
                process.wait(timeout=max(0.001, (total_deadline - CLOCK()) / 1e9))
            except subprocess.TimeoutExpired as exc:
                raise CallTimeout("worker result processing exceeded its deadline") from exc
            captured.seek(0)
            output = captured.read().decode(errors="replace")
            if process.returncode:
                raise RuntimeError(f"worker exited with status {process.returncode}: {output}")
            return elapsed_ms, output
    finally:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        for fd in descriptors:
            os.close(fd)
        signal.signal(signal.SIGTERM, previous)
