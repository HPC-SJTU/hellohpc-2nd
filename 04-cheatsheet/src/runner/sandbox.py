"""Per-run workspace isolation for the agent loop.

Layout created per run:

    runs/<task>/<run_id>/
    ├── workspace/          # rw; agent edits kernel.cpp here (seeded from starter_kernel.cpp)
    │   ├── kernel.cpp
    │   ├── compile_options.txt  # optional validated kernel-only GCC flags
    │   ├── tools/          # disposable agent-facing copy of the measurement tools
    │   ├── .git/           # private Git root; bounds OpenCode's worktree
    │   └── .opencode/skills/cpu-hpc-skill/
    │       ├── SKILL.md     # discovered and loaded by OpenCode on demand
    │       └── references/  # optional supporting files; not prompt-injected
    ├── harness/            # ro copy of the task harness sources the tools need
    │   ├── harness_main.cpp
    │   ├── kernel.h
    │   ├── kernel_reference.cpp
    │   ├── spec.yaml        # public diagnostic sizes only
    │   └── TASK.md

For local development, harness/ is chmod'd read-only and the agent is pointed at
workspace/ via ``opencode --dir``. The official platform still needs its normal
container and resource isolation.

Also provides a process-group timeout-kill helper used by opencode_agent and the
tool subprocess calls.
"""

from __future__ import annotations

import os
import queue
import re
import shutil
import signal
import stat
import subprocess
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Optional, Union

try:
    from . import submission_surface as _submission_surface
    from . import task_registry as _task_registry
except ImportError:
    import submission_surface as _submission_surface  # type: ignore
    import task_registry as _task_registry  # type: ignore


AGENT_BUNDLE_FILES = ("TASK.md", "starter_kernel.cpp", "kernel.h")
DIAGNOSTIC_BUNDLE_FILES = (
    "harness_main.cpp",
    "kernel_reference.cpp",
    "spec.yaml",
)

COMMON_HARNESS_FILES = [
    "common/isolated_runner.h",
]

SKILL_NAME = "cpu-hpc-skill"
GIT_INIT_TIMEOUT_SECONDS = 10.0
_CASE_KEY_RE = re.compile(r"^[a-z][a-z0-9_-]{1,63}$")


@dataclass(frozen=True)
class TaskDefinition:
    """Checked-in task views used by one Agent workspace."""

    case_key: str
    agent_bundle: Path
    diagnostic_bundle: Path


TaskDefinitionLike = TaskDefinition


@dataclass
class Sandbox:
    case_key: str
    run_id: str
    root: Path            # runs/<opaque-case-key>/<run_id>
    workspace: Path       # rw
    harness: Path         # ro assembled diagnostic harness
    source_root: Path     # absolute src/ root (for -I and profiler/hwcounters.cpp)
    agent_bundle: Path
    diagnostic_bundle: Path

    @property
    def kernel_path(self) -> Path:
        return self.workspace / "kernel.cpp"


def load_task_definition(
    task_id: str,
    source_root: Path,
    *,
    repo_root: Optional[Path] = None,
) -> TaskDefinition:
    """Resolve a checked-in task bundle after validating its identifier."""
    source_root = Path(source_root).resolve()
    repository = Path(repo_root).resolve() if repo_root is not None else source_root.parent
    registry = _task_registry.load_public_registry(repo_root=repository)
    if task_id not in registry.task_ids:
        raise _task_registry.RegistryError("task is not in the public training registry")
    bundle = _require_bundle_dir(source_root / "tasks" / task_id, "public task bundle")
    return TaskDefinition(task_id, bundle, bundle)


def _make_readonly(path: Path) -> None:
    """chmod a tree to read/execute-only (no write) for local-dev isolation."""
    for p in [path, *path.rglob("*")]:
        try:
            if p.is_dir():
                p.chmod(0o555)
            else:
                p.chmod(0o444)
        except OSError:
            pass


def _make_writable(path: Path) -> None:
    """Restore write bits so teardown/shutil can remove the tree."""
    for p in [path, *path.rglob("*")]:
        try:
            if p.is_dir():
                p.chmod(0o755)
            else:
                p.chmod(0o644)
        except OSError:
            pass


def create_sandbox(
    definition: TaskDefinitionLike,
    source_root: Path,
    runs_root: Path,
    run_id: Optional[str] = None,
    make_harness_readonly: bool = True,
    skill_path: Optional[Union[Path, _submission_surface.SubmissionSurface]] = None,
    root_override: Optional[Path] = None,
) -> Sandbox:
    """Create a sandbox from explicit, already-authorized bundle views."""
    if not isinstance(definition, TaskDefinition):
        raise TypeError("create_sandbox requires an explicit task definition")
    if not _CASE_KEY_RE.fullmatch(definition.case_key):
        raise ValueError("task definition case_key is invalid")
    source_root = Path(source_root).resolve()
    agent_bundle = _require_bundle_dir(definition.agent_bundle, "agent bundle")
    diagnostic_bundle = _require_bundle_dir(
        definition.diagnostic_bundle, "diagnostic bundle"
    )
    _require_bundle_files(agent_bundle, AGENT_BUNDLE_FILES, "agent bundle")
    _require_bundle_files(
        diagnostic_bundle, DIAGNOSTIC_BUNDLE_FILES, "diagnostic bundle"
    )

    run_id = run_id or time.strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:6]
    if root_override is not None:
        root = Path(root_override).resolve()
        if root.exists():
            raise FileExistsError(f"refusing to overwrite existing workspace: {root}")
    else:
        root = Path(runs_root).resolve() / definition.case_key / run_id
    workspace = root / "workspace"
    harness = root / "harness"
    for d in (workspace, harness):
        d.mkdir(parents=True, exist_ok=True)

    # seed workspace/kernel.cpp from starter_kernel.cpp
    starter = agent_bundle / "starter_kernel.cpp"
    shutil.copy2(starter, workspace / "kernel.cpp")
    # copy2 preserves permissions, including those of a read-only task bundle.
    (workspace / "kernel.cpp").chmod(0o644)
    # The model may inspect this ABI copy; trusted builds use harness/kernel.h.
    shutil.copy2(agent_bundle / "kernel.h", workspace / "kernel.h")
    (workspace / "kernel.h").chmod(0o444)
    # Optional, agent-controlled optimizer flags.  The trusted build validates
    # this file and applies it only to the submitted shared object.
    (workspace / "compile_options.txt").write_text("", encoding="utf-8")

    # The prompt tells the agent to run ``bash tools/*.sh`` from its working
    # directory. Give it a disposable copy; the Runner independently invokes
    # the original tools and never trusts edits or timings from this copy.
    tools_source = source_root / "tools"
    if not tools_source.is_dir():
        raise FileNotFoundError(f"tools directory not found: {tools_source}")
    shutil.copytree(tools_source, workspace / "tools")

    # Install the submission as a project-local native OpenCode Skill.  The
    # Skill loader advertises only its name/description initially; SKILL.md is
    # injected when the agent calls the skill tool, and supporting references
    # remain available for explicit, on-demand reads.
    if skill_path is not None:
        _copy_skill(skill_path, workspace)

    # OpenCode evaluates edit permissions relative to its discovered
    # Git worktree.  Without an inner repository, a development run inherits
    # the outer source repository (and can read it), while a packed non-Git run
    # uses filesystem root and never matches the exact ``kernel.cpp`` rule.
    # A real, private Git root makes both the read boundary and edit pattern
    # deterministic without granting a broad **/kernel.cpp permission.
    _initialize_workspace_repository(workspace)

    # Assemble the diagnostic view from two separately attested bundles.
    for name in ("TASK.md", "kernel.h"):
        shutil.copy2(agent_bundle / name, harness / name)
    for name in DIAGNOSTIC_BUNDLE_FILES:
        shutil.copy2(diagnostic_bundle / name, harness / name)

    # Security-critical trusted-parent helpers are copied beside the per-task
    # harness and compile through this private include root.  The formal
    # evaluator never takes these files from an agent-controlled workspace.
    for relative in COMMON_HARNESS_FILES:
        src = source_root / relative
        destination = harness / relative
        if not src.is_file():
            raise FileNotFoundError(f"trusted harness file not found: {src}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, destination)

    sb = Sandbox(
        case_key=definition.case_key,
        run_id=run_id,
        root=root,
        workspace=workspace,
        harness=harness,
        source_root=source_root,
        agent_bundle=agent_bundle,
        diagnostic_bundle=diagnostic_bundle,
    )
    if make_harness_readonly:
        _make_readonly(harness)
    return sb



def _require_bundle_dir(path: Path, label: str) -> Path:
    raw = Path(path)
    if not raw.is_absolute() or ".." in raw.parts:
        raise ValueError(f"{label} must be an absolute normalized path")
    try:
        resolved = raw.resolve(strict=True)
    except OSError as exc:
        raise FileNotFoundError(f"{label} is missing") from exc
    if resolved != raw:
        raise ValueError(f"{label} must not contain symbolic links or aliases")
    try:
        mode = os.lstat(resolved).st_mode
    except OSError as exc:
        raise FileNotFoundError(f"{label} is missing") from exc
    if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
        raise ValueError(f"{label} must be a non-symlink directory")
    return resolved


def _require_bundle_files(root: Path, names: tuple[str, ...], label: str) -> None:
    for name in names:
        path = root / name
        try:
            mode = os.lstat(path).st_mode
        except OSError as exc:
            raise FileNotFoundError(f"{label} is missing required file: {name}") from exc
        if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
            raise ValueError(f"{label} has unsafe required file: {name}")

def _copy_skill(skill_path: Union[Path, _submission_surface.SubmissionSurface], workspace: Path) -> Path:
    """Copy one validated snapshot into OpenCode's native Skill layout."""
    surface = skill_path if isinstance(skill_path, _submission_surface.SubmissionSurface) else (
        _submission_surface.require_eligible_surface(skill_path, repository_layout=True)
    )
    target = workspace / ".opencode" / "skills" / SKILL_NAME
    target.parent.mkdir(parents=True, exist_ok=True)
    return _submission_surface.materialize_surface(surface, target, include_config=False)


def _initialize_workspace_repository(workspace: Path) -> None:
    """Create and verify the private Git root OpenCode uses as its worktree."""
    env = {
        "PATH": "/usr/bin:/bin",
        "LANG": "C",
        "LC_ALL": "C",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_TERMINAL_PROMPT": "0",
    }

    init = run_pg(
        ["git", "-c", "init.templateDir=", "init", "--quiet", "."],
        timeout=GIT_INIT_TIMEOUT_SECONDS,
        env=env,
        cwd=str(workspace),
        capture=True,
    )
    if init.returncode != 0:
        detail = (init.stderr or init.stdout or "no diagnostic output").strip()[-1000:]
        raise RuntimeError(
            f"failed to initialize private workspace Git repository: "
            f"exit {init.returncode}: {detail}"
        )

    probe = run_pg(
        ["git", "rev-parse", "--show-toplevel"],
        timeout=GIT_INIT_TIMEOUT_SECONDS,
        env=env,
        cwd=str(workspace),
        capture=True,
    )
    actual = Path((probe.stdout or "").strip()).resolve() if probe.returncode == 0 else None
    if actual != workspace.resolve():
        detail = (probe.stderr or probe.stdout or "no diagnostic output").strip()[-1000:]
        raise RuntimeError(
            "private workspace Git root verification failed: "
            f"expected {workspace.resolve()}, got {actual}; {detail}"
        )


def teardown(sb: Sandbox, remove: bool = False) -> None:
    """Restore write bits; optionally delete the whole run dir."""
    _make_writable(sb.harness)
    if remove:
        shutil.rmtree(sb.root, ignore_errors=True)


# --------------------------------------------------------------------------- #
# process-group timeout-kill helper
# --------------------------------------------------------------------------- #
def run_pg(
    cmd: List[str],
    timeout: float,
    env: Optional[dict] = None,
    cwd: Optional[str] = None,
    capture: bool = True,
    on_output: Optional[Callable[[str, str], None]] = None,
    stdin_text: Optional[str] = None,
) -> subprocess.CompletedProcess:
    """Run cmd in its own process group; on timeout kill the WHOLE group (SIGKILL).

    Returns a CompletedProcess. On timeout the returncode is set to 124 (like
    coreutils `timeout`) and stdout/stderr hold whatever was captured.
    """
    if stdin_text is not None and not isinstance(stdin_text, str):
        raise TypeError("stdin_text must be str or None")
    popen_kwargs = dict(
        env=env,
        cwd=cwd,
        stdin=subprocess.PIPE if stdin_text is not None else subprocess.DEVNULL,
        start_new_session=True,  # new process group so we can kill children
    )
    if on_output is not None and not capture:
        raise ValueError("on_output requires capture=True")
    if stdin_text is not None:
        popen_kwargs.update(
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    if capture:
        popen_kwargs.update(
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

    # A file-backed input avoids blocking the output readers while sending a
    # large prompt. The child owns its descriptor after Popen returns.
    input_file = None
    try:
        if on_output is not None and stdin_text is not None:
            input_file = tempfile.TemporaryFile(mode="w+", encoding="utf-8")
            input_file.write(stdin_text)
            input_file.seek(0)
            popen_kwargs["stdin"] = input_file
        proc = subprocess.Popen(cmd, **popen_kwargs)
    finally:
        if input_file is not None:
            input_file.close()
    if on_output is not None:
        return _communicate_streaming(proc, cmd, timeout, on_output)
    try:
        out, err = proc.communicate(input=stdin_text, timeout=timeout)
        return subprocess.CompletedProcess(cmd, proc.returncode, out, err)
    except subprocess.TimeoutExpired:
        _kill_group(proc)
        try:
            out, err = proc.communicate(timeout=5)
        except Exception:
            out, err = "", ""
        return subprocess.CompletedProcess(cmd, 124, out or "", (err or "") + "\n[timeout: process group killed]")
    except BaseException:
        # KeyboardInterrupt/SystemExit and unexpected reader failures must not
        # leave an Agent or one of its descendants running outside the caller.
        if proc.poll() is None:
            _kill_group(proc)
        try:
            proc.communicate(timeout=5)
        except Exception:
            if proc.poll() is None:
                _kill_group(proc)
        raise


def _communicate_streaming(
    proc: subprocess.Popen,
    cmd: List[str],
    timeout: float,
    on_output: Callable[[str, str], None],
) -> subprocess.CompletedProcess:
    """Drain both child streams without deadlock and emit complete text lines."""
    started = time.monotonic()
    messages: "queue.Queue[tuple[str, Optional[str]]]" = queue.Queue()
    threads: list[threading.Thread] = []

    def reader(label: str, stream) -> None:
        try:
            for line in iter(stream.readline, ""):
                messages.put((label, line))
        finally:
            messages.put((label, None))

    try:
        threads = [
            threading.Thread(target=reader, args=("stdout", proc.stdout), daemon=True),
            threading.Thread(target=reader, args=("stderr", proc.stderr), daemon=True),
        ]
        for thread in threads:
            thread.start()

        chunks = {"stdout": [], "stderr": []}
        ended: set[str] = set()
        timed_out = False
        while len(ended) < 2:
            if (
                not timed_out
                and time.monotonic() - started >= timeout
                and proc.poll() is None
            ):
                timed_out = True
                _kill_group(proc)
            try:
                label, line = messages.get(timeout=0.1 if not timed_out else 0.5)
            except queue.Empty:
                if proc.poll() is not None and all(not thread.is_alive() for thread in threads):
                    break
                continue
            if line is None:
                ended.add(label)
                continue
            chunks[label].append(line)
            on_output(label, line)

        try:
            returncode = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            _kill_group(proc)
            returncode = 124
        if timed_out:
            returncode = 124
            timeout_message = "\n[timeout: process group killed]\n"
            chunks["stderr"].append(timeout_message)
            on_output("stderr", timeout_message)
        return subprocess.CompletedProcess(
            cmd,
            returncode,
            "".join(chunks["stdout"]),
            "".join(chunks["stderr"]),
        )
    except BaseException:
        if proc.poll() is None:
            _kill_group(proc)
        try:
            proc.wait(timeout=5)
        except (OSError, subprocess.SubprocessError):
            if proc.poll() is None:
                _kill_group(proc)
        raise
    finally:
        for thread in threads:
            thread.join(timeout=1)
        for stream in (proc.stdout, proc.stderr):
            if stream is not None:
                stream.close()


def _kill_group(proc: subprocess.Popen) -> None:
    if os.name == "nt":
        try:
            subprocess.run(
                ["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=2,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            pass
        if proc.poll() is None:
            try:
                proc.kill()
            except OSError:
                pass
        return
    try:
        pgid = os.getpgid(proc.pid)
    except (AttributeError, OSError):
        pgid = None
    if pgid is not None:
        try:
            os.killpg(pgid, signal.SIGTERM)
            time.sleep(0.5)
            os.killpg(pgid, signal.SIGKILL)
        except OSError:
            pass
    else:
        try:
            proc.kill()
        except OSError:
            pass
