#!/usr/bin/env python3
"""Linux local resource guard. Production also requires an outer cgroup."""
import argparse
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import time


def process_tree(root, known):
    rows = {}
    for path in Path("/proc").glob("[0-9]*/stat"):
        try:
            fields = path.read_text().rsplit(")", 1)[1].split()
            # stat fields: state(3), ppid(4), ... starttime(22), rss(24)
            rows[int(path.parent.name)] = (
                int(fields[1]), int(fields[19]), int(fields[21])
            )
        except (OSError, ValueError, IndexError):
            continue
    live = {pid for pid, birth in known.items()
            if pid in rows and rows[pid][1] == birth}
    if root in rows:
        live.add(root)
    while True:
        added = {pid for pid, row in rows.items() if row[0] in live} - live
        if not added:
            break
        live.update(added)
    known.update({pid: rows[pid][1] for pid in live})
    return live, sum(rows[pid][2] for pid in live) * os.sysconf("SC_PAGE_SIZE")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--memory-gib", type=float, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command or not 0 < args.memory_gib <= 64:
        parser.error("a command and memory limit in (0, 64] GiB are required")
    limit = int(args.memory_gib * 1024**3)
    soft, hard = resource.getrlimit(resource.RLIMIT_AS)
    cap = min(limit, hard) if hard != resource.RLIM_INFINITY else limit
    resource.setrlimit(resource.RLIMIT_AS, (cap, cap))
    process = subprocess.Popen(command, start_new_session=True)
    known = {}
    # CLI 0.4.0 uses subprocess.run(timeout=...), which kills only its direct
    # child with SIGKILL. A pipe-free watcher survives that kill to reap the
    # command tree, including timing workers in separate sessions.
    guard_pid = os.getpid()
    watcher = os.fork()
    if watcher == 0:
        null = os.open(os.devnull, os.O_RDWR)
        for fd in (0, 1, 2):
            os.dup2(null, fd)
        if null > 2:
            os.close(null)
        seen = {}
        while True:
            live, _ = process_tree(process.pid, seen)
            if os.getppid() != guard_pid:
                for pid in live:
                    try:
                        os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                os._exit(0)
            time.sleep(0.1)

    def cleanup(signum=None, frame=None):
        live, _ = process_tree(process.pid, known)
        # Include fresh workers that created their own sessions.
        for pid in live:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        try:
            os.kill(watcher, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(watcher, 0)
        except ChildProcessError:
            pass
        if signum is not None:
            raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, cleanup)
    signal.signal(signal.SIGINT, cleanup)
    try:
        while process.poll() is None:
            _, resident = process_tree(process.pid, known)
            if resident > limit:
                print("memory limit exceeded: aggregate process-tree RSS", file=sys.stderr)
                return 137
            time.sleep(0.1)
        return process.returncode
    finally:
        cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
