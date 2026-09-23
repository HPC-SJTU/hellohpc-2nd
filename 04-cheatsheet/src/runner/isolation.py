"""Fail-closed Linux isolation for formal untrusted compile/run commands.

The policy is intentionally small and explicit.  Bubblewrap starts with an
empty mount namespace; the child sees only read-only system/runtime inputs,
declared read-only task inputs, and declared writable output directories.  It
also receives fresh user, PID, network, IPC and UTS namespaces, a fresh procfs,
a minimal ``/dev`` and a private ``/tmp``.

There is deliberately no permissive fallback. If bubblewrap is missing or
cannot create every required namespace, formal evaluation must stop.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import argparse
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping, Optional, Sequence, Tuple


# Keep this list narrow.  In particular, HOME, XDG_*, proxy variables and all
# model credentials are excluded.  Additions should be justified by a formal
# compile/run requirement, not copied wholesale from the parent environment.
ALLOWED_CHILD_ENV = frozenset(
    {
        "PATH",
        "LANG",
        "LC_ALL",
        "TMPDIR",
        "OMP_NUM_THREADS",
        "OMP_PROC_BIND",
        "OMP_PLACES",
        "OMP_DYNAMIC",
        "OPENBLAS_NUM_THREADS",
        "MKL_NUM_THREADS",
        "CHEATSHEET_KERNEL_SO",
        "CHEATSHEET_KERNEL_WORKER",
        # This is a non-secret marker only.  The 128-bit paired-input domain
        # is delivered on stdin so it never appears in bwrap argv or env.
        "CHEATSHEET_PAIRED_INPUTS",
    }
)

DEFAULT_CHILD_ENV = {
    "HOME": "/tmp/home",
    "PATH": "/usr/bin:/bin",
    "LANG": "C",
    "LC_ALL": "C",
    "TMPDIR": "/tmp",
}

# These are mounted read-only if they exist.  /etc is intentionally not
# exposed wholesale: only files/directories needed by the loader and common
# alternatives-managed compiler symlinks are included.
DEFAULT_RUNTIME_PATHS = (
    "/usr",
    "/bin",
    "/sbin",
    "/lib",
    "/lib64",
    "/etc/ld.so.cache",
    "/etc/alternatives",
)

_SECRET_NAME = re.compile(
    r"(?:KEY|TOKEN|SECRET|PASSWORD|PASSWD|CREDENTIAL|AUTH|COOKIE|SESSION)",
    re.IGNORECASE,
)


class IsolationUnavailable(RuntimeError):
    """The required isolation boundary cannot be established."""


class IsolationPolicyError(ValueError):
    """A requested mount/environment would weaken the isolation policy."""


@dataclass(frozen=True)
class _Mount:
    source: Path
    destination: Path
    writable: bool


@dataclass(frozen=True)
class CompilerRuntime:
    executable: Path
    version: str
    readonly_roots: Tuple[Path, ...]


_SYSTEM_COMPILER_ROOTS = (Path("/usr"), Path("/bin"), Path("/lib"), Path("/lib64"))


def _under(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def resolve_compiler_runtime(
    cxx: os.PathLike[str] | str,
    *,
    readonly_roots: Sequence[os.PathLike[str] | str] = (),
    timeout: float = 5.0,
) -> CompilerRuntime:
    """Resolve and attest the formal compiler and its allowed filesystem roots.

    Formal configuration must use an absolute compiler path.  A compiler below
    the normal system roots needs explicit read-only runtime roots; otherwise it
    is rejected instead of implicitly exposing an arbitrary Spack/home/Lustre
    tree.  The current ARM deployment intentionally fixes ``/usr/bin/g++``.
    """
    configured = Path(cxx)
    if not configured.is_absolute():
        raise IsolationUnavailable(
            f"formal compiler path must be absolute, got: {configured}"
        )
    if not configured.is_file() or not os.access(configured, os.X_OK):
        raise IsolationUnavailable(f"formal compiler is not executable: {configured}")
    executable = configured.resolve(strict=True)
    roots = tuple(
        _checked_policy_path(raw, writable=False) for raw in readonly_roots
    )
    for root in roots:
        if root == Path(root.anchor):
            raise IsolationPolicyError(
                "compiler read-only root cannot be a filesystem root"
            )
    allowed = (*_SYSTEM_COMPILER_ROOTS, *roots)
    if not any(_under(executable, root) for root in allowed):
        raise IsolationUnavailable(
            f"compiler resolves outside system/declared read-only roots: {executable}"
        )

    try:
        proc = subprocess.run(
            [str(configured), "-dumpfullversion", "-dumpversion"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            env=_launcher_env(),
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise IsolationUnavailable(f"cannot attest compiler {configured}: {exc}") from exc
    version = (proc.stdout or "").strip()
    if proc.returncode != 0 or not version:
        detail = ((proc.stderr or "") + (proc.stdout or "")).strip()[-1000:]
        raise IsolationUnavailable(
            f"cannot attest compiler {configured} (exit {proc.returncode}): {detail}"
        )

    # Attest the executable helpers and OpenMP/C++ runtimes that GCC reports.
    # System entries are already covered by the default mounts.  A non-system
    # entry must stay below an explicitly declared read-only root.
    queries = (
        "-print-prog-name=cc1plus",
        "-print-prog-name=collect2",
        "-print-file-name=libgomp.so.1",
        "-print-file-name=libstdc++.so.6",
        "-print-file-name=libgcc_s.so.1",
    )
    for query in queries:
        result = subprocess.run(
            [str(configured), query],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            env=_launcher_env(),
            check=False,
        )
        value = (result.stdout or "").strip()
        if result.returncode != 0 or not value:
            raise IsolationUnavailable(f"compiler runtime query failed: {query}")
        candidate = Path(value)
        # A bare program name (normally as/ld) resolves through the fixed
        # in-sandbox PATH.  Absolute paths must be covered by an allowed root.
        if candidate.is_absolute():
            resolved = candidate.resolve(strict=True)
            if not any(_under(resolved, root) for root in allowed):
                raise IsolationUnavailable(
                    f"compiler helper/runtime is outside declared roots: {resolved}"
                )
    return CompilerRuntime(configured, version, roots)


def verify_compiler_in_isolation(
    cxx: os.PathLike[str] | str,
    *,
    readonly_roots: Sequence[os.PathLike[str] | str] = (),
    timeout: float = 20.0,
) -> CompilerRuntime:
    """Compile and run a two-thread OpenMP program under the exact policy."""
    runtime = resolve_compiler_runtime(
        cxx, readonly_roots=readonly_roots
    )
    with tempfile.TemporaryDirectory(prefix="cheatsheet-compiler-probe-") as temp:
        root = Path(temp).resolve()
        source = root / "probe.cpp"
        output = root / "probe"
        source.write_text(
            "#include <omp.h>\n"
            "int main() { int n = 0;\n"
            "#pragma omp parallel reduction(+:n)\n"
            "n += 1; return n == 2 ? 0 : 9; }\n",
            encoding="utf-8",
        )
        extra_runtime = tuple(str(path) for path in runtime.readonly_roots)
        runtime_paths = (*DEFAULT_RUNTIME_PATHS, *extra_runtime)
        build = run_isolated(
            [str(runtime.executable), "-fopenmp", "-std=c++17", str(source), "-o", str(output)],
            timeout=timeout,
            readonly_paths=[source],
            writable_paths=[root],
            env={},
            cwd=root,
            runtime_paths=runtime_paths,
            capture=True,
        )
        if build.returncode != 0 or not output.is_file():
            detail = ((build.stderr or "") + (build.stdout or "")).strip()[-1000:]
            raise IsolationUnavailable(
                f"compiler failed inside isolation (exit {build.returncode}): {detail}"
            )
        execute = run_isolated(
            [str(output)],
            timeout=timeout,
            readonly_paths=[output],
        env={"OMP_NUM_THREADS": "2", "OMP_DYNAMIC": "FALSE"},
            runtime_paths=runtime_paths,
            capture=True,
        )
        if execute.returncode != 0:
            detail = ((execute.stderr or "") + (execute.stdout or "")).strip()[-1000:]
            raise IsolationUnavailable(
                f"OpenMP runtime failed inside isolation (exit {execute.returncode}): {detail}"
            )
    return runtime


def resolve_bwrap(explicit: Optional[os.PathLike[str] | str] = None) -> Path:
    """Resolve bubblewrap on Linux or fail closed."""
    if not (os.name == "posix" and sys.platform.startswith("linux")):
        raise IsolationUnavailable("formal isolation requires Linux")
    candidate = str(explicit) if explicit is not None else shutil.which("bwrap")
    if not candidate:
        raise IsolationUnavailable("bwrap is not installed or not on PATH")
    path = Path(candidate).resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise IsolationUnavailable(f"bwrap is not executable: {path}")
    return path


def _launcher_env() -> dict[str, str]:
    """Environment for bwrap itself; never inherit the evaluator environment."""
    return {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"}


def _clean_child_env(
    supplied: Optional[Mapping[str, object]], api_key_env: str
) -> dict[str, str]:
    clean = dict(DEFAULT_CHILD_ENV)
    denied_names = {api_key_env}
    for raw_key, raw_value in (supplied or {}).items():
        key = str(raw_key)
        if (
            key in denied_names
            or key not in ALLOWED_CHILD_ENV
            or _SECRET_NAME.search(key)
        ):
            continue
        value = str(raw_value)
        if "\x00" in key or "=" in key or "\x00" in value:
            raise IsolationPolicyError(f"invalid environment entry: {key!r}")
        clean[key] = value
    return clean


def _checked_policy_path(
    raw: os.PathLike[str] | str, *, writable: bool
) -> Path:
    expanded = Path(raw).expanduser()
    if writable:
        # Resolving a submission-controlled symlink and then binding its target
        # writable turns the launcher into a deputy for an unrelated host
        # directory.  Reject a symlink at any existing path component before
        # canonicalizing the mount.
        absolute = Path(os.path.abspath(os.fspath(expanded)))
        for component in (absolute, *absolute.parents):
            if component.is_symlink():
                raise IsolationPolicyError(
                    f"writable mount traverses a symlink: {component}"
                )
    path = expanded.resolve(strict=True)
    if writable and not path.is_dir():
        raise IsolationPolicyError(f"writable mount must be a directory: {path}")
    if writable and path == Path(path.anchor):
        raise IsolationPolicyError("refusing to make a filesystem root writable")
    return path


def _runtime_mount(raw: os.PathLike[str] | str) -> Optional[_Mount]:
    """Preserve /bin-style destinations even when the host path is a symlink."""
    destination = Path(os.path.abspath(os.fspath(raw)))
    if not destination.exists():
        return None
    return _Mount(
        source=destination.resolve(strict=True),
        destination=destination,
        writable=False,
    )


def _path_depth(path: Path) -> int:
    return len(path.parts)


def _ancestor_directories(paths: Iterable[Path]) -> list[Path]:
    """Return mount-point parents that must exist in bwrap's empty root."""
    special = {Path("/"), Path("/proc"), Path("/dev"), Path("/tmp")}
    result: set[Path] = {Path("/tmp/home")}
    for path in paths:
        parent = path if path.suffix == "" and path.is_dir() else path.parent
        for candidate in (parent, *parent.parents):
            if candidate in special or str(candidate) in (".", ""):
                continue
            result.add(candidate)
    return sorted(result, key=lambda item: (_path_depth(item), str(item)))


def build_isolated_command(
    command: Sequence[os.PathLike[str] | str],
    *,
    readonly_paths: Sequence[os.PathLike[str] | str],
    writable_paths: Sequence[os.PathLike[str] | str] = (),
    env: Optional[Mapping[str, object]] = None,
    cwd: Optional[os.PathLike[str] | str] = None,
    api_key_env: str = "SJTU_API_KEY",
    bwrap_path: Optional[os.PathLike[str] | str] = None,
    runtime_paths: Optional[Sequence[os.PathLike[str] | str]] = None,
) -> Tuple[list[str], dict[str, str]]:
    """Build ``(argv, launcher_env)`` for an isolated direct exec.

    ``readonly_paths`` should contain the trusted harness/source and the exact
    generated input. ``writable_paths`` should be newly-created build/output
    directories.  No shell is involved, and unknown child environment keys are
    discarded rather than inherited.
    """
    if not command:
        raise IsolationPolicyError("isolated command must not be empty")
    if "\x00" in api_key_env or "=" in api_key_env:
        raise IsolationPolicyError("invalid API key environment variable name")

    if bwrap_path is None:
        bwrap = resolve_bwrap()
    else:
        bwrap = Path(bwrap_path).resolve(strict=True)

    mounts: list[_Mount] = []
    for raw in DEFAULT_RUNTIME_PATHS if runtime_paths is None else runtime_paths:
        mount = _runtime_mount(raw)
        if mount is not None:
            mounts.append(mount)
    for raw in readonly_paths:
        path = _checked_policy_path(raw, writable=False)
        mounts.append(_Mount(path, path, False))
    for raw in writable_paths:
        path = _checked_policy_path(raw, writable=True)
        mounts.append(_Mount(path, path, True))

    by_destination: dict[Path, _Mount] = {}
    for mount in mounts:
        previous = by_destination.get(mount.destination)
        if previous is not None:
            if previous.writable != mount.writable:
                raise IsolationPolicyError(
                    f"path requested both read-only and writable: {mount.destination}"
                )
            continue
        by_destination[mount.destination] = mount

    # Mount ancestors before children.  A deliberate child bind can therefore
    # narrow a writable parent to read-only, or expose a specific writable
    # output below a read-only input tree.
    ordered_mounts = sorted(
        by_destination.values(),
        key=lambda item: (_path_depth(item.destination), str(item.destination)),
    )
    child_env = _clean_child_env(env, api_key_env)

    argv = [
        str(bwrap),
        "--die-with-parent",
        "--new-session",
        "--unshare-user",
        "--uid",
        "0",
        "--gid",
        "0",
        "--unshare-pid",
        "--unshare-net",
        "--unshare-ipc",
        "--unshare-uts",
        "--hostname",
        "cheatsheet-sandbox",
        "--proc",
        "/proc",
        "--dev",
        "/dev",
        "--tmpfs",
        "/tmp",
    ]
    for key in sorted(child_env):
        argv.extend(("--setenv", key, child_env[key]))
    for directory in _ancestor_directories(
        mount.destination for mount in ordered_mounts
    ):
        argv.extend(("--dir", str(directory)))
    for mount in ordered_mounts:
        argv.extend(
            (
                "--bind" if mount.writable else "--ro-bind",
                str(mount.source),
                str(mount.destination),
            )
        )

    # /tmp exists only inside bwrap's just-created mount namespace.  Do not
    # resolve it on the host (this also keeps command construction testable on
    # non-Linux developer machines).
    inner_cwd = Path(cwd).expanduser().resolve(strict=True) if cwd else Path("/tmp")
    if cwd is not None and not inner_cwd.is_dir():
        raise IsolationPolicyError(f"isolated cwd must be a directory: {inner_cwd}")
    argv.extend(("--chdir", str(inner_cwd), "--"))
    for item in command:
        value = os.fspath(item)
        if "\x00" in value:
            raise IsolationPolicyError("NUL byte in isolated command")
        argv.append(value)
    return argv, _launcher_env()


def ensure_isolation_available(
    bwrap_path: Optional[os.PathLike[str] | str] = None,
    *,
    timeout: float = 5.0,
) -> Path:
    """Exercise the complete namespace policy with the installed bubblewrap."""
    bwrap = resolve_bwrap(bwrap_path)

    true_binary = shutil.which("true")
    if not true_binary:
        raise IsolationUnavailable("cannot locate true for the bwrap policy probe")
    argv, launcher_env = build_isolated_command(
        [str(Path(true_binary).resolve())],
        readonly_paths=(),
        writable_paths=(),
        env={},
        # None selects the private /tmp created by bwrap.  Passing the literal
        # /tmp here would incorrectly require it to exist on a non-Linux host
        # in unit tests before the namespace itself exists.
        cwd=None,
        bwrap_path=bwrap,
    )
    try:
        probe = subprocess.run(
            argv,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            env=launcher_env,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise IsolationUnavailable(f"bwrap namespace probe failed: {exc}") from exc
    if probe.returncode != 0:
        detail = ((probe.stderr or "") + (probe.stdout or "")).strip()[-1000:]
        raise IsolationUnavailable(
            f"bwrap cannot establish the required namespaces (exit "
            f"{probe.returncode}): {detail}"
        )
    return bwrap


def run_isolated(
    command: Sequence[os.PathLike[str] | str],
    *,
    timeout: float,
    readonly_paths: Sequence[os.PathLike[str] | str],
    writable_paths: Sequence[os.PathLike[str] | str] = (),
    env: Optional[Mapping[str, object]] = None,
    cwd: Optional[os.PathLike[str] | str] = None,
    api_key_env: str = "SJTU_API_KEY",
    bwrap_path: Optional[os.PathLike[str] | str] = None,
    runtime_paths: Optional[Sequence[os.PathLike[str] | str]] = None,
    capture: bool = True,
    stdin_text: Optional[str] = None,
) -> subprocess.CompletedProcess:
    """Probe the boundary, then run it with the repository's group timeout.

    ``--die-with-parent`` is essential here: ``--new-session`` moves the inner
    command out of bwrap's process group, so killing bwrap on timeout must also
    kill its sandbox child.
    """
    verified = ensure_isolation_available(bwrap_path)
    argv, launcher_env = build_isolated_command(
        command,
        readonly_paths=readonly_paths,
        writable_paths=writable_paths,
        env=env,
        cwd=cwd,
        api_key_env=api_key_env,
        bwrap_path=verified,
        runtime_paths=runtime_paths,
    )
    # Lazy import avoids an isolation <-> process-helper import cycle.
    try:
        from .sandbox import run_pg
    except ImportError:
        from sandbox import run_pg  # type: ignore
    return run_pg(
        argv,
        timeout=timeout,
        env=launcher_env,
        capture=capture,
        stdin_text=stdin_text,
    )


def _cli(argv: Optional[Sequence[str]] = None) -> int:
    """Small shell-tool bridge; direct Python callers should use the API."""
    parser = argparse.ArgumentParser(description="fail-closed bwrap launcher")
    subparsers = parser.add_subparsers(dest="action", required=True)
    probe = subparsers.add_parser("probe", help="verify the full namespace policy")
    probe.add_argument("--bwrap")
    probe.add_argument("--compiler")
    probe.add_argument("--compiler-root", action="append", default=[])

    execute = subparsers.add_parser("exec", help="execute one isolated command")
    execute.add_argument("--timeout", type=float, required=True)
    execute.add_argument("--ro", action="append", default=[])
    execute.add_argument("--rw", action="append", default=[])
    execute.add_argument("--cwd")
    execute.add_argument("--api-key-env", default="SJTU_API_KEY")
    execute.add_argument("--bwrap")
    execute.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    try:
        if args.action == "probe":
            ensure_isolation_available(args.bwrap)
            if args.compiler:
                verify_compiler_in_isolation(
                    args.compiler,
                    readonly_roots=args.compiler_root,
                )
            return 0
        command = list(args.command)
        if command[:1] == ["--"]:
            command.pop(0)
        if not command:
            parser.error("exec requires a command after --")
        result = run_isolated(
            command,
            timeout=args.timeout,
            readonly_paths=args.ro,
            writable_paths=args.rw,
            env=os.environ,
            cwd=args.cwd,
            api_key_env=args.api_key_env,
            bwrap_path=args.bwrap,
            capture=False,
        )
        if result.returncode == 124:
            print("isolation: command timed out", file=sys.stderr)
        return int(result.returncode)
    except (IsolationUnavailable, IsolationPolicyError) as exc:
        print(f"isolation: {exc}", file=sys.stderr)
        return 125


if __name__ == "__main__":
    raise SystemExit(_cli())
