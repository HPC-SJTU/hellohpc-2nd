"""Coordinate formal scoring with agent tools across evaluations on one node."""
from contextlib import contextmanager
import errno
import fcntl
import os
from pathlib import Path
import stat
import time


@contextmanager
def formal_timing_lock():
    raw = os.environ.get('CHEATSHEET_NODE_TIMING_LOCK')
    if not raw:
        yield
        return
    path = Path(raw)
    if not path.is_absolute():
        raise RuntimeError('timing lock must be absolute')
    fd = os.open(path, os.O_RDWR | os.O_NOFOLLOW)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise RuntimeError('timing lock must be a regular file')
        for attempt in range(10):
            try:
                fcntl.flock(fd, fcntl.LOCK_EX)
                break
            except OSError as exc:
                if exc.errno != errno.ENOLCK or attempt == 9:
                    raise
                time.sleep(0.2)
        yield
    finally:
        os.close(fd)
